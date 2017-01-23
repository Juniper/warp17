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
 *     tpg_tcp_sm.c
 *
 * Description:
 *     TCP state machine.
 *
 * Author:
 *     Dumitru Ceara, Eelco Chaudron
 *
 * Initial Created:
 *     03/11/2015
 *
 * Notes:
 *
 */

/*****************************************************************************
 * Include files
 ****************************************************************************/
#include "tcp_generator.h"

/*****************************************************************************
 * Globals
 ****************************************************************************/
/* Define TCP SM global statistics. Each thread has its own set of locally
 * allocated stats which are accessible through STATS_GLOBAL(type, core, port).
 */
STATS_DEFINE(tsm_statistics_t);

/* Callback to be executed whenever an interesting event happens. */
notif_cb_t tcp_notif_cb;

/*****************************************************************************
 * Forward References
 ****************************************************************************/
static cmdline_parse_ctx_t cli_ctx[];

static int tsm_SF_init(tcp_control_block_t *tcb, tcpEvent_t event,
                       void *tsm_arg);
static int tsm_SF_listen(tcp_control_block_t *tcb, tcpEvent_t event,
                         void *tsm_arg);
static int tsm_SF_syn_sent(tcp_control_block_t *tcb, tcpEvent_t event,
                           void *tsm_arg);
static int tsm_SF_syn_recv(tcp_control_block_t *tcb, tcpEvent_t event,
                           void *tsm_arg);
static int tsm_SF_estab(tcp_control_block_t *tcb, tcpEvent_t event,
                        void *tsm_arg);
static int tsm_SF_fin_wait_I(tcp_control_block_t *tcb, tcpEvent_t event,
                             void *tsm_arg);
static int tsm_SF_fin_wait_II(tcp_control_block_t *tcb, tcpEvent_t event,
                              void *tsm_arg);
static int tsm_SF_last_ack(tcp_control_block_t *tcb, tcpEvent_t event,
                           void *tsm_arg);
static int tsm_SF_closing(tcp_control_block_t *tcb, tcpEvent_t event,
                          void *tsm_arg);
static int tsm_SF_time_wait(tcp_control_block_t *tcb, tcpEvent_t event,
                            void *tsm_arg);
static int tsm_SF_close_wait(tcp_control_block_t *tcb, tcpEvent_t event,
                             void *tsm_arg);
static int tsm_SF_closed(tcp_control_block_t *tcb, tcpEvent_t event,
                         void *tsm_arg);

/*****************************************************************************
 * State functions call table
 ****************************************************************************/
static tsm_function tsm_function_array[] = {

    tsm_SF_init,
    tsm_SF_listen,
    tsm_SF_syn_sent,
    tsm_SF_syn_recv,
    tsm_SF_estab,
    tsm_SF_fin_wait_I,
    tsm_SF_fin_wait_II,
    tsm_SF_last_ack,
    tsm_SF_closing,
    tsm_SF_time_wait,
    tsm_SF_close_wait,
    tsm_SF_closed,

};

/*****************************************************************************
 * Define statemachine state and event names
 ****************************************************************************/
const char *stateNames[TS_MAX_STATE] = {

    "INIT",
    "LISTEN",
    "SYN_SENT",
    "SYN_RECV",
    "ESTAB",
    "FIN_WAIT_1",
    "FIN_WAIT_2",
    "LAST_ACK",
    "CLOSING",
    "TIME_WAIT",
    "CLOSE_WAIT",
    "CLOSED",

};

static const char *eventNames[TE_MAX_EVENT] = {

    "ENTER_STATE",
    "OPEN",
    "SEND",
    "RECEIVE",
    "CLOSE",
    "ABORT",
    "STATUS",
    "SEGMENT_ARRIVES",
    "USER_TIMEOUT",
    "RETRANSMIT_TIMEOUT",
    "ORPHAN_TIMEOUT",
    "FIN_TIMEOUT",
    "TIME_WAIT_TIMEOUT",

};

/*****************************************************************************
 * TCP flag macro
 ****************************************************************************/
#define TCP_IS_FLAG_SET(hdr, flag) (((hdr)->tcp_flags & flag) != 0)

#define TCP_TOO_MANY_RETRIES(tcb, limit, stat_fld)            \
    ((tcb)->tcb_retrans_cnt != (limit) ? false :              \
        (INC_STATS(STATS_LOCAL(tsm_statistics_t,              \
                               (tcb)->tcb_l4.l4cb_interface), \
                   stat_fld),                                 \
         true))

/*****************************************************************************
 * tsm_schedule_retransmission()
 ****************************************************************************/
static void tsm_schedule_retransmission(tcp_control_block_t *tcb)
{
    tcp_sockopt_t *tcp_opts;

    TCB_CHECK(tcb);

    if (tcb->tcb_snd.una == tcb->tcb_snd.nxt &&
            tcb->tcb_retrans.tr_total_size == 0) {
        tcb->tcb_retrans_cnt = 0;
        return;
    }

    if (TCB_RTO_TMR_IS_SET(tcb))
        return;

    tcp_opts = tcp_get_sockopt(&tcb->tcb_l4.l4cb_sockopt);
    tcp_timer_rto_set(&tcb->tcb_l4, tcp_opts->tcpo_rto);
}

/*****************************************************************************
 * tsm_send_data()
 ****************************************************************************/
static int tsm_send_data(tcp_control_block_t *tcb, tsm_data_arg_t *data)
{
    bool     win_was_full = tcp_snd_win_full(tcb);
    uint32_t status;

    TCB_CHECK(tcb);

    status = tcp_data_send(tcb, data);
    tsm_schedule_retransmission(tcb);

    if (!win_was_full && tcp_snd_win_full(tcb)) {
        TCP_NOTIF(TCB_NOTIF_SEG_WIN_UNAVAILABLE, tcb);

        INC_STATS(STATS_LOCAL(tsm_statistics_t, tcb->tcb_l4.l4cb_interface),
                  tsms_snd_win_full);
    }

    return status;
}

/*****************************************************************************
 * tsm_retrans_data()
 ****************************************************************************/
static void tsm_retrans_data(tcp_control_block_t *tcb)
{
    bool     win_was_full = tcp_snd_win_full(tcb);
    uint32_t retrans_bytes;

    TCB_CHECK(tcb);

    retrans_bytes = tcp_data_retrans(tcb);

    INC_STATS_VAL(STATS_LOCAL(tsm_statistics_t, tcb->tcb_l4.l4cb_interface),
                  tsms_retrans_bytes,
                  retrans_bytes);
    if (!win_was_full && tcp_snd_win_full(tcb)) {
        TCP_NOTIF(TCB_NOTIF_SEG_WIN_UNAVAILABLE, tcb);

        INC_STATS(STATS_LOCAL(tsm_statistics_t, tcb->tcb_l4.l4cb_interface),
                  tsms_snd_win_full);
    }
}

/*****************************************************************************
 * tsm_cleanup_retrans_queu()
 * NOTE:
 *      Should be called when processing an ACK. Will remove the segments that
 *      have already been acked.
 ****************************************************************************/
static void tsm_cleanup_retrans_queu(tcp_control_block_t *tcb, uint32_t seg_ack)
{
    tcb_retrans_t   *retrans;
    struct rte_mbuf *mbuf;
    uint32_t         acked_bytes;
    bool             win_was_full;

    TCB_CHECK(tcb);

    retrans = &tcb->tcb_retrans;
    acked_bytes = SEG_DIFF(seg_ack, tcb->tcb_snd.una);

    if (acked_bytes == 0)
        return;

    if (unlikely(retrans->tr_total_size < acked_bytes))
        return;

    win_was_full = tcp_snd_win_full(tcb);

    retrans->tr_total_size -= acked_bytes;

    while (acked_bytes && acked_bytes >= retrans->tr_data_mbufs->data_len) {
        TCB_TRACE(tcb, TSM, DEBUG, "RetransQ cleanup: RM bytes=%"PRIu32,
                  retrans->tr_data_mbufs->data_len);
        acked_bytes -= retrans->tr_data_mbufs->data_len;
        mbuf = retrans->tr_data_mbufs;
        retrans->tr_data_mbufs = retrans->tr_data_mbufs->next;

        if (retrans->tr_data_mbufs)
            retrans->tr_data_mbufs->pkt_len = mbuf->pkt_len - mbuf->data_len;

        rte_pktmbuf_free_seg(mbuf);
    }

    if (acked_bytes) {
        TCB_TRACE(tcb, TSM, DEBUG, "RetransQ cleanup: ADJ bytes=%"PRIu32,
                  acked_bytes);
        rte_pktmbuf_adj(retrans->tr_data_mbufs, acked_bytes);
    }

    if (retrans->tr_data_mbufs && retrans->tr_data_mbufs->data_len == 0) {
        mbuf = retrans->tr_data_mbufs;
        retrans->tr_data_mbufs = retrans->tr_data_mbufs->next;

        rte_pktmbuf_free_seg(mbuf);
    }

    if (retrans->tr_data_mbufs == NULL)
        retrans->tr_last_mbuf = NULL;

    /* Update SND.UNA based on what was acked. */
    tcb->tcb_snd.una = seg_ack;

    /* The remote tcp endpoint is active so we can reset his retrans count. */
    tcb->tcb_retrans_cnt = 0;

    TCP_NOTIF(TCB_NOTIF_SEG_DELIVERED, tcb);

    if (win_was_full && !tcp_snd_win_full(tcb)) {
        TCP_NOTIF(TCB_NOTIF_SEG_WIN_AVAILABLE, tcb);

        DEC_STATS(STATS_LOCAL(tsm_statistics_t, tcb->tcb_l4.l4cb_interface),
                  tsms_snd_win_full);
    }
}

/*****************************************************************************
 * tsm_handle_incoming()
 *
 * NOTE:
 *      Returns the number of bytes that were delivered.
 ****************************************************************************/
static uint32_t tsm_handle_incoming(tcp_control_block_t *tcb,
                                    packet_control_block_t *pcb,
                                    uint32_t seg_seq,
                                    uint32_t seg_len)
{
    uint32_t delivered;
    bool     was_missing;

    TCB_CHECK(tcb);

    /* TODO: the missing seq counter might affect performance due to all the
     * checks that we have to do. Need to check if that's true. If so, then
     * either remove it or maybe refactor how we determine whether the counter
     * should be incremented.
     */
    was_missing = !LIST_EMPTY(&tcb->tcb_rcv_buf);

    if (TCP_IS_FLAG_SET(pcb->pcb_tcp, TCP_URG_FLAG)) {
        /*
         * sixth, check the URG bit, [77]
         */
        tcb->tcb_rcv.up = TPG_MAX(tcb->tcb_rcv.up,
                                  rte_be_to_cpu_16(pcb->pcb_tcp->tcp_urp));

        delivered = tcp_data_handle(tcb, pcb, seg_seq, seg_len, true);
    } else {
        /*
         * seventh, process the segment text,
         */

        /* RCV.NXT and RCV.WND are adjusted before this function is called. */
        delivered = tcp_data_handle(tcb, pcb, seg_seq, seg_len, false);
    }

    if (!was_missing && !LIST_EMPTY(&tcb->tcb_rcv_buf)) {
        INC_STATS(STATS_LOCAL(tsm_statistics_t, tcb->tcb_l4.l4cb_interface),
                  tsms_missing_seq);
    } else if (was_missing && LIST_EMPTY(&tcb->tcb_rcv_buf)) {
        DEC_STATS(STATS_LOCAL(tsm_statistics_t, tcb->tcb_l4.l4cb_interface),
                  tsms_missing_seq);
    }

    return delivered;
}

/*****************************************************************************
 * tsm_do_receive_acceptance_test()
 ****************************************************************************/
