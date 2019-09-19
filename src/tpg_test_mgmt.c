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
 *     tpg_test_mgmt.c
 *
 * Description:
 *     Test management and API implementation.
 *
 * Author:
 *     Dumitru Ceara, Eelco Chaudron
 *
 * Initial Created:
 *     08/12/2015
 *
 * Notes:
 *
 */

/*****************************************************************************
 * Include files
 ****************************************************************************/
#include <unistd.h>


#include "tcp_generator.h"

/*****************************************************************************
 * Globals
 ****************************************************************************/
/* Array of port: will store the configuration per port. */
static test_env_t *test_env;

/* Array of port: will store the runtime stats per port + testcase. */
tpg_gen_stats_t *test_runtime_gen_stats;

#define TEST_CASE_STATS_GET(port, tcid) \
    (test_runtime_gen_stats + (port) * TPG_TEST_MAX_ENTRIES + tcid)

/* Array of port: will store the runtime rate stats per port + testcase. */
tpg_rate_stats_t *test_runtime_rate_stats;

#define TEST_CASE_RATE_STATS_GET(port, tcid) \
    (test_runtime_rate_stats + (port) * TPG_TEST_MAX_ENTRIES + tcid)

/* Array of port: will store the runtime rate stats per port + testcase. */
tpg_app_stats_t *test_runtime_app_stats;

#define TEST_CASE_APP_STATS_GET(port, tcid)  \
    (test_runtime_app_stats + (port) * TPG_TEST_MAX_ENTRIES + tcid)

/*****************************************************************************
 * Forward declarations.
 ****************************************************************************/
static void test_update_rates(tpg_test_case_t *test_case);
static void test_start_test_case(uint32_t eth_port, test_env_t *tcfg);
static void test_stop_test_case(uint32_t eth_port, tpg_test_case_t *entry,
                                test_env_oper_state_t *state,
                                tpg_test_case_state_t new_tc_state);
static void test_aggregate_latencies(tpg_latency_stats_t *dest,
                                     tpg_latency_stats_t *source);

/*****************************************************************************
 * test_update_status()
 ****************************************************************************/
static void test_update_status(tpg_test_case_t *test_case)
{
    tpg_gen_stats_t  gen_stats;
    tpg_gen_stats_t *ptotal_gen_stats;
    tpg_app_stats_t  app_stats;
    tpg_app_stats_t *ptotal_app_stats;
    int              error = 0;
    uint32_t         core;
    bool             is_server;
    uint32_t         srv_up = 0;
    MSG_LOCAL_DEFINE(test_case_stats_req_msg_t, smsg);

    is_server = (test_case->tc_type == TEST_CASE_TYPE__SERVER);

    ptotal_gen_stats = TEST_CASE_STATS_GET(test_case->tc_eth_port,
                                           test_case->tc_id);
    ptotal_app_stats = TEST_CASE_APP_STATS_GET(test_case->tc_eth_port,
                                               test_case->tc_id);

    bzero(&ptotal_gen_stats->gs_latency_stats.gls_sample_stats,
          sizeof(ptotal_gen_stats->gs_latency_stats.gls_sample_stats));
    ptotal_gen_stats->gs_latency_stats.gls_sample_stats.ls_min_latency = UINT32_MAX;

    ptotal_gen_stats->gs_latency_stats.gls_stats.ls_instant_jitter = 0;

    /* Initialize the start time. */
    ptotal_gen_stats->gs_start_time = UINT64_MAX;
    FOREACH_CORE_IN_PORT_START(core, test_case->tc_eth_port) {
        msg_t                     *msgp;
        test_case_stats_req_msg_t *stats_msg;
        tpg_app_proto_t            app_id;

        /* Skip non-packet cores */
        if (!cfg_is_pkt_core(core))
            continue;

        app_id = test_case->tc_app.app_proto;

        bzero(&gen_stats, sizeof(gen_stats));

        /* Applications might need to initialize the stats storage in
         * different ways (e.g., IMIX). Ask the app to initialize some
         * global stats storage for our stats request.
         */
        APP_CALL(stats_init_req, app_id)(&test_case->tc_app, &app_stats);

        msgp = MSG_LOCAL(smsg);
        msg_init(msgp, MSG_TEST_CASE_STATS_REQ, core, 0);

        stats_msg = MSG_INNER(test_case_stats_req_msg_t, msgp);
        stats_msg->tcsrm_eth_port = test_case->tc_eth_port;
        stats_msg->tcsrm_test_case_id = test_case->tc_id;
        stats_msg->tcsrm_test_case_stats = &gen_stats;
        stats_msg->tcsrm_test_case_app_stats = &app_stats;

        /* BLOCK waiting for msg to be processed */
        error = msg_send(msgp, 0);
        if (error) {
            TPG_ERROR_ABORT("ERROR: Failed to send stats req msg: %s(%d)!\n",
                            rte_strerror(-error), -error);
        }

        /* Aggregate all the latency stats */
        ptotal_gen_stats->gs_latency_stats.gls_invalid_lat +=
            gen_stats.gs_latency_stats.gls_invalid_lat;
        test_aggregate_latencies(&ptotal_gen_stats->gs_latency_stats.gls_stats,
                                 &gen_stats.gs_latency_stats.gls_stats);

        test_aggregate_latencies(&ptotal_gen_stats->gs_latency_stats.gls_sample_stats,
                                 &gen_stats.gs_latency_stats.gls_sample_stats);

        if (is_server) {
            /* LISTEN TCBs are duplicated on all cores. */
            if (gen_stats.gs_up > srv_up)
                srv_up = gen_stats.gs_up;
        } else {
            ptotal_gen_stats->gs_up += gen_stats.gs_up;
        }

        ptotal_gen_stats->gs_estab       += gen_stats.gs_estab;
        ptotal_gen_stats->gs_down        += gen_stats.gs_down;
        ptotal_gen_stats->gs_failed      += gen_stats.gs_failed;
        ptotal_gen_stats->gs_data_failed += gen_stats.gs_data_failed;
        ptotal_gen_stats->gs_data_null   += gen_stats.gs_data_null;


        APP_CALL(stats_add, app_id)(ptotal_app_stats, &app_stats);

        if (gen_stats.gs_start_time < ptotal_gen_stats->gs_start_time)
            ptotal_gen_stats->gs_start_time = gen_stats.gs_start_time;

        if (gen_stats.gs_end_time > ptotal_gen_stats->gs_end_time)
            ptotal_gen_stats->gs_end_time = gen_stats.gs_end_time;

    } FOREACH_CORE_IN_PORT_END()

    if (is_server)
        ptotal_gen_stats->gs_up += srv_up;
}

