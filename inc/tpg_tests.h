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
 *     tpg_tests.h
 *
 * Description:
 *     Test engine interface.
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
#ifndef _H_TPG_TESTS_
#define _H_TPG_TESTS_

/*****************************************************************************
 * Tests module message types.
 ****************************************************************************/
enum tests_control_msg_types {

    MSG_TYPE_DEF_START_MARKER(TESTS),
    MSG_TEST_CASE_INIT,
    MSG_TEST_CASE_START,
    MSG_TEST_CASE_RUN_OPEN,
    MSG_TEST_CASE_RUN_CLOSE,
    MSG_TEST_CASE_RUN_SEND,
    MSG_TEST_CASE_STOP,
    MSG_TEST_CASE_STATS_REQ,
    MSG_TYPE_DEF_END_MARKER(TESTS),

};

MSG_TYPE_MAX_CHECK(TESTS);

/*****************************************************************************
 * TEST event notifications
 ****************************************************************************/
enum {

    TEST_NOTIF_SERVER_UP = NOTIFID_INITIALIZER(NOTIF_TEST_MODULE),
    TEST_NOTIF_SERVER_DOWN,
    TEST_NOTIF_SERVER_FAILED,

    TEST_NOTIF_CLIENT_UP,
    TEST_NOTIF_CLIENT_DOWN,
    TEST_NOTIF_CLIENT_FAILED,

    TEST_NOTIF_TMR_FIRED,

    TEST_NOTIF_APP_CLIENT_SEND_START,
    TEST_NOTIF_APP_CLIENT_SEND_STOP,
    TEST_NOTIF_APP_CLIENT_CLOSE,

    TEST_NOTIF_APP_SERVER_SEND_START,
    TEST_NOTIF_APP_SERVER_SEND_STOP,
    TEST_NOTIF_APP_SERVER_CLOSE,

    TEST_NOTIF_DATA_FAILED,
    TEST_NOTIF_DATA_NULL,

};

#define TEST_NOTIF(notif, cb, tcid, iface)                       \
    do {                                                         \
        notif_arg_t arg = TEST_NOTIF_ARG((tcid), (iface), (cb)); \
        test_notif_cb((notif), &arg);                            \
    } while (0)

#define TEST_NOTIF_TCB(notif, cb_arg)              \
    TEST_NOTIF((notif), &(cb_arg)->tcb_l4,         \
               (cb_arg)->tcb_l4.l4cb_test_case_id, \
               (cb_arg)->tcb_l4.l4cb_interface)

#define TEST_NOTIF_UCB(notif, cb_arg)              \
    TEST_NOTIF((notif), &(cb_arg)->ucb_l4,         \
               (cb_arg)->ucb_l4.l4cb_test_case_id, \
               (cb_arg)->ucb_l4.l4cb_interface)

/* Callback to be executed whenever an interesting event happens. */
extern notif_cb_t test_notif_cb;

/*****************************************************************************
 * TCP/UDP test states list definitions
 ****************************************************************************/
typedef TAILQ_HEAD(tcp_test_cb_list_s, l4_control_block_s) tlkp_test_cb_list_t;

#define TEST_CBQ_INIT(list) TAILQ_INIT((list))

#define TEST_CBQ_ADD(list, cb) \
    L4_CB_ADD_LIST((list), (cb))

#define TEST_CBQ_REM(list, cb) \
    L4_CB_REM_LIST((list), (cb))

#define TEST_CBQ_EMPTY(list) TAILQ_EMPTY((list))

#define TEST_CBQ_FIRST(list, type) ((__typeof__(type) *)TAILQ_FIRST((list)))

#define TEST_CBQ_ADD_TO_OPEN(ts, cb)                                            \
    do {                                                                        \
        TEST_CBQ_ADD(&(ts)->tos_to_open_cbs, (cb));                             \
        test_resched_open((ts), (cb)->l4cb_interface, (cb)->l4cb_test_case_id); \
    } while (0)

#define TEST_CBQ_ADD_TO_CLOSE(ts, cb)                  \
    do {                                               \
        TEST_CBQ_ADD(&(ts)->tos_to_close_cbs, (cb));   \
        test_resched_close((ts), (cb)->l4cb_interface, \
                           (cb)->l4cb_test_case_id);   \
    } while (0)