static bool tsm_do_receive_acceptance_test(tcp_control_block_t *tcb,
                                           uint32_t seg_len, uint32_t seg_seq)
{
    bool rc = false;

    if (seg_len == 0 && tcb->tcb_rcv.wnd == 0) {

        if (SEG_EQ(seg_seq, tcb->tcb_rcv.nxt))
            rc = true;

    } else if (seg_len == 0 && tcb->tcb_rcv.wnd > 0) {

        if (SEG_GE(seg_seq, tcb->tcb_rcv.nxt) &&
            SEG_LT(seg_seq, (tcb->tcb_rcv.nxt + tcb->tcb_rcv.wnd)))
            rc = true;

    } else if (seg_len > 0 && tcb->tcb_rcv.wnd == 0) {
        /* Never valid */
    } else if (seg_len > 0 && tcb->tcb_rcv.wnd > 0) {

        if ((SEG_GE(seg_seq, tcb->tcb_rcv.nxt) &&
             SEG_LT(seg_seq, (tcb->tcb_rcv.nxt + tcb->tcb_rcv.wnd))) ||
            (SEG_GE((seg_seq + seg_len - 1), tcb->tcb_rcv.nxt) &&
             SEG_LT((seg_seq + seg_len - 1), (tcb->tcb_rcv.nxt + tcb->tcb_rcv.wnd))))
            rc = true;
    }
    return rc;
}

/*****************************************************************************
 * tsm_str_to_state()
 ****************************************************************************/
int tsm_str_to_state(const char *state_str)
{
    tcpState_t state;

    for (state = 0; state < TS_MAX_STATE; state++) {
        if (strcmp(state_str, stateNames[state]) == 0)
            return state;
    }
    /* Should never be reached! */
    assert(false);
    return 0;
}

/*****************************************************************************
 * tsm_get_event_str()
 ****************************************************************************/
static const char *tsm_get_event_str(tcpEvent_t event)
{
    if (event >= TE_MAX_EVENT)
        return "<unknown>";

    return eventNames[event];
}

/*****************************************************************************
 * tsm_dispatch_net_event()
 ****************************************************************************/
int tsm_dispatch_net_event(tcp_control_block_t *tcb, tcpEvent_t event,
                           packet_control_block_t *pcb)
{
    if (unlikely(pcb == NULL))
        return -EINVAL;

    TCB_CHECK(tcb);

    if (pcb->pcb_tcp != NULL) {
        TCB_TRACE(tcb, TSM, DEBUG, "   tcp {flags=%c%c%c%c%c%c, seq=%u, ack=%u, win=%u, urp=%u}",
                  (pcb->pcb_tcp->tcp_flags & TCP_URG_FLAG) == 0 ? '-' : 'u',
                  (pcb->pcb_tcp->tcp_flags & TCP_ACK_FLAG) == 0 ? '-' : 'a',
                  (pcb->pcb_tcp->tcp_flags & TCP_PSH_FLAG) == 0 ? '-' : 'p',
                  (pcb->pcb_tcp->tcp_flags & TCP_RST_FLAG) == 0 ? '-' : 'r',
                  (pcb->pcb_tcp->tcp_flags & TCP_SYN_FLAG) == 0 ? '-' : 's',
                  (pcb->pcb_tcp->tcp_flags & TCP_FIN_FLAG) == 0 ? '-' : 'f',
                  rte_be_to_cpu_32(pcb->pcb_tcp->sent_seq),
                  rte_be_to_cpu_32(pcb->pcb_tcp->recv_ack),
                  rte_be_to_cpu_16(pcb->pcb_tcp->rx_win),
                  rte_be_to_cpu_16(pcb->pcb_tcp->tcp_urp));
    }

    return tsm_dispatch_event(tcb, event, pcb);
}


/*****************************************************************************
 * tsm_dispatch_event()
 ****************************************************************************/
int tsm_dispatch_event(tcp_control_block_t *tcb, tcpEvent_t event,
                       void *tsm_arg)
{
    int err;

    if (tcb == NULL || event >= TE_MAX_EVENT)
        return -EINVAL;

    TCB_CHECK(tcb);

    TCB_TRACE(tcb, TSM, DEBUG, "event[%p] state=%s, event=%s, tsm_arg=%p, rto=%d, slow=%d",
              tcb,
              tsm_get_state_str(tcb->tcb_state),
              tsm_get_event_str(event), tsm_arg,
              TCB_RTO_TMR_IS_SET(tcb), TCB_SLOW_TMR_IS_SET(tcb));

    TCB_TRACE(tcb, TSM, DEBUG, "   snd {una=%u, nxt=%u, wnd=%u, uo=%u, wl1=%u, wl2=%u, iss=%u}",
              tcb->tcb_snd.una,
              tcb->tcb_snd.nxt,
              tcb->tcb_snd.wnd,
              tcb->tcb_snd.uo,
              tcb->tcb_snd.wl1,
              tcb->tcb_snd.wl2,
              tcb->tcb_snd.iss);

    TCB_TRACE(tcb, TSM, DEBUG, "   rcv {nxt=%u, wnd=%u, up=%u, irs=%u}",
              tcb->tcb_rcv.nxt,
              tcb->tcb_rcv.wnd,
              tcb->tcb_rcv.up,
              tcb->tcb_rcv.irs);

    err = tsm_function_array[tcb->tcb_state](tcb, event, tsm_arg);

    TRACE_FMT(TSM, DEBUG, "   err=%s", rte_strerror(-err));

    return err;
}

/*****************************************************************************
 * tsm_enter_state()
 ****************************************************************************/
static int tsm_enter_state(tcp_control_block_t *tcb, tcpState_t state,
                           void *tsm_arg)
{
    tsm_statistics_t *stats;

    if (tcb == NULL || state >= TS_MAX_STATE)
        return -EINVAL;

    TCB_CHECK(tcb);

    stats = STATS_LOCAL(tsm_statistics_t, tcb->tcb_l4.l4cb_interface);

    /* We clone TCBs from LISTEN so we shouldn't decrement if the previous
     * state is LISTEN.
     */
    if (state != tcb->tcb_state) {
        if (tcb->tcb_state != TS_LISTEN)
            DEC_STATS(stats, tsms_tcb_states[tcb->tcb_state]);

        tcb->tcb_state = state;
        TCP_NOTIF(TCB_NOTIF_STATE_CHANGE, tcb);
    }
    INC_STATS(stats, tsms_tcb_states[tcb->tcb_state]);

    return tsm_dispatch_event(tcb, TE_ENTER_STATE, tsm_arg);
}


/*****************************************************************************
 * tsm_initialize_minimal_statemachine()
 ****************************************************************************/
inline void tsm_initialize_minimal_statemachine(tcp_control_block_t *tcb,
                                                bool active)
{
    tcb->tcb_state  = TS_INIT;
    tcb->tcb_active = active;
}

/*****************************************************************************
 * tsm_initialize_statemachine()
 ****************************************************************************/
void tsm_initialize_statemachine(tcp_control_block_t *tcb, bool active)
{
    tcp_sockopt_t *tcp_opts;

    if (unlikely(tcb == NULL))
        return;

    TCB_CHECK(tcb);

    tsm_initialize_minimal_statemachine(tcb, active);
    tcb->tcb_fin_rcvd = false;
    tcb->tcb_rcv_fin_seq = 0;

    bzero(&tcb->tcb_snd, sizeof(tcb_snd_t));

    bzero(&tcb->tcb_rcv, sizeof(tcb_rcv_t));

    tcp_opts = tcp_get_sockopt(&tcb->tcb_l4.l4cb_sockopt);
    tcb->tcb_rcv.wnd = tcp_opts->tcpo_win_size;

    tcb->tcb_retrans_cnt = 0;

    bzero(&tcb->tcb_retrans, sizeof(tcb_retrans_t));
    LIST_INIT(&tcb->tcb_rcv_buf);

    tcb->tcb_trace = false;

    TCP_NOTIF(TCB_NOTIF_STATE_MACHINE_INIT, tcb);
    tsm_enter_state(tcb, TS_INIT, NULL);
}

/*****************************************************************************
 * tsm_terminate_statemachine()
 ****************************************************************************/
void tsm_terminate_statemachine(tcp_control_block_t *tcb)
{
    tsm_statistics_t *stats;

    TCB_CHECK(tcb);

    if (tcb->tcb_state != TS_INIT) {
        stats = STATS_LOCAL(tsm_statistics_t, tcb->tcb_l4.l4cb_interface);
        assert(stats->tsms_tcb_states[tcb->tcb_state] > 0);
        DEC_STATS(stats, tsms_tcb_states[tcb->tcb_state]);
    }

    TCP_NOTIF(TCB_NOTIF_STATE_MACHINE_TERM, tcb);
}

/*****************************************************************************
 * tsm_SF_init()
 ****************************************************************************/
static int tsm_SF_init(tcp_control_block_t *tcb, tcpEvent_t event,
                       void *tsm_arg __rte_unused)
{
    switch (event) {
    case TE_ENTER_STATE:
        break;

    case TE_OPEN:
        if (tcb->tcb_active) {
            tcb->tcb_snd.iss = rte_rand();
            tcb->tcb_snd.una = tcb->tcb_snd.iss;
            tcb->tcb_snd.nxt = tcb->tcb_snd.iss + 1;

            /* Send <SEQ=ISS><CTL=SYN> */
            tcp_send_ctrl_pkt(tcb, TCP_SYN_FLAG | TCP_BUILD_FLAG_USE_ISS);

            /* Always return after calling tsm_enter_state() */
            return tsm_enter_state(tcb, TS_SYN_SENT, NULL);
        }

        /* Always return after calling tsm_enter_state() */
        return tsm_enter_state(tcb, TS_LISTEN, NULL);

    case TE_SEND:
    case TE_RECEIVE:
        return -ENOTCONN;
    case TE_CLOSE:
        return tsm_enter_state(tcb, TS_CLOSED, NULL);
    case TE_ABORT:
    case TE_STATUS:
        break;

    case TE_SEGMENT_ARRIVES:
        break;

    case TE_USER_TIMEOUT:
    case TE_RETRANSMISSION_TIMEOUT:
    case TE_ORPHAN_TIMEOUT:
    case TE_FIN_TIMEOUT:
    case TE_TIME_WAIT_TIMEOUT:
        break;
    default:
        break;
    }
    return 0;
}

/*****************************************************************************
 * tsm_SF_listen()
 ****************************************************************************/
