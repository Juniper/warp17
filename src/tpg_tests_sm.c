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
 *     tpg_tests_sm.c
 *
 * Description:
 *     Tests state machine.
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
 * Include files
 ****************************************************************************/
#include "tcp_generator.h"

/*****************************************************************************
 * TCP Client Test State Machine Forward References
 ****************************************************************************/
static void test_client_sm_SF_to_init(l4_control_block_t *l4_cb,
                                      test_sm_event_t event,
                                      test_case_info_t *ctx);
static void test_client_sm_SF_to_open(l4_control_block_t *l4_cb,
                                      test_sm_event_t event,
                                      test_case_info_t *ctx);
static void test_client_sm_SF_opening(l4_control_block_t *l4_cb,
                                      test_sm_event_t event,
                                      test_case_info_t *ctx);
static void test_client_sm_SF_open(l4_control_block_t *l4_cb,
                                   test_sm_event_t event,
                                   test_case_info_t *ctx);
static void test_client_sm_SF_sending(l4_control_block_t *l4_cb,
                                      test_sm_event_t event,
                                      test_case_info_t *ctx);
static void test_client_sm_SF_no_snd_win(l4_control_block_t *l4_cb,
                                         test_sm_event_t event,
                                         test_case_info_t *ctx);
static void test_client_sm_SF_to_close(l4_control_block_t *l4_cb,
                                       test_sm_event_t event,
                                       test_case_info_t *ctx);
static void test_client_sm_SF_closing(l4_control_block_t *l4_cb,
                                      test_sm_event_t event,
                                      test_case_info_t *ctx);
static void test_client_sm_SF_closed(l4_control_block_t *l4_cb,
                                     test_sm_event_t event,
                                     test_case_info_t *ctx);

/*****************************************************************************
 * TCP Client Test State functions call table
 ****************************************************************************/
static test_sm_function test_client_sm_function_array[] = {

    [TST_CLS_TO_INIT]    = test_client_sm_SF_to_init,
    [TST_CLS_TO_OPEN]    = test_client_sm_SF_to_open,
    [TST_CLS_OPENING]    = test_client_sm_SF_opening,
    [TST_CLS_OPEN]       = test_client_sm_SF_open,
    [TST_CLS_SENDING]    = test_client_sm_SF_sending,
    [TST_CLS_NO_SND_WIN] = test_client_sm_SF_no_snd_win,
    [TST_CLS_TO_CLOSE]   = test_client_sm_SF_to_close,
    [TST_CLS_CLOSING]    = test_client_sm_SF_closing,
    [TST_CLS_CLOSED]     = test_client_sm_SF_closed,

};

/*****************************************************************************
 * test_client_sm_dispatch_event()
 ****************************************************************************/
static void test_client_sm_dispatch_event(l4_control_block_t *l4_cb,
                                          test_client_sm_event_t event,
                                          test_case_info_t *ctx)
{
    test_sm_event_t client_event = {.tte_client = event};

    L4_CB_CHECK(l4_cb);
    test_client_sm_function_array[l4_cb->l4cb_test_state.tts_client](l4_cb,
                                                                     client_event,
                                                                     ctx);
}

/*****************************************************************************
 * test_client_sm_enter_state()
 ****************************************************************************/
static void test_client_sm_enter_state(l4_control_block_t *l4_cb,
                                       test_client_sm_state_t state,
                                       test_case_info_t *ctx)
{
    l4_cb->l4cb_test_state.tts_client = state;

    test_client_sm_dispatch_event(l4_cb, TST_CLE_ENTER_STATE, ctx);
}

/*****************************************************************************
 * test_sm_SF_to_init()
 ****************************************************************************/
