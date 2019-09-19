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
 * State machine callbacks
 ****************************************************************************/

/*****************************************************************************
 * test_sm_SF_client_to_init()
 ****************************************************************************/
static void test_sm_SF_client_to_init(l4_control_block_t *l4_cb,
                                      test_sm_event_t event,
                                      test_case_info_t *ctx)
{
    tpg_delay_t *init_delay;

    switch (event) {
    case TSTE_ENTER_STATE:
        init_delay = &ctx->tci_cfg->tcim_test_case.tc_init_delay;
        if (TPG_DELAY_VAL(init_delay) == 0) {
            /* This is a special case. At init time no timers are yet running
             * and we want to refrain from scheduling a send before the
             * test case is already running.
             */
            TEST_CBQ_ADD_TO_OPEN_NO_RESCHED(&ctx->tci_state, l4_cb);
            test_sm_enter_state(l4_cb, TSTS_CL_TO_OPEN, ctx);
            return;
        } else {
            TEST_CBQ_ADD_TO_INIT(&ctx->tci_state, l4_cb);
            L4CB_TEST_TMR_SET(l4_cb,
                              TPG_DELAY_VAL(init_delay) * TPG_SEC_TO_USEC);
            break;
        }

    case TSTE_TMR_TO:
        TEST_CBQ_REM_TO_INIT(&ctx->tci_state, l4_cb);
        TEST_CBQ_ADD_TO_OPEN(&ctx->tci_state, l4_cb);
        test_sm_enter_state(l4_cb, TSTS_CL_TO_OPEN, ctx);
        return;

    case TSTE_PURGE:
        /* Remove from the to-init list and go to PURGED. */
        TEST_CBQ_REM_TO_INIT(&ctx->tci_state, l4_cb);
        test_sm_enter_state(l4_cb, TSTS_PURGED, ctx);
        return;

    default:
        assert(false);
        break;
    }
}

/*****************************************************************************
 * test_sm_SF_client_to_open()
 ****************************************************************************/
static void test_sm_SF_client_to_open(l4_control_block_t *l4_cb,
                                      test_sm_event_t event,
                                      test_case_info_t *ctx)
{
    tpg_delay_t *uptime;

    switch (event) {
    case TSTE_ENTER_STATE:
        break;

    case TSTE_CONNECTING:
        TEST_CBQ_REM_TO_OPEN(&ctx->tci_state, l4_cb);
        test_sm_enter_state(l4_cb, TSTS_CL_OPENING, ctx);
        return;

    case TSTE_CONNECTED:
        /* This can happen when we skip "connecting" (e.g., UDP). */
        TEST_CBQ_REM_TO_OPEN(&ctx->tci_state, l4_cb);

        uptime = &ctx->tci_cfg->tcim_test_case.tc_uptime;

        /* Set conn_uptime timer if any. */
        if (!TPG_DELAY_IS_INF(uptime))
            L4CB_TEST_TMR_SET(l4_cb, TPG_DELAY_VAL(uptime) * TPG_SEC_TO_USEC);

        /* WARNING: Normally we shouldn't call anything after enter_state
         * but in this case we know for sure the l4_cb is still valid!
         * Notify the application layer that the connection is established.
         * Enter state OPEN.
         */
        test_sm_enter_state(l4_cb, TSTS_CL_OPEN, ctx);
        APP_CALL(conn_up, l4_cb->l4cb_app_data.ad_type)(l4_cb,
                                                        &l4_cb->l4cb_app_data,
                                                        ctx->tci_app_stats);
        return;

    case TSTE_PURGE:
        /* Remove from the to-open list and go to PURGED. */
        TEST_CBQ_REM_TO_OPEN(&ctx->tci_state, l4_cb);
        test_sm_enter_state(l4_cb, TSTS_PURGED, ctx);
        return;

    default:
        assert(false);
        break;
    }
}

/*****************************************************************************
 * test_sm_SF_client_opening()
 ****************************************************************************/