#define TEST_CBQ_ADD_TO_SEND(ts, cb)                                            \
    do {                                                                        \
        TEST_CBQ_ADD(&(ts)->tos_to_send_cbs, (cb));                             \
        test_resched_send((ts), (cb)->l4cb_interface, (cb)->l4cb_test_case_id); \
    } while (0)

#define TEST_CBQ_ADD_TO_INIT(ts, cb) \
    TEST_CBQ_ADD(&(ts)->tos_to_init_cbs, (cb))
#define TEST_CBQ_ADD_CLOSED(ts, cb) \
    TEST_CBQ_ADD(&(ts)->tos_closed_cbs, (cb))

#define TEST_CBQ_REM_TO_OPEN(ts, cb) \
    TEST_CBQ_REM(&(ts)->tos_to_open_cbs, (cb))
#define TEST_CBQ_REM_TO_CLOSE(ts, cb) \
    TEST_CBQ_REM(&(ts)->tos_to_close_cbs, (cb))
#define TEST_CBQ_REM_TO_SEND(ts, cb) \
    TEST_CBQ_REM(&(ts)->tos_to_send_cbs, (cb))
#define TEST_CBQ_REM_TO_INIT(ts, cb) \
    TEST_CBQ_REM(&(ts)->tos_to_init_cbs, (cb))
#define TEST_CBQ_REM_CLOSED(ts, cb) \
    TEST_CBQ_REM(&(ts)->tos_closed_cbs, (cb))

/*****************************************************************************
 * Tests module message type definitions.
 ****************************************************************************/
typedef struct test_case_init_msg_s {

    uint32_t             tcim_eth_port;
    uint32_t             tcim_test_case_id;

    tpg_test_case_type_t tcim_type;
    union {
        tpg_client_t     tcim_client;
        tpg_server_t     tcim_server;
    };

} __tpg_msg test_case_init_msg_t;

typedef struct test_case_start_msg_s {

    uint32_t tcsm_eth_port;
    uint32_t tcsm_test_case_id;

} __tpg_msg test_case_start_msg_t;

/* NOTE: this is always a LOCAL message. */
typedef struct test_case_run_msg_s {

    uint32_t tcrm_eth_port;
    uint32_t tcrm_test_case_id;

} __tpg_msg test_case_run_msg_t;

typedef struct test_case_stop_msg_s {

    uint32_t       tcsm_eth_port;
    uint32_t       tcsm_test_case_id;

    volatile bool *tcsm_done; /* The sender blocks until 'done == true'. */

} __tpg_msg test_case_stop_msg_t;

typedef struct test_case_stats_req_msg_s {

    uint32_t                    tcsrm_eth_port;
    uint32_t                    tcsrm_test_case_id;
    tpg_test_case_stats_t      *tcsrm_test_case_stats;
    tpg_test_case_rate_stats_t *tcsrm_test_case_rate_stats;
    tpg_test_case_app_stats_t  *tcsrm_test_case_app_stats;

} __tpg_msg test_case_stats_req_msg_t;

/*****************************************************************************
 * Test case operational rate definitions
 ****************************************************************************/
typedef struct test_rate_info_s {

    struct {
        uint32_t cnt; /* per sec */
        uint32_t size;
        uint32_t max_burst;
        uint32_t exp_rate;
        uint32_t exp_rate_last;
        uint32_t exp_rate_break;
    } tri_cfg;

    struct {
        uint32_t exp_rate;
        uint32_t rate;
    } tri_op;

} test_rate_info_t;

#define TEST_RATE_ACHIEVED(rinfo) \
    ((rinfo)->tri_op.rate >= (rinfo)->tri_op.exp_rate)

/*****************************************************************************
 * Test case operational state definitions
 ****************************************************************************/