static void test_client_sm_SF_to_init(l4_control_block_t *l4_cb,
                                      test_sm_event_t event,
                                      test_case_info_t *ctx)
{
    tpg_delay_t *init_delay;

    switch (event.tte_client) {
    case TST_CLE_ENTER_STATE:
        init_delay = &ctx->tci_cfg->tcim_client.cl_delays.dc_init_delay;
        if (TPG_DELAY_VAL(init_delay) == 0) {
            /* This is a special case. At init time no timers are yet running
             * and we want to refrain from scheduling a send before the
             * test case is already running.
             */
            TEST_CBQ_ADD_TO_OPEN_NO_RESCHED(&ctx->tci_state, l4_cb);
            test_client_sm_enter_state(l4_cb, TST_CLS_TO_OPEN, ctx);
            return;
        } else {
            TEST_CBQ_ADD_TO_INIT(&ctx->tci_state, l4_cb);
            L4CB_TEST_TMR_SET(l4_cb,
                              TPG_DELAY_VAL(init_delay) * TPG_SEC_TO_USEC);
            break;
        }
    case TST_CLE_TMR_TO:
        TEST_CBQ_REM_TO_INIT(&ctx->tci_state, l4_cb);
        TEST_CBQ_ADD_TO_OPEN(&ctx->tci_state, l4_cb);
        test_client_sm_enter_state(l4_cb, TST_CLS_TO_OPEN, ctx);
        return;
    default:
        break;
    }
}

/*****************************************************************************
 * test_client_sm_SF_to_open()
 ****************************************************************************/
static void test_client_sm_SF_to_open(l4_control_block_t *l4_cb,
                                      test_sm_event_t event,
                                      test_case_info_t *ctx)
{
    tpg_delay_t *uptime;

    switch (event.tte_client) {
    case TST_CLE_TCP_STATE_CHG:
        TEST_CBQ_REM_TO_OPEN(&ctx->tci_state, l4_cb);
        test_client_sm_enter_state(l4_cb, TST_CLS_OPENING, ctx);
        return;
    case TST_CLE_UDP_STATE_CHG:
        TEST_CBQ_REM_TO_OPEN(&ctx->tci_state, l4_cb);

        uptime = &ctx->tci_cfg->tcim_client.cl_delays.dc_uptime;

        /* Set conn_uptime timer if any. */
        if (!TPG_DELAY_IS_INF(uptime)) {
            L4CB_TEST_TMR_SET(l4_cb,
                              TPG_DELAY_VAL(uptime) * TPG_SEC_TO_USEC);
        }
        /* WARNING: Normally we shouldn't call anything after enter_state
         * but in this case we know for sure the tcb is still valid!
         * Notify the application layer that the connection is established.
         * Enter state OPEN.
         */
        test_client_sm_enter_state(l4_cb, TST_CLS_OPEN, ctx);
        APP_CL_CALL(conn_up,
                    l4_cb->l4cb_app_data.ad_type)(l4_cb,
                                                  &l4_cb->l4cb_app_data,
                                                  ctx->tci_app_stats);
        return;
    default:
        break;
    }
}

/*****************************************************************************
 * test_client_sm_SF_opening()
 ****************************************************************************/
static void test_client_sm_SF_opening(l4_control_block_t *l4_cb,
                                      test_sm_event_t event,
                                      test_case_info_t *ctx)
{
    tcp_control_block_t *tcb;
    tpg_delay_t         *uptime;

    switch (event.tte_client) {
    case TST_CLE_TCP_STATE_CHG:
        tcb = container_of(l4_cb, tcp_control_block_t, tcb_l4);
        uptime = &ctx->tci_cfg->tcim_client.cl_delays.dc_uptime;

        if (tcb->tcb_state == TS_ESTABLISHED) {
            /* Set conn_uptime timer if any. */
            if (!TPG_DELAY_IS_INF(uptime)) {
                L4CB_TEST_TMR_SET(l4_cb,
                                  TPG_DELAY_VAL(uptime) * TPG_SEC_TO_USEC);
            }
            /* WARNING: Normally we shouldn't call anything after enter_state
             * but in this case we know for sure the tcb is still valid!
             * Notify the application layer that the connection is established.
             * Enter state OPEN.
             */
            test_client_sm_enter_state(l4_cb, TST_CLS_OPEN, ctx);
            APP_CL_CALL(conn_up,
                        l4_cb->l4cb_app_data.ad_type)(l4_cb,
                                                      &l4_cb->l4cb_app_data,
                                                      ctx->tci_app_stats);
            return;
        } else if (tcb->tcb_state == TS_CLOSED) {
            /* Setting the downtime timer will be done on state-enter in state
             * CLOSED!
             */
            test_client_sm_enter_state(l4_cb, TST_CLS_CLOSED, ctx);
            return;
        }
        break;
    case TST_CLE_UDP_STATE_CHG:
        /* Should never happen. */
        assert(false);
        break;
    default:
        break;
    }
}