static void test_sm_SF_client_opening(l4_control_block_t *l4_cb,
                                      test_sm_event_t event,
                                      test_case_info_t *ctx)
{
    tpg_delay_t *uptime;

    switch (event) {
    case TSTE_ENTER_STATE:
        break;

    case TSTE_CONNECTED:
        uptime = &ctx->tci_cfg->tcim_test_case.tc_uptime;

        /* Set conn_uptime timer if any. */
        if (!TPG_DELAY_IS_INF(uptime))
            L4CB_TEST_TMR_SET(l4_cb, TPG_DELAY_VAL(uptime) * TPG_SEC_TO_USEC);

        /* WARNING: Normally we shouldn't call anything after enter_state
         * but in this case we know for sure the l4_cb is still valid!
         * Notify the application layer that the connection is established.
         * Enter state OPEN.
         */
        test_sm_enter_state(l4_cb, TSTS_CL_OPEN, ctx);
        APP_CALL(conn_up, l4_cb->l4cb_app_data.ad_type)(l4_cb,
                                                        &l4_cb->l4cb_app_data,
                                                        ctx->tci_app_stats);
        return;

    case TSTE_CLOSING:
        test_sm_enter_state(l4_cb, TSTS_CL_CLOSING, ctx);
        return;

    case TSTE_PURGE:
        /* Not on any list so just go to PURGED. */
        test_sm_enter_state(l4_cb, TSTS_PURGED, ctx);
        return;

    default:
        assert(false);
        break;
    }
}

/*****************************************************************************
 * test_sm_SF_client_open()
 ****************************************************************************/
static void test_sm_SF_client_open(l4_control_block_t *l4_cb,
                                   test_sm_event_t event,
                                   test_case_info_t *ctx)
{
    switch (event) {
    case TSTE_ENTER_STATE:
        break;

    case TSTE_APP_SEND_START:
        TEST_CBQ_ADD_TO_SEND(&ctx->tci_state, l4_cb);
        test_sm_enter_state(l4_cb, TSTS_CL_SENDING, ctx);
        return;

    case TSTE_CLOSING:
        /* Cancel the uptime timer if it was set. */
        if (L4CB_TEST_TMR_IS_SET(l4_cb))
            L4CB_TEST_TMR_CANCEL(l4_cb);

        /* Notify the application that the connection is down. */
        APP_CALL(conn_down,
                 l4_cb->l4cb_app_data.ad_type)(l4_cb,
                                               &l4_cb->l4cb_app_data,
                                               ctx->tci_app_stats);

        test_sm_enter_state(l4_cb, TSTS_CL_CLOSING, ctx);
        return;

    case TSTE_SND_WIN:
    case TSTE_NO_SND_WIN:
        /* Might be that we already moved to OPEN because we managed to fit
         * the application data so (by filling the tcp send window) ignore
         * this notification.
         */
        break;

    case TSTE_TMR_TO:
        /* Need to mark the connection for closing because the uptime timer
         * expired.
         */
        TEST_CBQ_ADD_TO_CLOSE(&ctx->tci_state, l4_cb);
        test_sm_enter_state(l4_cb, TSTS_CL_TO_CLOSE, ctx);
        return;

    case TSTE_PURGE:
        /* Not on any list so just go to PURGED. */
        test_sm_enter_state(l4_cb, TSTS_PURGED, ctx);
        break;

    default:
        assert(false);
        break;
    }
}

/*****************************************************************************
 * test_sm_SF_client_sending()
 ****************************************************************************/
static void test_sm_SF_client_sending(l4_control_block_t *l4_cb,
                                      test_sm_event_t event,
                                      test_case_info_t *ctx)
{
    switch (event) {
    case TSTE_ENTER_STATE:
        break;

    case TSTE_CLOSING:
        /* Cancel the uptime timer if it was set. */
        if (L4CB_TEST_TMR_IS_SET(l4_cb))
            L4CB_TEST_TMR_CANCEL(l4_cb);

        /* Remove from the to send list. */
        TEST_CBQ_REM_TO_SEND(&ctx->tci_state, l4_cb);

        /* Notify the application that the connection is down. */
        APP_CALL(conn_down,
                 l4_cb->l4cb_app_data.ad_type)(l4_cb,
                                               &l4_cb->l4cb_app_data,
                                               ctx->tci_app_stats);

        test_sm_enter_state(l4_cb, TSTS_CL_CLOSING, ctx);
        return;

    case TSTE_SND_WIN:
        /* Ignore the SND_WIN event because we didn't get to try to send yet.
         * Might be that there was a time when the session was on the to_send
         * list but didn't have any send window available. It's not a bad
         * thing but it's quite hard to order the SND/NO_SND window messages
         * with the APP_SEND_START/STOP messages.
         */
        return;

    case TSTE_NO_SND_WIN:
        TEST_CBQ_REM_TO_SEND(&ctx->tci_state, l4_cb);
        test_sm_enter_state(l4_cb, TSTS_CL_NO_SND_WIN, ctx);
        return;

    case TSTE_TMR_TO:
        TEST_CBQ_REM_TO_SEND(&ctx->tci_state, l4_cb);
        TEST_CBQ_ADD_TO_CLOSE(&ctx->tci_state, l4_cb);
        test_sm_enter_state(l4_cb, TSTS_CL_TO_CLOSE, ctx);
        return;

    case TSTE_APP_SEND_STOP:
        TEST_CBQ_REM_TO_SEND(&ctx->tci_state, l4_cb);
        test_sm_enter_state(l4_cb, TSTS_CL_OPEN, ctx);
        return;

    case TSTE_PURGE:
        /* Remove from the to-send list and go to PURGED. */
        TEST_CBQ_REM_TO_SEND(&ctx->tci_state, l4_cb);
        test_sm_enter_state(l4_cb, TSTS_PURGED, ctx);
        break;

    default:
        assert(false);
        break;
    }
}

