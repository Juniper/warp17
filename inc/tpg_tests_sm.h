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

typedef void (*test_sm_function) (l4_control_block_t *l4_cb,
                                  test_sm_event_t event,
                                  test_case_info_t *ctx);

/*****************************************************************************
 * Externs for tpg_tests_sm.c
 ****************************************************************************/
test_sm_function test_sm_function_array[TSTS_MAX_STATE];

/*****************************************************************************
 * Static inlines
 ****************************************************************************/

/*****************************************************************************
 * test_sm_dispatch_event()
 ****************************************************************************/
static inline void test_sm_dispatch_event(l4_control_block_t *l4_cb,
                                          test_sm_event_t event,
                                          test_case_info_t *ctx)
{
    L4_CB_CHECK(l4_cb);
    test_sm_function_array[l4_cb->l4cb_test_state](l4_cb, event, ctx);
}

/*****************************************************************************
 * test_sm_enter_state()
 ****************************************************************************/
static inline void test_sm_enter_state(l4_control_block_t *l4_cb,
                                       test_sm_state_t state,
                                       test_case_info_t *ctx)
{
    l4_cb->l4cb_test_state = state;

    test_sm_dispatch_event(l4_cb, TSTE_ENTER_STATE, ctx);
}

/*****************************************************************************
 * test_sm_client_initialize()
 ****************************************************************************/
static inline void test_sm_client_initialize(l4_control_block_t *l4_cb,
                                             test_case_info_t *ctx)
{
    test_sm_enter_state(l4_cb, TSTS_CL_TO_INIT, ctx);
}

/*****************************************************************************
 * test_sm_listen_initialize()
 ****************************************************************************/
static inline void test_sm_listen_initialize(l4_control_block_t *l4_cb,
                                             test_case_info_t *ctx)
{
    test_sm_enter_state(l4_cb, TSTS_LISTEN, ctx);
}

/*****************************************************************************
 * test_sm_server_initialize()
 ****************************************************************************/
static inline void test_sm_server_initialize(l4_control_block_t *l4_cb,
                                             test_case_info_t *ctx)
{
    test_sm_enter_state(l4_cb, TSTS_SRV_OPENING, ctx);
}

/*****************************************************************************
 * test_sm_sess_connecting()
 ****************************************************************************/
static inline void test_sm_sess_connecting(l4_control_block_t *l4_cb,
                                           test_case_info_t *ctx)
{
    test_sm_dispatch_event(l4_cb, TSTE_CONNECTING, ctx);
}

/*****************************************************************************
 * test_sm_sess_connected()
 ****************************************************************************/
static inline void test_sm_sess_connected(l4_control_block_t *l4_cb,
                                          test_case_info_t *ctx)
{
    test_sm_dispatch_event(l4_cb, TSTE_CONNECTED, ctx);
}

/*****************************************************************************
 * test_sm_sess_closing()
 ****************************************************************************/
static inline void test_sm_sess_closing(l4_control_block_t *l4_cb,
                                        test_case_info_t *ctx)
{
    test_sm_dispatch_event(l4_cb, TSTE_CLOSING, ctx);
}

/*****************************************************************************
 * test_sm_sess_closed()
 ****************************************************************************/
static inline void test_sm_sess_closed(l4_control_block_t *l4_cb,
                                       test_case_info_t *ctx)
{
    test_sm_dispatch_event(l4_cb, TSTE_CLOSED, ctx);
}

/*****************************************************************************
 * test_sm_app_send_start()
 ****************************************************************************/
static inline void test_sm_app_send_start(l4_control_block_t *l4_cb,
                                          test_case_info_t *ctx)
{
    test_sm_dispatch_event(l4_cb, TSTE_APP_SEND_START, ctx);
}

/*****************************************************************************
 * test_sm_app_send_stop()
 ****************************************************************************/
static inline void test_sm_app_send_stop(l4_control_block_t *l4_cb,
                                         test_case_info_t *ctx)
{
    test_sm_dispatch_event(l4_cb, TSTE_APP_SEND_STOP, ctx);
}

/*****************************************************************************
 * test_sm_app_send_win_avail()
 ****************************************************************************/
static inline void test_sm_app_send_win_avail(l4_control_block_t *l4_cb,
                                              test_case_info_t *ctx)
{
    test_sm_dispatch_event(l4_cb, TSTE_SND_WIN, ctx);
}

/*****************************************************************************
 * test_sm_app_send_win_unavail()
 ****************************************************************************/
static inline void test_sm_app_send_win_unavail(l4_control_block_t *l4_cb,
                                                test_case_info_t *ctx)
{
    test_sm_dispatch_event(l4_cb, TSTE_NO_SND_WIN, ctx);
}

/*****************************************************************************
 * test_sm_tmr_to()
 ****************************************************************************/
static inline void test_sm_tmr_to(l4_control_block_t *l4_cb,
                                  test_case_info_t *ctx)
{
    test_sm_dispatch_event(l4_cb, TSTE_TMR_TO, ctx);
}

/*****************************************************************************
 * test_sm_purge()
 ****************************************************************************/
static inline void test_sm_purge(l4_control_block_t *l4_cb,
                                 test_case_info_t *ctx)
{
    test_sm_dispatch_event(l4_cb, TSTE_PURGE, ctx);
}

/*****************************************************************************
 * test_sm_has_data_pending()
 ****************************************************************************/
static inline bool test_sm_has_data_pending(l4_control_block_t *l4_cb)
{
    return l4_cb->l4cb_test_state == TSTS_CL_SENDING ||
        l4_cb->l4cb_test_state == TSTS_SRV_SENDING;
}

#endif /* _H_TPG_TESTS_SM_ */

