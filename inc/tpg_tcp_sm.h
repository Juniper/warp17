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
 *     tpg_tcp_sm.h
 *
 * Description:
 *     TCP state machine.
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
#ifndef _H_TPG_TCP_SM_
#define _H_TPG_TCP_SM_

/*****************************************************************************
 * TCP State Machine Definitions
 ****************************************************************************/
typedef enum {

    TS_INIT,
    TS_LISTEN,
    TS_SYN_SENT,
    TS_SYN_RECV,
    TS_ESTABLISHED,
    TS_FIN_WAIT_I,
    TS_FIN_WAIT_II,
    TS_LAST_ACK,
    TS_CLOSING,
    TS_TIME_WAIT,
    TS_CLOSE_WAIT,
    TS_CLOSED,
    TS_MAX_STATE

} tcpState_t;

typedef enum {

    TE_ENTER_STATE,
    /* User Calls */
    TE_OPEN,
    TE_SEND,
    TE_RECEIVE,
    TE_CLOSE,
    TE_ABORT,
    TE_STATUS,
    /* Arriving Segments */
    TE_SEGMENT_ARRIVES,
    /* Timeouts */
    TE_USER_TIMEOUT,
    TE_RETRANSMISSION_TIMEOUT,
    TE_ORPHAN_TIMEOUT,
    TE_FIN_TIMEOUT,
    TE_TIME_WAIT_TIMEOUT,
    TE_MAX_EVENT

} tcpEvent_t;

/*****************************************************************************
 * TSM Data arguments.
 ****************************************************************************/
typedef struct tsm_data_arg_s {

    struct rte_mbuf *tda_mbuf;
    uint32_t         tda_data_len;
    uint32_t        *tda_data_sent;

    uint32_t         tda_push :1;
    uint32_t         tda_urg  :1;

} tsm_data_arg_t;

/*****************************************************************************
 * Port statistics
 ****************************************************************************/
typedef struct tsm_statistics_s {

    uint32_t tsms_tcb_states[TS_MAX_STATE];

    uint32_t tsms_syn_to;
    uint32_t tsms_synack_to;
    uint32_t tsms_retry_to;

    uint64_t tsms_retrans_bytes;

    uint32_t tsms_missing_seq;
    uint32_t tsms_snd_win_full;

} tsm_statistics_t;

STATS_GLOBAL_DECLARE(tsm_statistics_t);

/*****************************************************************************
 * TCP stack notifications
 ****************************************************************************/
enum {
    TCB_NOTIF_STATE_MACHINE_INIT = NOTIFID_INITIALIZER(NOTIF_TCP_MODULE),
    TCB_NOTIF_STATE_CHANGE,
    TCB_NOTIF_SEG_DELIVERED,
    TCB_NOTIF_SEG_WIN_UNAVAILABLE,
    TCB_NOTIF_SEG_WIN_AVAILABLE,
    TCB_NOTIF_SEG_RECEIVED,
    TCB_NOTIF_STATE_MACHINE_TERM,
};

#define TCP_NOTIF(notif, tcb)                                        \
    do {                                                             \
        notif_arg_t arg = TCP_NOTIF_ARG((tcb),                       \
                                    (tcb)->tcb_l4.l4cb_test_case_id, \
                                    (tcb)->tcb_l4.l4cb_interface);   \
        tcp_notif_cb((notif), &arg);                                 \
    } while (0)

/* Callback to be executed whenever an interesting event happens. */
extern notif_cb_t tcp_notif_cb;

/*****************************************************************************
 * Globals for tpg_tcp_sm.c
 ****************************************************************************/

extern const char *stateNames[TS_MAX_STATE];

/*****************************************************************************
 * Static inlines for tpg_tcp_sm.
 ****************************************************************************/

/*****************************************************************************
 * tsm_get_state_str()
 ****************************************************************************/
static inline const char *tsm_get_state_str(tcpState_t state)
{
    if (state >= TS_MAX_STATE)
        return "<unknown>";

    return stateNames[state];
}

/*****************************************************************************
 * Externals for tpg_tcp_sm.c
 ****************************************************************************/
extern bool tsm_init(void);
extern void tsm_lcore_init(uint32_t lcore_id);
extern void tsm_initialize_minimal_statemachine(tcp_control_block_t *tcb,
                                                bool active);
extern void tsm_initialize_statemachine(tcp_control_block_t *tcb, bool active);
extern void tsm_terminate_statemachine(tcp_control_block_t *tcb);
extern int  tsm_dispatch_net_event(tcp_control_block_t *tcb, tcpEvent_t event,
                                   packet_control_block_t *pcb);
extern int  tsm_dispatch_event(tcp_control_block_t *tcb, tcpEvent_t event,
                               void *tsm_arg);
extern int  tsm_str_to_state(const char *state_str);
extern void tsm_total_stats_get(uint32_t port, tsm_statistics_t *total_stats);

typedef int (*tsm_function)(tcp_control_block_t *tcb, tcpEvent_t event,
                            void *tsm_arg);

#endif /* _H_TPG_TCP_SM_ */