static int tsm_SF_listen(tcp_control_block_t *tcb, tcpEvent_t event,
                         void *tsm_arg)
{
    switch (event) {
    case TE_ENTER_STATE:
        break;

    case TE_OPEN:
        break;

    case TE_SEND:
    case TE_RECEIVE:
        return -ENOTCONN;

    case TE_CLOSE:
        /* We're not established so we can safely do a close.
         * No need to inform the user.
         * Buffers will be cleaned upon close.
         */
        return tsm_enter_state(tcb, TS_CLOSED, NULL);
    case TE_ABORT:
    case TE_STATUS:
        break;

    case TE_SEGMENT_ARRIVES:
        if (likely(tsm_arg != NULL)) {
            packet_control_block_t *pcb = tsm_arg;
            struct tcp_hdr         *tcp = pcb->pcb_tcp;
            int                     error;
            uint32_t                seg_seq;

            seg_seq = rte_be_to_cpu_32(tcp->sent_seq);

            /* An incoming RST should be ignored.  Return. */
            if (TCP_IS_FLAG_SET(tcp, TCP_RST_FLAG))
                return 0;

            /* Any acknowledgment is bad if it arrives on a connection still in
            * the LISTEN state.  An acceptable reset segment should be formed
            * for any arriving ACK-bearing segment.  The RST should be
            * formatted as follows:
            *
            *   <SEQ=SEG.ACK><CTL=RST>
            */
            if (TCP_IS_FLAG_SET(tcp, TCP_ACK_FLAG)) {
                tcp_send_ctrl_pkt(tcb, TCP_ACK_FLAG | TCP_RST_FLAG);
                return 0;
            }

            if (likely(TCP_IS_FLAG_SET(tcp, TCP_SYN_FLAG))) {
                /* Keep the old tcb in the LISTEN state and continue using
                 * a clone.
                 */
                tcp_control_block_t *new_tcb;

                /* Clone the tcb and work on the clone from now on. */
                new_tcb = tcb_clone(tcb);
                if (unlikely(new_tcb == NULL)) {
                    /* TODO: how do we handle server allocation errors? */
                    return -ENOMEM;
                }
                new_tcb->tcb_l4.l4cb_dst_addr.ip_v4 =
                    rte_be_to_cpu_32(pcb->pcb_ipv4->src_addr);
                new_tcb->tcb_l4.l4cb_dst_port =
                    rte_be_to_cpu_16(pcb->pcb_tcp->src_port);

                /* Recompute the hash and add the new_tcb to the htable. */
                l4_cb_calc_connection_hash(&new_tcb->tcb_l4);

                /* TODO: No need to compute the hash, we can use it from the incomming packet,
                 *       however its messing up the performance numbers :( Need to make time
                 *       to investigate why! Similar for UDP!
                new_tcb->tcb_l4.l4cb_hash = pcb->pcb_hash;
                 */

                error = tlkp_add_tcb(new_tcb);
                if (error) {
                    TCB_TRACE(tcb, TSM, ERROR, "[%s()] failed to add clone tcb: %s(%d).",
                              __func__, rte_strerror(-error), -error);
                    return -ENOMEM;
                }

                if (unlikely(new_tcb->tcb_trace))
                    pcb->pcb_trace = true;

                /* If the SYN bit is set, check the security.
                 * TODO: not supported yet.
                 */

                /* Set RCV.NXT to SEG.SEQ+1, IRS is set to SEG.SEQ and any other
                 * control or text should be queued for processing later.  ISS
                 * should be selected and a SYN segment sent of the form:
                 *
                 * <SEQ=ISS><ACK=RCV.NXT><CTL=SYN,ACK>
                 *
                 * SND.NXT is set to ISS+1 and SND.UNA to ISS.  The connection
                 * state should be changed to SYN-RECEIVED.
                 */
                new_tcb->tcb_rcv.nxt = seg_seq + 1;
                new_tcb->tcb_rcv.irs = seg_seq;

                /* TODO: queuue control/text for later */

                new_tcb->tcb_snd.iss = rte_rand();
                new_tcb->tcb_snd.una = new_tcb->tcb_snd.iss;
                new_tcb->tcb_snd.nxt = new_tcb->tcb_snd.iss + 1;

                tcp_send_ctrl_pkt(new_tcb,
                            TCP_SYN_FLAG | TCP_ACK_FLAG | TCP_BUILD_FLAG_USE_ISS);
                return tsm_enter_state(new_tcb, TS_SYN_RECV, pcb);
            } else {
                /* Any other control or text-bearing segment
                 * (not containing SYN) must have an ACK and thus would be
                 * discarded by the ACK processing.  An incoming RST segment
                 * could not be valid, since it could not have been sent in
                 * response to anything sent by this incarnation of the
                 * connection.  So you are unlikely to get here,
                 * but if you do, drop the segment, and return.
                 */
                return 0;
            }
        }

    case TE_USER_TIMEOUT:
    case TE_RETRANSMISSION_TIMEOUT:
    case TE_ORPHAN_TIMEOUT:
    case TE_FIN_TIMEOUT:
    case TE_TIME_WAIT_TIMEOUT:
        break;
    default:
        break;
    }

    return 0;
}

/*****************************************************************************
 * tsm_SF_syn_sent()
 ****************************************************************************/
static int tsm_SF_syn_sent(tcp_control_block_t *tcb, tcpEvent_t event,
                           void *tsm_arg)
{
    tcp_sockopt_t *tcp_opts;

    switch (event) {
    case TE_ENTER_STATE:
        /* Starting here on we need to do retrans. */
        tsm_schedule_retransmission(tcb);
        break;

    case TE_OPEN:
        break;

    case TE_SEND:
    case TE_RECEIVE:
        return -ENOTCONN;

    case TE_CLOSE:
        /* We're not established so we can safely do a close. */
        /* No need to inform the user in our case. */
        return tsm_enter_state(tcb, TS_CLOSED, NULL);
    case TE_ABORT:
    case TE_STATUS:
        break;

    case TE_SEGMENT_ARRIVES:
        if (likely(tsm_arg != NULL)) {
            packet_control_block_t *pcb = tsm_arg;
            struct tcp_hdr         *tcp = pcb->pcb_tcp;
            uint32_t                seg_ack;
            uint32_t                seg_seq;
            uint32_t                seg_len;
            uint32_t                seg_wnd;

            seg_ack = rte_be_to_cpu_32(tcp->recv_ack);
            seg_seq = rte_be_to_cpu_32(tcp->sent_seq);
            seg_wnd = rte_be_to_cpu_16(tcp->rx_win);
            seg_len = pcb->pcb_l5_len;

            /*
             * first check the ACK bit [66]
             */
            if (TCP_IS_FLAG_SET(tcp, TCP_ACK_FLAG)) {
                /*
                 * If SEG.ACK =< ISS, or SEG.ACK > SND.NXT, send a reset (unless
                 * the RST bit is set, if so drop the segment and return)
                 *
                 * <SEQ=SEG.ACK><CTL=RST>
                 *
                 * and discard the segment.  Return.
                 */
                if (SEG_LE(seg_ack, tcb->tcb_snd.iss) ||
                        SEG_GT(seg_ack, tcb->tcb_snd.nxt)) {
                    if (TCP_IS_FLAG_SET(tcp, TCP_RST_FLAG))
                        return 0;
                    tcp_send_ctrl_pkt(tcb, TCP_ACK_FLAG | TCP_RST_FLAG);
                    return 0;
                }

                /*
                 * If SND.UNA =< SEG.ACK =< SND.NXT then the ACK is acceptable.
                 * Otherwise drop this segment.
                 */
                if (!(SEG_GE(seg_ack, tcb->tcb_snd.una) &&
                        SEG_LE(seg_ack, tcb->tcb_snd.nxt))) {
                    return 0;
                }
            }

            /*
             * second check the RST bit
             */
            /*
             * If the RST bit is set.
             */
            if (TCP_IS_FLAG_SET(tcp, TCP_RST_FLAG)) {
                /*
                 * If the ACK was acceptable then signal the user "error:
                 * connection reset", drop the segment, enter CLOSED state,
                 * delete TCB, and return.  Otherwise (no ACK) drop the segment
                 * and return.
                 */
                return tsm_enter_state(tcb, TS_CLOSED, NULL);
            }

            /*
             * SKIP: third check the security and precedence
             */

            /*
             * fourth check the SYN bit
             * This step should be reached only if the ACK is ok, or there is
             * no ACK, and it the segment did not contain a RST.
             */
            if (TCP_IS_FLAG_SET(tcp, TCP_SYN_FLAG)) {
                /*
                 * If the SYN bit is on and the security/compartment and precedence
                 * are acceptable then, RCV.NXT is set to SEG.SEQ+1, IRS is set to
                 * SEG.SEQ. SND.UNA should be advanced to equal SEG.ACK (if there
                 * is an ACK), and any segments on the retransmission queue which
                 * are thereby acknowledged should be removed.
                 */
                tcb->tcb_rcv.nxt = seg_seq + 1;
                tcb->tcb_rcv.irs = seg_seq;
                if (TCP_IS_FLAG_SET(tcp, TCP_ACK_FLAG)) {
                    /* Increment tcb_snd.una so we take into account the ack for
                     * the SYN we sent. There might be some data segments that
                     * were acked too.
                     */
                    tcb->tcb_snd.una++;

                    /* Will update tcb_snd.una inside! */
                    tsm_cleanup_retrans_queu(tcb, seg_ack);

                    /*
                     * If SND.UNA > ISS (our SYN has been ACKed), change the connection
                     * state to ESTABLISHED, form an ACK segment
                     *
                     *   <SEQ=SND.NXT><ACK=RCV.NXT><CTL=ACK>
                     *
                     * and send it.  Data or controls which were queued for
                     * transmission may be included.  If there are other controls or
                     * text in the segment then continue processing at the sixth step
                     * below where the URG bit is checked, otherwise return.
                     */
                    if (SEG_GT(tcb->tcb_snd.una, tcb->tcb_snd.iss)) {
                        tcb->tcb_snd.wnd = seg_wnd;
                        tcb->tcb_snd.wl1 = seg_seq;
                        tcb->tcb_snd.wl2 = seg_ack;

                        /*
                         * sixth, check the URG bit, [77]
                         * seventh, process the segment text,
                         */
                        if (seg_len)
                            tsm_handle_incoming(tcb, pcb, seg_seq, seg_len);

                        /* Send <SEQ=SND.NXT><ACK=RCV.NXT><CTL=ACK> */
                        tcp_send_ctrl_pkt(tcb, TCP_ACK_FLAG);

                        /* No need to retransmit the syn. */
                        tcp_timer_rto_cancel(&tcb->tcb_l4);
                        return tsm_enter_state(tcb, TS_ESTABLISHED, NULL);
                    }
                }

                /*
                 * Otherwise enter SYN-RECEIVED, form a SYN,ACK segment
                 * Send <SEQ=ISS><ACK=RCV.NXT><CTL=SYN,ACK>
                 */
                tcp_send_ctrl_pkt(tcb, TCP_ACK_FLAG | TCP_SYN_FLAG |
                                       TCP_BUILD_FLAG_USE_ISS);

                return tsm_enter_state(tcb, TS_SYN_RECV, NULL);
            }
            /*
             * else drop the segment
             */
        }
        break;

    case TE_USER_TIMEOUT:
        break;
    case TE_RETRANSMISSION_TIMEOUT:
        tcp_opts = tcp_get_sockopt(&tcb->tcb_l4.l4cb_sockopt);
        if (TCP_TOO_MANY_RETRIES(tcb, tcp_opts->tcpo_syn_retry_cnt,
                                 tsms_syn_to)) {
            /*
             * Buffers are cleaned up upon close.
             */
            return tsm_enter_state(tcb, TS_CLOSED, NULL);
        }
        /* Resend the SYN that got us in this state and reschedule the retrans
         * timer.
         */
        tcp_send_ctrl_pkt(tcb, TCP_SYN_FLAG | TCP_BUILD_FLAG_USE_ISS);
        tsm_schedule_retransmission(tcb);
        break;
    case TE_ORPHAN_TIMEOUT:
    case TE_FIN_TIMEOUT:
    case TE_TIME_WAIT_TIMEOUT:
        break;
    default:
        break;
    }

    return 0;
}

/*****************************************************************************
 * tsm_SF_syn_recv()
 ****************************************************************************/
