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
 *     tpg_tests_sm.h
 *
 * Description:
 *     State machine implementation to be used by the Test engine.
 *
 * Author:
 *     Dumitru Ceara, Eelco Chaudron
 *
 * Initial Created:
 *     01/07/2015
 *
 * Notes:
 *
 */

/*****************************************************************************
 * Multiple include protection
 ****************************************************************************/
#ifndef _H_TPG_TESTS_SM_
#define _H_TPG_TESTS_SM_

/*****************************************************************************
 * TCP Client Test state machine:
 * See tpg_test_client_sm.dot for the diagram. (xdot dot/tpg_test_client_sm.dot)
 *
 * In state TST_CLS_SENDING the control block is on the to-send list and the
 * test engine will try sending traffic on that connection.
 * Once the uptime timer fires the control block will change state (from
 * TST_CLS_OPEN/TST_CLS_SENDING/TST_CLS_NO_SND_WIN) to TST_CLS_TO_CLOSE and the
 * control block will be added on the to-close list. The to-close list is
 * walked by the test engine which will issue CLOSE on the control blocks on
 * that list.
 * In state TST_CLS_CLOSED the downtime timer might be running (based on config)
 * so if the timer fires then the control block moves to state TST_CLS_TO_OPEN
 * and is added to the to-open list which is walked by the test engine.
 ****************************************************************************/
typedef enum {

    TST_CLS_TO_INIT,
    TST_CLS_TO_OPEN,
    TST_CLS_OPENING,
    TST_CLS_OPEN,
    TST_CLS_SENDING,
    TST_CLS_NO_SND_WIN,
    TST_CLS_TO_CLOSE,
    TST_CLS_CLOSING,
    TST_CLS_CLOSED,
    TST_CLS_MAX_STATE

} test_client_sm_state_t;

typedef enum {

    TST_CLE_ENTER_STATE,

    /* Timeouts */
    TST_CLE_TMR_TO,

    /* TCP/UDP events. */
    TST_CLE_TCP_STATE_CHG,
    TST_CLE_UDP_STATE_CHG,
    TST_CLE_NO_SND_WIN,
    TST_CLE_SND_WIN,

    /* APP Events. */
    TST_CLE_APP_SEND_START,
    TST_CLE_APP_SEND_STOP,

    TST_CLE_MAX_EVENT

} test_client_sm_event_t;

/*****************************************************************************
 * TCP Server Test state machine:
 * See tpg_test_server_sm.dot for the diagram. (xdot dot/tpg_test_server_sm.dot)
 *
 * The server state machine is similar to the client state machine but there are
 * no test timers running and TCP states > ESTABLISHED are not tracked.
 ****************************************************************************/
typedef enum {

    TST_SRVS_INIT,
    TST_SRVS_OPEN,
    TST_SRVS_SENDING,
    TST_SRVS_NO_SND_WIN,
    TST_SRVS_CLOSING,
    TST_SRVS_MAX_STATE

} test_server_sm_state_t;

typedef enum {

    TST_SRVE_ENTER_STATE,

    /* TCP/UDP events. */
    TST_SRVE_TCP_STATE_CHG,
    TST_SRVE_UDP_STATE_CHG,
    TST_SRVE_NO_SND_WIN,
    TST_SRVE_SND_WIN,

    /* APP Events. */
    TST_SRVE_APP_SEND_START,
    TST_SRVE_APP_SEND_STOP,

    TST_SRVE_MAX_EVENT

} test_server_sm_event_t;

/*****************************************************************************
 * Unions for ease of use.
 ****************************************************************************/
typedef union {
    test_client_sm_state_t tts_client;
    test_server_sm_state_t tts_server;
} test_sm_state_t;

typedef union {
    test_client_sm_event_t tte_client;
    test_server_sm_event_t tte_server;
} test_sm_event_t;

typedef void (*test_sm_function) (l4_control_block_t *l4_cb,
                                  test_sm_event_t event,
                                  test_case_info_t *ctx);

/*****************************************************************************
 * Helper macros
 ****************************************************************************/
#define TEST_SRV_STATE(l4_cb) ((l4_cb)->l4cb_test_state.tts_server)

#define TEST_CL_STATE(l4_cb) ((l4_cb)->l4cb_test_state.tts_client)

/*****************************************************************************
 * Externals for tpg_tests_sm.c
 ****************************************************************************/
extern void test_client_sm_initialize(l4_control_block_t *l4_cb,
                                      test_case_info_t *ctx);

extern void test_server_sm_initialize(l4_control_block_t *l4_cb,
                                      test_case_info_t *ctx);

/*****************************************************************************
 * UDP Client Test state machine: N/A
 ****************************************************************************/

/*****************************************************************************
 * Externals for APP and TEST implementations
 ****************************************************************************/
extern void test_app_client_send_start(l4_control_block_t *l4_cb,
                                       test_case_info_t *ctx);
extern void test_app_client_send_stop(l4_control_block_t *l4_cb,
                                      test_case_info_t *ctx);

extern void test_app_server_send_start(l4_control_block_t *l4_cb,
                                       test_case_info_t *ctx);
extern void test_app_server_send_stop(l4_control_block_t *l4_cb,
                                      test_case_info_t *ctx);


extern void test_client_sm_tcp_state_change(tcp_control_block_t *tcb,
                                            test_case_info_t *ctx);
extern void test_client_sm_tcp_snd_win_avail(tcp_control_block_t *tcb,
                                            test_case_info_t *ctx);
extern void test_client_sm_tcp_snd_win_unavail(tcp_control_block_t *tcb,
                                               test_case_info_t *ctx);
extern void test_client_sm_udp_state_change(udp_control_block_t *ucb,
                                            test_case_info_t *ctx);
extern void test_client_sm_tmr_to(l4_control_block_t *l4_cb,
                                  test_case_info_t *ctx);


extern void test_server_sm_tcp_state_change(tcp_control_block_t *tcb,
                                            test_case_info_t *ctx);
extern void test_server_sm_tcp_snd_win_avail(tcp_control_block_t *tcb,
                                            test_case_info_t *ctx);
extern void test_server_sm_tcp_snd_win_unavail(tcp_control_block_t *tcb,
                                              test_case_info_t *ctx);
extern void test_server_sm_udp_state_change(udp_control_block_t *ucb,
                                            test_case_info_t *ctx);

extern bool test_server_sm_has_data_pending(l4_control_block_t *l4_cb);

extern bool test_client_sm_has_data_pending(l4_control_block_t *l4_cb);

#endif /* _H_TPG_TESTS_SM_ */