/*****************************************************************************
 * test_sm_SF_client_no_snd_win()
 ****************************************************************************/
static void test_sm_SF_client_no_snd_win(l4_control_block_t *l4_cb,
                                         test_sm_event_t event,
                                         test_case_info_t *ctx)
{
    switch (event) {
    case TSTE_ENTER_STATE:
        break;

    case TSTE_CLOSING:
        /* Cancel the uptime timer if it was set. */
        if (L4CB_TEST_TMR_IS_SET(l4_cb))
            L4CB_TEST_TMR_CANCEL(l4_cb);

        /* Notify the application that the connection is down. */
        APP_CALL(conn_down,
                 l4_cb->l4cb_app_data.ad_type)(l4_cb,
                                               &l4_cb->l4cb_app_data,
                                               ctx->tci_app_stats);
        test_sm_enter_state(l4_cb, TSTS_CL_CLOSING, ctx);
        return;

    case TSTE_TMR_TO:
        TEST_CBQ_ADD_TO_CLOSE(&ctx->tci_state, l4_cb);
        test_sm_enter_state(l4_cb, TSTS_CL_TO_CLOSE, ctx);
        return;

    case TSTE_SND_WIN:
        TEST_CBQ_ADD_TO_SEND(&ctx->tci_state, l4_cb);
        test_sm_enter_state(l4_cb, TSTS_CL_SENDING, ctx);
        return;

    case TSTE_APP_SEND_STOP:
        test_sm_enter_state(l4_cb, TSTS_CL_OPEN, ctx);
        return;

    case TSTE_PURGE:
        /* Not on any list so just go to PURGED. */
        test_sm_enter_state(l4_cb, TSTS_PURGED, ctx);
        break;

    default:
        assert(false);
        break;
    }
}

/*****************************************************************************
 * test_sm_SF_client_to_close()
 ****************************************************************************/
static void test_sm_SF_client_to_close(l4_control_block_t *l4_cb,
                                       test_sm_event_t event,
                                       test_case_info_t *ctx)
{
    switch (event) {
    case TSTE_ENTER_STATE:
        break;

    case TSTE_CLOSING:
        /* Notify the application that the connection will be going down! */
        APP_CALL(conn_down,
                 l4_cb->l4cb_app_data.ad_type)(l4_cb,
                                               &l4_cb->l4cb_app_data,
                                               ctx->tci_app_stats);
        /* Remove from the to-close list and go to the next state. */
        TEST_CBQ_REM_TO_CLOSE(&ctx->tci_state, l4_cb);

        test_sm_enter_state(l4_cb, TSTS_CL_CLOSING, ctx);
        return;

    case TSTE_APP_SEND_START:
    case TSTE_APP_SEND_STOP:
        /* Ignore any app send start/stop as we're going to close soon. */
        break;

    case TSTE_PURGE:
        /* Remove from the to-close list and go to PURGED. */
        TEST_CBQ_REM_CLOSED(&ctx->tci_state, l4_cb);
        test_sm_enter_state(l4_cb, TSTS_PURGED, ctx);
        break;

    default:
        assert(false);
        break;
    }
}

/*****************************************************************************
 * test_sm_SF_client_closing()
 ****************************************************************************/
