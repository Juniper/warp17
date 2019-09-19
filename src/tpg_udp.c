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
 *     tpg_udp.c
 *
 * Description:
 *     General UDP processing, hopefully it will work for v4 and v6.
 *
 * Author:
 *     Dumitru Ceara, Eelco Chaudron
 *
 * Initial Created:
 *     10/22/2015
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
/* Define UDP global statistics. Each thread has its own set of locally
 * allocated stats which are accessible through STATS_GLOBAL(type, core, port).
 */
STATS_DEFINE(tpg_udp_statistics_t);

/*****************************************************************************
 * Define statemachine state and event names
 ****************************************************************************/
const char *stateNamesUDP[US_MAX_STATE] = {

    "US_INIT",
    "US_LISTEN",
    "US_OPEN",
    "US_CLOSED"
};

/*****************************************************************************
 * Forward declarations
 ****************************************************************************/
static cmdline_parse_ctx_t cli_ctx[];

/*****************************************************************************
 * udp_init()
 ****************************************************************************/
bool udp_init(void)
{
    /*
     * Add port module CLI commands
     */
    if (!cli_add_main_ctx(cli_ctx)) {
        RTE_LOG(ERR, USER1, "ERROR: Can't add UDP specific CLI commands!\n");
        return false;
    }

    /*
     * Allocate memory for UDP statistics, and clear all of them
     */
    if (STATS_GLOBAL_INIT(tpg_udp_statistics_t, "udp_stats") == NULL) {
        RTE_LOG(ERR, USER1,
                "ERROR: Failed allocating UDP statistics memory!\n");
        return false;
    }

    return true;
}

/*****************************************************************************
 * udp_lcore_init()
 ****************************************************************************/
void udp_lcore_init(uint32_t lcore_id)
{
    /* Init the local stats. */
    if (STATS_LOCAL_INIT(tpg_udp_statistics_t, "udp_stats", lcore_id) == NULL) {
        TPG_ERROR_ABORT("[%d:%s() Failed to allocate per lcore udp_stats!\n",
                        rte_lcore_index(lcore_id),
                        __func__);
    }
}

/*****************************************************************************
 * udp_process_incoming()
 ****************************************************************************/
static int udp_process_incoming(udp_control_block_t *ucb,
                                packet_control_block_t *pcb)
{
    test_case_info_t *tc_info;
    app_deliver_cb_t  app_deliver_cb;
    tpg_app_proto_t   app_id;

    UCB_CHECK(ucb);

    app_id = ucb->ucb_l4.l4cb_app_data.ad_type;
    app_deliver_cb = APP_CALL(deliver, app_id);

    /* It's a bit ugly to use the test case here but this is the only
     * place where we need to look it up so it would be inneficient to avoid
     * doing it here but then look it up every time in the app implementation.
     */
    tc_info = TEST_GET_INFO(ucb->ucb_l4.l4cb_interface,
                            ucb->ucb_l4.l4cb_test_case_id);

    return app_deliver_cb(&ucb->ucb_l4, &ucb->ucb_l4.l4cb_app_data,
                          tc_info->tci_app_stats,
                          pcb->pcb_mbuf,
                          pcb->pcb_tstamp);
}

/*****************************************************************************
 * ucb_clone()
 ****************************************************************************/
static udp_control_block_t *ucb_clone(udp_control_block_t *ucb)
{
    udp_control_block_t  *new_ucb;
    uint32_t              new_ucb_id;
    phys_addr_t           new_phys_addr;
    tpg_udp_statistics_t *stats;

    UCB_CHECK(ucb);

    stats = STATS_LOCAL(tpg_udp_statistics_t, ucb->ucb_l4.l4cb_interface);
    new_ucb = tlkp_alloc_ucb();
    if (unlikely(new_ucb == NULL)) {
        INC_STATS(stats, us_ucb_alloc_err);
        return NULL;
    }

    /* The cb_id and phys address are the only things that should differ
     * between a clone and the real thing!
     */
    new_ucb_id = L4_CB_ID(&new_ucb->ucb_l4);
    new_phys_addr = new_ucb->ucb_l4.l4cb_phys_addr;

    rte_memcpy(new_ucb, ucb, sizeof(*new_ucb));

    L4_CB_ID_SET(&new_ucb->ucb_l4, new_ucb_id);
    new_ucb->ucb_l4.l4cb_phys_addr = new_phys_addr;

    /* Set the malloced bit. */
    new_ucb->ucb_malloced = true;
    INC_STATS(stats, us_ucb_malloced);

    return new_ucb;
}

