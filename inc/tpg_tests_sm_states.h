/*
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER
 *
 * Copyright (c) 2018, Juniper Networks, Inc. All rights reserved.
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
 *     tpg_tests_sm_states.h
 *
 * Description:
 *     State machine states and events.
 *
 * Author:
 *     Dumitru Ceara
 *
 * Initial Created:
 *     02/08/2018
 *
 * Notes:
 *
 */

/*****************************************************************************
 * Multiple include protection
 ****************************************************************************/
#ifndef _H_TPG_TESTS_SM_STATES_
#define _H_TPG_TESTS_SM_STATES_

/*****************************************************************************
 * Test state machine:
 * See tpg_test_sm.dot for the diagram. (xdot dot/tpg_test_sm.dot)
 *
 * In state TSTS_SENDING the control block is on the to-send list and the
 * test engine will try sending traffic on that connection.
 * Once the uptime timer fires the control block will change state (from
 * TSTS_OPEN/TSTS_SENDING/TSTS_NO_SND_WIN) to TSTS_TO_CLOSE and the
 * control block will be added on the to-close list. The to-close list is
 * walked by the test engine which will issue CLOSE on the control blocks on
 * that list.
 * In state TSTS_CLOSED the downtime timer might be running (based on config)
 * so if the timer fires then the control block moves to state TSTS_TO_OPEN
 * and is added to the to-open list which is walked by the test engine.
 ****************************************************************************/

typedef enum {

    TSTS_CL_TO_INIT,
    TSTS_CL_TO_OPEN,
    TSTS_CL_OPENING,
    TSTS_CL_OPEN,
    TSTS_CL_SENDING,
    TSTS_CL_NO_SND_WIN,
    TSTS_CL_TO_CLOSE,
    TSTS_CL_CLOSING,
    TSTS_CL_CLOSED,

    TSTS_SRV_OPENING,
    TSTS_SRV_OPEN,
    TSTS_SRV_SENDING,
    TSTS_SRV_NO_SND_WIN,
    TSTS_SRV_CLOSING,
    TSTS_SRV_CLOSED,

    TSTS_LISTEN,

    TSTS_PURGED,

    TSTS_MAX_STATE

} test_sm_state_t;

typedef enum {

    TSTE_ENTER_STATE,

    /* Timeouts */
    TSTE_TMR_TO,

    /* Session state events. */
    TSTE_CONNECTING,
    TSTE_CONNECTED,
    TSTE_CLOSING,
    TSTE_CLOSED,
    TSTE_NO_SND_WIN,
    TSTE_SND_WIN,

    /* APP Events. */
    TSTE_APP_SEND_START,
    TSTE_APP_SEND_STOP,

    /* Test case events. */
    TSTE_PURGE,

    TSTE_MAX_EVENT

} test_sm_event_t;

#endif /* _H_TPG_TESTS_SM_STATES_ */