static int tsm_SF_syn_recv(tcp_control_block_t *tcb, tcpEvent_t event,
                           void *tsm_arg)
{
    tcp_sockopt_t *tcp_opts;

    switch (event) {
    case TE_ENTER_STATE:
        /* Starting here on we need to do retrans. */
        tsm_schedule_retransmission(tcb);
        break;

    case TE_OPEN:
        break;

    case TE_SEND:
    case TE_RECEIVE:
        return -ENOTCONN;

    case TE_CLOSE:
        return tsm_enter_state(tcb, TS_FIN_WAIT_I, NULL);
    case TE_ABORT:
    case TE_STATUS:
        break;

    case TE_SEGMENT_ARRIVES:
        if (likely(tsm_arg != NULL)) {
            packet_control_block_t *pcb = tsm_arg;
            struct tcp_hdr         *tcp = pcb->pcb_tcp;
            uint32_t                seg_len = pcb->pcb_l5_len;
            uint32_t                seg_seq = rte_be_to_cpu_32(tcp->sent_seq);
            uint32_t                seg_ack = rte_be_to_cpu_32(tcp->recv_ack);
            uint32_t                seg_wnd = rte_be_to_cpu_16(tcp->rx_win);
            bool                    seg_ok;

            /*
             * first check sequence number [69]
             */

            seg_ok = tsm_do_receive_acceptance_test(tcb, seg_len, seg_seq);

            if (!seg_ok) {
                if (!TCP_IS_FLAG_SET(tcp, TCP_RST_FLAG)) {
                    /* Send <SEQ=SND.NXT><ACK=RCV.NXT><CTL=ACK> */
                    tcp_send_ctrl_pkt(tcb, TCP_ACK_FLAG);
                }
                return 0;
            }

            /*
             * second check the RST bit, [70]
             */

            if (TCP_IS_FLAG_SET(tcp, TCP_RST_FLAG)) {
                if (!tcb->tcb_active) {
                    /* If this connection was initiated with a passive OPEN
                     * (i.e., came from the LISTEN state), then return this
                     * connection to LISTEN state and return. [70]
                     * However, we never removed the tcb in state LISTEN so we
                     * can just cleanup this connection and close it.
                     */
                    /* No need to inform the user.
                     *
                     * Buffers are cleaned up upon close.
                     */
                    return tsm_enter_state(tcb, TS_CLOSED, NULL);
                }

                /*
                 * The user is informed of the state change through a
                 * notification.
                 * Buffers are cleaned up upon close.
                 */
                return tsm_enter_state(tcb, TS_CLOSED, NULL);
            }

            /*
             * third check security and precedence [71]
             */

            /*
             * fourth, check the SYN bit, [71]
             */

            if (TCP_IS_FLAG_SET(tcp, TCP_SYN_FLAG)) {
                /*
                 * The user is informed of the state change through a
                 * notification.
                 * Buffers are cleaned up upon close.
                 */
                return tsm_enter_state(tcb, TS_CLOSED, NULL);
            }

            /*
             * fifth check the ACK field, [72]
             */
            if (!TCP_IS_FLAG_SET(tcp, TCP_ACK_FLAG))
                return 0;


            if (!(SEG_GE(seg_ack, tcb->tcb_snd.una) &&
                    SEG_LE(seg_ack, tcb->tcb_snd.nxt))) {
                /* If the segment acknowledgment is not acceptable, form a
                 * reset segment,
                 *   <SEQ=SEG.ACK><CTL=RST>
                 * and send it.
                 */
                tcp_send_ctrl_pkt(tcb, TCP_ACK_FLAG | TCP_RST_FLAG);
                return 0;
            }

            /* SND.UNA =< SEG.ACK =< SND.NXT.. Continue with ESTABLISHED
             * processing.
             */

            /* If SND.UNA < SEG.ACK =< SND.NXT then, set SND.UNA <- SEG.ACK.
             * Any segments on the retransmission queue which are thereby
             * entirely acknowledged are removed. [72]
             */
            if (SEG_GT(seg_ack, tcb->tcb_snd.una)) {

                /* Increment tcb_snd.una so we take into account the ack for
                 * the SYN+ACK we sent. There might be some data segments that
                 * were acked too.
                 */
                tcb->tcb_snd.una++;

                /* Will update tcb_snd.una inside! */
                tsm_cleanup_retrans_queu(tcb, seg_ack);
            }

            /*
             * Only continue with NON duplicate ACKs,
             * duplicates can be ignored, continue.
             * If SEG.ACK < SND.UNA which can't happen in SYN_RCVD!!
             */

            /* TODO: No receive window update, as we keep it at max for auto consume */

            /* From here we need to update the window */
            tcb->tcb_snd.wnd = seg_wnd;
            tcb->tcb_snd.wl1 = seg_seq;
            tcb->tcb_snd.wl2 = seg_ack;

            /*
             * sixth, check the URG bit, [77]
             * seventh, process the segment text,
             */
            if (seg_len && tsm_handle_incoming(tcb, pcb, seg_seq, seg_len)) {
                /* Send <SEQ=SND.NXT><ACK=RCV.NXT><CTL=ACK> */
                tcp_send_ctrl_pkt(tcb, TCP_ACK_FLAG);
            }

            /*
             * eighth, check the FIN bit,
             */
            if (TCP_IS_FLAG_SET(tcp, TCP_FIN_FLAG)) {

                /*
                 * We do not ack the fin yet, this happens when we move to LAST_ACK,
                 * but we do update the RCV.NXT here.
                 */
                tcb->tcb_rcv.nxt++;
                return tsm_enter_state(tcb, TS_CLOSE_WAIT, NULL);
            }

            /* No need to retransmit the syn-ack. */
            tcp_timer_rto_cancel(&tcb->tcb_l4);
            return tsm_enter_state(tcb, TS_ESTABLISHED, NULL);
        }
        break;

    case TE_USER_TIMEOUT:
        break;
    case TE_RETRANSMISSION_TIMEOUT:
        tcp_opts = tcp_get_sockopt(&tcb->tcb_l4.l4cb_sockopt);
        if (TCP_TOO_MANY_RETRIES(tcb,
                                 tcp_opts->tcpo_syn_ack_retry_cnt,
                                 tsms_synack_to)) {
            /*
             * The user is informed of the state change through a
             * notification.
             * Buffers are cleaned up upon close.
             */
            return tsm_enter_state(tcb, TS_CLOSED, NULL);
        }

        /* Retrans the SYN-ACK packet that got us here. */
        tcp_send_ctrl_pkt(tcb, TCP_SYN_FLAG | TCP_ACK_FLAG | TCP_BUILD_FLAG_USE_ISS);
        tsm_schedule_retransmission(tcb);
        break;
    case TE_ORPHAN_TIMEOUT:
    case TE_FIN_TIMEOUT:
    case TE_TIME_WAIT_TIMEOUT:
        break;
    default:
        break;
    }

    return 0;
}

/*****************************************************************************
 * tsm_SF_estab()
 ****************************************************************************/
static int tsm_SF_estab(tcp_control_block_t *tcb, tcpEvent_t event,
                        void *tsm_arg)
{
    tcp_sockopt_t *tcp_opts;

    switch (event) {
    case TE_ENTER_STATE:
        break;

    case TE_OPEN:
        break;

    case TE_SEND:
        if (likely(tsm_arg != NULL))
            return tsm_send_data(tcb, tsm_arg);

        break;

    case TE_RECEIVE:
        break;
    case TE_CLOSE:
        return tsm_enter_state(tcb, TS_FIN_WAIT_I, NULL);
    case TE_ABORT:
    case TE_STATUS:
        break;

    case TE_SEGMENT_ARRIVES:
        if (likely(tsm_arg != NULL)) {
            packet_control_block_t *pcb = tsm_arg;
            struct tcp_hdr         *tcp = pcb->pcb_tcp;
            uint32_t                seg_len = pcb->pcb_l5_len;
            uint32_t                seg_seq = rte_be_to_cpu_32(tcp->sent_seq);
            uint32_t                seg_ack = rte_be_to_cpu_32(tcp->recv_ack);
            uint32_t                seg_wnd = rte_be_to_cpu_16(tcp->rx_win);
            bool                    seg_ok;

            /*
             * first check sequence number [69]
             */
            seg_ok = tsm_do_receive_acceptance_test(tcb, seg_len, seg_seq);

            if (!seg_ok) {
                if (!TCP_IS_FLAG_SET(tcp, TCP_RST_FLAG)) {
                    /* Send <SEQ=SND.NXT><ACK=RCV.NXT><CTL=ACK> */
                    tcp_send_ctrl_pkt(tcb, TCP_ACK_FLAG);
                }
                return 0;
            }

            /*
             * second check the RST bit, [70]
             */
            if (TCP_IS_FLAG_SET(tcp, TCP_RST_FLAG)) {
                /*
                 * The user is informed of the state change through a
                 * notification.
                 * Buffers are cleaned up upon close.
                 */
                return tsm_enter_state(tcb, TS_CLOSED, NULL);
            }

            /*
             * third check security and precedence [71]
             */

            /*
             * fourth, check the SYN bit, [71]
             */

            if (TCP_IS_FLAG_SET(tcp, TCP_SYN_FLAG)) {
                /*
                 * The user is informed of the state change through a
                 * notification.
                 * Buffers are cleaned up upon close.
                 */
                return tsm_enter_state(tcb, TS_CLOSED, NULL);
            }

            /*
             * fifth check the ACK field, [72]
             */
            if (!TCP_IS_FLAG_SET(tcp, TCP_ACK_FLAG))
                return 0;


            if (SEG_GT(seg_ack, tcb->tcb_snd.una) &&
                SEG_LE(seg_ack, tcb->tcb_snd.nxt)) {
                /* Will update tcb_snd.una inside! */
                tsm_cleanup_retrans_queu(tcb, seg_ack);
            }

            /*
             * Only continue with NON duplicate ACKs,
             * duplicates can be ignored, continue.
             * Duplicate condition: SEG.ACK < SND.UNA
             */
            if (SEG_LT(seg_ack, tcb->tcb_snd.una))
                return 0;

            if (SEG_GT(seg_ack, tcb->tcb_snd.nxt))
                /* Can't ack something not yet send ;) */
                return 0;

            /* From here we need to update the window */
            if (SEG_GT(seg_seq, tcb->tcb_snd.wl1) ||
                (SEG_EQ(seg_seq, tcb->tcb_snd.wl1) &&
                 SEG_GE(seg_ack, tcb->tcb_snd.wl2))) {

                tcb->tcb_snd.wnd = seg_wnd;
                tcb->tcb_snd.wl1 = seg_seq;
                tcb->tcb_snd.wl2 = seg_ack;
            }

            /* TODO: No receive window update, as we keep it at max for auto consume */

            /*
             * sixth, check the URG bit, [77]
             * seventh, process the segment text,
             */
            if (seg_len && tsm_handle_incoming(tcb, pcb, seg_seq, seg_len)) {
                /* Send <SEQ=SND.NXT><ACK=RCV.NXT><CTL=ACK> */
                tcp_send_ctrl_pkt(tcb, TCP_ACK_FLAG);
            }

            /*
             * eighth, check the FIN bit,
             */
            if (TCP_IS_FLAG_SET(tcp, TCP_FIN_FLAG)) {
                uint32_t fin_seq = seg_seq + seg_len;

                /* If we're missing some data we can't process the fin yet. Store
                 * the information in the TCB and process later.
                 */
                if (unlikely(!SEG_EQ(tcb->tcb_rcv.nxt, fin_seq))) {
                    tcb->tcb_fin_rcvd = true;
                    tcb->tcb_rcv_fin_seq = fin_seq;
                    return 0;
                }

                /*
                 * We do not ack the fin yet, this happens when we move to LAST_ACK,
                 * but we do update the RCV.NXT here.
                 */
                tcb->tcb_rcv.nxt++;
                return tsm_enter_state(tcb, TS_CLOSE_WAIT, NULL);
            } else if (unlikely(tcb->tcb_fin_rcvd)) {
                /* If we got the FIN before and we finally received all the data
                 * we were waiting for then we can advance to CLOSE_WAIT.
                 */
                if (SEG_EQ(tcb->tcb_rcv.nxt, tcb->tcb_rcv_fin_seq)) {
                    /*
                     * We do not ack the fin yet, this happens when we move to LAST_ACK,
                     * but we do update the RCV.NXT here.
                     */
                    tcb->tcb_rcv.nxt++;
                    return tsm_enter_state(tcb, TS_CLOSE_WAIT, NULL);
                }
                return 0;
            }

        }
        break;

    case TE_USER_TIMEOUT:
        break;
    case TE_RETRANSMISSION_TIMEOUT:
        tcp_opts = tcp_get_sockopt(&tcb->tcb_l4.l4cb_sockopt);
        if (TCP_TOO_MANY_RETRIES(tcb, tcp_opts->tcpo_data_retry_cnt,
                                 tsms_retry_to)) {
            /*
             * The user is informed of the state change through a
             * notification.
             * Buffers are cleaned up upon close.
             */
            return tsm_enter_state(tcb, TS_CLOSED, NULL);
        }

        tsm_retrans_data(tcb);
        /* TODO: change the tsm_schedule_retransmission to take the TO as argument */
        tsm_schedule_retransmission(tcb);
        break;
    case TE_ORPHAN_TIMEOUT:
    case TE_FIN_TIMEOUT:
    case TE_TIME_WAIT_TIMEOUT:
        break;
    default:
        break;
    }

    return 0;
}