/*****************************************************************************
 * udp_receive_pkt()
 *
 * Return the mbuf only if it needs to be free'ed back to the pool, if it was
 * consumed, or needed later (ip refrag), return NULL.
 ****************************************************************************/
struct rte_mbuf *udp_receive_pkt(packet_control_block_t *pcb,
                                 struct rte_mbuf *mbuf)
{
    tpg_udp_statistics_t *stats;
    struct udp_hdr       *udp_hdr;
    udp_control_block_t  *ucb;
    int                   error;

    stats = STATS_LOCAL(tpg_udp_statistics_t, pcb->pcb_port);

    if (unlikely(rte_pktmbuf_data_len(mbuf) < sizeof(struct udp_hdr))) {
        RTE_LOG(DEBUG, USER2, "[%d:%s()] ERR: mbuf fragment to small for udp_hdr!\n",
                pcb->pcb_core_index, __func__);

        INC_STATS(stats, us_to_small_fragment);
        return mbuf;
    }

    udp_hdr = rte_pktmbuf_mtod(mbuf, struct udp_hdr *);

    PKT_TRACE(pcb, UDP, DEBUG, "sport=%u, dport=%u, data_len=%u, cksum=%"PRIX16,
              rte_be_to_cpu_16(udp_hdr->src_port),
              rte_be_to_cpu_16(udp_hdr->dst_port),
              rte_be_to_cpu_16(udp_hdr->dgram_len),
              udp_hdr->dgram_cksum);

    /*
     * Update stats
     */
    INC_STATS(stats, us_received_pkts);
    INC_STATS_VAL(stats, us_received_bytes, pcb->pcb_l4_len);