/*****************************************************************************
 * test_update_rates()
 ****************************************************************************/
static void test_update_rates(tpg_test_case_t *test_case)
{
    tpg_rate_stats_t  rate_stats;
    tpg_rate_stats_t *prate_stats;
    int               error = 0;
    uint32_t          core;
    MSG_LOCAL_DEFINE(test_case_rates_req_msg_t, smsg);

    prate_stats = TEST_CASE_RATE_STATS_GET(test_case->tc_eth_port,
                                           test_case->tc_id);

    /* Initialize rates. */
    bzero(prate_stats, sizeof(*prate_stats));

    FOREACH_CORE_IN_PORT_START(core, test_case->tc_eth_port) {
        msg_t                     *msgp;
        test_case_rates_req_msg_t *stats_msg;
        uint64_t                   duration;

        /* Skip non-packet cores */
        if (!cfg_is_pkt_core(core))
            continue;

        bzero(&rate_stats, sizeof(rate_stats));

        msgp = MSG_LOCAL(smsg);
        msg_init(msgp, MSG_TEST_CASE_RATES_REQ, core, 0);

        stats_msg = MSG_INNER(test_case_rates_req_msg_t, msgp);
        stats_msg->tcrrm_eth_port = test_case->tc_eth_port;
        stats_msg->tcrrm_test_case_id = test_case->tc_id;
        stats_msg->tcrrm_test_case_rate_stats = &rate_stats;

        /* BLOCK waiting for msg to be processed */
        error = msg_send(msgp, 0);
        if (error) {
            TPG_ERROR_ABORT("ERROR: Failed to send rates req msg: %s(%d)!\n",
                            rte_strerror(-error), -error);
        }

        prate_stats->rs_start_time = rate_stats.rs_start_time / cycles_per_us;
        prate_stats->rs_end_time = rate_stats.rs_end_time / cycles_per_us;
        duration = TPG_TIME_DIFF(prate_stats->rs_end_time,
                                 prate_stats->rs_start_time);


        /* Compute the averages and aggregate. */
        prate_stats->rs_estab_per_s +=
            rate_stats.rs_estab_per_s * (uint64_t)TPG_SEC_TO_USEC /
            duration;
        prate_stats->rs_closed_per_s +=
            rate_stats.rs_closed_per_s * (uint64_t)TPG_SEC_TO_USEC /
            duration;
        prate_stats->rs_data_per_s +=
            rate_stats.rs_data_per_s * (uint64_t)TPG_SEC_TO_USEC /
            duration;
    } FOREACH_CORE_IN_PORT_END()
}

/*****************************************************************************
 * test_update_state_counter()
 ****************************************************************************/
void test_update_state_counter(const tpg_test_case_t *test_case,
                                test_state_counter_t *state_counter)
{
    test_state_counter_t tci_state;
    int               error = 0;
    uint32_t          core;
    MSG_LOCAL_DEFINE(test_case_rates_req_msg_t, smsg);

    bzero(state_counter, sizeof(test_state_counter_t));

    FOREACH_CORE_IN_PORT_START(core, test_case->tc_eth_port) {
        msg_t                      *msgp;
        test_case_states_req_msg_t *stats_msg;
        uint32_t                   state;

        /* Skip non-packet cores */
        if (!cfg_is_pkt_core(core))
            continue;

        bzero(&tci_state, sizeof(tci_state));

        msgp = MSG_LOCAL(smsg);
        msg_init(msgp, MSG_TEST_CASE_STATES_REQ, core, 0);

        stats_msg = MSG_INNER(test_case_states_req_msg_t, msgp);
        stats_msg->tcsrm_eth_port = test_case->tc_eth_port;
        stats_msg->tcsrm_test_case_id = test_case->tc_id;
        stats_msg->tcsrm_test_state_counter = &tci_state;

        /* BLOCK waiting for msg to be processed */
        error = msg_send(msgp, 0);
        if (error) {
            TPG_ERROR_ABORT("ERROR: Failed to send state req msg: %s(%d)!\n",
                            rte_strerror(-error), -error);
        }

        state_counter->tos_to_init_cbs += tci_state.tos_to_init_cbs;
        state_counter->tos_to_open_cbs += tci_state.tos_to_open_cbs;
        state_counter->tos_to_close_cbs += tci_state.tos_to_close_cbs;
        state_counter->tos_to_send_cbs += tci_state.tos_to_send_cbs;
        state_counter->tos_closed_cbs += tci_state.tos_closed_cbs;

        for (state = 0; state < TSTS_MAX_STATE; ++state) {
            state_counter->test_states_from_test[state] +=
                    tci_state.test_states_from_test[state];
            state_counter->test_states_from_tcp[state] += tci_state
                .test_states_from_tcp[state];
            state_counter->test_states_from_udp[state] += tci_state
                .test_states_from_udp[state];
        }

        for (state = 0; state < TS_MAX_STATE; ++state) {
            state_counter->tcp_states_from_test[state] +=
                    tci_state.tcp_states_from_test[state];
            state_counter->tcp_states_from_tcp[state] += tci_state
                .tcp_states_from_tcp[state];
            if (state == TS_SYN_RECV &&
                (tci_state.tcp_states_from_test[state] != 0))
                RTE_LOG(INFO, USER1, "core %d has %d TS_SYN_RECV\n", core,
                        tci_state.tcp_states_from_test[state]);
        }

        for (state = 0; state < US_MAX_STATE; ++state)
            state_counter->udp_states_from_udp[state] += tci_state
                .udp_states_from_udp[state];
    } FOREACH_CORE_IN_PORT_END()
}