/*****************************************************************************
 * tsm_SF_fin_wait_I()
 ****************************************************************************/
static int tsm_SF_fin_wait_I(tcp_control_block_t *tcb, tcpEvent_t event,
                             void *tsm_arg)
{
    tcp_sockopt_t *tcp_opts;

    switch (event) {
    case TE_ENTER_STATE:
        /* Send the FIN with the current sequence number. */
        tcp_send_ctrl_pkt(tcb, TCP_FIN_FLAG | TCP_ACK_FLAG);
        /* Increment snd.nxt to take into account the FIN we just sent. */
        tcb->tcb_snd.nxt++;
        /* We might need to retransmit the packets from previous states
         * but also the FIN we just sent.
         */
        tsm_schedule_retransmission(tcb);

        /* Schedule the orphan timer so we don't stay in FIN-WAIT-I forever. */
        tcp_opts = tcp_get_sockopt(&tcb->tcb_l4.l4cb_sockopt);
        tcp_timer_slow_set(&tcb->tcb_l4, tcp_opts->tcpo_orphan_to);
        break;

    case TE_OPEN:
    case TE_SEND:
    case TE_RECEIVE:
    case TE_CLOSE:
        return -ENOTCONN;

    case TE_ABORT:
    case TE_STATUS:
        break;

    case TE_SEGMENT_ARRIVES:
        if (likely(tsm_arg != NULL)) {
            packet_control_block_t *pcb = tsm_arg;
            struct tcp_hdr         *tcp = pcb->pcb_tcp;
            uint32_t                seg_len = pcb->pcb_l5_len;
            uint32_t                seg_seq = rte_be_to_cpu_32(tcp->sent_seq);
            uint32_t                seg_ack = rte_be_to_cpu_32(tcp->recv_ack);
            uint32_t                seg_wnd = rte_be_to_cpu_16(tcp->rx_win);
            bool                    seg_ok;
            bool                    our_fin_acked = false;

            /*
             * first check sequence number [69]
             */

            seg_ok = tsm_do_receive_acceptance_test(tcb, seg_len, seg_seq);

            if (!seg_ok) {
                if (!TCP_IS_FLAG_SET(tcp, TCP_RST_FLAG)) {
                    /* Send <SEQ=SND.NXT><ACK=RCV.NXT><CTL=ACK> */
                    tcp_send_ctrl_pkt(tcb, TCP_ACK_FLAG);
                }
                return 0;
            }

            /*
             * second check the RST bit, [70]
             */
            if (TCP_IS_FLAG_SET(tcp, TCP_RST_FLAG)) {
                /* Cancel the orphan timer first. */
                tcp_timer_slow_cancel(&tcb->tcb_l4);
                /*
                 * The user is informed of the state change through a
                 * notification.
                 * Buffers are cleaned up upon close.
                 */
                return tsm_enter_state(tcb, TS_CLOSED, NULL);
            }

            /*
             * third check security and precedence [71]
             */

            /*
             * fourth, check the SYN bit, [71]
             */

            if (TCP_IS_FLAG_SET(tcp, TCP_SYN_FLAG)) {
                /* Cancel the orphan timer first. */
                tcp_timer_slow_cancel(&tcb->tcb_l4);
                /*
                 * The user is informed of the state change through a
                 * notification.
                 * Buffers are cleaned up upon close.
                 */
                return tsm_enter_state(tcb, TS_CLOSED, NULL);
            }

            /*
             * fifth check the ACK field, [72]
             */
            if (!TCP_IS_FLAG_SET(tcp, TCP_ACK_FLAG))
                return 0;


            if (SEG_GT(seg_ack, tcb->tcb_snd.una) &&
                SEG_LE(seg_ack, tcb->tcb_snd.nxt)) {
                /* Increment tcb_snd.una so we take into account the ack for
                 * the FIN we sent. There might be some data segments that
                 * were acked too. However, we exclude the ack for the fin we
                 * sent as it doesn't account for real data. We'll take it into
                 * account after all the data processing (below).
                 * Will update tcb_snd.una inside!
                 */
                if (seg_ack == tcb->tcb_snd.nxt)
                    tsm_cleanup_retrans_queu(tcb, seg_ack - 1);
                else
                    tsm_cleanup_retrans_queu(tcb, seg_ack);
            }

            /*
             * Only continue with NON duplicate ACKs,
             * duplicates can be ignored, continue.
             * Duplicate condition: SEG.ACK < SND.UNA
             */
            if (SEG_LT(seg_ack, tcb->tcb_snd.una))
                return 0;

            if (SEG_GT(seg_ack, tcb->tcb_snd.nxt)) {
                /* Can't ack something not yet send ;) */
                return 0;
            }

            /* From here we need to update the window */
            if (SEG_GT(seg_seq, tcb->tcb_snd.wl1) ||
                (SEG_EQ(seg_seq, tcb->tcb_snd.wl1) &&
                 SEG_GE(seg_ack, tcb->tcb_snd.wl2))) {

                tcb->tcb_snd.wnd = seg_wnd;
                tcb->tcb_snd.wl1 = seg_seq;
                tcb->tcb_snd.wl2 = seg_ack;
            }

            /* Our fin was acked if everything was acked.. */
            if (tcb->tcb_snd.una == tcb->tcb_snd.nxt - 1 &&
                seg_ack == tcb->tcb_snd.nxt) {

                /* Increment SND.UNA to take into account the ack for the
                 * fin.
                 */
                tcb->tcb_snd.una++;
                our_fin_acked = true;
            }

            /* TODO: No receive window update, as we keep it at max for auto consume */

            /*
             * sixth, check the URG bit, [77]
             * seventh, process the segment text,
             */
            if (seg_len)
                tsm_handle_incoming(tcb, pcb, seg_seq, seg_len);

            /*
             * eighth, check the FIN bit,
             */
            if (!TCP_IS_FLAG_SET(tcp, TCP_FIN_FLAG)) {
                if (unlikely(tcb->tcb_fin_rcvd)) {
                    /* If we got the FIN before and we finally received all the
                     * data we were waiting for then we can increment nxt to
                     * take into account the fin as well.
                     */
                    if (SEG_EQ(tcb->tcb_rcv.nxt, tcb->tcb_rcv_fin_seq))
                        tcb->tcb_rcv.nxt++;
                }
                /* Send <SEQ=SND.NXT><ACK=RCV.NXT><CTL=ACK> */
                tcp_send_ctrl_pkt(tcb, TCP_ACK_FLAG);
            } else {
                uint32_t fin_seq = seg_seq + seg_len;

                /* IF our FIN has been ACKed (perhaps in this segment),
                 * then enter TIME_WAIT, start the time-wait timer, turn
                 * off the other timers; otherwise enter the closing state.
                 * [75]
                 */

                /* If we're missing some data we can't process the fin yet.
                 * Store the information in the TCB and process later.
                 */
                if (unlikely(!SEG_EQ(tcb->tcb_rcv.nxt, fin_seq))) {
                    tcb->tcb_fin_rcvd = true;
                    tcb->tcb_rcv_fin_seq = fin_seq;
                    return 0;
                }

                /* Ack everything including the FIN. */
                tcb->tcb_rcv.nxt++;
                tcp_send_ctrl_pkt(tcb, TCP_ACK_FLAG);

                /* Cancel the orphan timer first. */
                tcp_timer_slow_cancel(&tcb->tcb_l4);

                if (our_fin_acked)
                    return tsm_enter_state(tcb, TS_TIME_WAIT, NULL);

                return tsm_enter_state(tcb, TS_CLOSING, NULL);
            }

            /*
             * If our fin is now acknowledged then enter FIN-WAIT-2 and
             * continue processing in that state.
             */
            if (our_fin_acked) {
                /* Cancel the orphan timer first. */
                tcp_timer_slow_cancel(&tcb->tcb_l4);

                return tsm_enter_state(tcb, TS_FIN_WAIT_II, NULL);
            }
        }
        /* When receiving valid ack we need to see if everything we sent
         * was acked. If so, then the FIN was acked as well.
         */
        break;

    case TE_USER_TIMEOUT:
        break;
    case TE_RETRANSMISSION_TIMEOUT:
        tcp_opts = tcp_get_sockopt(&tcb->tcb_l4.l4cb_sockopt);
        if (TCP_TOO_MANY_RETRIES(tcb, tcp_opts->tcpo_retry_cnt,
                                 tsms_retry_to)) {
            /* Cancel the orphan timer first. */
            tcp_timer_slow_cancel(&tcb->tcb_l4);

            /*
             * The user is informed of the state change through a
             * notification.
             * Buffers are cleaned up upon close.
             */
            return tsm_enter_state(tcb, TS_CLOSED, NULL);
        }

        /* TODO: actually resend data here. */

        /* Resend the FIN. Make sure we decrement the NXT seq number in the
         * packet. We know for sure we didn't send any data after FIN!
         */
        tcp_send_ctrl_pkt_with_sseq(tcb, tcb->tcb_snd.nxt - 1,
                                    TCP_FIN_FLAG | TCP_ACK_FLAG);
        tsm_schedule_retransmission(tcb);
        break;
    case TE_ORPHAN_TIMEOUT:
        /* If we didn't manage to close the connection until the orphan
         * timeout expired then we just close it.
         * Similar to the tcp_orphan_retries variable in Linux.
         */

        /*
         * The user is informed of the state change through a
         * notification.
         * Buffers are cleaned up upon close.
         */
        return tsm_enter_state(tcb, TS_CLOSED, NULL);
    case TE_FIN_TIMEOUT:
    case TE_TIME_WAIT_TIMEOUT:
        break;
    default:
        break;
    }

    return 0;
}

/*****************************************************************************
 * tsm_SF_fin_wait_II()
 ****************************************************************************/