static void test_sm_SF_client_closing(l4_control_block_t *l4_cb,
                                      test_sm_event_t event,
                                      test_case_info_t *ctx)
{
    switch (event) {
    case TSTE_ENTER_STATE:
        break;

    case TSTE_CLOSED:
        test_sm_enter_state(l4_cb, TSTS_CL_CLOSED, ctx);
        return;

    case TSTE_APP_SEND_START:
    case TSTE_APP_SEND_STOP:
        /* Ignore any app send start/stop as we're going to close soon. */
        break;

    case TSTE_PURGE:
        /* Not on any list so just go to PURGED. */
        test_sm_enter_state(l4_cb, TSTS_PURGED, ctx);
        break;

    default:
        assert(false);
        break;
    }
}

/*****************************************************************************
 * test_sm_SF_client_closed()
 ****************************************************************************/
static void test_sm_SF_client_closed(l4_control_block_t *l4_cb,
                                     test_sm_event_t event,
                                     test_case_info_t *ctx)
{
    tpg_delay_t *downtime;

    switch (event) {
    case TSTE_ENTER_STATE:
        downtime = &ctx->tci_cfg->tcim_test_case.tc_downtime;
        /* Set the downtime timer if we had one configured. */
        if (!TPG_DELAY_IS_INF(downtime))
            L4CB_TEST_TMR_SET(l4_cb, TPG_DELAY_VAL(downtime) * TPG_SEC_TO_USEC);

        /* Add to the closed list. */
        TEST_CBQ_ADD_CLOSED(&ctx->tci_state, l4_cb);
        break;

    case TSTE_TMR_TO:
        TEST_CBQ_REM_CLOSED(&ctx->tci_state, l4_cb);
        TEST_CBQ_ADD_TO_OPEN(&ctx->tci_state, l4_cb);
        test_sm_enter_state(l4_cb, TSTS_CL_TO_OPEN, ctx);
        return;

    case TSTE_PURGE:
        /* Remove from the closed list and go to PURGED. */
        TEST_CBQ_REM_CLOSED(&ctx->tci_state, l4_cb);
        test_sm_enter_state(l4_cb, TSTS_PURGED, ctx);
        break;

    default:
        assert(false);
        break;
    }
}

/*****************************************************************************
 * test_sm_SF_purged()
 ****************************************************************************/
static void test_sm_SF_purged(l4_control_block_t *l4_cb __rte_unused,
                              test_sm_event_t event,
                              test_case_info_t *ctx __rte_unused)
{
    switch (event) {
    case TSTE_ENTER_STATE:
        /* Cancel the test timer if set. */
        if (L4CB_TEST_TMR_IS_SET(l4_cb))
            L4CB_TEST_TMR_CANCEL(l4_cb);
        break;

    case TSTE_CLOSING:
    case TSTE_CLOSED:
        /* When purging we will forcefully close connections so stay in state
         * PURGED.
         */
        break;

    default:
        assert(false);
        break;
    }
}

/*****************************************************************************
 * test_sm_SF_server_opening()
 ****************************************************************************/
static void test_sm_SF_server_opening(l4_control_block_t *l4_cb,
                                      test_sm_event_t event,
                                      test_case_info_t *ctx)
{
    switch (event) {
    case TSTE_ENTER_STATE:
        /* Intermediate state to allow us to call the conn_up callback! */

        /* WARNING: Normally we shouldn't call anything after enter_state
         * but in this case we know for sure the l4_cb is still valid!
         * Notify the application layer that the connection is established.
         * Enter state OPEN.
         */
        test_sm_enter_state(l4_cb, TSTS_SRV_OPEN, ctx);
        APP_CALL(conn_up, l4_cb->l4cb_app_data.ad_type)(l4_cb,
                                                        &l4_cb->l4cb_app_data,
                                                        ctx->tci_app_stats);
        return;

    default:
        assert(false);
        break;
    }
}

/*****************************************************************************
 * test_sm_SF_server_open()
 ****************************************************************************/
static void test_sm_SF_server_open(l4_control_block_t *l4_cb,
                                   test_sm_event_t event,
                                   test_case_info_t *ctx)
{
    switch (event) {
    case TSTE_ENTER_STATE:
        break;

    case TSTE_APP_SEND_START:
        TEST_CBQ_ADD_TO_SEND(&ctx->tci_state, l4_cb);
        test_sm_enter_state(l4_cb, TSTS_SRV_SENDING, ctx);
        return;

    case TSTE_CLOSING:
        /* Notify the application that the connection is down. */
        APP_CALL(conn_down,
                 l4_cb->l4cb_app_data.ad_type)(l4_cb,
                                               &l4_cb->l4cb_app_data,
                                               ctx->tci_app_stats);

        test_sm_enter_state(l4_cb, TSTS_SRV_CLOSING, ctx);
        return;

    case TSTE_SND_WIN:
    case TSTE_NO_SND_WIN:
        /* Might be that we already moved to OPEN because we managed to fit
         * the application data so (by filling the tcp send window) ignore
         * this notification.
         */
        break;

    case TSTE_PURGE:
        /* Not on any list so just go to PURGED. */
        test_sm_enter_state(l4_cb, TSTS_PURGED, ctx);
        break;

    default:
        assert(false);
        break;
    }
}

