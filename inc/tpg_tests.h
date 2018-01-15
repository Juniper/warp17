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
    MSG_TEST_CASE_RATES_REQ,
    MSG_TYPE_DEF_END_MARKER(TESTS),

};

MSG_TYPE_MAX_CHECK(TESTS);

/*
 * Subset of messages used only for running test cases (OPEN/CLOSE/SEND).
 */
typedef enum {

    TRMT_OPEN  = MSG_TEST_CASE_RUN_OPEN,
    TRMT_CLOSE = MSG_TEST_CASE_RUN_CLOSE,
    TRMT_SEND  = MSG_TEST_CASE_RUN_SEND

} test_run_msg_type_t;

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

#define TEST_NOTIF_CB(notif, cb_arg)        \
    TEST_NOTIF((notif), (cb_arg),           \
               (cb_arg)->l4cb_test_case_id, \
               (cb_arg)->l4cb_interface)

#define TEST_NOTIF_TCB(notif, cb_arg) \
    TEST_NOTIF_CB((notif), &(cb_arg)->tcb_l4)

#define TEST_NOTIF_UCB(notif, cb_arg) \
    TEST_NOTIF_CB((notif), &(cb_arg)->ucb_l4)

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

/* WARNING: Should never be called with an empty list! */
#define TEST_CBQ_FIRST(list, type, member) \
    (container_of(TAILQ_FIRST((list)), type, member))

#define TEST_CBQ_ADD_TO_OPEN(ts, cb)                                \
    do {                                                            \
        TEST_CBQ_ADD(&(ts)->tos_to_open_cbs, (cb));                 \
        test_resched_runner(&(ts)->tos_rates, (cb)->l4cb_interface, \
                            (cb)->l4cb_test_case_id,                \
                            TRS_FLAGS_OPEN_IN_PROGRESS,             \
                            TRS_FLAGS_OPEN_RATE_REACHED,            \
                            RTE_PER_LCORE(test_open_msgpool),       \
                            TRMT_OPEN);                             \
    } while (0)

#define TEST_CBQ_ADD_TO_CLOSE(ts, cb)                                \
    do {                                                             \
        TEST_CBQ_ADD(&(ts)->tos_to_close_cbs, (cb));                 \
        test_resched_runner(&(ts)->tos_rates, (cb)->l4cb_interface,  \
                            (cb)->l4cb_test_case_id,                 \
                            TRS_FLAGS_CLOSE_IN_PROGRESS,             \
                            TRS_FLAGS_CLOSE_RATE_REACHED,            \
                            RTE_PER_LCORE(test_close_msgpool),       \
                            TRMT_CLOSE);                             \
    } while (0)

#define TEST_CBQ_ADD_TO_SEND(ts, cb)                                \
    do {                                                            \
        TEST_CBQ_ADD(&(ts)->tos_to_send_cbs, (cb));                 \
        test_resched_runner(&(ts)->tos_rates, (cb)->l4cb_interface, \
                            (cb)->l4cb_test_case_id,                \
                            TRS_FLAGS_SEND_IN_PROGRESS,             \
                            TRS_FLAGS_SEND_RATE_REACHED,            \
                            RTE_PER_LCORE(test_send_msgpool),       \
                            TRMT_SEND);                             \
    } while (0)

/*
 * WARNING: Only to be used at INIT time!
 */
#define TEST_CBQ_ADD_TO_OPEN_NO_RESCHED(ts, cb) \
    TEST_CBQ_ADD(&(ts)->tos_to_open_cbs, (cb))

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

    uint32_t tcim_eth_port;
    uint32_t tcim_test_case_id;

    tpg_test_case_type_t tcim_type;
    tpg_l4_proto_t       tcim_l4_type;
    tpg_app_proto_t      tcim_app_id;

    union {
        tpg_client_t tcim_client;
        tpg_server_t tcim_server;
    };

    sockopt_t               tcim_sockopt;
    tpg_test_case_latency_t tcim_latency;

    uint32_t tcim_rx_tstamp : 1;
    uint32_t tcim_tx_tstamp : 1;

    struct {
        rate_limit_cfg_t *open_rate;
        rate_limit_cfg_t *close_rate;
        rate_limit_cfg_t *send_rate;
    } tcim_transient; /* Not safe to use these fields *AFTER* the init message
                       * was processed.
                       */

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

    uint32_t tcsm_eth_port;
    uint32_t tcsm_test_case_id;

    volatile bool *tcsm_done; /* The sender blocks until 'done == true'. */

} __tpg_msg test_case_stop_msg_t;