static int tsm_SF_fin_wait_II(tcp_control_block_t *tcb, tcpEvent_t event,
                              void *tsm_arg)
{
    tcp_sockopt_t *tcp_opts;

    switch (event) {
    case TE_ENTER_STATE:
        /* Schedule the fin timeout timer so we don't stay in FIN-WAIT-II
         * forever.
         */
        tcp_opts = tcp_get_sockopt(&tcb->tcb_l4.l4cb_sockopt);
        tcp_timer_slow_set(&tcb->tcb_l4, tcp_opts->tcpo_fin_to);
        break;

    case TE_OPEN:
    case TE_SEND:
    case TE_RECEIVE:
    case TE_CLOSE:
        return -ENOTCONN;

    case TE_ABORT:
    case TE_STATUS:
        break;

    case TE_SEGMENT_ARRIVES:
        if (likely(tsm_arg != NULL)) {
            packet_control_block_t *pcb = tsm_arg;
            struct tcp_hdr         *tcp = pcb->pcb_tcp;
            uint32_t                seg_len = pcb->pcb_l5_len;
            uint32_t                seg_seq = rte_be_to_cpu_32(tcp->sent_seq);
            uint32_t                seg_ack = rte_be_to_cpu_32(tcp->recv_ack);
            uint32_t                seg_wnd = rte_be_to_cpu_16(tcp->rx_win);
            bool                    seg_ok;

            /*
             * first check sequence number [69]
             */

            seg_ok = tsm_do_receive_acceptance_test(tcb, seg_len, seg_seq);

            if (!seg_ok) {
                if (!TCP_IS_FLAG_SET(tcp, TCP_RST_FLAG)) {
                    /* Send <SEQ=SND.NXT><ACK=RCV.NXT><CTL=ACK> */
                    tcp_send_ctrl_pkt(tcb, TCP_ACK_FLAG);
                }
                return 0;
            }

            /*
             * second check the RST bit, [70]
             */
            if (TCP_IS_FLAG_SET(tcp, TCP_RST_FLAG)) {
                /* Cancel the fin timer first. */
                tcp_timer_slow_cancel(&tcb->tcb_l4);
                /*
                 * The user is informed of the state change through a
                 * notification.
                 * Buffers are cleaned up upon close.
                 */
                return tsm_enter_state(tcb, TS_CLOSED, NULL);
            }

            /*
             * third check security and precedence [71]
             */

            /*
             * fourth, check the SYN bit, [71]
             */

            if (TCP_IS_FLAG_SET(tcp, TCP_SYN_FLAG)) {
                /* Cancel the fin timer first. */
                tcp_timer_slow_cancel(&tcb->tcb_l4);

                /*
                 * The user is informed of the state change through a
                 * notification.
                 * Buffers are cleaned up upon close.
                 */
                return tsm_enter_state(tcb, TS_CLOSED, NULL);
            }

            /*
             * fifth check the ACK field, [72]
             */
            if (!TCP_IS_FLAG_SET(tcp, TCP_ACK_FLAG))
                return 0;

            if (SEG_GT(seg_ack, tcb->tcb_snd.una) &&
                SEG_LE(seg_ack, tcb->tcb_snd.nxt)) {
                /* Will update tcb_snd.una inside! */
                tsm_cleanup_retrans_queu(tcb, seg_ack);
            }

            /*
             * Only continue with NON duplicate ACKs,
             * duplicates can be ignored, continue.
             * Duplicate condition: SEG.ACK < SND.UNA
             */
            if (SEG_LT(seg_ack, tcb->tcb_snd.una))
                return 0;

            if (SEG_GT(seg_ack, tcb->tcb_snd.nxt)) {
                /* Can't ack something not yet send ;) */
                return 0;
            }

            /* From here we need to update the window */
            if (SEG_GT(seg_seq, tcb->tcb_snd.wl1) ||
                (SEG_EQ(seg_seq, tcb->tcb_snd.wl1) &&
                 SEG_GE(seg_ack, tcb->tcb_snd.wl2))) {

                tcb->tcb_snd.wnd = seg_wnd;
                tcb->tcb_snd.wl1 = seg_seq;
                tcb->tcb_snd.wl2 = seg_ack;
            }

            /* In addition to the processing for the ESTABLISHED
             * state, if the retransmission queue is empty, the
             * user's CLOSE can be acknowledged ("OK") but do not
             * delete the TCB. [73] TODO
             */

            /* TODO: No receive window update, as we keep it at max for auto consume */

            /*
             * sixth, check the URG bit, [77]
             * seventh, process the segment text,
             */
            if (seg_len)
                tsm_handle_incoming(tcb, pcb, seg_seq, seg_len);

            /*
             * For now send ACK right away!!
             */
            /*
             * eighth, check the FIN bit,
             */
            if (!TCP_IS_FLAG_SET(tcp, TCP_FIN_FLAG)) {
                /* Send <SEQ=SND.NXT><ACK=RCV.NXT><CTL=ACK> */
                tcp_send_ctrl_pkt(tcb, TCP_ACK_FLAG);
            } else {
                /* Cancel the fin timer first. */
                tcp_timer_slow_cancel(&tcb->tcb_l4);

                /* Enter the TIME-WAIT state. Start the time-wait timer,
                 * turn off other timers.
                 * Ack everything including the FIN. TODO: this is wrong if we
                 * didn't receive all the data.
                 */
                tcb->tcb_rcv.nxt++;
                tcp_send_ctrl_pkt(tcb, TCP_ACK_FLAG);
                return tsm_enter_state(tcb, TS_TIME_WAIT, NULL);
            }

        }
        break;

    case TE_USER_TIMEOUT:
        break;
    case TE_RETRANSMISSION_TIMEOUT:
        /* TODO: check to see if in fin_wait_II we need to retrans anything here.. */
        break;
    case TE_ORPHAN_TIMEOUT:
        break;
    case TE_FIN_TIMEOUT:
        /* If we didn't manage to close the connection until the fin
         * timeout expired then we just close it.
         * Similar to the tcp_fin_timeout variable in Linux.
         */

        /*
         * The user is informed of the state change through a
         * notification.
         * Buffers are cleaned up upon close.
         */
        return tsm_enter_state(tcb, TS_CLOSED, NULL);
    case TE_TIME_WAIT_TIMEOUT:
        break;
    default:
        break;
    }

    return 0;
}

/*****************************************************************************
 * tsm_SF_last_ack()
 ****************************************************************************/
static int tsm_SF_last_ack(tcp_control_block_t *tcb, tcpEvent_t event,
                           void *tsm_arg)
{
    tcp_sockopt_t *tcp_opts;

    switch (event) {
    case TE_ENTER_STATE:
        /* Schedule retransmission of the packets from the CLOSE-WAIT state. */
        tsm_schedule_retransmission(tcb);
        break;

    case TE_OPEN:
    case TE_SEND:
    case TE_RECEIVE:
    case TE_CLOSE:
        return -ENOTCONN;

    case TE_ABORT:
    case TE_STATUS:
        break;

    case TE_SEGMENT_ARRIVES:
        if (likely(tsm_arg != NULL)) {
            packet_control_block_t *pcb = tsm_arg;
            struct tcp_hdr         *tcp = pcb->pcb_tcp;
            uint32_t                seg_len = pcb->pcb_l5_len;
            uint32_t                seg_seq = rte_be_to_cpu_32(tcp->sent_seq);
            uint32_t                seg_ack = rte_be_to_cpu_32(tcp->recv_ack);
            bool                    seg_ok;

            /*
             * first check sequence number [69]
             */
            seg_ok = tsm_do_receive_acceptance_test(tcb, seg_len, seg_seq);

            if (!seg_ok) {
                if (!TCP_IS_FLAG_SET(tcp, TCP_RST_FLAG)) {
                    /* Send <SEQ=SND.NXT><ACK=RCV.NXT><CTL=ACK> */
                    tcp_send_ctrl_pkt(tcb, TCP_ACK_FLAG);
                }
                return 0;
            }

            /*
             * second check the RST bit, [70]
             */
            if (TCP_IS_FLAG_SET(tcp, TCP_RST_FLAG))
                return tsm_enter_state(tcb, TS_CLOSED, NULL);

            /*
             * third check security and precedence [71]
             */

            /*
             * fourth, check the SYN bit, [71]
             */

            if (TCP_IS_FLAG_SET(tcp, TCP_SYN_FLAG)) {
                /*
                 * The user is informed of the state change through a
                 * notification.
                 * Buffers are cleaned up upon close.
                 */
                return tsm_enter_state(tcb, TS_CLOSED, NULL);
            }

            /*
             * fifth check the ACK field, [72]
             */
            if (!TCP_IS_FLAG_SET(tcp, TCP_ACK_FLAG))
                return 0;

            /*
             * If ack is tcb->tcb_snd.nxt, we have received the ACK of FIN.
             * We do not do tcb->tcb_snd.nxt++, in case of re-transmits.
             */

            if (SEG_EQ(seg_ack, tcb->tcb_snd.nxt))
                return tsm_enter_state(tcb, TS_CLOSED, NULL);

            return 0;
        }
        break;

    case TE_USER_TIMEOUT:
        break;
    case TE_RETRANSMISSION_TIMEOUT:
        tcp_opts = tcp_get_sockopt(&tcb->tcb_l4.l4cb_sockopt);
        if (TCP_TOO_MANY_RETRIES(tcb, tcp_opts->tcpo_retry_cnt,
                                 tsms_retry_to)) {
            /*
             * The user is informed of the state change through a
             * notification.
             * Buffers are cleaned up upon close.
             */
            return tsm_enter_state(tcb, TS_CLOSED, NULL);
        }

        /* Resend the packet that got us here..
         * Send <SEQ=SND.NXT><ACK=RCV.NXT><CTL=FIN,ACK>
         * Make sure we decrement the NXT seq number in the packet.
         * We know for sure we didn't send any data after FIN!
         */
        tcp_send_ctrl_pkt_with_sseq(tcb, tcb->tcb_snd.nxt - 1,
                                    TCP_ACK_FLAG | TCP_FIN_FLAG);
        /* TODO: anything else to resend here?. */
        tsm_schedule_retransmission(tcb);
        break;
    case TE_ORPHAN_TIMEOUT:
    case TE_FIN_TIMEOUT:
    case TE_TIME_WAIT_TIMEOUT:
        break;
    default:
        break;
    }

    return 0;
}

/*****************************************************************************
 * tsm_SF_closing()
 ****************************************************************************/
