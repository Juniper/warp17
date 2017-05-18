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
 *     tpg_tcp.h
 *
 * Description:
 *     General TCP processing, hopefully it will work for v4 and v6.
 *
 * Author:
 *     Dumitru Ceara, Eelco Chaudron
 *
 * Initial Created:
 *     06/29/2015
 *
 * Notes:
 *
 */

/*****************************************************************************
 * Multiple include protection
 ****************************************************************************/
#ifndef _H_TPG_TCP_
#define _H_TPG_TCP_

/*****************************************************************************
 * TCP statistics
 ****************************************************************************/
typedef struct tcp_statistics_s {

    uint64_t ts_received_pkts;
    uint64_t ts_received_bytes;

    uint64_t ts_sent_ctrl_pkts;
    uint64_t ts_sent_ctrl_bytes;

    uint64_t ts_sent_data_pkts;
    uint64_t ts_sent_data_bytes;

    uint64_t ts_tcb_malloced;
    uint64_t ts_tcb_freed;
    uint64_t ts_tcb_not_found;
    uint64_t ts_tcb_alloc_err;


    /* Unlikely uint16_t error counters */

    uint16_t ts_to_small_fragment;
    uint16_t ts_hdr_to_small;
    uint16_t ts_invalid_checksum;
    uint16_t ts_failed_ctrl_pkts;
    uint16_t ts_failed_data_pkts;
    uint16_t ts_failed_data_clone;

#ifndef _SPEEDY_PKT_PARSE_
    uint16_t ts_reserved_bit_set;
#endif

} __rte_cache_aligned tcp_statistics_t;

/*****************************************************************************
 * TCP Retransmission definition
 ****************************************************************************/
typedef struct tcb_retrans_s {
    struct rte_mbuf *tr_data_mbufs;
    struct rte_mbuf *tr_last_mbuf; /* Used to avoid walking the list every time. */

    /* Also includes not yet transmitted data. */
    uint32_t         tr_total_size;
} tcb_retrans_t;

/*****************************************************************************
 * TCP Receive buffer definitions
 ****************************************************************************/
typedef struct tcb_buf_hdr_s {
    LIST_ENTRY(tcb_buf_hdr_s) tbh_entry;

    struct rte_mbuf *tbh_mbuf;
    uint32_t         tbh_seg_seq;
} tcb_buf_hdr_t;

typedef LIST_HEAD(tcb_buf_hdr_entry_s, tcb_buf_hdr_s) tcb_buf_entry_t;

/*****************************************************************************
 * Checks if the start of the 'a' rcv_buffer is before the start of 'b'
 ****************************************************************************/
#define TCB_RCVBUF_LE(a, b) \
    (SEG_LE((a)->seg_seq, (b)->seg_seq))

/*****************************************************************************
 * Checks if the 'a' rcv buffer is included in 'b'
 ****************************************************************************/
#define TCB_RCVBUF_CONTAINED(a, b)                           \
    (SEG_GE((a)->tbh_seg_seq, (b)->tbh_seg_seq) &&           \
        (SEG_LE((a)->tbh_seg_seq + (a)->tbh_mbuf->pkt_len,   \
                (b)->tbh_seg_seq + (b)->tbh_mbuf->pkt_len)))

/*****************************************************************************
 * Stores the tcb_buf_hdr in the first bytes of the mbuf data.
 * NOTE:
 *      We make the assumption that by the time TCP handles a data packet the
 *      L2-L4 headers have been stripped and we have room to store the rcv_buf
 *      header.
 ****************************************************************************/
#define MBUF_STORE_RCVBUF_HDR(mbuf, hdr) \
    rte_memcpy((mbuf)->buf_addr, (hdr), sizeof(*hdr))

/*****************************************************************************
 * Returns the rcv buf header we embedded in the mbuf.
 * NOTE:
 *      We make the assumption that by the time TCP handles a data packet the
 *      L2-L4 headers have been stripped and we have room to store the rcv_buf
 *      header.
 ****************************************************************************/