/*****************************************************************************
 * test_max_pkt_size()
 *      Notes: returns the maximum allowed packet size for a given test case
 *             in a single send operation (useful for example when estimating
 *             send rates for various application types).
 ****************************************************************************/
static uint32_t test_max_pkt_size(const tpg_test_case_t *entry,
                                  const sockopt_t *sockopt)
{
    switch (entry->tc_type) {
    case TEST_CASE_TYPE__CLIENT:

        switch (entry->tc_client.cl_l4.l4c_proto) {
        case L4_PROTO__TCP:
            return TCP_GLOBAL_AVAIL_SEND(entry->tc_eth_port, sockopt);
        case L4_PROTO__UDP:
            return UDP_GLOBAL_MTU(entry->tc_eth_port, sockopt);
        default:
            return 0;
        }
        break;

    case TEST_CASE_TYPE__SERVER:

        switch (entry->tc_server.srv_l4.l4s_proto) {
        case L4_PROTO__TCP:
            return TCP_GLOBAL_AVAIL_SEND(entry->tc_eth_port, sockopt);
        case L4_PROTO__UDP:
            return UDP_GLOBAL_MTU(entry->tc_eth_port, sockopt);
        default:
            return 0;
        }
        break;

    default:
        return 0;
    }
}

/*****************************************************************************
 * test_init_msg_client_rates()
 ****************************************************************************/
static void test_init_msg_client_rates(const tpg_test_case_t *entry,
                                       const sockopt_t *sockopt,
                                       test_case_init_msg_t *msg)
{
    tpg_rate_t          send_rate = TPG_RATE_INF();
    const tpg_client_t *client_cfg = &entry->tc_client;

    rate_limit_cfg_init(&client_cfg->cl_rates.rc_open_rate,
                        msg->tcim_transient.open_rate);
    rate_limit_cfg_init(&client_cfg->cl_rates.rc_close_rate,
                        msg->tcim_transient.close_rate);

    /* If rate limiting is enabled (non-infinite) we translate the user config
     * (rate limiting for application sends) to rate limiting based on number
     * of packets sent.
     */
    if (!TPG_RATE_IS_INF(&client_cfg->cl_rates.rc_send_rate)) {
        uint32_t        target_send_rate;
        uint32_t        pkts_per_send;
        uint32_t        mtu = test_max_pkt_size(entry, sockopt);
        tpg_app_proto_t app_id = entry->tc_app.app_proto;
        uint32_t        min_rate_precision = GCFG_RATE_MIN_RATE_PRECISION;

        pkts_per_send = APP_CALL(pkts_per_send, app_id)(entry, &entry->tc_app,
                                                        mtu);
        target_send_rate =
            pkts_per_send * TPG_RATE_VAL(&client_cfg->cl_rates.rc_send_rate);

        if (target_send_rate > min_rate_precision) {
            target_send_rate =
                target_send_rate / min_rate_precision * min_rate_precision;
        }

        send_rate = TPG_RATE(target_send_rate);
    }

    rate_limit_cfg_init(&send_rate, msg->tcim_transient.send_rate);
}

/*****************************************************************************
 * test_init_msg_server_rates()
 ****************************************************************************/
static void
test_init_msg_server_rates(const tpg_test_case_t *entry __rte_unused,
                           const sockopt_t *sockopt __rte_unused,
                           test_case_init_msg_t *msg)
{
    tpg_rate_t          open_rate = TPG_RATE(0);
    tpg_rate_t          close_rate = TPG_RATE(0);
    tpg_rate_t          send_rate = TPG_RATE_INF();

    /* No rate limiting on the server side for now. */

    rate_limit_cfg_init(&open_rate, msg->tcim_transient.open_rate);
    rate_limit_cfg_init(&close_rate, msg->tcim_transient.close_rate);
    rate_limit_cfg_init(&send_rate, msg->tcim_transient.send_rate);
}

/*****************************************************************************
 * test_init_msg()
 ****************************************************************************/