static int tsm_SF_closing(tcp_control_block_t *tcb, tcpEvent_t event,
                          void *tsm_arg)
{
    tcp_sockopt_t *tcp_opts;

    switch (event) {
    case TE_ENTER_STATE:
        /* Schedule retransmission of the control packets from FIN-WAIT-1. */
        tsm_schedule_retransmission(tcb);
        break;

    case TE_OPEN:
    case TE_SEND:
    case TE_RECEIVE:
    case TE_CLOSE:
        return -ENOTCONN;

    case TE_ABORT:
    case TE_STATUS:
        break;

    case TE_SEGMENT_ARRIVES:
        if (likely(tsm_arg != NULL)) {
            packet_control_block_t *pcb = tsm_arg;
            struct tcp_hdr         *tcp = pcb->pcb_tcp;
            uint32_t                seg_len = pcb->pcb_l5_len;
            uint32_t                seg_seq = rte_be_to_cpu_32(tcp->sent_seq);
            uint32_t                seg_ack = rte_be_to_cpu_32(tcp->recv_ack);
            uint32_t                seg_wnd = rte_be_to_cpu_16(tcp->rx_win);
            bool                    seg_ok;

            /*
             * first check sequence number [69]
             */

            seg_ok = tsm_do_receive_acceptance_test(tcb, seg_len, seg_seq);

            if (!seg_ok) {
                if (!TCP_IS_FLAG_SET(tcp, TCP_RST_FLAG)) {
                    /* Send <SEQ=SND.NXT><ACK=RCV.NXT><CTL=ACK> */
                    tcp_send_ctrl_pkt(tcb, TCP_ACK_FLAG);
                }
                return 0;
            }

            /*
             * second check the RST bit, [70]
             * If the RST bit is set then, enter the CLOSED state, delete
             * the TCB and return.
             */
            if (TCP_IS_FLAG_SET(tcp, TCP_RST_FLAG))
                return tsm_enter_state(tcb, TS_CLOSED, NULL);

            /*
             * fourth, check the SYN bit, [71]
             */
            if (TCP_IS_FLAG_SET(tcp, TCP_SYN_FLAG)) {
                /*
                 * The user is informed of the state change through a
                 * notification.
                 * Buffers are cleaned up upon close.
                 */
                return tsm_enter_state(tcb, TS_CLOSED, NULL);
            }

            /*
             * fifth check the ACK field, [72]
             */
            if (!TCP_IS_FLAG_SET(tcp, TCP_ACK_FLAG))
                return 0;

            if (SEG_GT(seg_ack, tcb->tcb_snd.una) &&
                SEG_LE(seg_ack, tcb->tcb_snd.nxt)) {
                /* Increment tcb_snd.una so we take into account the ack for
                 * the FIN we sent. There might be some data segments that
                 * were acked too. However, we exclude the ack for the fin we
                 * sent as it doesn't account for real data. We'll take it into
                 * account after all the data processing (below).
                 * Will update tcb_snd.una inside!
                 */
                if (seg_ack == tcb->tcb_snd.nxt)
                    tsm_cleanup_retrans_queu(tcb, seg_ack - 1);
                else
                    tsm_cleanup_retrans_queu(tcb, seg_ack);
            }

            /*
             * Only continue with NON duplicate ACKs,
             * duplicates can be ignored, continue.
             * Duplicate condition: SEG.ACK < SND.UNA
             */
            if (SEG_LT(seg_ack, tcb->tcb_snd.una))
                return 0;

            if (SEG_GT(seg_ack, tcb->tcb_snd.nxt)) {
                /* Can't ack something not yet send ;) */
                return 0;
            }

            /* From here we need to update the window */
            if (SEG_GT(seg_seq, tcb->tcb_snd.wl1) ||
                (SEG_EQ(seg_seq, tcb->tcb_snd.wl1) &&
                 SEG_GE(seg_ack, tcb->tcb_snd.wl2))) {

                tcb->tcb_snd.wnd = seg_wnd;
                tcb->tcb_snd.wl1 = seg_seq;
                tcb->tcb_snd.wl2 = seg_ack;
            }

            /* In addition to the processing for ESTABLISHED state,
             * if the ACK acknowledges or FIN then enter the TIME-WAIT
             * state, otherwise ignore the segment.
             * Our fin was acked if everything was acked..
             */
            if (tcb->tcb_snd.una == tcb->tcb_snd.nxt - 1 &&
                seg_ack == tcb->tcb_snd.nxt) {
                /* From here on everything we sent was acked so we can cancel
                 * RTO timers.
                 */
                if (TCB_RTO_TMR_IS_SET(tcb))
                    tcp_timer_rto_cancel(&tcb->tcb_l4);

                return tsm_enter_state(tcb, TS_TIME_WAIT, NULL);
            }
            /* Ignore.. */
        }
        break;

    case TE_USER_TIMEOUT:
        break;
    case TE_RETRANSMISSION_TIMEOUT:
        tcp_opts = tcp_get_sockopt(&tcb->tcb_l4.l4cb_sockopt);
        if (TCP_TOO_MANY_RETRIES(tcb, tcp_opts->tcpo_retry_cnt,
                                 tsms_retry_to)) {
            /*
             * The user is informed of the state change through a
             * notification.
             * Buffers are cleaned up upon close.
             */
            return tsm_enter_state(tcb, TS_CLOSED, NULL);
        }

        /* TODO: actually resend data here. */

        /* Resend the FIN. Make sure we decrement the NXT seq number in the
         * packet. We know for sure we didn't send any data after FIN!
         */
        tcp_send_ctrl_pkt_with_sseq(tcb, tcb->tcb_snd.nxt - 1,
                                    TCP_FIN_FLAG | TCP_ACK_FLAG);
        tsm_schedule_retransmission(tcb);
        break;
    case TE_ORPHAN_TIMEOUT:
    case TE_FIN_TIMEOUT:
    case TE_TIME_WAIT_TIMEOUT:
        break;
    default:
        break;
    }

    return 0;
}

/*****************************************************************************
 * tsm_SF_time_wait()
 ****************************************************************************/
static int tsm_SF_time_wait(tcp_control_block_t *tcb, tcpEvent_t event,
                            void *tsm_arg)
{
    tcp_sockopt_t *tcp_opts;

    switch (event) {
    case TE_ENTER_STATE:
        /* On entering Time Wait we cancel all timers and start the TIME_WAIT
         * timer.
         */
        if (TCB_RTO_TMR_IS_SET(tcb))
            tcp_timer_rto_cancel(&tcb->tcb_l4);

        if (TCB_SLOW_TMR_IS_SET(tcb))
            tcp_timer_slow_cancel(&tcb->tcb_l4);

        tcp_opts = tcp_get_sockopt(&tcb->tcb_l4.l4cb_sockopt);
        if (tcp_opts->tcpo_skip_timewait)
            return tsm_enter_state(tcb, TS_CLOSED, NULL);
        else
            tcp_timer_slow_set(&tcb->tcb_l4, tcp_opts->tcpo_twait_to);
        break;

    case TE_OPEN:
    case TE_SEND:
    case TE_RECEIVE:
    case TE_CLOSE:
        return -ENOTCONN;

    case TE_ABORT:
    case TE_STATUS:
        break;

    case TE_SEGMENT_ARRIVES:
        if (likely(tsm_arg != NULL)) {
            packet_control_block_t *pcb = tsm_arg;
            struct tcp_hdr         *tcp = pcb->pcb_tcp;
            uint32_t                seg_len = pcb->pcb_l5_len;
            uint32_t                seg_seq = rte_be_to_cpu_32(tcp->sent_seq);
            bool                    seg_ok;

            /*
             * first check sequence number [69]
             */

            seg_ok = tsm_do_receive_acceptance_test(tcb, seg_len, seg_seq);

            if (!seg_ok) {
                if (!TCP_IS_FLAG_SET(tcp, TCP_RST_FLAG)) {
                    /* Send <SEQ=SND.NXT><ACK=RCV.NXT><CTL=ACK> */
                    tcp_send_ctrl_pkt(tcb, TCP_ACK_FLAG);
                }
                return 0;
            }

            /*
             * second check the RST bit, [70]
             * If the RST bit is set then, enter the CLOSED state, delete
             * the TCB and return.
             */
            if (TCP_IS_FLAG_SET(tcp, TCP_RST_FLAG))
                return tsm_enter_state(tcb, TS_CLOSED, NULL);

            /*
             * fourth, check the SYN bit, [71]
             */
            if (TCP_IS_FLAG_SET(tcp, TCP_SYN_FLAG)) {
                /*
                 * The user is informed of the state change through a
                 * notification.
                 * Buffers are cleaned up upon close.
                 */
                return tsm_enter_state(tcb, TS_CLOSED, NULL);
            }

            /* The only thing that can arrive in this state is a
             * retransmission of the remote FIN. Acknowledge it, and
             * restart the 2MSL timeout. [73]
             */
            tcp_send_ctrl_pkt(tcb, TCP_ACK_FLAG);

            assert(TCB_SLOW_TMR_IS_SET(tcb));

            tcp_opts = tcp_get_sockopt(&tcb->tcb_l4.l4cb_sockopt);
            tcp_timer_slow_cancel(&tcb->tcb_l4);
            tcp_timer_slow_set(&tcb->tcb_l4, tcp_opts->tcpo_twait_to);
        }
        break;

    case TE_USER_TIMEOUT:
    case TE_RETRANSMISSION_TIMEOUT:
    case TE_ORPHAN_TIMEOUT:
    case TE_FIN_TIMEOUT:
        break;
    case TE_TIME_WAIT_TIMEOUT:
        /* If the time-wait timeout expires on a connection delete the
         * TCB, enter the CLOSED state and return.
         */
        return tsm_enter_state(tcb, TS_CLOSED, NULL);
    default:
        break;
    }

    return 0;
}

/*****************************************************************************
 * tsm_SF_close_wait()
 ****************************************************************************/
static int tsm_SF_close_wait(tcp_control_block_t *tcb, tcpEvent_t event,
                             void *tsm_arg)
{
    tcp_sockopt_t *tcp_opts;

    switch (event) {
    case TE_ENTER_STATE:
        if (tcb->tcb_consume_all_data) {
            /* TODO: we should wait for all our sent data to be acked at least.. */

            /* Send <SEQ=SND.NXT><ACK=RCV.NXT><CTL=FIN,ACK>
             * Send the FIN with the current sequence number.
             */
            tcp_send_ctrl_pkt(tcb, TCP_ACK_FLAG | TCP_FIN_FLAG);
            /* Increment snd.nxt to take into account the FIN. */
            tcb->tcb_snd.nxt++;
            return tsm_enter_state(tcb, TS_LAST_ACK, NULL);
        }
        TPG_ERROR_ABORT("TODO: %s!\n",
                        "In real client mode we need client aproval");

        /* Schedule the retransmission of packets in previous states and
         * also of the FIN|ACK we just sent.
         */
        tsm_schedule_retransmission(tcb);
        break;

    case TE_OPEN:
        break;

    case TE_SEND:
        if (likely(tsm_arg != NULL))
            return tsm_send_data(tcb, tsm_arg);

        break;

    case TE_RECEIVE:
    case TE_CLOSE:
    case TE_ABORT:
    case TE_STATUS:
        break;

    case TE_SEGMENT_ARRIVES:
        if (likely(tsm_arg != NULL)) {
            packet_control_block_t *pcb = tsm_arg;
            struct tcp_hdr         *tcp = pcb->pcb_tcp;
            uint32_t                seg_len = pcb->pcb_l5_len;
            uint32_t                seg_seq = rte_be_to_cpu_32(tcp->sent_seq);
            uint32_t                seg_ack = rte_be_to_cpu_32(tcp->recv_ack);
            uint32_t                seg_wnd = rte_be_to_cpu_16(tcp->rx_win);
            bool                    seg_ok;

            /*
             * first check sequence number [69]
             */
            seg_ok = tsm_do_receive_acceptance_test(tcb, seg_len, seg_seq);

            if (!seg_ok) {
                if (!TCP_IS_FLAG_SET(tcp, TCP_RST_FLAG)) {
                    /* Send <SEQ=SND.NXT><ACK=RCV.NXT><CTL=ACK> */
                    tcp_send_ctrl_pkt(tcb, TCP_ACK_FLAG);
                }
                return 0;
            }

            /*
             * second check the RST bit, [70]
             */
            if (TCP_IS_FLAG_SET(tcp, TCP_RST_FLAG)) {
                /*
                 * The user is informed of the state change through a
                 * notification.
                 * Buffers are cleaned up upon close.
                 */
                return tsm_enter_state(tcb, TS_CLOSED, NULL);
            }

            /*
             * fourth, check the SYN bit, [71]
             */
            if (TCP_IS_FLAG_SET(tcp, TCP_SYN_FLAG)) {
                /*
                 * The user is informed of the state change through a
                 * notification.
                 * Buffers are cleaned up upon close.
                 */
                return tsm_enter_state(tcb, TS_CLOSED, NULL);
            }

            /*
             * fifth check the ACK field, [72]
             */
            if (!TCP_IS_FLAG_SET(tcp, TCP_ACK_FLAG))
                return 0;

            if (SEG_GT(seg_ack, tcb->tcb_snd.una) &&
                SEG_LE(seg_ack, tcb->tcb_snd.nxt)) {
                /* Will update tcb_snd.una inside! */
                tsm_cleanup_retrans_queu(tcb, seg_ack);
            }

            /*
             * Only continue with NON duplicate ACKs,
             * duplicates can be ignored, continue.
             * Duplicate condition: SEG.ACK < SND.UNA
             */
            if (SEG_LT(seg_ack, tcb->tcb_snd.una))
                return 0;

            if (SEG_GT(seg_ack, tcb->tcb_snd.nxt)) {
                /* Can't ack something not yet send ;) */
                return 0;
            }

            /* From here we need to update the window */
            if (SEG_GT(seg_seq, tcb->tcb_snd.wl1) ||
                (SEG_EQ(seg_seq, tcb->tcb_snd.wl1) &&
                 SEG_GE(seg_ack, tcb->tcb_snd.wl2))) {

                tcb->tcb_snd.wnd = seg_wnd;
                tcb->tcb_snd.wl1 = seg_seq;
                tcb->tcb_snd.wl2 = seg_ack;
            }

            /* Wait for the user to issue CLOSE and then send our FIN. */
        }
        break;

    case TE_USER_TIMEOUT:
        break;
    case TE_RETRANSMISSION_TIMEOUT:
        tcp_opts = tcp_get_sockopt(&tcb->tcb_l4.l4cb_sockopt);
        if (TCP_TOO_MANY_RETRIES(tcb, tcp_opts->tcpo_retry_cnt,
                                 tsms_retry_to)) {
            /*
             * User will be notified of the state change.
             * Buffers are cleaned up upon close.
             */
            return tsm_enter_state(tcb, TS_CLOSED, NULL);
        }

        /* Resend data here if we need to. */
        tsm_retrans_data(tcb);

        /* Resend <SEQ=SND.NXT><ACK=RCV.NXT><CTL=FIN,ACK>
         * Make sure we decrement the NXT seq number in the packet.
         * We know for sure we didn't send any data after FIN!
         */
        tcp_send_ctrl_pkt_with_sseq(tcb, tcb->tcb_snd.nxt - 1,
                                    TCP_FIN_FLAG | TCP_ACK_FLAG);
        break;
    case TE_ORPHAN_TIMEOUT:
    case TE_FIN_TIMEOUT:
    case TE_TIME_WAIT_TIMEOUT:
        break;
    default:
        break;
    }

    return 0;
}