/*****************************************************************************
 * test_client_sm_SF_open()
 ****************************************************************************/
static void test_client_sm_SF_open(l4_control_block_t *l4_cb,
                                   test_sm_event_t event,
                                   test_case_info_t *ctx)
{
    tcp_control_block_t *tcb;
    bool                 goto_closed = false;

    switch (event.tte_client) {
    case TST_CLE_APP_SEND_START:
        TEST_CBQ_ADD_TO_SEND(&ctx->tci_state, l4_cb);
        test_client_sm_enter_state(l4_cb, TST_CLS_SENDING, ctx);
        return;

    case TST_CLE_TCP_STATE_CHG:
        tcb = container_of(l4_cb, tcp_control_block_t, tcb_l4);

        /* Only interested in moving out of established. */
        if (tcb->tcb_state == TS_ESTABLISHED)
            break;

        if (tcb->tcb_state == TS_CLOSED)
            goto_closed = true;

        /* Fallthrough */
    case TST_CLE_UDP_STATE_CHG:
        /* Moving out of ESTABLISHED. */
        /* Cancel the uptime timer if it was set. */
        if (L4CB_TEST_TMR_IS_SET(l4_cb))
            L4CB_TEST_TMR_CANCEL(l4_cb);

        /* Notify the application that the connection is down. */
        APP_CL_CALL(conn_down,
                    l4_cb->l4cb_app_data.ad_type)(l4_cb,
                                                  &l4_cb->l4cb_app_data,
                                                  ctx->tci_app_stats);

        if (goto_closed) {
            test_client_sm_enter_state(l4_cb, TST_CLS_CLOSED, ctx);
            return;
        }
        test_client_sm_enter_state(l4_cb, TST_CLS_CLOSING, ctx);
        return;

    case TST_CLE_TMR_TO:
        /* Need to mark the connection for closing because the uptime timer
         * expired.
         */
        TEST_CBQ_ADD_TO_CLOSE(&ctx->tci_state, l4_cb);
        test_client_sm_enter_state(l4_cb, TST_CLS_TO_CLOSE, ctx);
        return;
    default:
        break;
    }
}

/*****************************************************************************
 * test_client_sm_SF_sending()
 ****************************************************************************/
static void test_client_sm_SF_sending(l4_control_block_t *l4_cb,
                                      test_sm_event_t event,
                                      test_case_info_t *ctx)
{
    tcp_control_block_t *tcb;
    bool                 goto_closed = false;

    switch (event.tte_client) {
    case TST_CLE_TCP_STATE_CHG:
        tcb = container_of(l4_cb, tcp_control_block_t, tcb_l4);

        /* Only interested in moving out of established. */
        if (tcb->tcb_state == TS_ESTABLISHED)
            break;

        if (tcb->tcb_state == TS_CLOSED)
            goto_closed = true;

        /* Fallthrough */
    case TST_CLE_UDP_STATE_CHG:
        /* Moving out of ESTABLISHED. */
        /* Cancel the uptime timer if it was set. */
        if (L4CB_TEST_TMR_IS_SET(l4_cb))
            L4CB_TEST_TMR_CANCEL(l4_cb);

        /* Remove from the to send list. */
        TEST_CBQ_REM_TO_SEND(&ctx->tci_state, l4_cb);

        /* Notify the application that the connection is down. */
        APP_CL_CALL(conn_down,
                    l4_cb->l4cb_app_data.ad_type)(l4_cb,
                                                  &l4_cb->l4cb_app_data,
                                                  ctx->tci_app_stats);
        if (goto_closed) {
            test_client_sm_enter_state(l4_cb, TST_CLS_CLOSED, ctx);
            return;
        }
        test_client_sm_enter_state(l4_cb, TST_CLS_CLOSING, ctx);
        return;

    case TST_CLE_NO_SND_WIN:
        TEST_CBQ_REM_TO_SEND(&ctx->tci_state, l4_cb);
        test_client_sm_enter_state(l4_cb, TST_CLS_NO_SND_WIN, ctx);
        return;

    case TST_CLE_TMR_TO:
        TEST_CBQ_REM_TO_SEND(&ctx->tci_state, l4_cb);
        TEST_CBQ_ADD_TO_CLOSE(&ctx->tci_state, l4_cb);
        test_client_sm_enter_state(l4_cb, TST_CLS_TO_CLOSE, ctx);
        return;

    case TST_CLE_APP_SEND_STOP:
        TEST_CBQ_REM_TO_SEND(&ctx->tci_state, l4_cb);
        test_client_sm_enter_state(l4_cb, TST_CLS_OPEN, ctx);
        return;

    default:
        break;
    }
}