    /*
     * Handle checksum...
     */
#if !defined(TPG_SW_CHECKSUMMING)
    if (true) {
#else
    if ((RTE_PER_LCORE(local_port_dev_info)[pcb->pcb_port].pi_dev_info.rx_offload_capa &
         DEV_RX_OFFLOAD_UDP_CKSUM) != 0) {
#endif

        if (unlikely((mbuf->ol_flags & PKT_RX_L4_CKSUM_BAD) != 0)) {
            RTE_LOG(DEBUG, USER2, "[%d:%s()] ERR: Invalid UDP checksum!\n",
                    pcb->pcb_core_index, __func__);

            INC_STATS(stats, us_invalid_checksum);
            return mbuf;
        }
    } else {
        /*
         * No HW checksum support do it manually...
         */
        if (unlikely(udp_hdr->dgram_cksum != 0) &&
            ipv4_general_l4_cksum(mbuf, pcb->pcb_ipv4, 0,
                                  rte_be_to_cpu_16(udp_hdr->dgram_len)) != 0xFFFF) {

            RTE_LOG(DEBUG, USER2, "[%d:%s()] ERR: Invalid UDP checksum!\n",
                    pcb->pcb_core_index, __func__);

            INC_STATS(stats, us_invalid_checksum);
            return mbuf;
        }
    }

    /*
     * Calculate hash...
     *
     */
    if (likely((mbuf->ol_flags & PKT_RX_RSS_HASH) != 0)) {
        pcb->pcb_hash = mbuf->hash.rss;
        pcb->pcb_hash_valid = true;
    } else {
        pcb->pcb_hash = tlkp_calc_pkt_hash(pcb->pcb_ipv4->src_addr,
                                           pcb->pcb_ipv4->dst_addr,
                                           udp_hdr->src_port,
                                           udp_hdr->dst_port);
        pcb->pcb_hash_valid = true;
    }

    /*
     * Update mbuf/pcb and send packet of to the client handler
     */
    pcb->pcb_udp = udp_hdr;
    pcb->pcb_l5_len = pcb->pcb_l4_len - sizeof(struct udp_hdr);
    assert(pcb->pcb_mbuf == mbuf);
    mbuf = data_adj_chain(mbuf, sizeof(struct udp_hdr));
    pcb->pcb_mbuf = mbuf;

    /*
     * First try known session lookup
     */
    ucb = tlkp_find_v4_ucb(pcb->pcb_port, pcb->pcb_hash,
                           rte_be_to_cpu_32(pcb->pcb_ipv4->dst_addr),
                           rte_be_to_cpu_32(pcb->pcb_ipv4->src_addr),
                           rte_be_to_cpu_16(udp_hdr->dst_port),
                           rte_be_to_cpu_16(udp_hdr->src_port));

    /*
     * If no existing ucb see if we have a server available that is
     * accepting new requests.
     */
    if (ucb == NULL) {
        uint32_t udp_listen_hash;

        /*
         * Do the server lookup based on the dest ip and dest port.
         */
        udp_listen_hash = tlkp_calc_pkt_hash(0, /* src_addr ANY */
                                             pcb->pcb_ipv4->dst_addr,
                                             0, /* src_port ANY */
                                             udp_hdr->dst_port);

        ucb = tlkp_find_v4_ucb(pcb->pcb_port, udp_listen_hash,
                               rte_be_to_cpu_32(pcb->pcb_ipv4->dst_addr),
                               0, /* src_addr ANY */
                               rte_be_to_cpu_16(udp_hdr->dst_port),
                               0 /* src_port ANY */);

        /* If found then we can create the 4-tuple UCB for this session. */
        if (ucb) {
            udp_control_block_t *new_ucb;

            /* Clone the tcb and work on the clone from now on. */
            new_ucb = ucb_clone(ucb);
            if (unlikely(new_ucb == NULL))
                return mbuf;

            new_ucb->ucb_l4.l4cb_dst_addr =
                TPG_IPV4(rte_be_to_cpu_32(pcb->pcb_ipv4->src_addr));
            new_ucb->ucb_l4.l4cb_dst_port =
                rte_be_to_cpu_16(pcb->pcb_tcp->src_port);

            /* Recompute the hash and add the new_tcb to the htable. */
            l4_cb_calc_connection_hash(&new_ucb->ucb_l4);

            error = tlkp_add_ucb(new_ucb);
            if (error) {
                TRACE_FMT(UDP, ERROR, "[%s()] failed to add clone ucb: %s(%d).",
                          __func__, rte_strerror(-error), -error);
                return mbuf;
            }
            /* Server UCBs go directly to OPEN when they receive data. */
            new_ucb->ucb_state = US_OPEN;

            ucb = new_ucb;

            /* Notify that the connection is UP. */
            UDP_NOTIF(TEST_NOTIF_SESS_SRV_CONNECTED, ucb);
        }
    }

    if (ucb) {
        /*
         * If the ucb is marked for tracing then we should also trace the
         * pcb (although this might be a little late as we already tried
         * to log everything about the pcb...)
         */
        if (unlikely(ucb->ucb_trace))
            pcb->pcb_trace = true;

        if (ucb != NULL)
            pcb->pcb_sockopt = &ucb->ucb_l4.l4cb_sockopt;

        udp_process_incoming(ucb, pcb);

        /* If the stack decided to keep this packet we shouldn' allow the
         * rest of the code to free it.
         */
        if (unlikely(pcb->pcb_mbuf_stored))
            mbuf = NULL;
    } else {
        INC_STATS(stats, us_ucb_not_found);
    }

    return mbuf;
}

/*****************************************************************************
 * udp_build_hdr()
 ****************************************************************************/
static struct udp_hdr *udp_build_hdr(udp_control_block_t *ucb,
                                     struct rte_mbuf *mbuf,
                                     struct ipv4_hdr *ipv4_hdr,
                                     uint16_t dgram_len)
{
    uint16_t        udp_hdr_len = sizeof(struct udp_hdr);
    uint16_t        udp_hdr_offset = rte_pktmbuf_data_len(mbuf);
    struct udp_hdr *udp_hdr;
    int             ip_hdr_len;

    udp_hdr = (struct udp_hdr *) rte_pktmbuf_append(mbuf, udp_hdr_len);

    if (udp_hdr == NULL)
        return NULL;

    udp_hdr->src_port = rte_cpu_to_be_16(ucb->ucb_l4.l4cb_src_port);
    udp_hdr->dst_port = rte_cpu_to_be_16(ucb->ucb_l4.l4cb_dst_port);

    udp_hdr->dgram_len = rte_cpu_to_be_16(dgram_len + udp_hdr_len);
    mbuf->l4_len = udp_hdr_len;
    ip_hdr_len = ((ipv4_hdr->version_ihl & 0x0F) << 2);

#if !defined(TPG_SW_CHECKSUMMING)
    if (true) {
#else
    if (ucb->ucb_l4.l4cb_sockopt.so_eth.ethso_tx_offload_udp_cksum) {
#endif
        mbuf->ol_flags |= PKT_TX_UDP_CKSUM | PKT_TX_IPV4;

        udp_hdr->dgram_cksum =
            ipv4_udptcp_phdr_cksum(ipv4_hdr,
                                   rte_cpu_to_be_16(ipv4_hdr->total_length) -
                                   (ip_hdr_len));
    } else {
        /*
         * No HW checksum support do it manually, however up to here we can only
         * calculate the header checksum, so we do...
         */
        udp_hdr->dgram_cksum = 0;
        udp_hdr->dgram_cksum = ipv4_general_l4_cksum(mbuf,
                                                     ipv4_hdr,
                                                     udp_hdr_offset,
                                                     udp_hdr_len);
    }

    return udp_hdr;
}

/*****************************************************************************
 * udp_build_hdr_mbuf()
 ****************************************************************************/
static struct rte_mbuf *udp_build_hdr_mbuf(udp_control_block_t *ucb,
                                           uint32_t l4_len,
                                           struct udp_hdr **udp_hdr_p)
{
    struct rte_mbuf *mbuf;
    struct ipv4_hdr *ip_hdr;

    if (ucb->ucb_l4.l4cb_domain != AF_INET) {
        TPG_ERROR_ABORT("TODO: UDP = IPv4 only for now!\n");
        return NULL;
    }

    mbuf = ipv4_build_hdr_mbuf(&ucb->ucb_l4, IPPROTO_UDP,
                                sizeof(struct udp_hdr) + l4_len,
                                &ip_hdr);
    if (unlikely(!mbuf))
        return NULL;

    /*
     * Build UDP header
     */
    *udp_hdr_p = udp_build_hdr(ucb, mbuf, ip_hdr, l4_len);
    if (unlikely(!(*udp_hdr_p))) {
        pkt_mbuf_free(mbuf);
        return NULL;
    }

    return mbuf;
}

/*****************************************************************************
 * udp_send_pkt()
 ****************************************************************************/
static int
udp_send_pkt(udp_control_block_t *ucb, struct rte_mbuf *hdr_mbuf,
             struct udp_hdr *udp_hdr,
             struct rte_mbuf *data_mbuf,
             uint32_t *data_sent)
{
    tpg_udp_statistics_t *stats;

    stats = STATS_LOCAL(tpg_udp_statistics_t, ucb->ucb_l4.l4cb_interface);

    /* Perform TX timestamp propagation if needed. */
    tstamp_data_append(hdr_mbuf, data_mbuf);

#if defined(TPG_SW_CHECKSUMMING)
    if (data_mbuf != NULL &&
            !ucb->ucb_l4.l4cb_sockopt.so_eth.ethso_tx_offload_udp_cksum) {
        if (unlikely(DATA_IS_TSTAMP(data_mbuf))) {
            tstamp_write_cksum_offset(hdr_mbuf,
                                      hdr_mbuf->pkt_len -
                                      sizeof(struct udp_hdr) +
                                      RTE_PTR_DIFF(&udp_hdr->dgram_cksum,
                                                   udp_hdr));
        }
    }
#endif /*defined(TPG_SW_CHECKSUMMING)*/
    /* Append the data part too. */
    hdr_mbuf->next = data_mbuf;
    hdr_mbuf->pkt_len += data_mbuf->pkt_len;
    hdr_mbuf->nb_segs += data_mbuf->nb_segs;

    *data_sent = data_mbuf->pkt_len;

    /*
     * Increment transmit bytes counters. Failed counters are incremented lower
     * in the stack.
     */
    INC_STATS_VAL(stats, us_sent_ctrl_bytes, hdr_mbuf->l4_len);
    INC_STATS_VAL(stats, us_sent_data_bytes, *data_sent);

    /* We need to update the checksum in the UDP part now the data has been
     * added.
     */
#if defined(TPG_SW_CHECKSUMMING)
    if (!ucb->ucb_l4.l4cb_sockopt.so_eth.ethso_tx_offload_udp_cksum) {
        udp_hdr->dgram_cksum =
            ipv4_update_general_l4_cksum(udp_hdr->dgram_cksum, data_mbuf);
    }
#else
    RTE_SET_USED(udp_hdr);
#endif /*defined(TPG_SW_CHECKSUMMING)*/

    /*
     * Send the packet!!
     */
    if (unlikely(!pkt_send_with_hash(ucb->ucb_l4.l4cb_interface, hdr_mbuf,
                                     L4CB_TX_HASH(&ucb->ucb_l4),
                                     ucb->ucb_trace))) {

        TRACE_FMT(UDP, DEBUG, "[%s()] ERR: Failed tx on port %d\n",
                  __func__,
                  ucb->ucb_l4.l4cb_interface);
        return -ENOMEM;
    }

    return 0;
}

/*****************************************************************************
 * udp_open_v4_connection()
 *
 * NOTE:
 *   We will accept any source/destination IP/port so odd values can be
 *   tested. If *ucb is NULL we should malloc one.
 *
 *   The caller needs to make sure that the core opening the connection is
 *   the core that will handle the rx queue on which the traffic is received
 *   for the connection!
 ****************************************************************************/
int udp_open_v4_connection(udp_control_block_t **ucb, uint32_t eth_port,
                           uint32_t src_ip_addr, uint16_t src_port,
                           uint32_t dst_ip_addr, uint16_t dst_port,
                           uint32_t test_case_id, tpg_app_proto_t app_id,
                           sockopt_t *sockopt, uint32_t flags)
{
    int                  rc = 0;
    udp_control_block_t *ucb_p;
    bool                 active;
    uint32_t             malloc_flag;
    bool                 ucb_reuse;

    if (unlikely(ucb == NULL))
        return -EINVAL;

    ucb_reuse = (flags & TCG_CB_REUSE_CB);

    if (unlikely(!ucb_reuse && sockopt == NULL))
        return -EINVAL;

    /* If the *ucb is NULL we should malloc one and mark that we need
     * to free it later.
     */
    if (*ucb == NULL) {
        *ucb = tlkp_alloc_ucb();
        if (*ucb == NULL) {
            INC_STATS(STATS_LOCAL(tpg_udp_statistics_t, eth_port),
                      us_ucb_alloc_err);
            return -ENOMEM;
        }
        malloc_flag = TCG_CB_MALLOCED;
        INC_STATS(STATS_LOCAL(tpg_udp_statistics_t, eth_port), us_ucb_malloced);
    } else {
        UCB_CHECK(*ucb);
        malloc_flag = 0;
    }

    /* Although ip 0.0.0.0 is invalid and port 0 is also invalid, we
     * assume that if at least the dst_ip or dst_port are set then
     * this is an active connection.
     */
    active = (dst_ip_addr != 0 || dst_port != 0);

    ucb_p = *ucb;

    if (!ucb_reuse) {
        tlkp_init_ucb(ucb_p, src_ip_addr, dst_ip_addr, src_port, dst_port,
                      0,
                      eth_port,
                      test_case_id,
                      app_id,
                      sockopt,
                      (flags | malloc_flag));
    }

    if (active) {
        ucb_p->ucb_active = true;
        ucb_p->ucb_state = US_OPEN;

        /* Udp skips some states so let's batch some notifications. */
        UDP_NOTIF(TEST_NOTIF_SESS_CONNECTED_IMM, ucb_p);
    } else {
        ucb_p->ucb_active = false;
        ucb_p->ucb_state = US_LISTEN;

        UDP_NOTIF(TEST_NOTIF_SESS_LISTEN, ucb_p);
    }

    rc = tlkp_add_ucb(ucb_p);
    if (rc != 0) {

        if (!ucb_reuse && ucb_p->ucb_malloced)
            tlkp_free_ucb(ucb_p);

        return rc;
    }

    return rc;
}

/*****************************************************************************
 * udp_listen_v4()
 * NOTE:
 *      the function MUST be called by all cores that might process incoming
 *      packets for this server.
 ****************************************************************************/
int udp_listen_v4(udp_control_block_t **ucb, uint32_t eth_port,
                  uint32_t local_ip_addr, uint16_t local_port,
                  uint32_t test_case_id, tpg_app_proto_t app_id,
                  sockopt_t *sockopt, uint32_t flags)
{
    return udp_open_v4_connection(ucb, eth_port, local_ip_addr, local_port,
                                  0, /* remote_ip ANY */
                                  0, /* remote_port ANY */
                                  test_case_id,
                                  app_id,
                                  sockopt,
                                  flags);
}

/*****************************************************************************
 * udp_connection_cleanup()
 ****************************************************************************/
void udp_connection_cleanup(udp_control_block_t *ucb)
{
    UCB_CHECK(ucb);

    /* Free any allocated memory. */
    tlkp_free_ucb(ucb);
}

/*****************************************************************************
 * udp_close_v4()
 ****************************************************************************/
int udp_close_v4(udp_control_block_t *ucb)
{
    if (unlikely(ucb == NULL))
        return -EINVAL;

    UCB_CHECK(ucb);

    ucb->ucb_state = US_CLOSED;

    /* Notify that the connection is DOWN. */
    UDP_NOTIF(TEST_NOTIF_SESS_CLOSING, ucb);
    UDP_NOTIF(TEST_NOTIF_SESS_CLOSED, ucb);

    /*
     * Remove from the lookup table.
     */
    tlkp_delete_ucb(ucb);

    if (ucb->ucb_malloced) {
        INC_STATS(STATS_LOCAL(tpg_udp_statistics_t, ucb->ucb_l4.l4cb_interface),
                  us_ucb_freed);
        udp_connection_cleanup(ucb);
    }

    return 0;
}

/*****************************************************************************
 * udp_send_v4()
 ****************************************************************************/
int udp_send_v4(udp_control_block_t *ucb, struct rte_mbuf *data_mbuf,
                uint32_t *data_sent)
{
    struct rte_mbuf      *hdr;
    struct udp_hdr       *udp_hdr;
    tpg_udp_statistics_t *stats;
    int                   err;

    UCB_CHECK(ucb);

    if (ucb->ucb_l4.l4cb_domain != AF_INET) {
        TPG_ERROR_ABORT("TODO: UDP = IPv4 only for now!\n");
        return -EINVAL;
    }

    stats = STATS_LOCAL(tpg_udp_statistics_t, ucb->ucb_l4.l4cb_interface);

    hdr = udp_build_hdr_mbuf(ucb, data_mbuf->pkt_len, &udp_hdr);
    if (unlikely(!hdr)) {
        INC_STATS(stats, us_failed_pkts);
        pkt_mbuf_free(data_mbuf);
        return -ENOMEM;
    }

    err = udp_send_pkt(ucb, hdr, udp_hdr, data_mbuf, data_sent);
    if (likely(err == 0)) {
        /*
        * Increment transmit counters. Failed counters are incremented lower in
        * the stack.
        */
        INC_STATS(stats, us_sent_pkts);
    }

    return err;
}

/*****************************************************************************
 * CLI commands
 *****************************************************************************
 * - "show udp statistics {details}"
 ****************************************************************************/
struct cmd_show_udp_statistics_result {
    cmdline_fixed_string_t show;
    cmdline_fixed_string_t udp;
    cmdline_fixed_string_t statistics;
    cmdline_fixed_string_t details;
    cmdline_fixed_string_t port_kw;
    uint32_t               port;
};

static cmdline_parse_token_string_t cmd_show_udp_statistics_T_show =
    TOKEN_STRING_INITIALIZER(struct cmd_show_udp_statistics_result, show, "show");
static cmdline_parse_token_string_t cmd_show_udp_statistics_T_udp =
    TOKEN_STRING_INITIALIZER(struct cmd_show_udp_statistics_result, udp, "udp");
static cmdline_parse_token_string_t cmd_show_udp_statistics_T_statistics =
    TOKEN_STRING_INITIALIZER(struct cmd_show_udp_statistics_result, statistics, "statistics");
static cmdline_parse_token_string_t cmd_show_udp_statistics_T_details =
    TOKEN_STRING_INITIALIZER(struct cmd_show_udp_statistics_result, details, "details");
static cmdline_parse_token_string_t cmd_show_udp_statistics_T_port_kw =
        TOKEN_STRING_INITIALIZER(struct cmd_show_udp_statistics_result, port_kw, "port");
static cmdline_parse_token_num_t cmd_show_udp_statistics_T_port =
        TOKEN_NUM_INITIALIZER(struct cmd_show_udp_statistics_result, port, UINT32);

static void cmd_show_udp_statistics_parsed(void *parsed_result __rte_unused,
                                           struct cmdline *cl,
                                           void *data)
{
    uint32_t                               port;
    int                                    option = (intptr_t) data;
    struct cmd_show_udp_statistics_result *pr = parsed_result;
    printer_arg_t                          parg = TPG_PRINTER_ARG(cli_printer, cl);

    for (port = 0; port < rte_eth_dev_count(); port++) {
        if ((option == 'p' || option == 'c') && port != pr->port)
            continue;

        /*
         * Calculate totals first
         */
        tpg_udp_statistics_t total_stats;

        if (test_mgmt_get_udp_stats(port, &total_stats, &parg) != 0)
            continue;

        /*
         * Display individual counters
         */
        cmdline_printf(cl, "Port %d UDP statistics:\n", port);

        SHOW_64BIT_STATS("Received Packets", tpg_udp_statistics_t,
                         us_received_pkts,
                         port,
                         option);

        SHOW_64BIT_STATS("Received Bytes", tpg_udp_statistics_t,
                         us_received_bytes,
                         port,
                         option);

        SHOW_64BIT_STATS("Sent Packets", tpg_udp_statistics_t,
                         us_sent_pkts,
                         port,
                         option);

        SHOW_64BIT_STATS("Sent Ctrl Bytes", tpg_udp_statistics_t,
                         us_sent_ctrl_bytes,
                         port,
                         option);

        SHOW_64BIT_STATS("Sent Data Bytes", tpg_udp_statistics_t,
                         us_sent_data_bytes,
                         port,
                         option);


        cmdline_printf(cl, "\n");

        SHOW_64BIT_STATS("Malloced UCBs", tpg_udp_statistics_t,
                         us_ucb_malloced,
                         port,
                         option);

        SHOW_64BIT_STATS("Freed UCBs", tpg_udp_statistics_t,
                         us_ucb_freed,
                         port,
                         option);
        SHOW_64BIT_STATS("Not found UCBs", tpg_udp_statistics_t,
                         us_ucb_not_found,
                         port,
                         option);

        SHOW_64BIT_STATS("UCB alloc errors", tpg_udp_statistics_t,
                         us_ucb_alloc_err,
                         port,
                         option);

        cmdline_printf(cl, "\n");

        SHOW_32BIT_STATS("Invalid checksum", tpg_udp_statistics_t,
                         us_invalid_checksum,
                         port,
                         option);

        SHOW_32BIT_STATS("Small mbuf fragment", tpg_udp_statistics_t,
                         us_to_small_fragment,
                         port,
                         option);

        SHOW_32BIT_STATS("Failed Packets", tpg_udp_statistics_t,
                         us_failed_pkts,
                         port,
                         option);

        cmdline_printf(cl, "\n");
    }

}

cmdline_parse_inst_t cmd_show_udp_statistics = {
    .f = cmd_show_udp_statistics_parsed,
    .data = NULL,
    .help_str = "show udp statistics",
    .tokens = {
        (void *)&cmd_show_udp_statistics_T_show,
        (void *)&cmd_show_udp_statistics_T_udp,
        (void *)&cmd_show_udp_statistics_T_statistics,
        NULL,
    },
};

cmdline_parse_inst_t cmd_show_udp_statistics_details = {
    .f = cmd_show_udp_statistics_parsed,
    .data = (void *) (intptr_t) 'd',
    .help_str = "show udp statistics details",
    .tokens = {
        (void *)&cmd_show_udp_statistics_T_show,
        (void *)&cmd_show_udp_statistics_T_udp,
        (void *)&cmd_show_udp_statistics_T_statistics,
        (void *)&cmd_show_udp_statistics_T_details,
        NULL,
    },
};

cmdline_parse_inst_t cmd_show_udp_statistics_port = {
    .f = cmd_show_udp_statistics_parsed,
    .data = (void *) (intptr_t) 'p',
    .help_str = "show udp statistics port <id>",
    .tokens = {
        (void *)&cmd_show_udp_statistics_T_show,
        (void *)&cmd_show_udp_statistics_T_udp,
        (void *)&cmd_show_udp_statistics_T_statistics,
        (void *)&cmd_show_udp_statistics_T_port_kw,
        (void *)&cmd_show_udp_statistics_T_port,
        NULL,
    },
};

/* ATTENTION: data is gonna be filled with 'c' which means "port and details" */
cmdline_parse_inst_t cmd_show_udp_statistics_port_details = {
    .f = cmd_show_udp_statistics_parsed,
    .data = (void *) (intptr_t) 'c',
    .help_str = "show udp statistics details port <id>",
    .tokens = {
        (void *)&cmd_show_udp_statistics_T_show,
        (void *)&cmd_show_udp_statistics_T_udp,
        (void *)&cmd_show_udp_statistics_T_statistics,
        (void *)&cmd_show_udp_statistics_T_details,
        (void *)&cmd_show_udp_statistics_T_port_kw,
        (void *)&cmd_show_udp_statistics_T_port,
        NULL,
    },
};

/*****************************************************************************
 * Main menu context
 ****************************************************************************/
static cmdline_parse_ctx_t cli_ctx[] = {
    &cmd_show_udp_statistics,
    &cmd_show_udp_statistics_details,
    &cmd_show_udp_statistics_port,
    &cmd_show_udp_statistics_port_details,
    NULL,
};