static void test_init_msg(const tpg_test_case_t *entry,
                          const sockopt_t *sockopt,
                          test_case_init_msg_t *msg)
{
    msg->tcim_test_case = *entry;

    switch (entry->tc_type) {
    case TEST_CASE_TYPE__CLIENT:
        msg->tcim_l4_type = entry->tc_client.cl_l4.l4c_proto;

        test_init_msg_client_rates(entry, sockopt, msg);
        break;
    case TEST_CASE_TYPE__SERVER:
        msg->tcim_l4_type = entry->tc_server.srv_l4.l4s_proto;

        test_init_msg_server_rates(entry, sockopt, msg);
        break;
    default:
        assert(false);
        break;
    }

    /* Struct copy. */
    msg->tcim_sockopt = *sockopt;

    /* Determine if RX/TX timestamping should be enabled. */
    msg->tcim_rx_tstamp = test_mgmt_rx_tstamp_enabled(entry);
    msg->tcim_tx_tstamp = test_mgmt_tx_tstamp_enabled(entry);
}

/*****************************************************************************
 * test_init_test_case()
 ****************************************************************************/
static void test_init_test_case(tpg_test_case_t *entry,
                                sockopt_t *sockopt)
{
    uint32_t              core;
    int                   error;
    msg_t                *msgp;
    test_case_init_msg_t *init_msg;

    /*
     * Allocate the configs in .bss because they can get quite big.
     * This works fine as long as the MSG_TEST_CASE_INIT is sent in a blocking
     * way and the sender waits for the destination to process it.
     */
    static rate_limit_cfg_t open_rate_cfg;
    static rate_limit_cfg_t close_rate_cfg;
    static rate_limit_cfg_t send_rate_cfg;
    static MSG_LOCAL_DEFINE(test_case_init_msg_t, imsg);

    msgp = MSG_LOCAL(imsg);

    init_msg = MSG_INNER(test_case_init_msg_t, msgp);
    init_msg->tcim_transient.open_rate = &open_rate_cfg;
    init_msg->tcim_transient.close_rate = &close_rate_cfg;
    init_msg->tcim_transient.send_rate = &send_rate_cfg;

    test_init_msg(entry, sockopt, init_msg);

    FOREACH_CORE_IN_PORT_START(core, entry->tc_eth_port) {
        /* Skip non-packet cores */
        if (!cfg_is_pkt_core(core))
            continue;

        msg_init(msgp, MSG_TEST_CASE_INIT, core, 0);

        /* Send the message and wait for the destination to process it! */
        error = msg_send(msgp, 0);
        if (error) {
            /* Should uninitialize what we did in the beginning. */
            TPG_ERROR_ABORT("ERROR: Failed to send INIT msg: %s(%d)!\n",
                            rte_strerror(-error), -error);
        }
    } FOREACH_CORE_IN_PORT_END()
}

/*****************************************************************************
 * test_check_run_time_tc_status()
 ****************************************************************************/
static bool test_check_run_time_tc_status(tpg_test_case_t *test_case,
                                          test_env_oper_state_t *state,
                                          uint64_t now)
{
    state->teos_result.tc_run_time_s = TPG_TIME_DIFF(now, state->teos_start_time) /
                                       rte_get_timer_hz();
    if (state->teos_result.tc_run_time_s >=
                test_case->tc_criteria.tc_run_time_s) {
        state->teos_stop_time = now;
        return true;
    }

    return false;
}

/*****************************************************************************
 * test_check_srv_up_tc_status()
 ****************************************************************************/
static bool test_check_srv_up_tc_status(tpg_test_case_t *test_case,
                                        test_env_oper_state_t *state,
                                        uint64_t now)
{
    tpg_gen_stats_t *gen_stats;

    gen_stats = TEST_CASE_STATS_GET(test_case->tc_eth_port,
                                    test_case->tc_id);

    state->teos_result.tc_srv_up = gen_stats->gs_up;

    if (state->teos_result.tc_srv_up >= test_case->tc_criteria.tc_srv_up) {
        state->teos_stop_time = now;
        return true;
    }

    return false;
}

/*****************************************************************************
 * test_check_cl_up_tc_status()
 ****************************************************************************/
static bool test_check_cl_up_tc_status(tpg_test_case_t *test_case,
                                       test_env_oper_state_t *state,
                                       uint64_t now)
{
    tpg_gen_stats_t *gen_stats;

    gen_stats = TEST_CASE_STATS_GET(test_case->tc_eth_port,
                                    test_case->tc_id);

    state->teos_result.tc_cl_up = gen_stats->gs_up;

    if (state->teos_result.tc_cl_up >= test_case->tc_criteria.tc_cl_up) {
        state->teos_stop_time = now;
        return true;
    }
    return true;
}

/*****************************************************************************
 * test_check_cl_estab_tc_status()
 ****************************************************************************/
static bool test_check_cl_estab_tc_status(tpg_test_case_t *test_case,
                                          test_env_oper_state_t *state)
{
    tpg_gen_stats_t *gen_stats;

    gen_stats = TEST_CASE_STATS_GET(test_case->tc_eth_port,
                                    test_case->tc_id);

    state->teos_result.tc_cl_estab = gen_stats->gs_estab;

    if (state->teos_result.tc_cl_estab >= test_case->tc_criteria.tc_cl_estab) {

        if (state->teos_result.tc_cl_estab > test_case->tc_criteria.tc_cl_estab) {
            RTE_LOG(ERR, USER1,
                    "[%s()] Port %"PRIu32", Test Case: %"PRIu32" "
                    "Unexpected ESTAB count: "
                    "Real %"PRIu32" Expected %"PRIu32"\n",
                    __func__,
                    test_case->tc_eth_port,
                    test_case->tc_id,
                    state->teos_result.tc_cl_estab,
                    test_case->tc_criteria.tc_cl_estab);
        }

        /*
         * Update the start time if we have an updated version from the test
         * threads.
         */
        if (gen_stats->gs_start_time)
            state->teos_start_time = gen_stats->gs_start_time;

        state->teos_stop_time = gen_stats->gs_end_time;

        assert(state->teos_stop_time > state->teos_start_time);
        return true;
    }

    return false;
}