/*****************************************************************************
 * test_client_sm_SF_no_snd_win()
 ****************************************************************************/
static void test_client_sm_SF_no_snd_win(l4_control_block_t *l4_cb,
                                         test_sm_event_t event,
                                         test_case_info_t *ctx)
{
    tcp_control_block_t *tcb;

    switch (event.tte_client) {
    case TST_CLE_TCP_STATE_CHG:
        tcb = container_of(l4_cb, tcp_control_block_t, tcb_l4);

        /* Moving out of ESTABLISHED. */
        /* Cancel the uptime timer if it was set. */
        if (L4CB_TEST_TMR_IS_SET(l4_cb))
            L4CB_TEST_TMR_CANCEL(l4_cb);

        /* Notify the application that the connection is down. */
        APP_CL_CALL(conn_down,
                    l4_cb->l4cb_app_data.ad_type)(l4_cb,
                                                  &l4_cb->l4cb_app_data,
                                                  ctx->tci_app_stats);
        if (tcb->tcb_state == TS_CLOSED) {
            test_client_sm_enter_state(l4_cb, TST_CLS_CLOSED, ctx);
            return;
        }
        test_client_sm_enter_state(l4_cb, TST_CLS_CLOSING, ctx);
        return;

    case TST_CLE_UDP_STATE_CHG:
        assert(false);
        /* Should never happen! */
        break;

    case TST_CLE_TMR_TO:
        TEST_CBQ_ADD_TO_CLOSE(&ctx->tci_state, l4_cb);
        test_client_sm_enter_state(l4_cb, TST_CLS_TO_CLOSE, ctx);
        return;

    case TST_CLE_SND_WIN:
        TEST_CBQ_ADD_TO_SEND(&ctx->tci_state, l4_cb);
        test_client_sm_enter_state(l4_cb, TST_CLS_SENDING, ctx);
        return;

    case TST_CLE_APP_SEND_STOP:
        test_client_sm_enter_state(l4_cb, TST_CLS_OPEN, ctx);
        return;

    default:
        break;
    }
}

/*****************************************************************************
 * test_client_sm_SF_to_close()
 ****************************************************************************/
static void test_client_sm_SF_to_close(l4_control_block_t *l4_cb,
                                       test_sm_event_t event,
                                       test_case_info_t *ctx)
{
    bool goto_closed = false;

    switch (event.tte_client) {
    case TST_CLE_UDP_STATE_CHG:
        /* Fallthrough */
        goto_closed = true;
    case TST_CLE_TCP_STATE_CHG:
        /* Notify the application that the connection will be going down! */
        APP_CL_CALL(conn_down,
                    l4_cb->l4cb_app_data.ad_type)(l4_cb,
                                                  &l4_cb->l4cb_app_data,
                                                  ctx->tci_app_stats);
        /* Remove from the to-close list and go to the next state. */
        TEST_CBQ_REM_TO_CLOSE(&ctx->tci_state, l4_cb);

        if (goto_closed)
            test_client_sm_enter_state(l4_cb, TST_CLS_CLOSED, ctx);
        else
            test_client_sm_enter_state(l4_cb, TST_CLS_CLOSING, ctx);
        return;

    default:
        break;
    }
}