/*****************************************************************************
 * test_sm_SF_server_sending()
 ****************************************************************************/
static void test_sm_SF_server_sending(l4_control_block_t *l4_cb,
                                      test_sm_event_t event,
                                      test_case_info_t *ctx)
{
    switch (event) {
    case TSTE_ENTER_STATE:
        break;

    case TSTE_CLOSING:
        /* Remove from the to send list. */
        TEST_CBQ_REM_TO_SEND(&ctx->tci_state, l4_cb);

        /* Notify the application that the connection is down. */
        APP_CALL(conn_down,
                 l4_cb->l4cb_app_data.ad_type)(l4_cb,
                                               &l4_cb->l4cb_app_data,
                                               ctx->tci_app_stats);

        test_sm_enter_state(l4_cb, TSTS_SRV_CLOSING, ctx);
        return;

    case TSTE_SND_WIN:
        /* Ignore the SND_WIN event because we didn't get to try to send yet.
         * Might be that there was a time when the session was on the to_send
         * list but didn't have any send window available. It's not a bad
         * thing but it's quite hard to order the SND/NO_SND window messages
         * with the APP_SEND_START/STOP messages.
         */
        return;

    case TSTE_NO_SND_WIN:
        TEST_CBQ_REM_TO_SEND(&ctx->tci_state, l4_cb);
        test_sm_enter_state(l4_cb, TSTS_SRV_NO_SND_WIN, ctx);
        return;

    case TSTE_APP_SEND_STOP:
        TEST_CBQ_REM_TO_SEND(&ctx->tci_state, l4_cb);
        test_sm_enter_state(l4_cb, TSTS_SRV_OPEN, ctx);
        return;

    case TSTE_PURGE:
        /* Remove from the to-send list and go to PURGED. */
        TEST_CBQ_REM_TO_SEND(&ctx->tci_state, l4_cb);
        test_sm_enter_state(l4_cb, TSTS_PURGED, ctx);
        break;

    default:
        assert(false);
        break;
    }
}

/*****************************************************************************
 * test_sm_SF_server_no_snd_win()
 ****************************************************************************/
static void test_sm_SF_server_no_snd_win(l4_control_block_t *l4_cb,
                                         test_sm_event_t event,
                                         test_case_info_t *ctx)
{
    switch (event) {
    case TSTE_ENTER_STATE:
        break;

    case TSTE_CLOSING:
        /* Cancel the uptime timer if it was set. */
        if (L4CB_TEST_TMR_IS_SET(l4_cb))
            L4CB_TEST_TMR_CANCEL(l4_cb);

        /* Notify the application that the connection is down. */
        APP_CALL(conn_down,
                 l4_cb->l4cb_app_data.ad_type)(l4_cb,
                                               &l4_cb->l4cb_app_data,
                                               ctx->tci_app_stats);

        test_sm_enter_state(l4_cb, TSTS_SRV_CLOSING, ctx);
        return;

    case TSTE_SND_WIN:
        TEST_CBQ_ADD_TO_SEND(&ctx->tci_state, l4_cb);
        test_sm_enter_state(l4_cb, TSTS_SRV_SENDING, ctx);
        return;

    case TSTE_APP_SEND_STOP:
        test_sm_enter_state(l4_cb, TSTS_SRV_OPEN, ctx);
        return;

    case TSTE_PURGE:
        /* Not on any list so just go to PURGED. */
        test_sm_enter_state(l4_cb, TSTS_PURGED, ctx);
        break;

    default:
        assert(false);
        break;
    }
}

/*****************************************************************************
 * test_sm_SF_server_closing()
 ****************************************************************************/
