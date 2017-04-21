/*
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER
 *
 * Copyright (c) 2016, Juniper Networks, Inc. All rights reserved.
 *
 *
 * The contents of this file are subject to the terms of the BSD 3 clause
 * License (the "License"). You may not use this file except in compliance
 * with the License.
 *
 * You can obtain a copy of the license at
 * https://github.com/Juniper/warp17/blob/master/LICENSE.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its
 * contributors may be used to endorse or promote products derived from this
 * software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * File name:
 *     tpg_tcp.c
 *
 * Description:
 *     General TCP processing, hopefully it will work for v4 and v6.
 *
 * Author:
 *     Dumitru Ceara, Eelco Chaudron
 *
 * Initial Created:
 *     03/04/2015
 *
 * Notes:
 *
 */

/*****************************************************************************
 * Include files
 ****************************************************************************/
#include "tcp_generator.h"

/*****************************************************************************
 * Global variables
 ****************************************************************************/
/* Define TCP global statistics. Each thread has its own set of locally
 * allocated stats which are accessible through STATS_GLOBAL(type, core, port).
 */
STATS_DEFINE(tcp_statistics_t);

/*****************************************************************************
 * Forward declarations
 ****************************************************************************/
static cmdline_parse_ctx_t cli_ctx[];

/*****************************************************************************
 * tcp_init()
 ****************************************************************************/
bool tcp_init(void)
{
    /*
     * Add port module CLI commands
     */
    if (!cli_add_main_ctx(cli_ctx)) {
        RTE_LOG(ERR, USER1, "ERROR: Can't add TCP specific CLI commands!\n");
        return false;
    }

    /*
     * Allocate memory for TCP statistics, and clear all of them
     */
    if (STATS_GLOBAL_INIT(tcp_statistics_t, "tcp_stats") == NULL) {
        RTE_LOG(ERR, USER1,
                "ERROR: Failed allocating TCP statistics memory!\n");
        return false;
    }

    return true;
}

/*****************************************************************************
 * tcp_lcore_init()
 ****************************************************************************/
void tcp_lcore_init(uint32_t lcore_id)
{
    /* Init the local stats. */
    if (STATS_LOCAL_INIT(tcp_statistics_t, "tcp_stats", lcore_id) == NULL) {
        TPG_ERROR_ABORT("[%d:%s() Failed to allocate per lcore tcp_stats!\n",
                        rte_lcore_index(lcore_id),
                        __func__);
    }
}

/*****************************************************************************
 * tcp_total_stats_clear()
 *****************************************************************************/
void tcp_total_stats_clear(uint32_t port)
{
    tcp_statistics_t *tcp_stats;
    uint32_t          core;

    STATS_FOREACH_CORE(tcp_statistics_t, port, core, tcp_stats) {
        bzero(tcp_stats, sizeof(*tcp_stats));
    }
}

/*****************************************************************************
 * tcp_open_v4_connection()
 *
 * NOTE:
 *   We will accept any source/destination IP/port so odd values can be
 *   tested. If *tcb is NULL we should malloc one.
 *
 *   The caller needs to make sure that the core opening the connection is
 *   the core that will handle the rx queue on which the traffic is received
 *   for the connection!
 ****************************************************************************/
int tcp_open_v4_connection(tcp_control_block_t **tcb, uint32_t eth_port,
                           uint32_t src_ip_addr, uint16_t src_port,
                           uint32_t dst_ip_addr, uint16_t dst_port,
                           uint32_t test_case_id, tpg_app_proto_t app_id,
                           sockopt_t *sockopt,
                           uint32_t flags)
{
    int                  rc = 0;
    tcp_control_block_t *tcb_p;
    bool                 active;
    uint32_t             malloc_flag;
    bool                 tcb_reuse;

    if (unlikely(tcb == NULL))
        return -EINVAL;

    tcb_reuse = (flags & TCG_CB_REUSE_CB);

    if (unlikely(!tcb_reuse && sockopt == NULL))
        return -EINVAL;

    /* If the *tcb is NULL we should malloc one and mark that we need
     * to free it later.
     */
    if (*tcb == NULL) {
        *tcb = tlkp_alloc_tcb();
        if (*tcb == NULL) {
            INC_STATS(STATS_LOCAL(tcp_statistics_t, eth_port),
                      ts_tcb_alloc_err);
            return -ENOMEM;
        }
        malloc_flag = TCG_CB_MALLOCED;
        INC_STATS(STATS_LOCAL(tcp_statistics_t, eth_port), ts_tcb_malloced);
    } else {
        TCB_CHECK(*tcb);
        malloc_flag = 0;
    }

    /* Although ip 0.0.0.0 is invalid and port 0 is also invalid, we
     * assume that if at least the dst_ip or dst_port are set then
     * this is an active connection.
     */
    active = (dst_ip_addr != 0 || dst_port != 0);

    tcb_p = *tcb;

    if (!tcb_reuse) {
        tlkp_init_tcb(tcb_p, src_ip_addr, dst_ip_addr, src_port, dst_port,
                      0,
                      eth_port,
                      test_case_id,
                      app_id,
                      sockopt,
                      (flags | malloc_flag));
    }

    tsm_initialize_statemachine(tcb_p, active);

    rc = tlkp_add_tcb(tcb_p);
    if (rc != 0) {
        if (!tcb_reuse && tcb_p->tcb_malloced)
            tlkp_free_tcb(tcb_p);

        return rc;
    }

    tsm_dispatch_event(tcb_p, TE_OPEN, NULL);

    return rc;
}