/*****************************************************************************
 * test_client_sm_SF_closing()
 ****************************************************************************/
static void test_client_sm_SF_closing(l4_control_block_t *l4_cb,
                                      test_sm_event_t event,
                                      test_case_info_t *ctx)
{
    tcp_control_block_t *tcb;

    switch (event.tte_client) {
    case TST_CLE_TCP_STATE_CHG:
        tcb = container_of(l4_cb, tcp_control_block_t, tcb_l4);

        if (tcb->tcb_state == TS_CLOSED) {
            test_client_sm_enter_state(l4_cb, TST_CLS_CLOSED, ctx);
            return;
        }
        break;

    case TST_CLE_UDP_STATE_CHG:
        /* Should never happen */
        assert(false);
        break;

    default:
        break;
    }
}

/*****************************************************************************
 * test_client_sm_SF_closed()
 ****************************************************************************/
static void test_client_sm_SF_closed(l4_control_block_t *l4_cb,
                                     test_sm_event_t event,
                                     test_case_info_t *ctx)
{
    tpg_delay_t *downtime;

    switch (event.tte_client) {
    case TST_CLE_ENTER_STATE:
        downtime = &ctx->tci_cfg->tcim_client.cl_delays.dc_downtime;
        /* Set the downtime timer if we had one configured. */
        if (!TPG_DELAY_IS_INF(downtime)) {
            L4CB_TEST_TMR_SET(l4_cb,
                              TPG_DELAY_VAL(downtime) * TPG_SEC_TO_USEC);
        }
        /* Add to the closed list. */
        TEST_CBQ_ADD_CLOSED(&ctx->tci_state, l4_cb);
        break;
    case TST_CLE_TMR_TO:
        TEST_CBQ_REM_CLOSED(&ctx->tci_state, l4_cb);
        TEST_CBQ_ADD_TO_OPEN(&ctx->tci_state, l4_cb);
        test_client_sm_enter_state(l4_cb, TST_CLS_TO_OPEN, ctx);
        return;
    default:
        break;
    }
}


/*****************************************************************************
 * Global TCP Client functions
 ****************************************************************************/
/*****************************************************************************
 * test_client_sm_initialize()
 ****************************************************************************/
void test_client_sm_initialize(l4_control_block_t *l4_cb,
                               test_case_info_t *ctx)
{
    test_client_sm_enter_state(l4_cb, TST_CLS_TO_INIT, ctx);
}

/*****************************************************************************
 * TCP Server Test State Machine Forward References
 ****************************************************************************/
static void test_server_sm_SF_init(l4_control_block_t *l4_cb,
                                   test_sm_event_t event,
                                   test_case_info_t *ctx);
static void test_server_sm_SF_open(l4_control_block_t *l4_cb,
                                   test_sm_event_t event,
                                   test_case_info_t *ctx);
static void test_server_sm_SF_sending(l4_control_block_t *l4_cb,
                                      test_sm_event_t event,
                                      test_case_info_t *ctx);
static void test_server_sm_SF_no_snd_win(l4_control_block_t *l4_cb,
                                         test_sm_event_t event,
                                         test_case_info_t *ctx);
static void test_server_sm_SF_closing(l4_control_block_t *l4_cb,
                                      test_sm_event_t event,
                                      test_case_info_t *ctx);

/*****************************************************************************
 * TCP server Test State functions call table
 ****************************************************************************/
static test_sm_function test_server_sm_function_array[] = {

    [TST_SRVS_INIT]       = test_server_sm_SF_init,
    [TST_SRVS_OPEN]       = test_server_sm_SF_open,
    [TST_SRVS_SENDING]    = test_server_sm_SF_sending,
    [TST_SRVS_NO_SND_WIN] = test_server_sm_SF_no_snd_win,
    [TST_SRVS_CLOSING]    = test_server_sm_SF_closing,

};

/*****************************************************************************
 * test_server_sm_dispatch_event()
 ****************************************************************************/