/*****************************************************************************
 * tsm_SF_closed()
 ****************************************************************************/
static int tsm_SF_closed(tcp_control_block_t *tcb, tcpEvent_t event,
                         void *tsm_arg)
{
    /* Any other event except for TE_ENTER_STATE & TE_SEGMENT_ARRIVES
     * should be invalid. TE_SEGMENT_ARRIVES only happens when we didn't
     * actually find any TCB. The one we get as parameter will most likely
     * be a fake one.
     */
    switch (event) {
    case TE_ENTER_STATE:
        /* Here we need to cleanup everything about the tcb and also free
         * any memory we allocated for it. This is equivalent to a silent
         * close call.
         */
        return tcp_close_connection(tcb, TCG_SILENT_CLOSE);

    case TE_OPEN:
    case TE_SEND:
    case TE_RECEIVE:
    case TE_CLOSE:
    case TE_ABORT:
        return -ENOTCONN;

    case TE_STATUS:
        break;

    case TE_SEGMENT_ARRIVES:
        if (likely(tsm_arg != NULL)) {
            packet_control_block_t *pcb = tsm_arg;
            struct tcp_hdr         *tcp = pcb->pcb_tcp;
            uint32_t                seg_len = pcb->pcb_l5_len;
            uint32_t                seg_seq = rte_be_to_cpu_32(tcp->sent_seq);
            uint32_t                seg_ack = rte_be_to_cpu_32(tcp->recv_ack);

            /* If the state is CLOSED (i.e., TCB does not exist) then
             * all data in the incoming segment is discarded.  An incoming
             * segment containing a RST is discarded.  An incoming segment not
             * containing a RST causes a RST to be sent in response.  The
             * acknowledgment and sequence field values are selected to make the
             * reset sequence acceptable to the TCP that sent the offending
             * segment. [65]
             */
            if (!TCP_IS_FLAG_SET(tcp, TCP_RST_FLAG)) {
                if (TCP_IS_FLAG_SET(tcp, TCP_ACK_FLAG)) {
                    /* <SEQ=SEG.ACK><CTL=RST> */
                    tcb->tcb_snd.nxt = seg_ack;
                    tcp_send_ctrl_pkt(tcb, TCP_RST_FLAG);
                } else {
                    /* <SEQ=0><ACK=SEG.SEQ+SEG.LEN><CTL=RST,ACK> */
                    tcb->tcb_rcv.nxt = seg_seq + seg_len;
                    tcp_send_ctrl_pkt(tcb, TCP_ACK_FLAG | TCP_RST_FLAG);
                }
            }
        }
        break;

    case TE_USER_TIMEOUT:
    case TE_RETRANSMISSION_TIMEOUT:
    case TE_ORPHAN_TIMEOUT:
    case TE_FIN_TIMEOUT:
    case TE_TIME_WAIT_TIMEOUT:
        break;
    default:
        break;
    }

    return 0;
}

/*****************************************************************************
 * tsm_init()
 ****************************************************************************/
bool tsm_init(void)
{
    /*
     * Add TSM module CLI commands
     */
    if (!cli_add_main_ctx(cli_ctx)) {
        RTE_LOG(ERR, USER1, "ERROR: Can't add TSM specific CLI commands!\n");
        return false;
    }

    /*
     * Allocate memory for TCP SM statistics, and clear all of them
     */
    if (STATS_GLOBAL_INIT(tsm_statistics_t, "tsm_stats") == NULL) {
        RTE_LOG(ERR, USER1,
                "ERROR: Failed allocating TCP SM statistics memory!\n");
        return false;
    }

    return true;
}

/*****************************************************************************
 * tsm_lcore_init()
 ****************************************************************************/
void tsm_lcore_init(uint32_t lcore_id)
{
    /* Init the local stats. */
    if (STATS_LOCAL_INIT(tsm_statistics_t, "tsm_stats", lcore_id) == NULL) {
        TPG_ERROR_ABORT("[%d:%s() Failed to allocate per lcore tsm_stats!\n",
                        rte_lcore_index(lcore_id),
                        __func__);
    }
}

/*****************************************************************************
 * tsm_total_stats_get()
 ****************************************************************************/
void tsm_total_stats_get(uint32_t port, tsm_statistics_t *total_stats)
{
    uint32_t          core;
    int               state;
    tsm_statistics_t *tsm_stats;

    bzero(total_stats, sizeof(*total_stats));

    STATS_FOREACH_CORE(tsm_statistics_t, port, core, tsm_stats) {
        for (state = 0; state < TS_MAX_STATE; state++) {
            total_stats->tsms_tcb_states[state] +=
                tsm_stats->tsms_tcb_states[state];
        }

        total_stats->tsms_syn_to += tsm_stats->tsms_syn_to;
        total_stats->tsms_synack_to += tsm_stats->tsms_synack_to;
        total_stats->tsms_retry_to += tsm_stats->tsms_retry_to;
        total_stats->tsms_retrans_bytes += tsm_stats->tsms_retrans_bytes;
        total_stats->tsms_missing_seq += tsm_stats->tsms_missing_seq;
        total_stats->tsms_snd_win_full += tsm_stats->tsms_snd_win_full;
    }
}

/*****************************************************************************
 * CLI commands
 *****************************************************************************
 * - "show tsm statistics {details}"
 ****************************************************************************/
struct cmd_show_tsm_statistics_result {
    cmdline_fixed_string_t show;
    cmdline_fixed_string_t tsm;
    cmdline_fixed_string_t statistics;
    cmdline_fixed_string_t details;
};

static cmdline_parse_token_string_t cmd_show_tsm_statistics_T_show =
    TOKEN_STRING_INITIALIZER(struct cmd_show_tsm_statistics_result, show, "show");
static cmdline_parse_token_string_t cmd_show_tsm_statistics_T_tsm =
    TOKEN_STRING_INITIALIZER(struct cmd_show_tsm_statistics_result, tsm, "tsm");
static cmdline_parse_token_string_t cmd_show_tsm_statistics_T_statistics =
    TOKEN_STRING_INITIALIZER(struct cmd_show_tsm_statistics_result, statistics, "statistics");
static cmdline_parse_token_string_t cmd_show_tsm_statistics_T_details =
    TOKEN_STRING_INITIALIZER(struct cmd_show_tsm_statistics_result, details, "details");

static void cmd_show_tsm_statistics_parsed(void *parsed_result __rte_unused,
                                           struct cmdline *cl,
                                           void *data)
{
    int port;
    int core;
    int option = (intptr_t) data;
    int state;

    for (port = 0; port < rte_eth_dev_count(); port++) {
        /*
         * Calculate totals first
         */
        tsm_statistics_t    total_stats;

        tsm_total_stats_get(port, &total_stats);

        /*
         * Display individual counters
         */
        cmdline_printf(cl, "Port %d TSM statistics:\n", port);

        for (state = 0; state < TS_MAX_STATE; state++) {
            cmdline_printf(cl, "  %-20s: %20"PRIu32"\n",
                           tsm_get_state_str(state),
                           total_stats.tsms_tcb_states[state]);
        }

        if (option == 'd') {
            tsm_statistics_t *tsm_stats;

            STATS_FOREACH_CORE(tsm_statistics_t, port, core, tsm_stats) {
                int idx = rte_lcore_index(core);

                cmdline_printf(cl, "    - core idx %3.3u :\n", idx);
                for (state = 0; state < TS_MAX_STATE; state++) {
                    cmdline_printf(cl, "        %-10s    :  %20"PRIu32"\n",
                                   tsm_get_state_str(state),
                                   tsm_stats->tsms_tcb_states[state]);
                }
                cmdline_printf(cl, "\n");
            }
        }

        cmdline_printf(cl, "\n");

        SHOW_32BIT_STATS("SYN retrans TO", tsm_statistics_t, tsms_syn_to,
                         port,
                         option);
        SHOW_32BIT_STATS("SYN/ACK retrans TO", tsm_statistics_t, tsms_synack_to,
                         port,
                         option);
        SHOW_32BIT_STATS("Retrans TO", tsm_statistics_t, tsms_retry_to,
                         port,
                         option);
        SHOW_64BIT_STATS("Retrans bytes", tsm_statistics_t, tsms_retrans_bytes,
                         port,
                         option);
        SHOW_32BIT_STATS("Missing seq", tsm_statistics_t, tsms_missing_seq,
                         port,
                         option);
        SHOW_32BIT_STATS("SND win full", tsm_statistics_t, tsms_snd_win_full,
                         port,
                         option);

        cmdline_printf(cl, "\n");
    }
}

cmdline_parse_inst_t cmd_show_tsm_statistics = {
    .f = cmd_show_tsm_statistics_parsed,
    .data = NULL,
    .help_str = "show tsm statistics",
    .tokens = {
        (void *)&cmd_show_tsm_statistics_T_show,
        (void *)&cmd_show_tsm_statistics_T_tsm,
        (void *)&cmd_show_tsm_statistics_T_statistics,
        NULL,
    },
};

cmdline_parse_inst_t cmd_show_tsm_statistics_details = {
    .f = cmd_show_tsm_statistics_parsed,
    .data = (void *) (intptr_t) 'd',
    .help_str = "show tsm statistics details",
    .tokens = {
        (void *)&cmd_show_tsm_statistics_T_show,
        (void *)&cmd_show_tsm_statistics_T_tsm,
        (void *)&cmd_show_tsm_statistics_T_statistics,
        (void *)&cmd_show_tsm_statistics_T_details,
        NULL,
    },
};

/*****************************************************************************
 * Main menu context
 ****************************************************************************/
static cmdline_parse_ctx_t cli_ctx[] = {
    &cmd_show_tsm_statistics,
    &cmd_show_tsm_statistics_details,
    NULL,
};