typedef struct test_case_stats_req_msg_s {

    uint32_t                   tcsrm_eth_port;
    uint32_t                   tcsrm_test_case_id;
    tpg_test_case_stats_t     *tcsrm_test_case_stats;
    tpg_test_case_app_stats_t *tcsrm_test_case_app_stats;

} __tpg_msg test_case_stats_req_msg_t;

typedef struct test_case_rates_req_msg_s {

    uint32_t                    tcrrm_eth_port;
    uint32_t                    tcrrm_test_case_id;
    tpg_test_case_rate_stats_t *tcrrm_test_case_rate_stats;

} __tpg_msg test_case_rates_req_msg_t;

/*****************************************************************************
 * Test run open/send/mtu/close callbacks
 ****************************************************************************/
typedef int      (*test_case_client_open_cb_t)(l4_control_block_t *l4_cb);
typedef void     (*test_case_client_close_cb_t)(l4_control_block_t *l4_cb);
typedef uint32_t (*test_case_session_mtu_cb_t)(l4_control_block_t *l4_cb);
typedef int      (*test_case_session_send_cb_t)(l4_control_block_t *l4_cb,
                                                struct rte_mbuf *data_mbuf,
                                                uint32_t *data_sent);
typedef void     (*test_case_session_close_cb_t)(l4_control_block_t *l4_cb);

/*****************************************************************************
 * Test message (OPEN/CLOSE/SEND) pool definitions
 ****************************************************************************/
typedef MSG_TYPEDEF(test_case_run_msg_t) test_run_msgpool_t;

/*****************************************************************************
 * Test case operational state definitions
 ****************************************************************************/
#define TRS_FLAGS_OPEN_IN_PROGRESS   0x00000001
#define TRS_FLAGS_OPEN_RATE_REACHED  0x00000002
#define TRS_FLAGS_CLOSE_IN_PROGRESS  0x00000004
#define TRS_FLAGS_CLOSE_RATE_REACHED 0x00000008
#define TRS_FLAGS_SEND_IN_PROGRESS   0x00000010
#define TRS_FLAGS_SEND_RATE_REACHED  0x00000020

typedef struct test_rate_state_s {

    rate_limit_t trs_open;
    rate_limit_t trs_close __rte_cache_min_aligned;
    rate_limit_t trs_send  __rte_cache_min_aligned;

    uint32_t trs_flags; /* Actually a mask of TRS_FLAGS_* values. */

} __rte_cache_aligned test_rate_state_t;

/* Timers that will be used for updating the rates of the test and
 * triggering the run.
 */
typedef struct test_rate_timers_s {

    struct rte_timer trt_open_timer;
    struct rte_timer trt_close_timer __rte_cache_min_aligned;
    struct rte_timer trt_send_timer  __rte_cache_min_aligned;

} __rte_cache_aligned test_rate_timers_t;

typedef struct test_oper_state_s {

    /* Rate limiting state. */
    test_rate_state_t tos_rates;

    tlkp_test_cb_list_t tos_to_init_cbs;  /* In Init, will move to to_open. */
    tlkp_test_cb_list_t tos_to_open_cbs;  /* In Closed, should open. */
    tlkp_test_cb_list_t tos_to_close_cbs; /* In Estab, should close. */
    tlkp_test_cb_list_t tos_to_send_cbs;  /* In Established, need to send. */
    tlkp_test_cb_list_t tos_closed_cbs;   /* In Closed, willmove to to_open.*/

    /* Callbacks for run_open/run_close/run_send/close-sess */
    test_case_client_open_cb_t   tos_client_open_cb;
    test_case_client_close_cb_t  tos_client_close_cb;
    test_case_session_mtu_cb_t   tos_session_mtu_cb;
    test_case_session_send_cb_t  tos_session_send_cb;
    test_case_session_close_cb_t tos_session_close_cb;

} __rte_cache_aligned test_oper_state_t;

/*****************************************************************************
 * Test latency state
 ****************************************************************************/
typedef struct test_oper_latency_state_s {
    uint64_t tols_timestamps[TPG_TSTAMP_SAMPLES_MAX_BUFSIZE];
    uint32_t tols_length;
    uint32_t tols_actual_length;
    uint32_t tols_start_index;
} test_oper_latency_state_t;