/*****************************************************************************
 * test_check_data_tc_status()
 ****************************************************************************/
static bool test_check_data_tc_status(tpg_test_case_t *test_case,
                                      test_env_oper_state_t *state)
{
    /* TODO */
    (void)test_case;
    (void)state;
    assert(false);
    return false;
}

/*****************************************************************************
 * test_check_tc_status()
 *  Notes: returns whether the test finished running and populates 'passed'
 *         with the test execution result.
 ****************************************************************************/
static bool test_check_tc_status(tpg_test_case_t *test_case,
                                 test_env_oper_state_t *state,
                                 bool *passed)
{
    uint64_t now;

    now = rte_get_timer_cycles();
    test_update_status(test_case);

    switch (test_case->tc_criteria.tc_crit_type) {
    case TEST_CRIT_TYPE__RUN_TIME:
        *passed = test_check_run_time_tc_status(test_case, state, now);
        break;
    case TEST_CRIT_TYPE__SRV_UP:
        *passed = test_check_srv_up_tc_status(test_case, state, now);
        break;
    case TEST_CRIT_TYPE__CL_UP:
        *passed = test_check_cl_up_tc_status(test_case, state, now);
        break;
    case TEST_CRIT_TYPE__CL_ESTAB:
        *passed = test_check_cl_estab_tc_status(test_case, state);
        break;
    case TEST_CRIT_TYPE__DATAMB_SENT:
        *passed = test_check_data_tc_status(test_case, state);
        break;
    default:
        assert(false);
    }

    /*
     * If not yet passed we need to check if we consider the test failed. If
     * so then update its stop_time. A test is considered failed if we reached
     * the max runtime and didn't meet the PASS criteria.
     */
    if (!(*passed) &&
            test_case->tc_criteria.tc_crit_type != TEST_CRIT_TYPE__RUN_TIME) {
        if (((now - state->teos_start_time) / cycles_per_us) >
                (cfg_get_config()->gcfg_test_max_tc_runtime)) {
            state->teos_stop_time = now;
            return true;
        }
    }

    return *passed;
}

/*****************************************************************************
 * test_aggregate_latencies()
 ****************************************************************************/
void test_aggregate_latencies(tpg_latency_stats_t *dest,
                              tpg_latency_stats_t *source)
{
    dest->ls_max_average_exceeded += source->ls_max_average_exceeded;
    dest->ls_max_exceeded += source->ls_max_exceeded;

    dest->ls_max_latency =
        (dest->ls_max_latency >= source->ls_max_latency ?
         dest->ls_max_latency : source->ls_max_latency);

    dest->ls_min_latency =
        (dest->ls_min_latency <= source->ls_min_latency ?
         dest->ls_min_latency : source->ls_min_latency);

    dest->ls_samples_count += source->ls_samples_count;
    dest->ls_sum_latency += source->ls_sum_latency;
    dest->ls_sum_jitter += source->ls_sum_jitter;

    dest->ls_instant_jitter =
        (dest->ls_instant_jitter >= source->ls_instant_jitter ?
         dest->ls_instant_jitter : source->ls_instant_jitter);

    dest->ls_samples_count += source->ls_samples_count;
}

/*****************************************************************************
 * test_entry_tmr_cb()
 ****************************************************************************/
static void test_entry_tmr_cb(struct rte_timer *tmr __rte_unused, void *arg)
{
    bool                   done;
    test_env_tmr_arg_t    *tmr_arg  = arg;
    bool                   passed   = false;
    test_env_t            *tenv     = tmr_arg->teta_test_env;
    uint32_t               eth_port = tmr_arg->teta_eth_port;
    uint32_t               tcid     = tmr_arg->teta_test_case_id;
    tpg_test_case_t       *entry    = &tenv->te_test_cases[tcid].cfg;
    test_env_oper_state_t *state    = &tenv->te_test_cases[tcid].state;

    done = test_check_tc_status(entry, state, &passed);

    /* Check the status of our own test. */
    /* Server testcases can be "PASSED" but still running! */
    if (passed && state->teos_test_case_state != TEST_CASE_STATE__PASSED) {
        assert(done);

        state->teos_test_case_state = TEST_CASE_STATE__PASSED;
        RTE_LOG(INFO, USER1,
                "Port %"PRIu32", Test Case %"PRIu32" \"PASSED\"!\n",
                eth_port, tcid);
    }

    if (done) {
        if (!passed) {
            state->teos_test_case_state = TEST_CASE_STATE__FAILED;
            RTE_LOG(INFO, USER1,
                    "Port %"PRIu32", Test Case %"PRIu32" \"FAILED\"!\n",
                    eth_port, tcid);
        }

        /* Servers UP doesn't mean we're done with the server test case. Server
         * test cases must be explicitly stopped!
         */
        if (entry->tc_type != TEST_CASE_TYPE__SERVER) {
            rte_timer_stop(tmr);
            test_stop_test_case(eth_port, entry, state,
                                state->teos_test_case_state);
        }
    }

    if (done || tenv->te_test_cases[tcid].cfg.tc_async) {
        /* Check if we need to start another test entry. */
        if (tenv->te_test_case_to_start && tcid == tenv->te_test_case_next - 1)
            test_start_test_case(eth_port, tenv);
    }

    if (state->teos_update_rates == true) {
        test_update_rates(&tenv->te_test_cases[tcid].cfg);
        state->teos_update_rates = false;
    } else {
        state->teos_update_rates = true;
    }

    /* Check if all test cases have stopped running.
     * If so then mark the tests as not running on this port.
     */
    if (done && !tenv->te_test_case_to_start) {
        TEST_CASE_FOREACH_START(tenv, tcid, entry, state) {

            if (TEST_CASE_RUNNING(entry, state->teos_test_case_state))
                return;

        } TEST_CASE_FOREACH_END()

        tenv->te_test_running = false;
    }
}