/*****************************************************************************
 * tcp_listen_v4()
 *
 * NOTE:
 *   We will accept any source IP/port so odd values can be
 *   tested. If the *tcb is NULL we should malloc one.
 *
 *   The next hop used will always be the DUT for the ethernet port.
 *
 *   Unlike tcp_open_v4_connection, tcp_listen_v4 MUST be called by all cores
 *   that might process incoming connections for the given local ip/port.
 ****************************************************************************/
int tcp_listen_v4(tcp_control_block_t **tcb, uint32_t eth_port,
                  uint32_t local_ip_addr, uint16_t local_port,
                  uint32_t test_case_id, tpg_app_proto_t app_id,
                  sockopt_t *sockopt,
                  uint32_t flags)
{
    return tcp_open_v4_connection(tcb, eth_port, local_ip_addr, local_port,
                                  0, /* remote_ip ANY */
                                  0, /* remote_port ANY */
                                  test_case_id,
                                  app_id,
                                  sockopt,
                                  flags);
}

/*****************************************************************************
 * tcp_send_v4()
 *
 * NOTE:
 *  Only PUSH is supported for now. If the flags don't contain PUSH we crash
 *  for now.
 ****************************************************************************/
int tcp_send_v4(tcp_control_block_t *tcb, struct rte_mbuf *data,
                uint32_t flags,
                uint32_t timeout __rte_unused,
                uint32_t *data_sent)
{
    tsm_data_arg_t darg;

    if (unlikely(tcb == NULL))
        return -EINVAL;

    TCB_CHECK(tcb);

    if (unlikely(!(flags & TCG_SEND_PSH)))
        TPG_ERROR_ABORT("TODO: %s!\n", "We only support PUSH SEND for now");

    darg.tda_mbuf = data;
    darg.tda_data_len = data->pkt_len;
    darg.tda_data_sent = data_sent;
    darg.tda_push = (flags & TCG_SEND_PSH);
    darg.tda_urg = (flags & TCG_SEND_URG);

    return tsm_dispatch_event(tcb, TE_SEND, &darg);
}

/*****************************************************************************
 * tcp_connection_cleanup()
 ****************************************************************************/
void tcp_connection_cleanup(tcp_control_block_t *tcb)
{
    TCB_CHECK(tcb);

    /* Free any allocated memory. */
    tlkp_free_tcb(tcb);
}

/*****************************************************************************
 * tcp_close_connection()
 ****************************************************************************/
int tcp_close_connection(tcp_control_block_t *tcb, uint32_t flags)
{
    if (unlikely(tcb == NULL))
        return -EINVAL;

    TCB_CHECK(tcb);

    /*
     * For silent close all we do is cleanup the tcb
     */
    if ((flags & TCG_SILENT_CLOSE) != 0) {

        tlkp_delete_tcb(tcb);

        /* Cancel any scheduled timers. */
        if (TCB_RTO_TMR_IS_SET(tcb))
            tcp_timer_rto_cancel(&tcb->tcb_l4);
        if (TCB_SLOW_TMR_IS_SET(tcb))
            tcp_timer_slow_cancel(&tcb->tcb_l4);


        /* Cleanup retrans queue. */
        if (tcb->tcb_retrans.tr_data_mbufs) {
            /* This will free the whole chain! */
            rte_pktmbuf_free(tcb->tcb_retrans.tr_data_mbufs);
        }

        /* Cleanup recv buf. */
        while (!LIST_EMPTY(&tcb->tcb_rcv_buf)) {
            tcb_buf_hdr_t *recvbuf = tcb->tcb_rcv_buf.lh_first;

            rte_pktmbuf_free(recvbuf->tbh_mbuf);
            LIST_REMOVE(recvbuf, tbh_entry);
        }

        tsm_terminate_statemachine(tcb);

        if (tcb->tcb_malloced) {
            INC_STATS(STATS_LOCAL(tcp_statistics_t, tcb->tcb_l4.l4cb_interface),
                      ts_tcb_freed);
            tcp_connection_cleanup(tcb);
        }
        return 0;
    }

    /*
     * Else handle it trough the statemachine
     */
    return tsm_dispatch_event(tcb, TE_CLOSE, NULL);
}

/*****************************************************************************
 * tcp_store_sockopt()
 ****************************************************************************/
void tcp_store_sockopt(tcp_sockopt_t *dest, const tpg_tcp_sockopt_t *options)
{
    dest->tcpo_win_size = options->to_win_size;
    dest->tcpo_syn_retry_cnt = options->to_syn_retry_cnt;
    dest->tcpo_syn_ack_retry_cnt = options->to_syn_ack_retry_cnt;
    dest->tcpo_data_retry_cnt = options->to_data_retry_cnt;
    dest->tcpo_retry_cnt = options->to_retry_cnt;
    dest->tcpo_rto = options->to_rto * 1000;
    dest->tcpo_fin_to = options->to_fin_to * 1000;
    dest->tcpo_twait_to = options->to_twait_to * 1000;
    dest->tcpo_orphan_to = options->to_orphan_to * 1000;

    /* Bit flags. */
    dest->tcpo_skip_timewait = (options->to_skip_timewait > 0 ? true : false);
}