typedef struct test_oper_state_s {

    tlkp_test_cb_list_t tos_to_init_cbs;    /* In Init, will move to to_open. */
    tlkp_test_cb_list_t tos_to_open_cbs;    /* In Closed, should open. */
    tlkp_test_cb_list_t tos_to_close_cbs;   /* In Estab, should close. */
    tlkp_test_cb_list_t tos_to_send_cbs;    /* In Established, need to send. */
    tlkp_test_cb_list_t tos_closed_cbs;     /* In Closed, willmove to to_open.*/

    /* Bit Flags */
    uint32_t            tos_configured          : 1;
    uint32_t            tos_running             : 1;
    uint32_t            tos_stopping            : 1;
    uint32_t            tos_open_in_progress    : 1; /* OPEN msg in progress */
    uint32_t            tos_open_rate_achieved  : 1; /* OPEN rate achieved */
    uint32_t            tos_close_in_progress   : 1; /* CLOSE msg in progress */
    uint32_t            tos_close_rate_achieved : 1; /* OPEN rate achieved */
    uint32_t            tos_send_in_progress    : 1; /* SEND msg in progress */
    uint32_t            tos_send_rate_achieved  : 1; /* SEND rate achieved */
    /* uint32_t         tos_unused              : 23; */

} test_oper_state_t;

/*****************************************************************************
 * Test run open/send/close callbacks
 ****************************************************************************/
typedef uint32_t (*test_case_runner_cb_t)(test_case_info_t *tc_info,
                                          test_case_init_msg_t *cfg,
                                          uint32_t to_execute_cnt);

typedef void (*test_case_close_cb_t)(l4_control_block_t *l4_cb);

/*****************************************************************************
 * Test case info
 ****************************************************************************/
typedef struct test_case_info_s {

    /* A copy of the message that configured this test. */
    test_case_init_msg_t       tci_cfg_msg;

    /* Operational part */
    test_rate_info_t           tci_rate_open_info;
    test_rate_info_t           tci_rate_close_info;
    test_rate_info_t           tci_rate_send_info;
    test_oper_state_t          tci_state;

    /* Callbacks for run_open/run_close/run_send/close */
    test_case_runner_cb_t      tci_run_open_cb;
    test_case_runner_cb_t      tci_run_close_cb;
    test_case_runner_cb_t      tci_run_send_cb;

    /* Callback for closing sessions to avoid checking the L4 connection
     * type.
     */
    test_case_close_cb_t       tci_close_cb;

    /* Stats */
    tpg_test_case_stats_t      tci_general_stats;
    tpg_test_case_rate_stats_t tci_rate_stats;
    tpg_test_case_app_stats_t  tci_app_stats;

    /* Timers that will be used for updating the rates of the test and
     * triggering the run.
     */
    struct rte_timer           tci_open_timer;  /* For TCP only. */
    struct rte_timer           tci_close_timer; /* For TCP only. */
    struct rte_timer           tci_send_timer;  /* Both TCP & UDP. */

} test_case_info_t;

/*****************************************************************************
 * Test timer arg definition
 ****************************************************************************/
typedef struct test_tmr_arg_s {

    uint32_t          tta_lcore_id;
    uint32_t          tta_eth_port;
    uint32_t          tta_test_case_id;
    test_case_info_t *tta_test_case_info;

} test_tmr_arg_t;

/*****************************************************************************
 * Externals globals for tpg_test.c
 ****************************************************************************/
/*
 * Array[port][tcid] holding the test case config and operational state for
 * testcases on a port.
 */
RTE_DECLARE_PER_LCORE(test_case_info_t *, test_case_info);

#define TEST_GET_INFO(port, tcid) \
    (RTE_PER_LCORE(test_case_info) + (port) * TPG_TEST_MAX_ENTRIES + (tcid))

/*****************************************************************************
 * Externals for tpg_test.c
 ****************************************************************************/
extern bool test_init(void);
extern void test_lcore_init(uint32_t lcore_id);

extern void test_resched_open(test_oper_state_t *ts, uint32_t eth_port,
                              uint32_t test_case_id);
extern void test_resched_close(test_oper_state_t *ts, uint32_t eth_port,
                               uint32_t test_case_id);
extern void test_resched_send(test_oper_state_t *ts, uint32_t eth_port,
                              uint32_t test_case_id);

#endif /* _H_TPG_TESTS_ */