/*****************************************************************************
 * test_start_test_case()
 ****************************************************************************/
static void test_start_test_case(uint32_t eth_port, test_env_t *tenv)
{
    uint32_t               core;
    uint32_t               tcid;
    uint32_t               tcid_next;
    tpg_test_case_t       *entry;
    test_env_oper_state_t *state;

    tcid  = tenv->te_test_case_next;
    entry = &tenv->te_test_cases[tcid].cfg;

    FOREACH_CORE_IN_PORT_START(core, eth_port) {
        msg_t                 *msgp;
        test_case_start_msg_t *sm;
        int                    error;

        /* Skip non-packet cores */
        if (!cfg_is_pkt_core(core))
            continue;

        msgp = msg_alloc(MSG_TEST_CASE_START, sizeof(*sm), core);
        if (!msgp) {
            TPG_ERROR_ABORT("ERROR: %s!\n", "Failed to alloc START message!");
            return;
        }

        sm = MSG_INNER(test_case_start_msg_t, msgp);
        sm->tcsm_eth_port = entry->tc_eth_port;
        sm->tcsm_test_case_id = entry->tc_id;

        /* Send the message and forget about it!! */
        error = msg_send(msgp, MSG_SND_FLAG_NOWAIT);
        if (error) {
            /* Should uninitialize what we did in the beginning. */
            TPG_ERROR_ABORT("ERROR: Failed to send START msg: %s(%d)!\n",
                            rte_strerror(-error), -error);
            msg_free(msgp);
        }
    } FOREACH_CORE_IN_PORT_END()

    RTE_LOG(INFO, USER1, "Test Case %"PRIu32" started on port %"PRIu32"\n",
            tcid,
            eth_port);

    /*
     * The start_time is just a reference for now. It will get updated once the
     * packet cores process the START message.
     */
    state = &tenv->te_test_cases[tcid].state;
    state->teos_start_time = rte_get_timer_cycles();
    state->teos_stop_time = 0;

    state->teos_test_case_state = TEST_CASE_STATE__RUNNING;
    rte_timer_reset(&state->teos_timer,
                    GCFG_TEST_MGMT_TMR_TO * cycles_per_us,
                    PERIODICAL,
                    rte_lcore_id(),
                    test_entry_tmr_cb,
                    &state->teos_timer_arg);

    /* Mark if we need to start more tests later. */
    TEST_CASE_FOREACH_START(tenv, tcid_next, entry, state) {
        if (tcid_next > tcid) {
            tenv->te_test_case_next = tcid_next;
            return;
        }
    } TEST_CASE_FOREACH_END()

    tenv->te_test_case_to_start = false;
}

/*****************************************************************************
 * test_stop_test_case()
 ****************************************************************************/
static void test_stop_test_case(uint32_t eth_port, tpg_test_case_t *entry,
                                test_env_oper_state_t *state,
                                tpg_test_case_state_t new_tc_state)
{
    tpg_rate_stats_t *rate_stats;
    uint32_t          core;

    /* WARNING: we're actually trying to stop PASSED testcases too.
     * This has to be done as server testcases keep running until explicitly
     * stopped even if declared "PASSED".
     */
    if (state->teos_test_case_state == TEST_CASE_STATE__IDLE ||
        state->teos_test_case_state == TEST_CASE_STATE__STOPPED)
        return;

    FOREACH_CORE_IN_PORT_START(core, eth_port) {
        msg_t                *msgp;
        test_case_stop_msg_t *sm;
        int                   error;
        volatile bool         tc_done = false;

        /* Skip non-packet cores */
        if (!cfg_is_pkt_core(core))
            continue;

        msgp = msg_alloc(MSG_TEST_CASE_STOP, sizeof(*sm), core);
        if (!msgp) {
            TPG_ERROR_ABORT("ERROR: %s!\n", "Failed to alloc STOP message!");
            return;
        }

        sm = MSG_INNER(test_case_stop_msg_t, msgp);
        sm->tcsm_eth_port = entry->tc_eth_port;
        sm->tcsm_test_case_id = entry->tc_id;
        sm->tcsm_done = &tc_done;

        /* Send the message async!! */
        /* The message can't be sync because it can get reposted by the
         * receiver.
         */
        error = msg_send(msgp, MSG_SND_FLAG_NOWAIT);
        if (error) {
            /* Should uninitialize what we did in the beginning. */
            TPG_ERROR_ABORT("ERROR: Failed to send STOP msg: %s(%d)!\n",
                            rte_strerror(-error), -error);
            msg_free(msgp);
        }

        /* Spin until the message completely processed. It's not nice but it
         * does the job.
         */
        while (!tc_done)
            rte_mb();

    } FOREACH_CORE_IN_PORT_END()

    state->teos_test_case_state = new_tc_state;
    if (!state->teos_stop_time)
        state->teos_stop_time = rte_get_timer_cycles();

    /* Clear the rate stats as those are not interesting anymore. */
    rate_stats = TEST_CASE_RATE_STATS_GET(eth_port, entry->tc_id);
    bzero(rate_stats, sizeof(*rate_stats));
}