#define MBUF_TO_RCVBUF_HDR(mbuf) \
    ((tcb_buf_hdr_t *)(mbuf)->buf_addr)

/*****************************************************************************
 * TCP Control Block
 ****************************************************************************/
typedef struct tcb_snd_s {
    uint32_t una;
    uint32_t nxt;
    uint32_t wnd;
    uint32_t uo;
    uint32_t wl1;
    uint32_t wl2;
    uint32_t iss;
} tcb_snd_t;

typedef struct tcb_rcv_s {
    uint32_t nxt;
    uint32_t wnd;
    uint32_t up;
    uint32_t irs;
} tcb_rcv_t;

typedef struct tcp_control_block_s {
    /*
     * Generic L4 control block.
     */
    l4_control_block_t tcb_l4;

    /*
     * TCP slow timer linkage (slow wait/keep-alive/etc.)
     */
    tmr_list_entry(tcp_control_block_s) tcb_slow_tmr_entry;

    /*
     * TCP retrans timer linkage
     */
    tmr_list_entry(tcp_control_block_s) tcb_retrans_tmr_entry;

    /*
     * TCP state-machine information
     */
    tcpState_t         tcb_state;

    /*
     * TCP send receive pointers.
     */
    tcb_snd_t          tcb_snd;
    tcb_rcv_t          tcb_rcv;

    /*
     * Retrans information.
     */
    tcb_retrans_t      tcb_retrans;

    /*
     * Receive buffer information.
     */
    tcb_buf_entry_t    tcb_rcv_buf;

    /*
     * TCB flags
     */
    uint32_t           tcb_active           :1;
    uint32_t           tcb_consume_all_data :1;
    uint32_t           tcb_malloced         :1;
    uint32_t           tcb_on_slow_list     :1;
    uint32_t           tcb_on_rto_list      :1;

    uint32_t           tcb_trace            :1;

    uint32_t           tcb_retrans_cnt      :8;
    uint32_t           tcb_fin_rcvd         :1;

    /* uint32_t        tcb_unused           :17; */

    uint32_t           tcb_rcv_fin_seq;

} tcp_control_block_t;

#define TCB_SLOW_TMR_IS_SET(tcb) ((tcb)->tcb_on_slow_list)
#define TCB_RTO_TMR_IS_SET(tcb)  ((tcb)->tcb_on_rto_list)

/* Maximum values for TCP configurable options. */
#define TCP_MAX_WINDOW_SIZE  65535
#define TCP_MAX_RETRY_CNT      128

#define TCP_MAX_RTO_MS        1000
#define TCP_MAX_FIN_TO_MS     1000
#define TCP_MAX_TWAIT_TO_MS  10000
#define TCP_MAX_ORPHAN_TO_MS  2000


/*****************************************************************************
 * Modulo2 macro's for sequence comparison
 *
 * The macro's below assume that variable b is the base variable,
 * and to support modulo2 the window is half of the uint32_t size.
 *
 * This will cover all TCP segment cases, as the compare is based on the
 * TCP packet size, which does not exceed 64K.
 * Actually this works to window sizes upto UINT32_MAX - 1.
 ****************************************************************************/
#define SEG_EQ(a, b) ((a) == (b))
#define SEG_GE(a, b) SEG_LE(b, a)
#define SEG_GT(a, b) SEG_LT(b, a)
#define SEG_LE(a, b) ((a) == (b) || SEG_LT(a, b))
#define SEG_LT(a, b) ((b < 0x80000000 && (a < b || a >= (b + 0x80000000))) || \
                      (b >= 0x80000000 && a < b && a >= (b - 0x80000000)))

#define SEG_DIFF(a, b) ((a) - (b))

/*****************************************************************************
 * Inlines for tpg_tcp.c
 ****************************************************************************/