static void test_sm_SF_server_closing(l4_control_block_t *l4_cb,
                                      test_sm_event_t event,
                                      test_case_info_t *ctx)
{
    switch (event) {
    case TSTE_ENTER_STATE:
        break;

    case TSTE_CLOSED:
        test_sm_enter_state(l4_cb, TSTS_SRV_CLOSED, ctx);
        return;

    case TSTE_APP_SEND_START:
    case TSTE_APP_SEND_STOP:
        /* Ignore any app send start/stop as we're going to close soon. */
        break;

    case TSTE_PURGE:
        /* Not on any list so just go to PURGED. */
        test_sm_enter_state(l4_cb, TSTS_PURGED, ctx);
        break;

    default:
        assert(false);
        break;
    }
}

/*****************************************************************************
 * test_sm_SF_server_closed()
 ****************************************************************************/
static void test_sm_SF_server_closed(l4_control_block_t *l4_cb __rte_unused,
                                     test_sm_event_t event,
                                     test_case_info_t *ctx __rte_unused)
{
    switch (event) {
    case TSTE_ENTER_STATE:
        break;

    default:
        assert(false);
        break;
    }
}

/*****************************************************************************
 * test_sm_SF_listen()
 ****************************************************************************/
static void test_sm_SF_listen(l4_control_block_t *l4_cb, test_sm_event_t event,
                              test_case_info_t *ctx)
{
    switch (event) {
    case TSTE_ENTER_STATE:
        break;

    case TSTE_PURGE:
        test_sm_enter_state(l4_cb, TSTS_PURGED, ctx);
        return;

    default:
        assert(false);
        break;
    }
}

/*****************************************************************************
 * Session Test State functions call table
 ****************************************************************************/
test_sm_function test_sm_function_array[TSTS_MAX_STATE] = {

    [TSTS_CL_TO_INIT]    = test_sm_SF_client_to_init,
    [TSTS_CL_TO_OPEN]    = test_sm_SF_client_to_open,
    [TSTS_CL_OPENING]    = test_sm_SF_client_opening,
    [TSTS_CL_OPEN]       = test_sm_SF_client_open,
    [TSTS_CL_SENDING]    = test_sm_SF_client_sending,
    [TSTS_CL_NO_SND_WIN] = test_sm_SF_client_no_snd_win,
    [TSTS_CL_TO_CLOSE]   = test_sm_SF_client_to_close,
    [TSTS_CL_CLOSING]    = test_sm_SF_client_closing,
    [TSTS_CL_CLOSED]     = test_sm_SF_client_closed,

    [TSTS_SRV_OPENING]    = test_sm_SF_server_opening,
    [TSTS_SRV_OPEN]       = test_sm_SF_server_open,
    [TSTS_SRV_SENDING]    = test_sm_SF_server_sending,
    [TSTS_SRV_NO_SND_WIN] = test_sm_SF_server_no_snd_win,
    [TSTS_SRV_CLOSING]    = test_sm_SF_server_closing,
    [TSTS_SRV_CLOSED]     = test_sm_SF_server_closed,

    [TSTS_LISTEN] = test_sm_SF_listen,

    [TSTS_PURGED] = test_sm_SF_purged,
};

/*****************************************************************************
 * Session Test State name array
 ****************************************************************************/
const char *test_sm_states_array_array[TSTS_MAX_STATE] = {
    [TSTS_CL_TO_INIT]     = "TSTS_CL_TO_INIT",
    [TSTS_CL_TO_OPEN]     = "TSTS_CL_TO_OPEN",
    [TSTS_CL_OPENING]     = "TSTS_CL_OPENING",
    [TSTS_CL_OPEN]        = "TSTS_CL_OPEN",
    [TSTS_CL_SENDING]     = "TSTS_CL_SENDING",
    [TSTS_CL_NO_SND_WIN]  = "TSTS_CL_NO_SND_WIN",
    [TSTS_CL_TO_CLOSE]    = "TSTS_CL_TO_CLOSE",
    [TSTS_CL_CLOSING]     = "TSTS_CL_CLOSING",
    [TSTS_CL_CLOSED]      = "TSTS_CL_CLOSED",
    [TSTS_SRV_OPENING]    = "TSTS_SRV_OPENING",
    [TSTS_SRV_OPEN]       = "TSTS_SRV_OPEN",
    [TSTS_SRV_SENDING]    = "TSTS_SRV_SENDING",
    [TSTS_SRV_NO_SND_WIN] = "TSTS_SRV_NO_SND_WIN",
    [TSTS_SRV_CLOSING]    = "TSTS_SRV_CLOSING",
    [TSTS_SRV_CLOSED]     = "TSTS_SRV_CLOSED",
    [TSTS_LISTEN]         = "TSTS_LISTEN",
    [TSTS_PURGED]         = "TSTS_PURGED"
};