/*****************************************************************************
 * test_start_cb()
 ****************************************************************************/
static int test_start_cb(uint16_t msgid, uint16_t lcore __rte_unused, void *msg)
{
    uint32_t               i;
    test_start_msg_t      *start_msg;
    test_env_t            *tenv;
    test_env_oper_state_t *state;
    tpg_port_cfg_t        *pcfg;
    tpg_test_case_t       *entry;

    if (MSG_INVALID(msgid, msg, MSG_TEST_MGMT_START_TEST))
        return -EINVAL;

    start_msg = msg;
    tenv = &test_env[start_msg->tssm_eth_port];

    if (tenv->te_test_running)
        return 0;

    tenv->te_test_running = true;
    pcfg = &tenv->te_port_cfg;

    if (tenv->te_test_cases_count == 0)
        return -EINVAL;

    /* Initialize l3 interfaces and gw. */
    for (i = 0; i < pcfg->pc_l3_intfs_count; i++) {
        route_v4_intf_add(start_msg->tssm_eth_port,
                          pcfg->pc_l3_intfs[i].l3i_ip,
                          pcfg->pc_l3_intfs[i].l3i_mask,
                          pcfg->pc_l3_intfs[i].l3i_vlan_id,
                          pcfg->pc_l3_intfs[i].l3i_gw,
                          i);
    }

    if (pcfg->pc_def_gw.ip_v4 != 0)
        route_v4_gw_add(start_msg->tssm_eth_port, pcfg->pc_def_gw);

    /* TODO: what we should actually do is to wait until the arp reply for
     * the gw reaches us (or until we timeout or something else). We assume
     * for now that 1 second is enough for the reply to arrive.
     */
    sleep(1);

    /* Initialize test entry timers. */
    for (i = 0; i < TPG_TEST_MAX_ENTRIES; i++) {
        state = &tenv->te_test_cases[i].state;
        rte_timer_init(&state->teos_timer);
        state->teos_timer_arg.teta_eth_port = start_msg->tssm_eth_port;
        state->teos_timer_arg.teta_test_case_id = i;
        state->teos_timer_arg.teta_test_env = tenv;
    }

    /* Send INIT for all test cases and wait for it to be processed. */
    TEST_CASE_FOREACH_START(tenv, i, entry, state) {

        tpg_gen_stats_t  *gen_stats;
        tpg_rate_stats_t *rate_stats;
        tpg_app_stats_t  *app_stats;

        tpg_gen_latency_stats_t *gen_latency_stats;

        gen_stats = TEST_CASE_STATS_GET(start_msg->tssm_eth_port, i);
        gen_latency_stats = &gen_stats->gs_latency_stats;

        /* Clear test stats. */
        bzero(gen_stats, sizeof(*gen_stats));
        gen_latency_stats->gls_stats.ls_min_latency = UINT32_MAX;
        gen_latency_stats->gls_sample_stats.ls_min_latency = UINT32_MAX;

        rate_stats = TEST_CASE_RATE_STATS_GET(start_msg->tssm_eth_port, i);
        bzero(rate_stats, sizeof(*rate_stats));

        /* Let the app know that global stats should be cleared. */
        app_stats = TEST_CASE_APP_STATS_GET(start_msg->tssm_eth_port, i);
        APP_CALL(stats_init_global,
                 entry->tc_app.app_proto)(&entry->tc_app, app_stats);

        /* Finally send the message to all cores. */
        test_init_test_case(entry, &tenv->te_test_cases[i].sockopt);

    } TEST_CASE_FOREACH_END()

    /* Start first test and it's associated timer. In the timer callback we
     * either wait for completion of the test (if async == false) or start
     * the next test too.
     */
    tenv->te_test_case_to_start = (tenv->te_test_cases_count > 1);
    tenv->te_test_case_next = test_mgmt_test_case_first(tenv)->tc_id;
    test_start_test_case(start_msg->tssm_eth_port, tenv);

    return 0;
}

/*****************************************************************************
 * test_stop_cb()
 ****************************************************************************/
static int test_stop_cb(uint16_t msgid, uint16_t lcore __rte_unused, void *msg)
{
    uint32_t               i;
    test_stop_msg_t       *stop_msg;
    test_env_t            *tenv;
    test_env_oper_state_t *state;
    tpg_port_cfg_t        *pcfg;
    tpg_test_case_t       *entry;

    if (MSG_INVALID(msgid, msg, MSG_TEST_MGMT_STOP_TEST))
        return -EINVAL;

    stop_msg = msg;
    tenv = &test_env[stop_msg->tssm_eth_port];

    if (!tenv->te_test_running)
        return 0;

    pcfg = &tenv->te_port_cfg;

    if (tenv->te_test_cases_count == 0)
        return -EINVAL;

    /* Stop test cases. */
    TEST_CASE_FOREACH_START(tenv, i, entry, state) {
        /* Cancel test case timers. */
        rte_timer_stop(&state->teos_timer);

        test_stop_test_case(stop_msg->tssm_eth_port, entry, state,
                            TEST_CASE_STATE__STOPPED);
    } TEST_CASE_FOREACH_END()


    /* Delete default gw. */
    if (pcfg->pc_def_gw.ip_v4 != 0)
        route_v4_gw_del(stop_msg->tssm_eth_port, pcfg->pc_def_gw);

    /* Delete L3 interfaces. */
    for (i = 0; i < pcfg->pc_l3_intfs_count; i++) {
        route_v4_intf_del(stop_msg->tssm_eth_port,
                          pcfg->pc_l3_intfs[i].l3i_ip,
                          pcfg->pc_l3_intfs[i].l3i_mask,
                          pcfg->pc_l3_intfs[i].l3i_vlan_id,
                          pcfg->pc_l3_intfs[i].l3i_gw);
    }

    /* Mark test as not running.*/
    tenv->te_test_running = false;
    return 0;
}