/*****************************************************************************
 * tcp_load_sockopt()
 ****************************************************************************/
void tcp_load_sockopt(tpg_tcp_sockopt_t *dest, const tcp_sockopt_t *options)
{
    dest->to_win_size = options->tcpo_win_size;
    dest->has_to_win_size = true;

    dest->to_syn_retry_cnt = options->tcpo_syn_retry_cnt;
    dest->has_to_syn_retry_cnt = true;

    dest->to_syn_ack_retry_cnt = options->tcpo_syn_ack_retry_cnt;
    dest->has_to_syn_ack_retry_cnt = true;

    dest->to_data_retry_cnt = options->tcpo_data_retry_cnt;
    dest->has_to_data_retry_cnt = true;

    dest->to_retry_cnt = options->tcpo_retry_cnt;
    dest->has_to_retry_cnt = true;

    dest->to_rto = options->tcpo_rto / 1000;
    dest->has_to_rto = true;

    dest->to_fin_to = options->tcpo_fin_to / 1000;
    dest->has_to_fin_to = true;

    dest->to_twait_to = options->tcpo_twait_to / 1000;
    dest->has_to_twait_to = true;

    dest->to_orphan_to = options->tcpo_orphan_to / 1000;
    dest->has_to_orphan_to = true;

    /* Bit flags. */
    dest->to_skip_timewait = options->tcpo_skip_timewait;
    dest->has_to_skip_timewait = true;
}


/*****************************************************************************
 * tcp_build_tcp_hdr()
 ****************************************************************************/