static void test_server_sm_dispatch_event(l4_control_block_t *l4_cb,
                                          test_server_sm_event_t event,
                                          test_case_info_t *ctx)
{
    test_sm_event_t server_event = {.tte_server = event};

    L4_CB_CHECK(l4_cb);
    test_server_sm_function_array[l4_cb->l4cb_test_state.tts_server](l4_cb,
                                                                     server_event,
                                                                     ctx);
}

/*****************************************************************************
 * test_server_sm_enter_state()
 ****************************************************************************/
static void test_server_sm_enter_state(l4_control_block_t *l4_cb,
                                       test_server_sm_state_t state,
                                       test_case_info_t *ctx)
{
    l4_cb->l4cb_test_state.tts_server = state;

    test_server_sm_dispatch_event(l4_cb, TST_SRVE_ENTER_STATE, ctx);
}

/*****************************************************************************
 * test_server_sm_SF_init()
 ****************************************************************************/
static void test_server_sm_SF_init(l4_control_block_t *l4_cb,
                                   test_sm_event_t event,
                                   test_case_info_t *ctx)
{
    tcp_control_block_t *tcb;
    udp_control_block_t *ucb;

    switch (event.tte_server) {
    case TST_SRVE_ENTER_STATE:
        /* The server might receive valid data already from state
         * SYN-RECVD (for TCP) or US_SERVER (for UDP).
         */
        APP_SRV_CALL(conn_up,
                     l4_cb->l4cb_app_data.ad_type)(l4_cb,
                                                   &l4_cb->l4cb_app_data,
                                                   ctx->tci_app_stats);
        break;
    case TST_SRVE_TCP_STATE_CHG:
        tcb = container_of(l4_cb, tcp_control_block_t, tcb_l4);

        if (tcb->tcb_state == TS_SYN_RECV) {
            /* If moving to SYN_RECV then change state to open. */
            test_server_sm_enter_state(l4_cb, TST_SRVS_OPEN, ctx);
            return;
        }
        break;

    case TST_SRVE_UDP_STATE_CHG:
        ucb = container_of(l4_cb, udp_control_block_t, ucb_l4);

        if (ucb->ucb_state == US_OPEN) {
            /* Change state to open. */
            test_server_sm_enter_state(l4_cb, TST_SRVS_OPEN, ctx);
            return;
        }
        break;

    default:
        break;
    }
}

/*****************************************************************************
 * test_server_sm_SF_open()
 ****************************************************************************/
static void test_server_sm_SF_open(l4_control_block_t *l4_cb,
                                   test_sm_event_t event,
                                   test_case_info_t *ctx)
{
    tcp_control_block_t *tcb;

    switch (event.tte_server) {
    case TST_SRVE_TCP_STATE_CHG:
        tcb = container_of(l4_cb, tcp_control_block_t, tcb_l4);

        /* If we moved to established we stay in the same test state. */
        if (tcb->tcb_state == TS_ESTABLISHED)
            return;

        /* Fallthrough */
    case TST_SRVE_UDP_STATE_CHG:
        /* Notify the application that the connection went down. */
        APP_SRV_CALL(conn_down,
                     l4_cb->l4cb_app_data.ad_type)(l4_cb,
                                                   &l4_cb->l4cb_app_data,
                                                   ctx->tci_app_stats);
        test_server_sm_enter_state(l4_cb, TST_SRVS_CLOSING, ctx);
        return;

    case TST_SRVE_APP_SEND_START:
        TEST_CBQ_ADD_TO_SEND(&ctx->tci_state, l4_cb);
        test_server_sm_enter_state(l4_cb, TST_SRVS_SENDING, ctx);
        break;

    default:
        break;
    }
}

/*****************************************************************************
 * test_server_sm_SF_sending()
 ****************************************************************************/