/*****************************************************************************
 * Test case info
 ****************************************************************************/
typedef struct test_case_info_s {

    /* Operational state */
    test_oper_state_t tci_state;

    /* Stats */
    tpg_test_case_stats_t      *tci_general_stats;
    tpg_test_case_rate_stats_t *tci_rate_stats;
    tpg_test_case_app_stats_t  *tci_app_stats;

    /* A pointer to a copy of the message that configured this test. */
    test_case_init_msg_t *tci_cfg;

    test_oper_latency_state_t *tci_latency_state;

    /* Keep rate rte_timers lower in tc info structure as they are quite big
     * and we access them rarely.
     */
    test_rate_timers_t tci_rate_timers;

    /* Bit Flags */
    uint32_t    tci_configured : 1;
    uint32_t    tci_running    : 1;
    uint32_t    tci_stopping   : 1;
    /* uint32_t tci_unused     : 29; */

} test_case_info_t;

/*****************************************************************************
 * Test timer arg definition
 ****************************************************************************/
typedef struct test_tmr_arg_s {

    uint32_t tta_lcore_id;
    uint32_t tta_eth_port;
    uint32_t tta_test_case_id;

    test_rate_state_t *tta_rate_state;
    rate_limit_t      *tta_rate_limit;
    uint32_t           tta_rate_in_progress_flag;
    uint32_t           tta_rate_reached_flag;

    test_run_msgpool_t  *tta_run_msg_pool;
    test_run_msg_type_t  tta_run_msg_type;

} test_tmr_arg_t;

/*****************************************************************************
 * Externals globals for tpg_test.c
 ****************************************************************************/
/*
 * Array[port][tcid] holding the test case operational state for
 * testcases on a port.
 */
RTE_DECLARE_PER_LCORE(test_case_info_t *, test_case_info);

#define TEST_GET_INFO(port, tcid) \
    (RTE_PER_LCORE(test_case_info) + (port) * TPG_TEST_MAX_ENTRIES + (tcid))

/*
 * Array[port][tcid] holding the test case config for
 * testcases on a port.
 */
RTE_DECLARE_PER_LCORE(test_case_init_msg_t *, test_case_cfg);

#define TEST_GET_CFG(port, tcid) \
    (RTE_PER_LCORE(test_case_cfg) + (port) * TPG_TEST_MAX_ENTRIES + (tcid))

/*
 * Arrays[port][tcid] holding the preallocated messages to drive the test
 * engine (for OPEN/SEND/CLOSE).
 */
RTE_DECLARE_PER_LCORE(test_run_msgpool_t *, test_open_msgpool);
RTE_DECLARE_PER_LCORE(test_run_msgpool_t *, test_close_msgpool);
RTE_DECLARE_PER_LCORE(test_run_msgpool_t *, test_send_msgpool);

/*****************************************************************************
 * Externals for tpg_test.c
 ****************************************************************************/
extern bool test_init(void);
extern void test_lcore_init(uint32_t lcore_id);

extern int test_case_run_msg(uint32_t lcore_id,
                             uint32_t eth_port, uint32_t test_case_id,
                             test_run_msgpool_t *msgpool,
                             test_run_msg_type_t msg_type);

extern void test_update_latency(l4_control_block_t *l4cb,
                                uint64_t pkt_orig_tstamp, uint64_t pcb_tstamp);

/*****************************************************************************
 * Static inlines
 ****************************************************************************/

/*****************************************************************************
 * test_resched_runner()
 *      NOTES: should be called whenever a session is available for a new
 *             OPEN/CLOSE/SEND operation.
 ****************************************************************************/
static inline void
test_resched_runner(test_rate_state_t *rate_state,
                    uint32_t eth_port, uint32_t test_case_id,
                    uint32_t rate_in_progress_flag, uint32_t rate_reached_flag,
                    test_run_msgpool_t *msgpool,
                    test_run_msg_type_t msg_type)
{
    /* If there's already a "message" in progress for this type of operation
     * or if we already reached the desired rate then stop and let the
     * rate limiting engine do it's job when the next interval starts.
     */
    if (rate_state->trs_flags & (rate_in_progress_flag | rate_reached_flag))
        return;

    if (test_case_run_msg(rte_lcore_id(), eth_port, test_case_id,
                          msgpool, msg_type) == 0)
        rate_state->trs_flags |= rate_in_progress_flag;
}

#endif /* _H_TPG_TESTS_ */