struct tcp_hdr *tcp_build_tcp_hdr(struct rte_mbuf *mbuf, tcp_control_block_t *tcb,
                                  struct ipv4_hdr *ipv4_hdr,
                                  uint32_t sseq,
                                  uint32_t flags)
{
    uint16_t        tcp_hdr_len = sizeof(struct tcp_hdr);
    uint16_t        tcp_hdr_offset = rte_pktmbuf_data_len(mbuf);
    struct tcp_hdr *tcp_hdr;

    /* TODO: Support options, we need more room */
    tcp_hdr = (struct tcp_hdr *) rte_pktmbuf_append(mbuf, tcp_hdr_len);

    if (tcp_hdr == NULL)
        return NULL;

    tcp_hdr->src_port = rte_cpu_to_be_16(tcb->tcb_l4.l4cb_src_port);
    tcp_hdr->dst_port = rte_cpu_to_be_16(tcb->tcb_l4.l4cb_dst_port);
    if ((flags & TCP_BUILD_FLAG_USE_ISS) != 0)
        tcp_hdr->sent_seq = rte_cpu_to_be_32(tcb->tcb_snd.iss);
    else
        tcp_hdr->sent_seq = rte_cpu_to_be_32(sseq);
    tcp_hdr->recv_ack = rte_cpu_to_be_32(tcb->tcb_rcv.nxt);
    tcp_hdr->data_off = tcp_hdr_len >> 2 << 4;
    tcp_hdr->tcp_flags = flags & TCP_BUILD_FLAG_MASK;
    tcp_hdr->rx_win = rte_cpu_to_be_16(tcb->tcb_rcv.wnd);
    tcp_hdr->tcp_urp = rte_cpu_to_be_16(0); /* TODO: set correctly if urgen flag is set */

    /*
     * TODO: Do we want TCP segmentation offload, if so do it before
     *       calculating the checksum!
     */

#if !defined(TPG_SW_CHECKSUMMING)
    if (true) {
#else
    if (tcb->tcb_l4.l4cb_sockopt.so_eth.ethso_tx_offload_tcp_cksum) {
#endif
        mbuf->ol_flags |= PKT_TX_TCP_CKSUM | PKT_TX_IPV4;
        mbuf->l4_len = tcp_hdr_len;

        tcp_hdr->cksum = rte_ipv4_phdr_cksum(ipv4_hdr, mbuf->ol_flags);

    } else {
        /*
         * No HW checksum support do it manually, however up to here we can only
         * calculate the header checksum, so we do...
         */
        tcp_hdr->cksum = 0;
        tcp_hdr->cksum = ipv4_general_l4_cksum(mbuf,
                                               ipv4_hdr,
                                               tcp_hdr_offset,
                                               tcp_hdr_len);
    }

    return tcp_hdr;
}

/*****************************************************************************
 * tcp_receive_pkt()
 *
 * Return the mbuf only if it needs to be free'ed back to the pool, if it was
 * consumed, or needed later (ip refrag), return NULL.
 ****************************************************************************/
struct rte_mbuf *tcp_receive_pkt(packet_control_block_t *pcb,
                                 struct rte_mbuf *mbuf)
{
    unsigned int         tcp_hdr_len;
    tcp_statistics_t    *stats;
    struct tcp_hdr      *tcp_hdr;
    tcp_control_block_t *tcb;

    stats = STATS_LOCAL(tcp_statistics_t, pcb->pcb_port);

    if (unlikely(rte_pktmbuf_data_len(mbuf) < sizeof(struct tcp_hdr))) {
        RTE_LOG(DEBUG, USER2, "[%d:%s()] ERR: mbuf fragment to small for tcp_hdr!\n",
                pcb->pcb_core_index, __func__);

        INC_STATS(stats, ts_to_small_fragment);
        return mbuf;
    }

    tcp_hdr = rte_pktmbuf_mtod(mbuf, struct tcp_hdr *);
    tcp_hdr_len = (tcp_hdr->data_off >> 4) << 2;

    PKT_TRACE(pcb, TCP, DEBUG, "sport=%u, dport=%u, hdrlen=%u, flags=%c%c%c%c%c%c, data_len=%u",
              rte_be_to_cpu_16(tcp_hdr->src_port),
              rte_be_to_cpu_16(tcp_hdr->dst_port),
              tcp_hdr_len,
              (tcp_hdr->tcp_flags & TCP_URG_FLAG) == 0 ? '-' : 'u',
              (tcp_hdr->tcp_flags & TCP_ACK_FLAG) == 0 ? '-' : 'a',
              (tcp_hdr->tcp_flags & TCP_PSH_FLAG) == 0 ? '-' : 'p',
              (tcp_hdr->tcp_flags & TCP_RST_FLAG) == 0 ? '-' : 'r',
              (tcp_hdr->tcp_flags & TCP_SYN_FLAG) == 0 ? '-' : 's',
              (tcp_hdr->tcp_flags & TCP_FIN_FLAG) == 0 ? '-' : 'f',
              pcb->pcb_l4_len - tcp_hdr_len);

    PKT_TRACE(pcb, TCP, DEBUG, "  seq=%u, ack=%u, window=%u, urgent=%u",
              rte_be_to_cpu_32(tcp_hdr->sent_seq),
              rte_be_to_cpu_32(tcp_hdr->recv_ack),
              rte_be_to_cpu_16(tcp_hdr->rx_win),
              rte_be_to_cpu_16(tcp_hdr->tcp_urp));


    /*
     * Update stats
     */

    INC_STATS(stats, ts_received_pkts);
    INC_STATS_VAL(stats, ts_received_bytes, pcb->pcb_l4_len);

    /*
     * Check header content, and buffer space
     */
    if (unlikely(rte_pktmbuf_data_len(mbuf) < tcp_hdr_len)) {
        RTE_LOG(DEBUG, USER2, "[%d:%s()] ERR: mbuf fragment to small for tcp header!\n",
                pcb->pcb_core_index, __func__);

        INC_STATS(stats, ts_to_small_fragment);
        return mbuf;
    }

    if (unlikely(tcp_hdr_len < sizeof(struct tcp_hdr))) {
        RTE_LOG(DEBUG, USER2, "[%d:%s()] ERR: TCP hdr len smaller than header!\n",
                pcb->pcb_core_index, __func__);

        INC_STATS(stats, ts_hdr_to_small);
        return mbuf;
    }

#ifndef _SPEEDY_PKT_PARSE_

    if (unlikely((tcp_hdr->tcp_flags & ~TCP_FLAG_ALL) != 0 ||
                 (tcp_hdr->data_off & 0x0F) != 0)) {
        /* No log message for this, as I know Francois is misusing these bits */
        INC_STATS(stats, ts_reserved_bit_set);
    }

    if (PKT_TRACE_ENABLED(pcb)) {
        unsigned int  i;
        uint32_t     *options = (uint32_t *) (tcp_hdr + 1);

        for (i = 0;
             i < ((tcp_hdr_len - sizeof(struct tcp_hdr)) / sizeof(uint32_t));
             i++) {

            PKT_TRACE(pcb, TCP, DEBUG, "  option word 0x%2.2X: 0x%8.8X",
                      i,
                      rte_be_to_cpu_32(options[i]));
        }
    }

#endif

    /*
     * Handle checksum...
     */

#if !defined(TPG_SW_CHECKSUMMING)
    if (true) {
#else
    if ((RTE_PER_LCORE(local_port_dev_info)[pcb->pcb_port].pi_dev_info.rx_offload_capa &
         DEV_RX_OFFLOAD_TCP_CKSUM) != 0) {
#endif
        if (unlikely((mbuf->ol_flags & PKT_RX_L4_CKSUM_BAD) != 0)) {
            RTE_LOG(DEBUG, USER2, "[%d:%s()] ERR: Invalid TCP checksum!\n",
                    pcb->pcb_core_index, __func__);

            INC_STATS(stats, ts_invalid_checksum);
            return mbuf;
        }
    } else {
        /*
         * No HW checksum support do it manually...
         */

        if (unlikely(ipv4_general_l4_cksum(mbuf, pcb->pcb_ipv4, 0,
                                           pcb->pcb_l4_len) != 0xffff)) {
            RTE_LOG(DEBUG, USER2, "[%d:%s()] ERR: Invalid TCP checksum!\n",
                    pcb->pcb_core_index, __func__);

            INC_STATS(stats, ts_invalid_checksum);
            return mbuf;
        }
    }

    /*
     * Calculate hash...
     *
     * The following link has some general details on RSS, not sure what dpdk supports:
     *   https://msdn.microsoft.com/en-us/library/windows/hardware/ff567236(v=vs.85).aspx
     */

    if (likely((mbuf->ol_flags & PKT_RX_RSS_HASH) != 0)) {
        pcb->pcb_hash = mbuf->hash.rss;
        pcb->pcb_hash_valid = true;
    } else {
        pcb->pcb_hash = tlkp_calc_pkt_hash(pcb->pcb_ipv4->src_addr,
                                           pcb->pcb_ipv4->dst_addr,
                                           tcp_hdr->src_port,
                                           tcp_hdr->dst_port);
        pcb->pcb_hash_valid = true;
    }

    /*
     * Update mbuf/pcb and send packet of to the client handler
     */

    pcb->pcb_tcp = tcp_hdr;
    pcb->pcb_l5_len = pcb->pcb_l4_len - tcp_hdr_len;
    rte_pktmbuf_adj(mbuf, tcp_hdr_len);

    /*
     * First try known session lookup
     */
    tcb = tlkp_find_v4_tcb(pcb->pcb_port, pcb->pcb_hash,
                           rte_be_to_cpu_32(pcb->pcb_ipv4->dst_addr),
                           rte_be_to_cpu_32(pcb->pcb_ipv4->src_addr),
                           rte_be_to_cpu_16(tcp_hdr->dst_port),
                           rte_be_to_cpu_16(tcp_hdr->src_port));

    /*
     * If no existing tcb see if we have a server available that is
     * accepting new requests.
     */
    if (tcb == NULL && (tcp_hdr->tcp_flags & TCP_SYN_FLAG) != 0) {
        uint32_t tcp_listen_hash;

        tcp_listen_hash = tlkp_calc_pkt_hash(0, /* src_addr ANY */
                                             pcb->pcb_ipv4->dst_addr,
                                             0, /* src_port ANY */
                                             tcp_hdr->dst_port);

        tcb = tlkp_find_v4_tcb(pcb->pcb_port, tcp_listen_hash,
                               rte_be_to_cpu_32(pcb->pcb_ipv4->dst_addr),
                               0, /* src_addr ANY */
                               rte_be_to_cpu_16(tcp_hdr->dst_port),
                               0 /* src_port ANY */);

        if (unlikely(tcb && tcb->tcb_state != TS_LISTEN)) {
            TPG_ERROR_ABORT("[%d:%s()]expected tcb->s=%d found=%d\n",
                            pcb->pcb_core_index,
                            __func__,
                            TS_LISTEN,
                            tcb->tcb_state);
        }
    }

    if (tcb != NULL) {
        /*
         * If the tcb is marked for tracing then we should also trace the
         * pcb (although this might be a little late as we already tried
         * to log everything about the pcb...)
         */
        if (unlikely(tcb->tcb_trace))
            pcb->pcb_trace = true;

        PKT_TRACE(pcb, TCP, DEBUG, "tcb found in state(%d)", tcb->tcb_state);
        tsm_dispatch_net_event(tcb, TE_SEGMENT_ARRIVES, pcb);

        /* If the stack decided to keep this packet we shouldn' allow the
         * rest of the code to free it.
         */
        if (unlikely(pcb->pcb_mbuf_stored))
            mbuf = NULL;

    } else {

        /* We just abuse the TCP state machine and pass a local TCB for
         * simulating a CLOSED TCB.
         */
        tcp_control_block_t closed_tcb;

        INC_STATS(stats, ts_tcb_not_found);

        bzero(&closed_tcb, sizeof(closed_tcb));

        closed_tcb.tcb_l4.l4cb_valid = true;
        closed_tcb.tcb_l4.l4cb_src_addr.ip_v4 = rte_be_to_cpu_32(pcb->pcb_ipv4->dst_addr);
        closed_tcb.tcb_l4.l4cb_dst_addr.ip_v4 = rte_be_to_cpu_32(pcb->pcb_ipv4->src_addr);
        closed_tcb.tcb_l4.l4cb_src_port = rte_be_to_cpu_16(tcp_hdr->dst_port);
        closed_tcb.tcb_l4.l4cb_dst_port = rte_be_to_cpu_16(tcp_hdr->src_port);
        closed_tcb.tcb_l4.l4cb_interface = pcb->pcb_port;
        closed_tcb.tcb_l4.l4cb_domain = AF_INET;
        closed_tcb.tcb_state = TS_CLOSED;

        tsm_dispatch_net_event(&closed_tcb, TE_SEGMENT_ARRIVES, pcb);
    }

    return mbuf;
}

/*****************************************************************************
 * tcp_build_tcp_hdr_mbuf()
 ****************************************************************************/
static struct rte_mbuf *tcp_build_tcp_hdr_mbuf(tcp_control_block_t *tcb,
                                               uint32_t sseq, uint32_t flags,
                                               uint16_t l4_len,
                                               struct tcp_hdr **tcp_hdr)
{
    int              next_header_offset;
    struct rte_mbuf *hdr;
    struct ipv4_hdr *ipv4;
    uint64_t         nh_mac;

    hdr = rte_pktmbuf_alloc(mem_get_mbuf_local_pool_tx_hdr());
    if (unlikely(!hdr)) {
        RTE_LOG(ERR, USER2, "[%d:%s()] ERR: Failed mbuf hdr alloc for send on port %d\n",
                rte_lcore_index(rte_lcore_id()),
                __func__,
                tcb->tcb_l4.l4cb_interface);

        return NULL;
    }

    /* Prepend the header & update header fields. */
    hdr->port = tcb->tcb_l4.l4cb_interface;

    /*
     * Search for next hop mac.
     */
    nh_mac = route_v4_nh_lookup(tcb->tcb_l4.l4cb_interface,
                                tcb->tcb_l4.l4cb_dst_addr.ip_v4);
    if (nh_mac == TPG_ARP_MAC_NOT_FOUND) {
        /*
         * Normally we should queue the packet if the ARP is not there yet
         * however here we want high volume of traffic, and the ARP should
         * be there before we start testing.
         *
         * Revisit if we want this to be a "REAL" TCP stack ;)
         */
        rte_pktmbuf_free(hdr);
        return NULL;
    }

    /*
     * Build ethernet header
     */
    next_header_offset = eth_build_eth_hdr(hdr, nh_mac, TPG_USE_PORT_MAC,
                                           ETHER_TYPE_IPv4);

    if (next_header_offset <= 0) {
        rte_pktmbuf_free(hdr);
        return NULL;
    }

    /*
     * Build IP header
     */
    if (tcb->tcb_l4.l4cb_domain != AF_INET) {

        TPG_ERROR_ABORT("TODO: %s!\n", "TCP = IPv4 only for now!");

    } else {

        ipv4 = (struct ipv4_hdr *) (rte_pktmbuf_mtod(hdr, uint8_t *) + next_header_offset);

        if (ipv4_build_ipv4_hdr(&tcb->tcb_l4.l4cb_sockopt,
                                hdr, tcb->tcb_l4.l4cb_src_addr.ip_v4,
                                tcb->tcb_l4.l4cb_dst_addr.ip_v4,
                                IPPROTO_TCP,
                                sizeof(struct tcp_hdr) + l4_len,
                                NULL) <= 0) {
            rte_pktmbuf_free(hdr);
            return NULL;
        }
    }

    /*
     * Build TCP header
     */

    *tcp_hdr = tcp_build_tcp_hdr(hdr, tcb, ipv4, sseq, flags);

    if (*tcp_hdr == NULL) {
        rte_pktmbuf_free(hdr);
        return NULL;
    }

    return hdr;
}

/*****************************************************************************
 * tcp_send_data_pkt()
 ****************************************************************************/
bool tcp_send_data_pkt(tcp_control_block_t *tcb, uint32_t sseq, uint32_t flags,
                       struct rte_mbuf *data)
{
    struct rte_mbuf  *hdr;
    struct tcp_hdr   *tcp_hdr;
    tcp_statistics_t *stats;
    uint32_t          pkt_len;

    if (unlikely(!data))
        return false;

    TCB_CHECK(tcb);

    stats = STATS_LOCAL(tcp_statistics_t, tcb->tcb_l4.l4cb_interface);

    hdr = tcp_build_tcp_hdr_mbuf(tcb, sseq, flags, data->pkt_len, &tcp_hdr);
    if (unlikely(!hdr)) {
        INC_STATS(STATS_LOCAL(tcp_statistics_t, tcb->tcb_l4.l4cb_interface),
                  ts_failed_data_pkts);
        rte_pktmbuf_free(data);
        return false;
    }

    /* Append the data part too. */
    hdr->next = data;
    hdr->pkt_len += data->pkt_len;
    hdr->nb_segs += data->nb_segs;

    pkt_len = hdr->pkt_len;

    /* We need to update the checksum in the TCP part now the data has been added */
#if defined(TPG_SW_CHECKSUMMING)
    if (!tcb->tcb_l4.l4cb_sockopt.so_eth.ethso_tx_offload_tcp_cksum)
        tcp_hdr->cksum = ipv4_update_general_l4_cksum(tcp_hdr->cksum, data);
#endif

    /*
     * Send the packet!!
     */
    if (unlikely(!pkt_send_with_hash(tcb->tcb_l4.l4cb_interface, hdr,
                                     L4CB_TX_HASH(&tcb->tcb_l4),
                                     tcb->tcb_trace))) {

        TCB_TRACE(tcb, TSM, DEBUG, "[%s()] ERR: Failed tx on port %d\n",
                  __func__,
                  tcb->tcb_l4.l4cb_interface);
        return false;
    }

    /*
     * Increment transmit counters. Failed counters are incremented lower in
     * the stack.
     */
    INC_STATS(stats, ts_sent_data_pkts);
    INC_STATS_VAL(stats, ts_sent_data_bytes, pkt_len);
    return true;
}

/*****************************************************************************
 * tcp_send_ctrl_pkt_with_sseq()
 ****************************************************************************/
inline bool tcp_send_ctrl_pkt_with_sseq(tcp_control_block_t *tcb, uint32_t sseq,
                                        uint32_t flags)
{
    struct rte_mbuf  *hdr;
    struct tcp_hdr   *tcp_hdr;
    tcp_statistics_t *stats;
    uint32_t          pkt_len;

    TCB_CHECK(tcb);

    stats = STATS_LOCAL(tcp_statistics_t, tcb->tcb_l4.l4cb_interface);

    hdr = tcp_build_tcp_hdr_mbuf(tcb, sseq, flags, 0, &tcp_hdr);
    if (unlikely(!hdr)) {
        INC_STATS(stats, ts_failed_ctrl_pkts);
        return false;
    }

    pkt_len = hdr->pkt_len;

    /*
     * Send the packet!!
     */
    if (unlikely(!pkt_send_with_hash(tcb->tcb_l4.l4cb_interface, hdr,
                                     L4CB_TX_HASH(&tcb->tcb_l4),
                                     tcb->tcb_trace))) {

        TCB_TRACE(tcb, TSM, DEBUG, "[%s()] ERR: Failed tx on port %d\n",
                  __func__,
                  tcb->tcb_l4.l4cb_interface);
        return false;
    }

    /*
     * Increment transmit counters. Failed counters are incremented lower in
     * the stack.
     */
    INC_STATS(stats, ts_sent_ctrl_pkts);
    INC_STATS_VAL(stats, ts_sent_ctrl_bytes, pkt_len);
    return true;
}

/*****************************************************************************
 * tcp_send_ctrl_pkt()
 ****************************************************************************/
bool tcp_send_ctrl_pkt(tcp_control_block_t *tcb, uint32_t flags)
{
    return tcp_send_ctrl_pkt_with_sseq(tcb, tcb->tcb_snd.nxt, flags);
}

/*****************************************************************************
 * tcb_clone()
 ****************************************************************************/
tcp_control_block_t *tcb_clone(tcp_control_block_t *tcb)
{
    tcp_control_block_t *new_tcb;
    uint32_t             new_tcb_id;
    tcp_statistics_t    *stats;

    TCB_CHECK(tcb);

    stats = STATS_LOCAL(tcp_statistics_t, tcb->tcb_l4.l4cb_interface);

    new_tcb = tlkp_alloc_tcb();
    if (unlikely(new_tcb == NULL)) {
        INC_STATS(stats, ts_tcb_alloc_err);
        return NULL;
    }

    /* The cb_id is the only thing that should differ between a clone and the
     * real thing!
     */
    new_tcb_id = L4_CB_ID(&new_tcb->tcb_l4);
    rte_memcpy(new_tcb, tcb, sizeof(*new_tcb));
    L4_CB_ID_SET(&new_tcb->tcb_l4, new_tcb_id);

    /* Set the malloced bit. */
    new_tcb->tcb_malloced = true;
    INC_STATS(stats, ts_tcb_malloced);

    return new_tcb;
}

/*****************************************************************************
 * CLI commands
 *****************************************************************************
 * - "show tcp statistics {details}"
 ****************************************************************************/
struct cmd_show_tcp_statistics_result {
    cmdline_fixed_string_t show;
    cmdline_fixed_string_t tcp;
    cmdline_fixed_string_t statistics;
    cmdline_fixed_string_t details;
};

static cmdline_parse_token_string_t cmd_show_tcp_statistics_T_show =
    TOKEN_STRING_INITIALIZER(struct cmd_show_tcp_statistics_result, show, "show");
static cmdline_parse_token_string_t cmd_show_tcp_statistics_T_tcp =
    TOKEN_STRING_INITIALIZER(struct cmd_show_tcp_statistics_result, tcp, "tcp");
static cmdline_parse_token_string_t cmd_show_tcp_statistics_T_statistics =
    TOKEN_STRING_INITIALIZER(struct cmd_show_tcp_statistics_result, statistics, "statistics");
static cmdline_parse_token_string_t cmd_show_tcp_statistics_T_details =
    TOKEN_STRING_INITIALIZER(struct cmd_show_tcp_statistics_result, details, "details");

static void cmd_show_tcp_statistics_parsed(void *parsed_result __rte_unused,
                                           struct cmdline *cl,
                                           void *data)
{
    int port;
    int core;
    int option = (intptr_t) data;

    for (port = 0; port < rte_eth_dev_count(); port++) {

        /*
         * Calculate totals first
         */
        tcp_statistics_t  total_stats;
        tcp_statistics_t *tcp_stats;

        bzero(&total_stats, sizeof(total_stats));
        STATS_FOREACH_CORE(tcp_statistics_t, port, core, tcp_stats) {
            total_stats.ts_received_pkts += tcp_stats->ts_received_pkts;
            total_stats.ts_received_bytes += tcp_stats->ts_received_bytes;
            total_stats.ts_sent_ctrl_pkts += tcp_stats->ts_sent_ctrl_pkts;
            total_stats.ts_sent_ctrl_bytes += tcp_stats->ts_sent_ctrl_bytes;
            total_stats.ts_sent_data_pkts += tcp_stats->ts_sent_data_pkts;
            total_stats.ts_sent_data_bytes += tcp_stats->ts_sent_data_bytes;
            total_stats.ts_tcb_malloced += tcp_stats->ts_tcb_malloced;
            total_stats.ts_tcb_freed += tcp_stats->ts_tcb_freed;
            total_stats.ts_tcb_not_found += tcp_stats->ts_tcb_not_found;
            total_stats.ts_tcb_alloc_err += tcp_stats->ts_tcb_alloc_err;
            total_stats.ts_to_small_fragment += tcp_stats->ts_to_small_fragment;
            total_stats.ts_hdr_to_small += tcp_stats->ts_hdr_to_small;
            total_stats.ts_invalid_checksum += tcp_stats->ts_invalid_checksum;
            total_stats.ts_failed_ctrl_pkts += tcp_stats->ts_failed_ctrl_pkts;
            total_stats.ts_failed_data_pkts += tcp_stats->ts_failed_data_pkts;
            total_stats.ts_failed_data_clone += tcp_stats->ts_failed_data_clone;

#ifndef _SPEEDY_PKT_PARSE_
            total_stats.ts_reserved_bit_set += tcp_stats->ts_reserved_bit_set;
#endif
        }

        /*
         * Display individual counters
         */
        cmdline_printf(cl, "Port %d TCP statistics:\n", port);

        SHOW_64BIT_STATS("Received Packets", tcp_statistics_t, ts_received_pkts,
                         port,
                         option);

        SHOW_64BIT_STATS("Received Bytes", tcp_statistics_t, ts_received_bytes,
                         port,
                         option);

        SHOW_64BIT_STATS("Sent Ctrl Packets", tcp_statistics_t,
                         ts_sent_ctrl_pkts,
                         port,
                         option);

        SHOW_64BIT_STATS("Sent Ctrl Bytes", tcp_statistics_t,
                         ts_sent_ctrl_bytes,
                         port,
                         option);


        SHOW_64BIT_STATS("Sent Data Packets", tcp_statistics_t,
                         ts_sent_data_pkts,
                         port,
                         option);

        SHOW_64BIT_STATS("Sent Data Bytes", tcp_statistics_t,
                         ts_sent_data_bytes,
                         port,
                         option);

        cmdline_printf(cl, "\n");

        SHOW_64BIT_STATS("Malloced TCBs", tcp_statistics_t, ts_tcb_malloced,
                         port,
                         option);

        SHOW_64BIT_STATS("Freed TCBs", tcp_statistics_t, ts_tcb_freed,
                         port,
                         option);
        SHOW_64BIT_STATS("Not found TCBs", tcp_statistics_t, ts_tcb_not_found,
                         port,
                         option);

        SHOW_64BIT_STATS("TCB alloc errors", tcp_statistics_t, ts_tcb_alloc_err,
                         port,
                         option);

        cmdline_printf(cl, "\n");

        SHOW_16BIT_STATS("Invalid checksum", tcp_statistics_t,
                         ts_invalid_checksum,
                         port,
                         option);

        SHOW_16BIT_STATS("Small mbuf fragment", tcp_statistics_t,
                         ts_to_small_fragment,
                         port,
                         option);

        SHOW_16BIT_STATS("TCP hdr to small", tcp_statistics_t, ts_hdr_to_small,
                         port,
                         option);

        SHOW_16BIT_STATS("Ctrl Failed Packets", tcp_statistics_t,
                         ts_failed_ctrl_pkts,
                         port,
                         option);

        SHOW_16BIT_STATS("DATA Failed Packets", tcp_statistics_t,
                         ts_failed_data_pkts,
                         port,
                         option);

        SHOW_16BIT_STATS("DATA Clone Failed", tcp_statistics_t,
                         ts_failed_data_clone,
                         port,
                         option);

#ifndef _SPEEDY_PKT_PARSE_
        SHOW_16BIT_STATS("Reserved bit set", tcp_statistics_t,
                         ts_reserved_bit_set,
                         port,
                         option);
#endif

        cmdline_printf(cl, "\n");
    }

}

cmdline_parse_inst_t cmd_show_tcp_statistics = {
    .f = cmd_show_tcp_statistics_parsed,
    .data = NULL,
    .help_str = "show tcp statistics",
    .tokens = {
        (void *)&cmd_show_tcp_statistics_T_show,
        (void *)&cmd_show_tcp_statistics_T_tcp,
        (void *)&cmd_show_tcp_statistics_T_statistics,
        NULL,
    },
};

cmdline_parse_inst_t cmd_show_tcp_statistics_details = {
    .f = cmd_show_tcp_statistics_parsed,
    .data = (void *) (intptr_t) 'd',
    .help_str = "show tcp statistics details",
    .tokens = {
        (void *)&cmd_show_tcp_statistics_T_show,
        (void *)&cmd_show_tcp_statistics_T_tcp,
        (void *)&cmd_show_tcp_statistics_T_statistics,
        (void *)&cmd_show_tcp_statistics_T_details,
        NULL,
    },
};

/*****************************************************************************
 * Main menu context
 ****************************************************************************/
static cmdline_parse_ctx_t cli_ctx[] = {
    &cmd_show_tcp_statistics,
    &cmd_show_tcp_statistics_details,
    NULL,
};