static void test_server_sm_SF_sending(l4_control_block_t *l4_cb,
                                      test_sm_event_t event,
                                      test_case_info_t *ctx)
{
    tcp_control_block_t *tcb;

    switch (event.tte_server) {
    case TST_SRVE_TCP_STATE_CHG:
        /* If we moved to established we stay in the same test state. */
        tcb = container_of(l4_cb, tcp_control_block_t, tcb_l4);

        if (tcb->tcb_state == TS_ESTABLISHED)
            return;

        /* Fallthrough */
    case TST_SRVE_UDP_STATE_CHG:
        /* Remove from the to send list. */
        TEST_CBQ_REM_TO_SEND(&ctx->tci_state, l4_cb);
        /* Notify the application that the connection went down. */
        APP_SRV_CALL(conn_down,
                     l4_cb->l4cb_app_data.ad_type)(l4_cb,
                                                   &l4_cb->l4cb_app_data,
                                                   ctx->tci_app_stats);
        test_server_sm_enter_state(l4_cb, TST_SRVS_CLOSING, ctx);
        return;

    case TST_SRVE_NO_SND_WIN:
        /* Remove from the to send list. */
        TEST_CBQ_REM_TO_SEND(&ctx->tci_state, l4_cb);
        test_server_sm_enter_state(l4_cb, TST_SRVS_NO_SND_WIN, ctx);
        return;

    case TST_SRVE_APP_SEND_STOP:
        /* Remove from the to send list. */
        TEST_CBQ_REM_TO_SEND(&ctx->tci_state, l4_cb);
        test_server_sm_enter_state(l4_cb, TST_SRVS_OPEN, ctx);
        return;
    default:
        break;
    }
}

/*****************************************************************************
 * test_server_sm_SF_no_snd_win()
 ****************************************************************************/
static void test_server_sm_SF_no_snd_win(l4_control_block_t *l4_cb,
                                         test_sm_event_t event,
                                         test_case_info_t *ctx)
{
    switch (event.tte_server) {
    case TST_SRVE_TCP_STATE_CHG:
        /* Notify the application that the connection went down. */
        APP_SRV_CALL(conn_down,
                     l4_cb->l4cb_app_data.ad_type)(l4_cb,
                                                   &l4_cb->l4cb_app_data,
                                                   ctx->tci_app_stats);
        test_server_sm_enter_state(l4_cb, TST_SRVS_CLOSING, ctx);
        return;

    case TST_SRVE_UDP_STATE_CHG:
        /* Should never happen. */
        assert(false);
        break;

    case TST_SRVE_SND_WIN:
        TEST_CBQ_ADD_TO_SEND(&ctx->tci_state, l4_cb);
        test_server_sm_enter_state(l4_cb, TST_SRVS_SENDING, ctx);
        break;

    case TST_SRVE_APP_SEND_STOP:
        test_server_sm_enter_state(l4_cb, TST_SRVS_OPEN, ctx);
        return;

    default:
        break;
    }
}

/*****************************************************************************
 * test_server_sm_SF_closing()
 ****************************************************************************/
static void test_server_sm_SF_closing(l4_control_block_t *tcb __rte_unused,
                                      test_sm_event_t event __rte_unused,
                                      test_case_info_t *ctx __rte_unused)
{
    /* Nothing to do actually.. Maybe we should remove the state? */
}

/*****************************************************************************
 * Global TCP Server functions
 ****************************************************************************/
/*****************************************************************************
 * test_server_sm_initialize()
 ****************************************************************************/
void test_server_sm_initialize(l4_control_block_t *l4_cb,
                               test_case_info_t *ctx)
{
    test_server_sm_enter_state(l4_cb, TST_SRVS_INIT, ctx);
}

/*****************************************************************************
 * test_app_client_send_start()
 ****************************************************************************/
void test_app_client_send_start(l4_control_block_t *l4_cb,
                                test_case_info_t *ctx)
{
    test_client_sm_dispatch_event(l4_cb, TST_CLE_APP_SEND_START, ctx);
}

/*****************************************************************************
 * test_app_client_send_stop()
 ****************************************************************************/
void test_app_client_send_stop(l4_control_block_t *l4_cb,
                               test_case_info_t *ctx)
{
    test_client_sm_dispatch_event(l4_cb, TST_CLE_APP_SEND_STOP, ctx);
}

/*****************************************************************************
 * test_client_sm_tcp_state_change()
 ****************************************************************************/