/*****************************************************************************
 * test_mgmt_loop()
 ****************************************************************************/
int test_mgmt_loop(void *arg __rte_unused)
{
    int lcore_id    = rte_lcore_id();
    int lcore_index = rte_lcore_index(lcore_id);
    int error;

    RTE_LOG(INFO, USER1, "Starting test management loop on lcore %d, core index %d\n",
            lcore_id, lcore_index);

    /*
     * Call per core initialization functions
     */
    msg_sys_lcore_init(lcore_id);

    while (!tpg_exit) {
        /* Poll for messages from other modules/cores. */
        error = msg_poll();
        if (error) {
            RTE_LOG(ERR, USER1, "Failed to poll for messages on mgmt core: %s(%d)\n",
                    rte_strerror(-error), -error);
        }

        /* Check for the RTE timers too. That's how we actually manage the tests. */
        rte_timer_manage();

        /* Check for status updates on KNI interfaces*/
#if defined(TPG_KNI_IF)
        kni_handle_kernel_status_requests();
#endif
    }

    return 0;
}


/*****************************************************************************
 * test_mgmt_init()
 ****************************************************************************/
static bool test_mgmt_init_env(void)
{
    /*
     * Allocate port test configuration array.
     */
    test_env = rte_zmalloc_socket("test_env",
                                  rte_eth_dev_count() * sizeof(*test_env),
                                  0,
                                  rte_lcore_to_socket_id(rte_lcore_id()));

    return test_env != NULL;
}

/*****************************************************************************
 * test_mgmt_init_runtime_stats()
 ****************************************************************************/
static bool test_mgmt_init_runtime_stats(void)
{
    /*
     * Allocate port test runtime stats array.
     */
    test_runtime_gen_stats =
        rte_zmalloc_socket("test_runtime_gen_stats",
                           rte_eth_dev_count() * TPG_TEST_MAX_ENTRIES *
                           sizeof(*test_runtime_gen_stats),
                           0,
                           rte_lcore_to_socket_id(rte_lcore_id()));
    if (!test_runtime_gen_stats)
        return false;

    test_runtime_rate_stats =
        rte_zmalloc_socket("test_runtime_rate_stats",
                           rte_eth_dev_count() * TPG_TEST_MAX_ENTRIES *
                           sizeof(*test_runtime_rate_stats),
                           0,
                           rte_lcore_to_socket_id(rte_lcore_id()));
    if (!test_runtime_rate_stats)
        return false;

    test_runtime_app_stats =
        rte_zmalloc_socket("test_runtime_app_stats",
                           rte_eth_dev_count() * TPG_TEST_MAX_ENTRIES *
                           sizeof(*test_runtime_app_stats),
                           0,
                           rte_lcore_to_socket_id(rte_lcore_id()));
    if (!test_runtime_app_stats)
        return false;

    return true;
}

/*****************************************************************************
 * test_mgmt_init()
 ****************************************************************************/
bool test_mgmt_init(void)
{
    int error;

    /*
     * Register the handlers for our message types.
     */
    error = msg_register_handler(MSG_TEST_MGMT_START_TEST, test_start_cb);
    if (error) {
        RTE_LOG(ERR, USER1, "Failed to register Tests start msg handlers: %s(%d)\n",
            rte_strerror(-error), -error);
        return false;
    }

    error = msg_register_handler(MSG_TEST_MGMT_STOP_TEST, test_stop_cb);
    if (error) {
        RTE_LOG(ERR, USER1, "Failed to register Tests stop msg handlers: %s(%d)\n",
            rte_strerror(-error), -error);
        return false;
    }

    if (test_mgmt_init_env() == false) {
        RTE_LOG(ERR, USER1, "Failed to allocate tests config!\n");
        return false;
    }

    if (test_mgmt_init_runtime_stats() == false) {
        RTE_LOG(ERR, USER1, "Failed to allocate tests runtime stats!\n");
        return false;
    }

    /*
     * Add tests mgmt module CLI commands
     */
    if (!test_mgmt_cli_init()) {
        RTE_LOG(ERR, USER1, "ERROR: Can't add Test MGMT specific CLI commands!\n");
        return false;
    }

    return true;
}

/*****************************************************************************
 * test_mgmt_get_port_env()
 ****************************************************************************/
test_env_t *test_mgmt_get_port_env(uint32_t eth_port)
{
    return &test_env[eth_port];
}

/*****************************************************************************
 * test_mgmt_get_stats()
 ****************************************************************************/
tpg_gen_stats_t *test_mgmt_get_stats(uint32_t eth_port, uint32_t test_case_id)
{
    return TEST_CASE_STATS_GET(eth_port, test_case_id);
}

/*****************************************************************************
 * test_mgmt_get_rate_stats()
 ****************************************************************************/
tpg_rate_stats_t *test_mgmt_get_rate_stats(uint32_t eth_port,
                                           uint32_t test_case_id)
{
    return TEST_CASE_RATE_STATS_GET(eth_port, test_case_id);
}

/*****************************************************************************
 * test_mgmt_get_app_stats()
 ****************************************************************************/
tpg_app_stats_t *test_mgmt_get_app_stats(uint32_t eth_port,
                                         uint32_t test_case_id)
{
    return TEST_CASE_APP_STATS_GET(eth_port, test_case_id);
}