static inline bool tcp_snd_win_full(tcp_control_block_t *tcb)
{
    return SEG_EQ(tcb->tcb_snd.wnd,
                  SEG_DIFF(tcb->tcb_snd.nxt, tcb->tcb_snd.una));
}

/*****************************************************************************
 * Globals for tpg_tcp.c
 ****************************************************************************/
/* TCP stats are also updated in tpg_tcp_data.c */
STATS_GLOBAL_DECLARE(tcp_statistics_t);
STATS_LOCAL_DECLARE(tcp_statistics_t);

/*****************************************************************************
 * Externals for tpg_tcp.c
 ****************************************************************************/
extern bool             tcp_init(void);
extern void             tcp_lcore_init(uint32_t lcore_id);
extern void             tcp_store_sockopt(tcp_sockopt_t *dest,
                                          const tpg_tcp_sockopt_t *options);
extern void             tcp_load_sockopt(tpg_tcp_sockopt_t *dest,
                                         const tcp_sockopt_t *options);
extern struct tcp_hdr  *tcp_build_tcp_hdr(struct rte_mbuf *mbuf,
                                          tcp_control_block_t *tcb,
                                          struct ipv4_hdr *ipv4hdr,
                                          uint32_t sseq,
                                          uint32_t flags);
extern struct rte_mbuf *tcp_receive_pkt(packet_control_block_t *pcb,
                                        struct rte_mbuf *mbuf);
extern bool             tcp_send_data_pkt(tcp_control_block_t *tcb,
                                          uint32_t sseq,
                                          uint32_t flags,
                                          struct rte_mbuf *data);
extern bool             tcp_send_ctrl_pkt(tcp_control_block_t *tcb,
                                          uint32_t flags);
extern bool             tcp_send_ctrl_pkt_with_sseq(tcp_control_block_t *tcb,
                                                    uint32_t sseq,
                                                    uint32_t flags);
extern int              tcp_open_v4_connection(tcp_control_block_t **tcb,
                                               uint32_t eth_port,
                                               uint32_t src_ip_addr,
                                               uint16_t src_port,
                                               uint32_t dst_ip_addr,
                                               uint16_t dst_port,
                                               uint32_t test_case_id,
                                               tpg_app_proto_t app_id,
                                               sockopt_t *sockopt,
                                               uint32_t flags);
extern int              tcp_listen_v4(tcp_control_block_t **tcb,
                                      uint32_t eth_port,
                                      uint32_t local_ip_addr,
                                      uint16_t local_port,
                                      uint32_t test_case_id,
                                      tpg_app_proto_t app_id,
                                      sockopt_t *sockopt,
                                      uint32_t flags);

extern int              tcp_send_v4(tcp_control_block_t *tcb,
                                    struct rte_mbuf *data,
                                    uint32_t flags,
                                    uint32_t timeout,
                                    uint32_t *data_sent);
extern void             tcp_connection_cleanup(tcp_control_block_t *tcb);
extern int              tcp_close_connection(tcp_control_block_t *tcb,
                                             uint32_t flags);


extern tcp_control_block_t *tcb_clone(tcp_control_block_t *tcb);


/*****************************************************************************
 * Flags for tcp_build_tcp_hdr() and tsm_send_tcp_packet()
 ****************************************************************************/
#define TCP_BUILD_FLAG_MASK    0x0000ffff
#define TCP_BUILD_FLAG_USE_ISS 0x00010000

 /*****************************************************************************
 * Flags for tcp_send_v4()
 ****************************************************************************/
#define TCG_SEND_PSH           0x00000001
#define TCG_SEND_URG           0x00000002

/*****************************************************************************
 * Flags for tcp_close_connection()
 ****************************************************************************/
#define TCG_SILENT_CLOSE       0x00000001 /* Remove all local state, but
                                           * do not inform the remote side.
                                           */

#endif /* _H_TPG_TCP_ */