void test_client_sm_tcp_state_change(tcp_control_block_t *tcb,
                                     test_case_info_t *ctx)
{
    test_client_sm_dispatch_event(&tcb->tcb_l4, TST_CLE_TCP_STATE_CHG, ctx);
}

/*****************************************************************************
 * test_client_sm_tcp_snd_win_avail()
 ****************************************************************************/
void test_client_sm_tcp_snd_win_avail(tcp_control_block_t *tcb,
                                      test_case_info_t *ctx)
{
    test_client_sm_dispatch_event(&tcb->tcb_l4, TST_CLE_SND_WIN, ctx);
}

/*****************************************************************************
 * test_client_sm_tcp_snd_win_unavail()
 ****************************************************************************/
void test_client_sm_tcp_snd_win_unavail(tcp_control_block_t *tcb,
                                        test_case_info_t *ctx)
{
    test_client_sm_dispatch_event(&tcb->tcb_l4, TST_CLE_NO_SND_WIN, ctx);
}

/*****************************************************************************
 * test_client_sm_udp_state_change()
 ****************************************************************************/
void test_client_sm_udp_state_change(udp_control_block_t *ucb,
                                     test_case_info_t *ctx)
{
    test_client_sm_dispatch_event(&ucb->ucb_l4, TST_CLE_UDP_STATE_CHG, ctx);
}

/*****************************************************************************
 * test_client_sm_tmr_to()
 ****************************************************************************/
void test_client_sm_tmr_to(l4_control_block_t *l4_cb,
                           test_case_info_t *ctx)
{
    test_client_sm_dispatch_event(l4_cb, TST_CLE_TMR_TO, ctx);
}

/*****************************************************************************
 * test_app_server_send_start()
 ****************************************************************************/
void test_app_server_send_start(l4_control_block_t *l4_cb,
                                test_case_info_t *ctx)
{
    test_server_sm_dispatch_event(l4_cb, TST_SRVE_APP_SEND_START, ctx);
}

/*****************************************************************************
 * test_app_server_send_stop()
 ****************************************************************************/
void test_app_server_send_stop(l4_control_block_t *l4_cb,
                               test_case_info_t *ctx)
{
    test_server_sm_dispatch_event(l4_cb, TST_SRVE_APP_SEND_STOP, ctx);
}

/*****************************************************************************
 * test_server_sm_tcp_state_change()
 ****************************************************************************/
void test_server_sm_tcp_state_change(tcp_control_block_t *tcb,
                                     test_case_info_t *ctx)
{
    test_server_sm_dispatch_event(&tcb->tcb_l4, TST_SRVE_TCP_STATE_CHG, ctx);
}

/*****************************************************************************
 * test_server_sm_tcp_snd_win_avail()
 ****************************************************************************/
void test_server_sm_tcp_snd_win_avail(tcp_control_block_t *tcb,
                                      test_case_info_t *ctx)
{
    test_server_sm_dispatch_event(&tcb->tcb_l4, TST_SRVE_SND_WIN, ctx);
}

/*****************************************************************************
 * test_server_sm_tcp_snd_win_unavail()
 ****************************************************************************/
void test_server_sm_tcp_snd_win_unavail(tcp_control_block_t *tcb,
                                        test_case_info_t *ctx)
{
    test_server_sm_dispatch_event(&tcb->tcb_l4, TST_SRVE_NO_SND_WIN, ctx);
}

/*****************************************************************************
 * test_server_sm_udp_state_change()
 ****************************************************************************/
void test_server_sm_udp_state_change(udp_control_block_t *ucb,
                                     test_case_info_t *ctx)
{
    test_server_sm_dispatch_event(&ucb->ucb_l4, TST_SRVE_UDP_STATE_CHG, ctx);
}

bool test_server_sm_has_data_pending(l4_control_block_t *l4_cb)
{
    return TEST_SRV_STATE(l4_cb) == TST_SRVS_SENDING;
}

bool test_client_sm_has_data_pending(l4_control_block_t *l4_cb)
{
    return TEST_CL_STATE(l4_cb) == TST_CLS_SENDING;
}
