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
 *     tpg_tests.c
 *
 * Description:
 *     Test engine implementation
 *
 * Author:
 *     Dumitru Ceara, Eelco Chaudron
 *
 * Initial Created:
 *     03/19/2015
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
/*
 * Array[port][tcid] holding the test case operational state for
 * testcases on a port.
 */
RTE_DEFINE_PER_LCORE(test_case_info_t *, test_case_info);

/*
 * Array[port][tcid] holding the test case config for
 * testcases on a port.
 */
RTE_DEFINE_PER_LCORE(test_case_init_msg_t *, test_case_cfg);

/*
 * Arrays[port][tcid] holding the test case stats (gen, rate, app) for
 * testcases on a port.
 */
RTE_DEFINE_PER_LCORE(tpg_test_case_stats_t *,      test_case_gen_stats);
RTE_DEFINE_PER_LCORE(tpg_test_case_rate_stats_t *, test_case_rate_stats);
RTE_DEFINE_PER_LCORE(tpg_test_case_app_stats_t *,  test_case_app_stats);

#define TEST_GET_GEN_STATS(port, tcid)       \
    (RTE_PER_LCORE(test_case_gen_stats) +    \
     (port) * TPG_TEST_MAX_ENTRIES + (tcid))

#define TEST_GET_RATE_STATS(port, tcid)      \
    (RTE_PER_LCORE(test_case_rate_stats) +   \
     (port) * TPG_TEST_MAX_ENTRIES + (tcid))

#define TEST_GET_APP_STATS(port, tcid)       \
    (RTE_PER_LCORE(test_case_app_stats) +    \
     (port) * TPG_TEST_MAX_ENTRIES + (tcid))

/*
 * Array[port][tcid] holding the test case latency operational state for
 * testcases on a port.
 */
RTE_DEFINE_PER_LCORE(test_oper_latency_state_t *, test_case_latency_state);

#define TEST_GET_LATENCY_STATE(port, tcid)    \
    (RTE_PER_LCORE(test_case_latency_state) + \
     (port) * TPG_TEST_MAX_ENTRIES + (tcid))

/*
 * Pool of messages/tmr args to be used for running client tests (TCP/UDP).
 * We never need more than one message per port + test case + op.
 * Array[testcase][port].
 */

static RTE_DEFINE_PER_LCORE(MSG_TYPEDEF(test_case_run_msg_t) *,
                            test_open_msgpool);
static RTE_DEFINE_PER_LCORE(MSG_TYPEDEF(test_case_run_msg_t) *,
                            test_close_msgpool);
static RTE_DEFINE_PER_LCORE(MSG_TYPEDEF(test_case_run_msg_t) *,
                            test_send_msgpool);

#define TEST_GET_MSG_PTR(type, port, tcid)   \
    (RTE_PER_LCORE(test_##type##_msgpool) +  \
     (port) * TPG_TEST_MAX_ENTRIES + (tcid))

static RTE_DEFINE_PER_LCORE(test_tmr_arg_t *, test_tmr_open_args);
static RTE_DEFINE_PER_LCORE(test_tmr_arg_t *, test_tmr_close_args);
static RTE_DEFINE_PER_LCORE(test_tmr_arg_t *, test_tmr_send_args);

#define TEST_GET_TMR_ARG(type, port, tcid)   \
    (RTE_PER_LCORE(test_tmr_##type##_args) + \
     (port) * TPG_TEST_MAX_ENTRIES + (tcid))

/* Callback to be executed whenever an interesting event happens. */
notif_cb_t test_notif_cb;

/*****************************************************************************
 * Forward declarations
 ****************************************************************************/
static uint32_t test_case_execute_tcp_open(test_case_info_t *tc_info,
                                           uint32_t to_open_cnt);
static uint32_t test_case_execute_tcp_close(test_case_info_t *tc_info,
                                            uint32_t to_close_cnt);
static uint32_t test_case_execute_tcp_send(test_case_info_t *tc_info,
                                           uint32_t to_send_cnt);
static uint32_t test_case_execute_udp_open(test_case_info_t *tc_info,
                                           uint32_t to_send_cnt);
static uint32_t test_case_execute_udp_close(test_case_info_t *tc_info,
                                            uint32_t to_send_cnt);
static uint32_t test_case_execute_udp_send(test_case_info_t *tc_info,
                                           uint32_t to_send_cnt);
static void test_update_recent_latency_stats(tpg_latency_stats_t *stats,
                                             test_oper_latency_state_t *buffer,
                                             tpg_test_case_latency_t *tc_latency);

static void test_case_latency_init(test_case_info_t *tc_info);

/*****************************************************************************
 * Client and server config control block walk functions
 ****************************************************************************/

/*
 * Callback definition to be used when walking control block configs (both
 * clients and servers).
 */
typedef void (*test_walk_cfg_cb_t)(uint32_t lcore,
                                   uint32_t eth_port, uint32_t test_case_id,
                                   uint32_t src_ip, uint32_t dst_ip,
                                   uint16_t src_port, uint16_t dst_port,
                                   uint32_t conn_hash, void *arg);

/*****************************************************************************
 * test_case_for_each_client()
 *      Notes: walks the list of client control blocks from a given config.
 *             Only core-local clients are processed.
 ****************************************************************************/
static void test_case_for_each_client(uint32_t lcore,
                                      const test_case_init_msg_t *cfg,
                                      test_walk_cfg_cb_t callback,
                                      void *callback_arg)
{
    uint32_t src_ip, dst_ip;
    uint16_t src_port, dst_port;
    uint32_t conn_hash;
    uint32_t rx_queue_id = port_get_rx_queue_id(lcore, cfg->tcim_eth_port);

    const tpg_client_t *client_cfg = &cfg->tcim_client;

    TPG_FOREACH_CB_IN_RANGE(&client_cfg->cl_src_ips,
                            &client_cfg->cl_dst_ips,
                            &client_cfg->cl_l4.l4c_tcp_udp.tuc_sports,
                            &client_cfg->cl_l4.l4c_tcp_udp.tuc_dports,
                            src_ip, dst_ip, src_port, dst_port) {
        conn_hash = tlkp_calc_connection_hash(dst_ip, src_ip, dst_port,
                                              src_port);
        if (tlkp_get_qindex_from_hash(conn_hash, cfg->tcim_eth_port) !=
                rx_queue_id)
            continue;

        callback(lcore, cfg->tcim_eth_port, cfg->tcim_test_case_id,
                 src_ip, dst_ip, src_port, dst_port, conn_hash, callback_arg);
    }
}

/*****************************************************************************
 * test_case_client_cfg_count()
 *      Notes: returns the total number of client connections that would be
 *             generated from a given config.
 ****************************************************************************/
static uint32_t test_case_client_cfg_count(const tpg_client_t *client_cfg)
{
    return TPG_IPV4_RANGE_SIZE(&client_cfg->cl_src_ips) *
                TPG_IPV4_RANGE_SIZE(&client_cfg->cl_dst_ips) *
                TPG_PORT_RANGE_SIZE(&client_cfg->cl_l4.l4c_tcp_udp.tuc_sports) *
                TPG_PORT_RANGE_SIZE(&client_cfg->cl_l4.l4c_tcp_udp.tuc_dports);
}

/*****************************************************************************
 * test_case_for_each_server()
 *      Notes: walks the list of server control blocks from a given config.
 ****************************************************************************/
static void test_case_for_each_server(uint32_t lcore,
                                      const test_case_init_msg_t *cfg,
                                      test_walk_cfg_cb_t callback,
                                      void *callback_arg)
{
    uint32_t src_ip;
    uint16_t src_port;

    const tpg_server_t *server_cfg = &cfg->tcim_server;

    TPG_IPV4_FOREACH(&server_cfg->srv_ips, src_ip) {
        TPG_PORT_FOREACH(&server_cfg->srv_l4.l4s_tcp_udp.tus_ports, src_port) {
            callback(lcore, cfg->tcim_eth_port, cfg->tcim_test_case_id,
                     src_ip, 0, src_port, 0, 0, callback_arg);
        }
    }
}

/*****************************************************************************
 * test_case_server_cfg_count()
 *      Notes: returns the total number of server listeners that would be
 *             generated from a given config.
 ****************************************************************************/
static uint32_t test_case_server_cfg_count(const tpg_server_t *server_cfg)
{
    return TPG_IPV4_RANGE_SIZE(&server_cfg->srv_ips) *
                TPG_PORT_RANGE_SIZE(&server_cfg->srv_l4.l4s_tcp_udp.tus_ports);
}

/*****************************************************************************
 * TCP/UDP close functions
 ****************************************************************************/
/*****************************************************************************
 * test_case_tcp_close()
 ****************************************************************************/
static void test_case_tcp_close(l4_control_block_t *l4_cb)
{
    tcp_close_connection(container_of(l4_cb, tcp_control_block_t, tcb_l4), 0);
}

/*****************************************************************************
 * test_case_udp_close()
 ****************************************************************************/
static void test_case_udp_close(l4_control_block_t *l4_cb)
{
    udp_close_v4((udp_control_block_t *)l4_cb);
}

/*****************************************************************************
 * Test stats timer & run triggering static functions.
 ****************************************************************************/
/*****************************************************************************
 * test_get_run_cl_arg_ptr()
 ****************************************************************************/
static test_tmr_arg_t *
test_init_run_arg_ptr(test_tmr_arg_t *tmr_arg, uint32_t lcore_id,
                      uint32_t eth_port,
                      uint32_t test_case_id,
                      test_rate_state_t *rate_state)
{
    tmr_arg->tta_lcore_id = lcore_id;
    tmr_arg->tta_eth_port = eth_port;
    tmr_arg->tta_test_case_id = test_case_id;
    tmr_arg->tta_rate_state = rate_state;

    return tmr_arg;
}

/*****************************************************************************
 * test_case_do_run_msg()
 ****************************************************************************/
static int test_case_do_run_msg(msg_t *msgp, uint32_t msg_type,
                                uint32_t lcore_id,
                                uint32_t eth_port,
                                uint32_t test_case_id)
{
    int error;

    msg_init(msgp, msg_type, lcore_id, MSG_FLAG_LOCAL);

    MSG_INNER(test_case_run_msg_t, msgp)->tcrm_eth_port = eth_port;
    MSG_INNER(test_case_run_msg_t, msgp)->tcrm_test_case_id = test_case_id;

    /* Send the message and forget about it. */
    error = msg_send_local(msgp, MSG_SND_FLAG_NOWAIT);

    if (unlikely(error != 0)) {
        RTE_LOG(ERR, USER1,
                "[%d:%s()] Failed to send RUN message: tcid=%"PRIu32" %s(%d)\n",
                rte_lcore_index(lcore_id),
                __func__,
                test_case_id,
                rte_strerror(-error), -error);
    }

    return 0;
}

/*****************************************************************************
 * test_resched_open()
 ****************************************************************************/
void test_resched_open(test_rate_state_t *rate_state, uint32_t eth_port,
                       uint32_t test_case_id)
{
    msg_t *msgp;

    if (rate_state->trs_open_in_progress)
        return;
    if (rate_state->trs_open_rate_reached)
        return;

    msgp = &TEST_GET_MSG_PTR(open, eth_port, test_case_id)->msg;

    if (test_case_do_run_msg(msgp, MSG_TEST_CASE_RUN_OPEN, rte_lcore_id(),
                             eth_port,
                             test_case_id) == 0) {
        rate_state->trs_open_in_progress = true;
    }
}

/*****************************************************************************
 * test_resched_close()
 ****************************************************************************/
void test_resched_close(test_rate_state_t *rate_state, uint32_t eth_port,
                        uint32_t test_case_id)
{
    msg_t *msgp;

    if (rate_state->trs_close_in_progress)
        return;
    if (rate_state->trs_close_rate_reached)
        return;

    msgp = &TEST_GET_MSG_PTR(close, eth_port, test_case_id)->msg;

    if (test_case_do_run_msg(msgp, MSG_TEST_CASE_RUN_CLOSE, rte_lcore_id(),
                             eth_port,
                             test_case_id) == 0) {
        rate_state->trs_close_in_progress = true;
    }
}

/*****************************************************************************
 * test_resched_send()
 ****************************************************************************/
void test_resched_send(test_rate_state_t *rate_state, uint32_t eth_port,
                       uint32_t test_case_id)
{
    msg_t *msgp;

    if (rate_state->trs_send_in_progress)
        return;
    if (rate_state->trs_send_rate_reached)
        return;

    msgp = &TEST_GET_MSG_PTR(send, eth_port, test_case_id)->msg;

    if (test_case_do_run_msg(msgp, MSG_TEST_CASE_RUN_SEND, rte_lcore_id(),
                             eth_port,
                             test_case_id) == 0) {
        rate_state->trs_send_in_progress = true;
    }
}

/*****************************************************************************
 * test_latency_state_init
 ****************************************************************************/
static void test_latency_state_init(test_oper_latency_state_t *buffer,
                                    uint32_t len)
{
    bzero(buffer, sizeof(*buffer));
    buffer->tols_length = len;
}

/*****************************************************************************
 * test_latency_state_add()
 ****************************************************************************/
static void test_latency_state_add(test_oper_latency_state_t *buffer,
                                   uint64_t tstamp,
                                   tpg_latency_stats_t *tcls_sample_stats)
{
    uint32_t position;

    position = (buffer->tols_actual_length + buffer->tols_start_index) %
        buffer->tols_length;

    if (buffer->tols_actual_length + 1 <= buffer->tols_length) {
        buffer->tols_timestamps[position] = tstamp;
        buffer->tols_actual_length++;
    } else {
        uint64_t retain_tstamp;

        retain_tstamp = buffer->tols_timestamps[position];
        buffer->tols_timestamps[position] = tstamp;

        tcls_sample_stats->ls_sum_latency -= retain_tstamp;
        tcls_sample_stats->ls_samples_count--;
    }

}

/*****************************************************************************
 * test_update_latency_stats()
 ****************************************************************************/
static void test_update_latency_stats(tpg_latency_stats_t *stats,
                                      uint64_t latency,
                                      tpg_test_case_latency_t *tci_latency)
{
    int64_t avg = 0;

    if (stats->ls_samples_count > 0) {
        avg = stats->ls_sum_latency / stats->ls_samples_count;
        stats->ls_instant_jitter = (avg >= (int) latency) ?
                                  (avg - latency) : (latency - avg);
        stats->ls_sum_jitter += stats->ls_instant_jitter;
    }

    if (tci_latency->has_tcs_max) {
        if (latency > tci_latency->tcs_max)
            INC_STATS(stats, ls_max_exceeded);
    }

    if (tci_latency->has_tcs_max_avg) {
        if (avg > tci_latency->tcs_max_avg)
            INC_STATS(stats, ls_max_average_exceeded);
    }

    stats->ls_samples_count++;
    stats->ls_sum_latency += latency;

    if (latency < stats->ls_min_latency)
        stats->ls_min_latency = latency;
    if (latency > stats->ls_max_latency)
        stats->ls_max_latency = latency;
}

/*****************************************************************************
 * test_update_latency()
 ****************************************************************************/
void test_update_latency(l4_control_block_t *l4cb, uint64_t sent_tstamp,
                         uint64_t rcv_tstamp)
{
    test_case_info_t              *tc_info;
    tpg_test_case_latency_stats_t *stats;
    int64_t                        latency;

    tc_info = TEST_GET_INFO(l4cb->l4cb_interface, l4cb->l4cb_test_case_id);
    stats = &tc_info->tci_general_stats->tcs_latency_stats;

    if (rcv_tstamp < sent_tstamp || sent_tstamp == 0 || rcv_tstamp == 0) {
        INC_STATS(stats, tcls_invalid_lat);
        return;
    }

    latency = rcv_tstamp - sent_tstamp;

    /* Global stats */
    test_update_latency_stats(&stats->tcls_stats, latency,
                              &tc_info->tci_cfg->tcim_latency);

    /* Recent stats */
    if (tc_info->tci_latency_state->tols_length != 0) {
        test_latency_state_add(tc_info->tci_latency_state,
                               latency, &stats->tcls_sample_stats);
    }
}

/*****************************************************************************
 * test_case_tmr_open_cb()
 ****************************************************************************/
static void test_case_tmr_open_cb(struct rte_timer *tmr __rte_unused, void *arg)
{
    test_tmr_arg_t    *tmr_arg = arg;
    test_rate_state_t *rate_state = tmr_arg->tta_rate_state;

    /* We step into a new time interval... "Advance" the rates. */
    rate_limit_advance_interval(&rate_state->trs_open);

    /* Start from scratch.. */
    rate_state->trs_open_rate_reached = false;

    test_resched_open(rate_state, tmr_arg->tta_eth_port,
                      tmr_arg->tta_test_case_id);
}

/*****************************************************************************
 * test_case_tmr_close_cb()
 ****************************************************************************/
static void test_case_tmr_close_cb(struct rte_timer *tmr __rte_unused, void *arg)
{
    test_tmr_arg_t    *tmr_arg = arg;
    test_rate_state_t *rate_state = tmr_arg->tta_rate_state;

    /* We step into a new time interval... "Advance" the rates. */
    rate_limit_advance_interval(&rate_state->trs_close);

    /* Start from scratch.. */
    rate_state->trs_close_rate_reached = false;

    test_resched_close(rate_state, tmr_arg->tta_eth_port,
                      tmr_arg->tta_test_case_id);
}

/*****************************************************************************
 * test_case_tmr_send_cb()
 ****************************************************************************/
static void test_case_tmr_send_cb(struct rte_timer *tmr __rte_unused, void *arg)
{
    test_tmr_arg_t    *tmr_arg = arg;
    test_rate_state_t *rate_state = tmr_arg->tta_rate_state;

    /* We step into a new time interval... "Advance" the rates. */
    rate_limit_advance_interval(&rate_state->trs_send);

    /* Start from scratch.. */
    rate_state->trs_send_rate_reached = false;

    test_resched_send(rate_state, tmr_arg->tta_eth_port,
                      tmr_arg->tta_test_case_id);
}

/*****************************************************************************
 * Test notifications.
 ****************************************************************************/
/****************************************************************************
 * test_tcp_notif_syn_recv()
 ****************************************************************************/
static void test_tcp_notif_syn_recv(tcp_control_block_t *tcb,
                                    test_case_info_t *tc_info)
{
    tpg_app_proto_t app_id = tcb->tcb_l4.l4cb_app_data.ad_type;

    /* Initialize the application state. */
    APP_SRV_CALL(init, app_id)(&tcb->tcb_l4.l4cb_app_data, tc_info->tci_cfg);

    /* Start the server test state machine. */
    test_server_sm_initialize(&tcb->tcb_l4, tc_info);
}
/****************************************************************************
 * test_tcp_notif_syn_sent()
 ****************************************************************************/
static void test_tcp_notif_syn_sent(tcp_control_block_t *tcb __rte_unused,
                                    test_case_info_t *tc_info)
{
    if (unlikely(tc_info->tci_general_stats->tcs_start_time == 0))
        tc_info->tci_general_stats->tcs_start_time = rte_get_timer_cycles();
}

/****************************************************************************
 * test_tcp_notif_established()
 ****************************************************************************/
static void test_tcp_notif_established(tcp_control_block_t *tcb,
                                       test_case_info_t *tc_info)
{
    if (tcb->tcb_active) {
        tc_info->tci_general_stats->tcs_client.tccs_estab++;
        /*
         * This is not really the end time.. but we can keep it
         * here for now for tracking how long it took to establish sessions.
         */
        tc_info->tci_general_stats->tcs_end_time = rte_get_timer_cycles();
    } else {
        tc_info->tci_general_stats->tcs_server.tcss_estab++;
    }

    /* Update rate per second. */
    tc_info->tci_rate_stats->tcrs_estab_per_s++;
}

/****************************************************************************
 * test_tcp_notif_closed()
 ****************************************************************************/
static void test_tcp_notif_closed(tcp_control_block_t *tcb __rte_unused,
                                  test_case_info_t *tc_info)
{
    /* Update rate per second. */
    tc_info->tci_rate_stats->tcrs_closed_per_s++;
}

/****************************************************************************
 * test_notif_state_chg_handler()
 *
 * NOTE:
 *   This will be called from the packet processing core context.
 ****************************************************************************/
static void test_tcp_notif_state_chg_handler(notif_arg_t *arg)
{
    tcp_notif_arg_t     *tcp = &arg->narg_tcp;
    tcp_control_block_t *tcb = tcp->tnarg_tcb;
    test_case_info_t    *tc_info = TEST_GET_INFO(arg->narg_interface,
                                                 arg->narg_tcid);

    if (tcb->tcb_state == TS_SYN_RECV)
        test_tcp_notif_syn_recv(tcb, tc_info);
    else if (tcb->tcb_state == TS_SYN_SENT)
        test_tcp_notif_syn_sent(tcb, tc_info);
    else if (tcb->tcb_state == TS_ESTABLISHED)
        test_tcp_notif_established(tcb, tc_info);
    else if (tcb->tcb_state == TS_CLOSED)
        test_tcp_notif_closed(tcb, tc_info);

    if (tcb->tcb_active)
        test_client_sm_tcp_state_change(tcb, tc_info);
    else if (tcb->tcb_state >= TS_SYN_RECV) {
        /* Server TCBs are not part of the TEST state machine until they reach
         * SYN-RCVD.
         */
        test_server_sm_tcp_state_change(tcb, tc_info);
    }
}

/****************************************************************************
 * test_tcp_notif_win_avail_handler()
 *
 * NOTE:
 *   This will be called from the packet processing core context.
 ****************************************************************************/
static void test_tcp_notif_win_avail_handler(notif_arg_t *arg)
{
    test_case_info_t    *tc_info;
    tcp_control_block_t *tcb;

    tc_info = TEST_GET_INFO(arg->narg_interface, arg->narg_tcid);
    tcb = arg->narg_tcp.tnarg_tcb;

    if (tcb->tcb_active)
        test_client_sm_tcp_snd_win_avail(tcb, tc_info);
    else
        test_server_sm_tcp_snd_win_avail(tcb, tc_info);
}

/****************************************************************************
 * test_tcp_notif_win_unavail_handler()
 *
 * NOTE:
 *   This will be called from the packet processing core context.
 ****************************************************************************/
static void test_tcp_notif_win_unavail_handler(notif_arg_t *arg)
{
    test_case_info_t    *tc_info;
    tcp_control_block_t *tcb;

    tc_info = TEST_GET_INFO(arg->narg_interface, arg->narg_tcid);
    tcb = arg->narg_tcp.tnarg_tcb;

    if (tcb->tcb_active)
        test_client_sm_tcp_snd_win_unavail(tcb, tc_info);
    else
        test_server_sm_tcp_snd_win_unavail(tcb, tc_info);
}

/****************************************************************************
 * test_udp_notif_client_connected()
 ****************************************************************************/
static void test_udp_notif_client_connected(udp_control_block_t *ucb __rte_unused,
                                            test_case_info_t *tc_info)
{
    if (unlikely(tc_info->tci_general_stats->tcs_start_time == 0))
        tc_info->tci_general_stats->tcs_start_time = rte_get_timer_cycles();

    tc_info->tci_general_stats->tcs_client.tccs_estab++;
    /*
     * This is not really the end time.. but we can keep it
     * here for now for tracking how long it took to establish sessions.
     */
    tc_info->tci_general_stats->tcs_end_time = rte_get_timer_cycles();

    /* Update rate per second. */
    tc_info->tci_rate_stats->tcrs_estab_per_s++;
}

/****************************************************************************
 * test_udp_notif_server_connected()
 ****************************************************************************/
static void test_udp_notif_server_connected(udp_control_block_t *ucb __rte_unused,
                                            test_case_info_t *tc_info)
{
    tc_info->tci_general_stats->tcs_server.tcss_estab++;

    /* Update rate per second. */
    tc_info->tci_rate_stats->tcrs_estab_per_s++;
}

/****************************************************************************
 * test_udp_notif_state_chg_handler()
 *
 * NOTE:
 *   This will be called from the packet processing core context.
 ****************************************************************************/
static void test_udp_notif_state_chg_handler(notif_arg_t *arg)
{
    udp_notif_arg_t     *udp = &arg->narg_udp;
    udp_control_block_t *ucb = udp->unarg_ucb;
    test_case_info_t    *tc_info = TEST_GET_INFO(arg->narg_interface,
                                                 arg->narg_tcid);

    if (ucb->ucb_state == US_OPEN) {
        if (ucb->ucb_active) {
            test_udp_notif_client_connected(ucb, tc_info);
        } else {
            tpg_app_proto_t app_id = ucb->ucb_l4.l4cb_app_data.ad_type;

            test_udp_notif_server_connected(ucb, tc_info);

            /* Initialize the server state machine. */
            test_server_sm_initialize(&ucb->ucb_l4, tc_info);

            /* Initialize the application state. */
            APP_SRV_CALL(init, app_id)(&ucb->ucb_l4.l4cb_app_data,
                                       tc_info->tci_cfg);
        }
    }

    /* Notify the test state machine about the state change. */
    if (ucb->ucb_active) {
        test_client_sm_udp_state_change(ucb, tc_info);
    } else if (ucb->ucb_state > US_LISTEN) {
        /* Server UCBs are not part of the TEST state machine until they reach
         * US_OPEN.
         */
        test_server_sm_udp_state_change(ucb, tc_info);
    }
}

/****************************************************************************
 * test_tcp_udp_notif_handler()
 *
 * NOTE:
 *   This will be called from the packet processing core context.
 ****************************************************************************/
static void test_tcp_udp_notif_handler(uint32_t notification,
                                       notif_arg_t *notif_arg)
{
    if (notification == TCB_NOTIF_STATE_CHANGE)
        test_tcp_notif_state_chg_handler(notif_arg);
    else if (notification == TCB_NOTIF_SEG_WIN_AVAILABLE)
        test_tcp_notif_win_avail_handler(notif_arg);
    else if (notification == TCB_NOTIF_SEG_WIN_UNAVAILABLE)
        test_tcp_notif_win_unavail_handler(notif_arg);
    else if (notification == UCB_NOTIF_STATE_CHANGE)
        test_udp_notif_state_chg_handler(notif_arg);
}

/****************************************************************************
 * test_tmr_to()
 ****************************************************************************/
static void test_tmr_to(l4_control_block_t *l4_cb, test_case_info_t *tc_info)
{
    test_client_sm_tmr_to(l4_cb, tc_info);
}

/****************************************************************************
 * test_notif_handler()
 *
 * NOTE:
 *   This will be called from the packet processing core context.
 ****************************************************************************/
static void test_notif_handler(uint32_t notification, notif_arg_t *arg)
{
    test_case_info_t *tc_info;

    tc_info = TEST_GET_INFO(arg->narg_interface, arg->narg_tcid);

    switch (notification) {
    case TEST_NOTIF_SERVER_UP:
        tc_info->tci_general_stats->tcs_server.tcss_up++;
        break;
    case TEST_NOTIF_SERVER_DOWN:
        tc_info->tci_general_stats->tcs_server.tcss_down++;
        break;
    case TEST_NOTIF_SERVER_FAILED:
        tc_info->tci_general_stats->tcs_server.tcss_failed++;
        break;

    case TEST_NOTIF_CLIENT_UP:
        tc_info->tci_general_stats->tcs_client.tccs_up++;
        break;
    case TEST_NOTIF_CLIENT_DOWN:
        tc_info->tci_general_stats->tcs_client.tccs_down++;
        break;
    case TEST_NOTIF_CLIENT_FAILED:
        tc_info->tci_general_stats->tcs_client.tccs_failed++;
        break;

    case TEST_NOTIF_TMR_FIRED:
        test_tmr_to(arg->narg_test.tnarg_l4_cb, tc_info);
        break;

    case TEST_NOTIF_APP_CLIENT_SEND_START:
        test_app_client_send_start(arg->narg_test.tnarg_l4_cb, tc_info);
        break;
    case TEST_NOTIF_APP_CLIENT_SEND_STOP:
        test_app_client_send_stop(arg->narg_test.tnarg_l4_cb, tc_info);
        break;
    case TEST_NOTIF_APP_CLIENT_CLOSE:
        tc_info->tci_close_cb(arg->narg_test.tnarg_l4_cb);
        break;
    case TEST_NOTIF_APP_SERVER_SEND_START:
        test_app_server_send_start(arg->narg_test.tnarg_l4_cb, tc_info);
        break;
    case TEST_NOTIF_APP_SERVER_SEND_STOP:
        test_app_server_send_stop(arg->narg_test.tnarg_l4_cb, tc_info);
        break;
    case TEST_NOTIF_APP_SERVER_CLOSE:
        tc_info->tci_close_cb(arg->narg_test.tnarg_l4_cb);
        break;

    case TEST_NOTIF_DATA_FAILED:
        tc_info->tci_general_stats->tcs_data_failed++;
        break;
    case TEST_NOTIF_DATA_NULL:
        tc_info->tci_general_stats->tcs_data_null++;
        break;

    default:
        assert(false);
    }
}

/*****************************************************************************
 * Static functions for Rate Limiting engine initialization/start/stop.
 ****************************************************************************/

/*****************************************************************************
 * test_case_rate_init()
 *      NOTES: scales down the rate limit based on the percentage of sessions
 *      actually running on this core. However, if rate limiting is unlimited
 *      (TPG_RATE_LIM_INFINITE_VAL) there's no need to scale down.
 ****************************************************************************/
static void test_case_rate_init(const char *rl_name,
                                rate_limit_t *rl,
                                rate_limit_cfg_t *rate_cfg,
                                uint32_t lcore,
                                uint32_t eth_port,
                                uint32_t max_burst,
                                uint32_t total_sessions,
                                uint32_t local_sessions)
{
    uint32_t target_rate = rate_cfg->rlc_target;
    uint32_t core_count = PORT_QCNT(eth_port);
    int      err;

    if (target_rate != TPG_RATE_LIM_INFINITE_VAL && total_sessions != 0)
        target_rate = (uint64_t)local_sessions * target_rate / total_sessions;

    err = rate_limit_init(rl, rate_cfg, lcore, core_count, target_rate,
                          max_burst);
    if (unlikely(err != 0)) {
        /* Unfortunately we can't do much here.. Just log the error for the
         * user. Rate limiting will stay 0 so no operations of the
         * corresponding type (i.e., open/close/send) will be performed.
         */
        RTE_LOG(ERR, USER1,
                "[%d:%s()] Failed to initialize %s rate limit: %d(%s)\n",
                rte_lcore_index(lcore),
                __func__, rl_name,
                -err, rte_strerror(-err));
    }
}

/*****************************************************************************
 * test_case_rate_zero()
 *      NOTES: Change the current rate to zero (i.e., free & init).
 ****************************************************************************/
static void test_case_rate_zero(const char *rl_name, rate_limit_t *rl,
                                uint32_t lcore,
                                uint32_t eth_port)
{
    rate_limit_cfg_t zero_rate_cfg = RATE_CFG_ZERO();

    rate_limit_free(rl);

    /* TODO: We assume that a zero rate will not allocate any memory inside the
     * rate object..
     */
    test_case_rate_init(rl_name, rl, &zero_rate_cfg, lcore, eth_port, 0, 0, 0);
}

/*****************************************************************************
 * test_case_rate_start_timer()
 ****************************************************************************/
static void test_case_rate_start_timer(struct rte_timer *tmr,
                                       test_tmr_arg_t *tmr_arg,
                                       test_rate_state_t *rate_state,
                                       rate_limit_t *rl,
                                       rte_timer_cb_t tmr_cb,
                                       uint32_t lcore_id,
                                       uint32_t eth_port,
                                       uint32_t test_case_id)
{

    /* No need to start a periodic timer if rate-limiting is set to 0. */
    if (!rate_limit_interval_us(rl))
        return;

    test_init_run_arg_ptr(tmr_arg, lcore_id, eth_port, test_case_id,
                          rate_state);
    rte_timer_reset(tmr, rate_limit_interval_us(rl) * cycles_per_us, PERIODICAL,
                    lcore_id,
                    tmr_cb,
                    tmr_arg);
}

/*****************************************************************************
 * test_case_rate_state_init()
 ****************************************************************************/
static void test_case_rate_state_init(uint32_t lcore, uint32_t eth_port,
                                      uint32_t test_case_id __rte_unused,
                                      test_oper_state_t *test_state,
                                      test_rate_timers_t *rate_timers,
                                      test_case_init_msg_t *im,
                                      uint32_t total_sessions,
                                      uint32_t local_sessions)
{
    test_rate_state_t *rate_state = &test_state->tos_rates;
    uint32_t           max_burst = 0;

    switch (im->tcim_l4_type) {
    case L4_PROTO__TCP:
        max_burst = GCFG_TCP_CLIENT_BURST_MAX;
        break;
    case L4_PROTO__UDP:
        max_burst = GCFG_UDP_CLIENT_BURST_MAX;
        break;
    default:
        assert(false);
        break;
    }

    /* Initialize open/close/send timers. */
    rte_timer_init(&rate_timers->trt_open_timer);
    rte_timer_init(&rate_timers->trt_close_timer);
    rte_timer_init(&rate_timers->trt_send_timer);

    /* Initialize open/close/send rate limiter states. */
    rate_state->trs_open_in_progress = false;
    rate_state->trs_open_rate_reached = false;
    rate_state->trs_close_in_progress = false;
    rate_state->trs_close_rate_reached = false;
    rate_state->trs_send_in_progress = false;
    rate_state->trs_send_rate_reached = false;

    /* WARNING: It's safe to use the values from im->tcim_transient here as
     * long as this function is called only from the MSG_TEST_CASE_INIT
     * callback!
     */
    test_case_rate_init("open", &rate_state->trs_open,
                        im->tcim_transient.open_rate,
                        lcore, eth_port, max_burst,
                        total_sessions, local_sessions);

    test_case_rate_init("close", &rate_state->trs_close,
                        im->tcim_transient.close_rate,
                        lcore, eth_port, max_burst,
                        total_sessions, local_sessions);

    test_case_rate_init("send", &rate_state->trs_send,
                        im->tcim_transient.send_rate,
                        lcore, eth_port, max_burst,
                        total_sessions, local_sessions);
}

/*****************************************************************************
 * test_case_rate_state_start()
 ****************************************************************************/
static void
test_case_rate_state_start(uint32_t lcore, uint32_t eth_port,
                           uint32_t test_case_id,
                           test_oper_state_t *test_state,
                           test_rate_timers_t *rate_timers)
{
    test_rate_state_t *rate_state = &test_state->tos_rates;

    test_case_rate_start_timer(&rate_timers->trt_open_timer,
                               TEST_GET_TMR_ARG(open, eth_port, test_case_id),
                               rate_state,
                               &rate_state->trs_open,
                               test_case_tmr_open_cb,
                               lcore,
                               eth_port,
                               test_case_id);

    test_case_rate_start_timer(&rate_timers->trt_close_timer,
                               TEST_GET_TMR_ARG(close, eth_port, test_case_id),
                               rate_state,
                               &rate_state->trs_close,
                               test_case_tmr_close_cb,
                               lcore,
                               eth_port,
                               test_case_id);

    test_case_rate_start_timer(&rate_timers->trt_send_timer,
                               TEST_GET_TMR_ARG(send, eth_port, test_case_id),
                               rate_state,
                               &rate_state->trs_send,
                               test_case_tmr_send_cb,
                               lcore,
                               eth_port,
                               test_case_id);
}

/*****************************************************************************
 * test_case_rate_state_stop()
 ****************************************************************************/
static void
test_case_rate_state_stop(uint32_t lcore, uint32_t eth_port,
                          uint32_t test_case_id __rte_unused,
                          test_oper_state_t *test_state,
                          test_rate_timers_t *rate_timers)
{
    test_rate_state_t *rate_state = &test_state->tos_rates;

    /* Change the target rates to 0 so we don't open/close/anymore.
     * Cancel the open/close/send timers.
     */
    rte_timer_stop(&rate_timers->trt_open_timer);
    rte_timer_stop(&rate_timers->trt_close_timer);
    rte_timer_stop(&rate_timers->trt_send_timer);

    test_case_rate_zero("open-zero", &rate_state->trs_open, lcore, eth_port);
    test_case_rate_zero("close-zero", &rate_state->trs_close, lcore, eth_port);
    test_case_rate_zero("send-zero", &rate_state->trs_send, lcore, eth_port);
}

/*****************************************************************************
 * test_case_rate_state_running()
 *      Notes: return true if there's any rate limiting message in progress.
 ****************************************************************************/
static bool
test_case_rate_state_running(test_oper_state_t *test_state)
{
    test_rate_state_t *rate_state = &test_state->tos_rates;

    return rate_state->trs_open_in_progress ||
                rate_state->trs_close_in_progress ||
                rate_state->trs_send_in_progress;
}

/*****************************************************************************
 * Static functions for Initializing test cases.
 ****************************************************************************/

/*****************************************************************************
 * test_case_init_state_client_counters_cb()
 ****************************************************************************/
static void
test_case_init_state_client_counters_cb(uint32_t lcore __rte_unused,
                                        uint32_t eth_port __rte_unused,
                                        uint32_t test_case_id __rte_unused,
                                        uint32_t src_ip __rte_unused,
                                        uint32_t dst_ip __rte_unused,
                                        uint16_t src_port __rte_unused,
                                        uint16_t dst_port __rte_unused,
                                        uint32_t conn_hash __rte_unused,
                                        void *arg)
{
    uint32_t *local_sessions = arg;

    (*local_sessions)++;
}

/*****************************************************************************
 * test_case_init_state()
 ****************************************************************************/
static void test_case_init_state(uint32_t lcore, test_case_init_msg_t *im,
                                 test_oper_state_t *ts,
                                 test_rate_timers_t *rate_timers)
{
    uint32_t total_sessions = 0;
    uint32_t local_sessions = 0;

    TEST_CBQ_INIT(&ts->tos_to_init_cbs);
    TEST_CBQ_INIT(&ts->tos_to_open_cbs);
    TEST_CBQ_INIT(&ts->tos_to_close_cbs);
    TEST_CBQ_INIT(&ts->tos_to_send_cbs);
    TEST_CBQ_INIT(&ts->tos_closed_cbs);

    /* Initialize the rates based on the percentage of clients running on
     * this core.
     */
    switch (im->tcim_type) {
    case TEST_CASE_TYPE__CLIENT:

        /* Get local and total session count. Unfortunately there's no other
         * way to compute the number of local sessions than to walk the list..
         */
        test_case_for_each_client(lcore, im,
                                  test_case_init_state_client_counters_cb,
                                  &local_sessions);
        total_sessions = test_case_client_cfg_count(&im->tcim_client);
        break;
    case TEST_CASE_TYPE__SERVER:
        /* We know that servers are created on all lcores
         * (i.e., local == total).
         */
        local_sessions = test_case_server_cfg_count(&im->tcim_server);
        total_sessions = test_case_server_cfg_count(&im->tcim_server);
        break;

    default:
        assert(false);
        break;
    }

    /* Initialize the rates. */
    test_case_rate_state_init(lcore, im->tcim_eth_port, im->tcim_test_case_id,
                              ts, rate_timers, im,
                              total_sessions, local_sessions);
}

/*****************************************************************************
 * test_case_init_callbacks()
 ****************************************************************************/
static void test_case_init_callbacks(test_case_info_t *tc_info,
                                     test_case_runner_cb_t run_open_cb,
                                     test_case_runner_cb_t run_close_cb,
                                     test_case_runner_cb_t run_send_cb,
                                     test_case_close_cb_t close_cb)
{
    tc_info->tci_run_open_cb = run_open_cb;
    tc_info->tci_run_close_cb = run_close_cb;
    tc_info->tci_run_send_cb = run_send_cb;
    tc_info->tci_close_cb = close_cb;
}

/*****************************************************************************
 * test_case_start_tcp_server()
 ****************************************************************************/
static void
test_case_start_tcp_server(uint32_t lcore __rte_unused,
                           uint32_t eth_port, uint32_t test_case_id,
                           uint32_t src_ip, uint32_t dst_ip __rte_unused,
                           uint16_t src_port, uint16_t dst_port __rte_unused,
                           uint32_t conn_hash __rte_unused,
                           void *arg)
{
    tcp_control_block_t *server_tcb;
    test_case_info_t    *tc_info;
    sockopt_t           *sockopt;
    int                  error;

    tc_info = arg;
    sockopt = &tc_info->tci_cfg->tcim_sockopt;

    /* We need to pass NULL in order for tcp_listen_v4 to allocate one
     * for us.
     */
    server_tcb = NULL;

    /* Listen on the specified address + port. */
    error = tcp_listen_v4(&server_tcb, eth_port, src_ip, src_port,
                          test_case_id,
                          tc_info->tci_cfg->tcim_app_id,
                          sockopt,
                          TCG_CB_CONSUME_ALL_DATA);
    if (unlikely(error)) {
        TEST_NOTIF(TEST_NOTIF_SERVER_FAILED, NULL, test_case_id, eth_port);
    } else {
        TEST_NOTIF(TEST_NOTIF_SERVER_UP, NULL, test_case_id, eth_port);

        /* Initialize the server state machine. */
        test_server_sm_initialize(&server_tcb->tcb_l4, tc_info);
    }
}

/*****************************************************************************
 * test_case_start_udp_server()
 ****************************************************************************/
static void
test_case_start_udp_server(uint32_t lcore __rte_unused,
                           uint32_t eth_port, uint32_t test_case_id,
                           uint32_t src_ip, uint32_t dst_ip __rte_unused,
                           uint16_t src_port, uint16_t dst_port __rte_unused,
                           uint32_t conn_hash __rte_unused,
                           void *arg)
{
    udp_control_block_t *server_ucb;
    test_case_info_t    *tc_info;
    sockopt_t           *sockopt;
    int                  error;

    tc_info = arg;
    sockopt = &tc_info->tci_cfg->tcim_sockopt;

    /* We need to pass NULL in order for udp_listen_v4 to allocate one
     * for us.
     */
    server_ucb = NULL;

    /* Listen on the first specified address + port. */
    error = udp_listen_v4(&server_ucb, eth_port, src_ip, src_port,
                          test_case_id,
                          tc_info->tci_cfg->tcim_app_id,
                          sockopt,
                          0);
    if (unlikely(error)) {
        TEST_NOTIF(TEST_NOTIF_SERVER_FAILED, NULL, test_case_id, eth_port);
    } else {
        TEST_NOTIF(TEST_NOTIF_SERVER_UP, NULL, test_case_id, eth_port);

        /* Initialize the server state machine. */
        test_server_sm_initialize(&server_ucb->ucb_l4, tc_info);
    }
}

/*****************************************************************************
 * test_case_start_tcp_client()
 ****************************************************************************/
static void
test_case_start_tcp_client(uint32_t lcore,
                           uint32_t eth_port, uint32_t test_case_id,
                           uint32_t src_ip, uint32_t dst_ip,
                           uint16_t src_port, uint16_t dst_port,
                           uint32_t conn_hash,
                           void *arg)
{
    tcp_control_block_t *tcb;
    test_case_info_t    *tc_info;
    tpg_app_proto_t      app_id;
    sockopt_t           *sockopt;

    tc_info = arg;
    app_id = tc_info->tci_cfg->tcim_app_id;
    sockopt = &tc_info->tci_cfg->tcim_sockopt;

    tcb = tlkp_alloc_tcb();
    if (unlikely(tcb == NULL)) {
        RTE_LOG(ERR, USER1, "[%d:%s()] Failed to allocate TCB.\n",
                rte_lcore_index(lcore), __func__);
        return;
    }

    tlkp_init_tcb_client(tcb, src_ip, dst_ip, src_port, dst_port, conn_hash,
                         eth_port, test_case_id,
                         app_id, sockopt,
                         (TPG_CB_USE_L4_HASH_FLAG | TCG_CB_CONSUME_ALL_DATA));

    /* Initialize the application state. */
    APP_CL_CALL(init, app_id)(&tcb->tcb_l4.l4cb_app_data, tc_info->tci_cfg);

    /* Initialize the client state machine. */
    test_client_sm_initialize(&tcb->tcb_l4, tc_info);
}

/*****************************************************************************
 * test_case_start_udp_client()
 ****************************************************************************/
static void
test_case_start_udp_client(uint32_t lcore,
                           uint32_t eth_port, uint32_t test_case_id,
                           uint32_t src_ip, uint32_t dst_ip,
                           uint16_t src_port, uint16_t dst_port,
                           uint32_t conn_hash,
                           void *arg)
{
    udp_control_block_t *ucb;
    test_case_info_t    *tc_info;
    tpg_app_proto_t      app_id;
    sockopt_t           *sockopt;

    tc_info = arg;
    app_id = tc_info->tci_cfg->tcim_app_id;
    sockopt = &tc_info->tci_cfg->tcim_sockopt;

    ucb = tlkp_alloc_ucb();
    if (unlikely(ucb == NULL)) {
        RTE_LOG(ERR, USER1, "[%d:%s()] Failed to allocate UCB.\n",
                rte_lcore_index(lcore),
                __func__);
        return;
    }

    tlkp_init_ucb_client(ucb, src_ip, dst_ip, src_port, dst_port, conn_hash,
                         eth_port, test_case_id,
                         app_id, sockopt,
                         (TPG_CB_USE_L4_HASH_FLAG | 0));

    /* Initialize the application state. */
    APP_CL_CALL(init, app_id)(&ucb->ucb_l4.l4cb_app_data, tc_info->tci_cfg);

    /* Initialize the client state machine. */
    test_client_sm_initialize(&ucb->ucb_l4, tc_info);
}

/*****************************************************************************
 * Static functions for Running test cases.
 ****************************************************************************/
/*****************************************************************************
 * test_case_execute_tcp_send()
 ****************************************************************************/
static uint32_t
test_case_execute_tcp_send(test_case_info_t *tc_info, uint32_t to_send_cnt)
{
    test_oper_state_t    *ts = &tc_info->tci_state;
    uint32_t              sent_cnt;
    uint32_t              sent_real_cnt;
    tpg_test_case_type_t  tc_type = tc_info->tci_cfg->tcim_type;

    for (sent_cnt = 0, sent_real_cnt = 0;
            !TEST_CBQ_EMPTY(&ts->tos_to_send_cbs) &&
                sent_real_cnt < to_send_cnt;
            sent_real_cnt++) {
        int                  error;
        struct rte_mbuf     *data_mbuf;
        uint32_t             data_sent = 0;
        tcp_control_block_t *tcb;
        tpg_app_proto_t      app_id;

        tcb = TEST_CBQ_FIRST(&ts->tos_to_send_cbs, tcp_control_block_t, tcb_l4);
        app_id = tcb->tcb_l4.l4cb_app_data.ad_type;

        data_mbuf = APP_CALL(send, tc_type, app_id)(&tcb->tcb_l4,
                                                    &tcb->tcb_l4.l4cb_app_data,
                                                    tc_info->tci_app_stats,
                                                    TCB_AVAIL_SEND(tcb));
        if (unlikely(data_mbuf == NULL)) {
            TEST_NOTIF_TCB(TEST_NOTIF_DATA_NULL, tcb);

            /* Move at the end to try again later. */
            TEST_CBQ_REM_TO_SEND(ts, &tcb->tcb_l4);
            TEST_CBQ_ADD_TO_SEND(ts, &tcb->tcb_l4);
            continue;
        }

        /* Try to send.
         * if we sent something then let the APP know
         * else if app still needs to send move at end of list
         * else do-nothing as the TEST state machine moved us already to
         * TSTS_NO_SND_WIN.
         */
        error = tcp_send_v4(tcb, data_mbuf, TCG_SEND_PSH,
                            0, /* Timeout */
                            &data_sent);
        if (unlikely(error)) {
            TEST_NOTIF_TCB(TEST_NOTIF_DATA_FAILED, tcb);

            if (test_sm_has_data_pending(&tcb->tcb_l4, tc_info)) {
                /* Move at the end to try again later. */
                TEST_CBQ_REM_TO_SEND(ts, &tcb->tcb_l4);
                TEST_CBQ_ADD_TO_SEND(ts, &tcb->tcb_l4);
                continue;
            }
        }

        if (likely(data_sent != 0)) {
            bool msg_done;

            msg_done = APP_CALL(data_sent,
                                tc_type, app_id)(&tcb->tcb_l4,
                                                 &tcb->tcb_l4.l4cb_app_data,
                                                 tc_info->tci_app_stats,
                                                 data_sent);
            /* We increment the sent count only if the application managed to
             * transmit a whole message.
             */
            if (msg_done)
                sent_cnt++;

        }
    }

    TRACE_FMT(TST, DEBUG, "TCP_SEND_WAIT data cnt %"PRIu32, sent_cnt);

    /* TODO: Duplicated in test_case_execute_udp_send. */

    /* Update the transaction send rate. */
    tc_info->tci_rate_stats->tcrs_data_per_s += sent_cnt;

    /* Return the number of individual sent packets (not transactions!). */
    return sent_real_cnt;
}

/*****************************************************************************
 * test_case_execute_tcp_close()
 ****************************************************************************/
static uint32_t
test_case_execute_tcp_close(test_case_info_t *tc_info, uint32_t to_close_cnt)
{
    test_oper_state_t   *ts = &tc_info->tci_state;
    tcp_control_block_t *tcb;
    uint32_t             closed_cnt = 0;

    /* Stop a batch of clients from the to_close list. */
    while (!TEST_CBQ_EMPTY(&ts->tos_to_close_cbs) && closed_cnt < to_close_cnt) {

        tcb = TEST_CBQ_FIRST(&ts->tos_to_close_cbs, tcp_control_block_t,
                             tcb_l4);

        /* Warning! CLOSE will change the TCB state which will cause it to be
         * removed from the to_close list. No need to do it ourselves.
         * The state change will also notify the application that the connection
         * is going down.
         */
        tcp_close_connection(tcb, 0);
        closed_cnt++;

        TRACE_FMT(TST, DEBUG, "Stop client: "
                  "tcb=%p, eth_port=%"PRIu32", ip src/dst=%8.8X/%8.8X, "
                  "port src/dst=%"PRIu16"/%"PRIu16,
                  tcb,
                  tcb->tcb_l4.l4cb_interface,
                  tcb->tcb_l4.l4cb_src_addr.ip_v4,
                  tcb->tcb_l4.l4cb_dst_addr.ip_v4,
                  tcb->tcb_l4.l4cb_src_port,
                  tcb->tcb_l4.l4cb_dst_port);
    }

    TRACE_FMT(TST, DEBUG, "TCP_CLOSE closed cnt %"PRIu32, closed_cnt);

    return closed_cnt;
}

/*****************************************************************************
 * test_case_execute_tcp_open()
 ****************************************************************************/
static uint32_t
test_case_execute_tcp_open(test_case_info_t *tc_info, uint32_t to_open_cnt)
{
    test_oper_state_t   *ts = &tc_info->tci_state;
    tcp_control_block_t *tcb;
    uint32_t             opened_cnt = 0;
    int                  error;

    /* Start a batch of clients from the to_open list. */
    while (!TEST_CBQ_EMPTY(&ts->tos_to_open_cbs) && opened_cnt < to_open_cnt) {

        tcb = TEST_CBQ_FIRST(&ts->tos_to_open_cbs, tcp_control_block_t,
                             tcb_l4);

        /* OPEN will change the TCB state and cause it to be removed from the
         * to_send list. Careful here!!
         * Once the connection reaches Established the application layer will
         * be notified.
         */
        error = tcp_open_v4_connection(&tcb, tcb->tcb_l4.l4cb_interface,
                                       tcb->tcb_l4.l4cb_src_addr.ip_v4,
                                       tcb->tcb_l4.l4cb_src_port,
                                       tcb->tcb_l4.l4cb_dst_addr.ip_v4,
                                       tcb->tcb_l4.l4cb_dst_port,
                                       tcb->tcb_l4.l4cb_test_case_id,
                                       tcb->tcb_l4.l4cb_app_data.ad_type,
                                       NULL,
                                       TCG_CB_REUSE_CB);

        if (unlikely(error)) {
            TEST_NOTIF_TCB(TEST_NOTIF_CLIENT_FAILED, tcb);
            /* Readd to the open list and try again later. */
            TEST_CBQ_ADD_TO_OPEN(ts, &tcb->tcb_l4);
        } else {
            TEST_NOTIF_TCB(TEST_NOTIF_CLIENT_UP, tcb);
        }

        opened_cnt++;

        TRACE_FMT(TST, DEBUG, "Start client: "
                  "tcb=%p, eth_port=%"PRIu32", ip src/dst=%8.8X/%8.8X, "
                  "port src/dst=%"PRIu16"/%"PRIu16", result=%s(%d)",
                  tcb,
                  tcb->tcb_l4.l4cb_interface,
                  tcb->tcb_l4.l4cb_src_addr.ip_v4,
                  tcb->tcb_l4.l4cb_dst_addr.ip_v4,
                  tcb->tcb_l4.l4cb_src_port,
                  tcb->tcb_l4.l4cb_dst_port,
                  rte_strerror(-error),
                  -error);
    }
    TRACE_FMT(TST, DEBUG, "TCP_OPEN start cnt %"PRIu32, opened_cnt);

    return opened_cnt;
}

/*****************************************************************************
 * test_case_execute_udp_send()
 ****************************************************************************/
static uint32_t
test_case_execute_udp_send(test_case_info_t *tc_info, uint32_t to_send_cnt)
{
    test_oper_state_t    *ts = &tc_info->tci_state;
    uint32_t              sent_cnt;
    uint32_t              sent_real_cnt;
    tpg_test_case_type_t  tc_type = tc_info->tci_cfg->tcim_type;

    for (sent_cnt = 0, sent_real_cnt = 0;
            !TEST_CBQ_EMPTY(&ts->tos_to_send_cbs) &&
                sent_real_cnt < to_send_cnt;
            sent_real_cnt++) {
        int                  error;
        struct rte_mbuf     *data_mbuf;
        uint32_t             data_sent = 0;
        udp_control_block_t *ucb;
        tpg_app_proto_t      app_id;

        ucb = TEST_CBQ_FIRST(&ts->tos_to_send_cbs, udp_control_block_t, ucb_l4);
        app_id = ucb->ucb_l4.l4cb_app_data.ad_type;

        data_mbuf = APP_CALL(send, tc_type, app_id)(&ucb->ucb_l4,
                                                    &ucb->ucb_l4.l4cb_app_data,
                                                    tc_info->tci_app_stats,
                                                    UCB_MTU(ucb));
        if (unlikely(data_mbuf == NULL)) {
            TEST_NOTIF_UCB(TEST_NOTIF_DATA_NULL, ucb);

            /* Move at the end and try again later. */
            TEST_CBQ_REM_TO_SEND(ts, &ucb->ucb_l4);
            TEST_CBQ_ADD_TO_SEND(ts, &ucb->ucb_l4);
            continue;
        }


        /* Try to send.
         * if we sent something then let the APP know
         * else if app still needs to send move at end of list
         * else do-nothing as the TEST state machine moved us already to
         * TSTS_NO_SND_WIN.
         */
        error = udp_send_v4(ucb, data_mbuf, &data_sent);
        if (unlikely(error)) {
            TEST_NOTIF_UCB(TEST_NOTIF_DATA_FAILED, ucb);

            if (test_sm_has_data_pending(&ucb->ucb_l4, tc_info)) {
                /* Move at the end to try again later. */
                TEST_CBQ_REM_TO_SEND(ts, &ucb->ucb_l4);
                TEST_CBQ_ADD_TO_SEND(ts, &ucb->ucb_l4);
                continue;
            }
        }

        if (likely(data_sent != 0)) {
            bool msg_done;

            msg_done = APP_CALL(data_sent,
                                tc_type, app_id)(&ucb->ucb_l4,
                                                 &ucb->ucb_l4.l4cb_app_data,
                                                 tc_info->tci_app_stats,
                                                 data_sent);
            /* We increment the sent count only if the application managed to
             * transmit a whole message.
             */
            if (msg_done)
                sent_cnt++;

        }
    }
    TRACE_FMT(TST, DEBUG, "UDP_SEND data cnt %"PRIu32, sent_cnt);

    /* TODO: Duplicated in test_case_execute_tcp_send. */

    /* Update the transaction send rate. */
    tc_info->tci_rate_stats->tcrs_data_per_s += sent_cnt;

    /* Return the number of individual sent packets (not transactions!). */
    return sent_real_cnt;
}

/*****************************************************************************
 * test_case_execute_udp_close()
 ****************************************************************************/
static uint32_t
test_case_execute_udp_close(test_case_info_t *tc_info, uint32_t to_close_cnt)
{
    test_oper_state_t   *ts = &tc_info->tci_state;
    udp_control_block_t *ucb;
    uint32_t             closed_cnt = 0;

    /* Stop a batch of clients from the to_close list. */
    while (!TEST_CBQ_EMPTY(&ts->tos_to_close_cbs) && closed_cnt < to_close_cnt) {

        ucb = TEST_CBQ_FIRST(&ts->tos_to_close_cbs, udp_control_block_t,
                             ucb_l4);

        /* Warning! CLOSE will change the TCB state which will cause it to be
         * removed from the to_close list. No need to do it ourselves.
         * The state change will also notify the application that the connection
         * is going down.
         */
        udp_close_v4(ucb);
        closed_cnt++;

        TRACE_FMT(TST, DEBUG, "Stop client: "
                  "ucb=%p, eth_port=%"PRIu32", ip src/dst=%8.8X/%8.8X, "
                  "port src/dst=%"PRIu16"/%"PRIu16,
                  ucb,
                  ucb->ucb_l4.l4cb_interface,
                  ucb->ucb_l4.l4cb_src_addr.ip_v4,
                  ucb->ucb_l4.l4cb_dst_addr.ip_v4,
                  ucb->ucb_l4.l4cb_src_port,
                  ucb->ucb_l4.l4cb_dst_port);
    }

    TRACE_FMT(TST, DEBUG, "UDP_CLOSE closed cnt %"PRIu32, closed_cnt);

    return closed_cnt;
}

/*****************************************************************************
 * test_case_execute_udp_open()
 ****************************************************************************/
static uint32_t
test_case_execute_udp_open(test_case_info_t *tc_info, uint32_t to_open_cnt)
{
    test_oper_state_t   *ts = &tc_info->tci_state;
    udp_control_block_t *ucb;
    uint32_t             opened_cnt = 0;
    int                  error;

    /* Start a batch of clients from the to_open list. */
    while (!TEST_CBQ_EMPTY(&ts->tos_to_open_cbs) && opened_cnt < to_open_cnt) {

        ucb = TEST_CBQ_FIRST(&ts->tos_to_open_cbs, udp_control_block_t, ucb_l4);

        /* OPEN will change the TCB state and cause it to be removed from the
         * to_send list. Careful here!!
         * Once the connection reaches Established the application layer will
         * be notified.
         */
        error = udp_open_v4_connection(&ucb, ucb->ucb_l4.l4cb_interface,
                                       ucb->ucb_l4.l4cb_src_addr.ip_v4,
                                       ucb->ucb_l4.l4cb_src_port,
                                       ucb->ucb_l4.l4cb_dst_addr.ip_v4,
                                       ucb->ucb_l4.l4cb_dst_port,
                                       ucb->ucb_l4.l4cb_test_case_id,
                                       ucb->ucb_l4.l4cb_app_data.ad_type,
                                       NULL,
                                       TCG_CB_REUSE_CB);

        if (unlikely(error)) {
            TEST_NOTIF_UCB(TEST_NOTIF_CLIENT_FAILED, ucb);
            /* Readd to the open list and try again later. */
            TEST_CBQ_ADD_TO_OPEN(ts, &ucb->ucb_l4);
        } else {
            TEST_NOTIF_UCB(TEST_NOTIF_CLIENT_UP, ucb);
        }

        opened_cnt++;

        TRACE_FMT(TST, DEBUG, "Start client: "
                  "ucb=%p, eth_port=%"PRIu32", ip src/dst=%8.8X/%8.8X, "
                  "port src/dst=%"PRIu16"/%"PRIu16", result=%s(%d)",
                  ucb,
                  ucb->ucb_l4.l4cb_interface,
                  ucb->ucb_l4.l4cb_src_addr.ip_v4,
                  ucb->ucb_l4.l4cb_dst_addr.ip_v4,
                  ucb->ucb_l4.l4cb_src_port,
                  ucb->ucb_l4.l4cb_dst_port,
                  rte_strerror(-error),
                  -error);
    }
    TRACE_FMT(TST, DEBUG, "TCP_OPEN start cnt %"PRIu32, opened_cnt);

    return opened_cnt;
}


/*****************************************************************************
 * test_case_run_open_clients()
 ****************************************************************************/
static int test_case_run_open_clients(test_case_info_t *tc_info,
                                      test_case_runner_cb_t runner)
{
    test_oper_state_t *ts = &tc_info->tci_state;
    test_rate_state_t *rate_state = &ts->tos_rates;

    rate_limit_consume(&rate_state->trs_open,
                       runner(tc_info,
                              rate_limit_available(&rate_state->trs_open)));

    /* If we still didn't reach the expected rate for this
     * interval then we should repost if we still have cbs in the list.
     */
    if (likely(!rate_limit_reached(&rate_state->trs_open))) {

        if (TEST_CBQ_EMPTY(&ts->tos_to_open_cbs)) {
            rate_state->trs_open_in_progress = false;
            return 0;
        }
        return -EAGAIN;
    }

    rate_state->trs_open_rate_reached = true;

    /* Otherwise stop. */
    rate_state->trs_open_in_progress = false;
    return 0;
}

/*****************************************************************************
 * test_case_run_close_clients()
 ****************************************************************************/
static int test_case_run_close_clients(test_case_info_t *tc_info,
                                       test_case_runner_cb_t runner)
{
    test_oper_state_t *ts = &tc_info->tci_state;
    test_rate_state_t *rate_state = &ts->tos_rates;

    rate_limit_consume(&rate_state->trs_close,
                       runner(tc_info,
                              rate_limit_available(&rate_state->trs_close)));

    /* If we still didn't reach the expected rate for this
     * interval then we should repost if we still have cbs in the list.
     */
    if (likely(!rate_limit_reached(&rate_state->trs_close))) {

        if (TEST_CBQ_EMPTY(&ts->tos_to_close_cbs)) {
            rate_state->trs_close_in_progress = false;
            return 0;
        }
        return -EAGAIN;
    }

    rate_state->trs_close_rate_reached = true;

    /* Otherwise stop. */
    rate_state->trs_close_in_progress = false;
    return 0;
}

/*****************************************************************************
 * test_case_run_send_clients()
 ****************************************************************************/
static int test_case_run_send_clients(test_case_info_t *tc_info,
                                      test_case_runner_cb_t runner)
{
    test_oper_state_t *ts = &tc_info->tci_state;
    test_rate_state_t *rate_state = &ts->tos_rates;

    rate_limit_consume(&rate_state->trs_send,
                       runner(tc_info,
                              rate_limit_available(&rate_state->trs_send)));

    /* If we still didn't reach the expected rate for this
     * interval then we should repost if we still have cbs in the list.
     */
    if (likely(!rate_limit_reached(&rate_state->trs_send))) {

        if (TEST_CBQ_EMPTY(&ts->tos_to_send_cbs)) {
            rate_state->trs_send_in_progress = false;
            return 0;
        }
        return -EAGAIN;
    }

    rate_state->trs_send_rate_reached = true;

    /* Otherwise stop. */
    rate_state->trs_send_in_progress = false;
    return 0;
}

/*****************************************************************************
 * test_tcp_purge_tcb()
 ****************************************************************************/
static void test_tcp_purge_tcb(tcp_control_block_t *tcb)
{
    bool to_free = (!tcb->tcb_malloced);

    if (L4CB_TEST_TMR_IS_SET(&tcb->tcb_l4))
        L4CB_TEST_TMR_CANCEL(&tcb->tcb_l4);

    if (tcb->tcb_state != TS_INIT && tcb->tcb_state != TS_CLOSED)
        tcp_close_connection(tcb, TCG_SILENT_CLOSE);

    if (to_free)
        tcp_connection_cleanup(tcb);
}

/*****************************************************************************
 * test_udp_purge_ucb()
 ****************************************************************************/
static void test_udp_purge_ucb(udp_control_block_t *ucb)
{
    bool to_free = (!ucb->ucb_malloced);

    if (L4CB_TEST_TMR_IS_SET(&ucb->ucb_l4))
        L4CB_TEST_TMR_CANCEL(&ucb->ucb_l4);

    if (ucb->ucb_state != US_INIT && ucb->ucb_state != US_CLOSED)
        udp_close_v4(ucb);

    if (to_free)
        udp_connection_cleanup(ucb);
}

/*****************************************************************************
 * test_tcp_purge_list()
 ****************************************************************************/
static uint32_t test_tcp_purge_list(tlkp_test_cb_list_t *cbs)
{
    uint32_t purge_cnt = 0;

    while (!TEST_CBQ_EMPTY(cbs)) {
        tcp_control_block_t *tcb;

        purge_cnt++;

        tcb = TEST_CBQ_FIRST(cbs, tcp_control_block_t, tcb_l4);
        TEST_CBQ_REM(cbs, &tcb->tcb_l4);
        test_tcp_purge_tcb(tcb);
    }

    return purge_cnt;
}

/*****************************************************************************
 * test_udp_purge_list()
 ****************************************************************************/
static uint32_t test_udp_purge_list(tlkp_test_cb_list_t *cbs)
{
    uint32_t purge_cnt = 0;

    while (!TEST_CBQ_EMPTY(cbs)) {
        udp_control_block_t *ucb;

        purge_cnt++;

        ucb = TEST_CBQ_FIRST(cbs, udp_control_block_t, ucb_l4);
        TEST_CBQ_REM(cbs, &ucb->ucb_l4);
        test_udp_purge_ucb(ucb);
    }

    return purge_cnt;
}

/*****************************************************************************
 * test_case_purge_tcp_cbs()
 ****************************************************************************/
static uint32_t
test_case_purge_tcp_cbs(test_case_info_t *tc_info, test_case_init_msg_t *tc_cfg)
{
    uint32_t purge_cnt = 0;

    bool tcp_purge_htable_cb(l4_control_block_t *cb, void *arg __rte_unused)
    {
        if (cb->l4cb_test_case_id != tc_cfg->tcim_test_case_id)
            return true;

        purge_cnt++;

        test_tcp_purge_tcb(container_of(cb, tcp_control_block_t, tcb_l4));
        return true;
    }

    purge_cnt += test_tcp_purge_list(&tc_info->tci_state.tos_to_init_cbs);
    purge_cnt += test_tcp_purge_list(&tc_info->tci_state.tos_to_open_cbs);
    purge_cnt += test_tcp_purge_list(&tc_info->tci_state.tos_to_close_cbs);
    purge_cnt += test_tcp_purge_list(&tc_info->tci_state.tos_to_send_cbs);
    purge_cnt += test_tcp_purge_list(&tc_info->tci_state.tos_closed_cbs);
    tlkp_walk_tcb(tc_cfg->tcim_eth_port, tcp_purge_htable_cb, NULL);

    return purge_cnt;
}

/*****************************************************************************
 * test_case_purge_udp_cbs()
 ****************************************************************************/
static uint32_t
test_case_purge_udp_cbs(test_case_info_t *tc_info, test_case_init_msg_t *tc_cfg)
{
    uint32_t purge_cnt = 0;

    bool udp_purge_htable_cb(l4_control_block_t *cb, void *arg __rte_unused)
    {
        if (cb->l4cb_test_case_id != tc_cfg->tcim_test_case_id)
            return true;

        purge_cnt++;

        test_udp_purge_ucb(container_of(cb, udp_control_block_t, ucb_l4));
        return true;
    }

    purge_cnt += test_udp_purge_list(&tc_info->tci_state.tos_to_init_cbs);
    purge_cnt += test_udp_purge_list(&tc_info->tci_state.tos_to_open_cbs);
    purge_cnt += test_udp_purge_list(&tc_info->tci_state.tos_to_close_cbs);
    purge_cnt += test_udp_purge_list(&tc_info->tci_state.tos_to_send_cbs);
    purge_cnt += test_udp_purge_list(&tc_info->tci_state.tos_closed_cbs);
    tlkp_walk_ucb(tc_cfg->tcim_eth_port, udp_purge_htable_cb, NULL);

    return purge_cnt;
}

/*****************************************************************************
 * test_case_purge_cbs()
 ****************************************************************************/
static void
test_case_purge_cbs(test_case_info_t *tc_info, test_case_init_msg_t *tc_cfg)
{
    uint32_t purge_cnt;

    switch (tc_cfg->tcim_l4_type) {
    case L4_PROTO__TCP:
        purge_cnt = test_case_purge_tcp_cbs(tc_info, tc_cfg);
        break;
    case L4_PROTO__UDP:
        purge_cnt = test_case_purge_udp_cbs(tc_info, tc_cfg);
        break;
    default:
        assert(false);
        break;
    }

    RTE_LOG(INFO, USER1, "[%d:%s()] purged %u tcid %u\n", rte_lcore_index(-1),
            __func__,
            purge_cnt, tc_cfg->tcim_test_case_id);
}

/*****************************************************************************
 * Test Message Handlers which will run on the packet threads.
 ****************************************************************************/
/*****************************************************************************
 * test_case_init_cb()
 ****************************************************************************/
static int test_case_init_cb(uint16_t msgid, uint16_t lcore, void *msg)
{
    test_case_init_msg_t *im;
    uint32_t              eth_port;
    uint32_t              tcid;
    test_case_info_t     *tc_info;
    tpg_test_case_type_t  tc_type;
    tpg_l4_proto_t        l4_proto;
    tpg_app_proto_t       app_id;


    static struct {
        test_case_runner_cb_t open;
        test_case_runner_cb_t close;
        test_case_runner_cb_t send;
        test_case_close_cb_t  sess_close;
    } start_callbacks[TEST_CASE_TYPE__MAX][L4_PROTO__L4_PROTO_MAX] = {
        [TEST_CASE_TYPE__SERVER][L4_PROTO__TCP] = {
            .open = NULL,
            .close = NULL,
            .send = test_case_execute_tcp_send,
            .sess_close = test_case_tcp_close,
        },
        [TEST_CASE_TYPE__SERVER][L4_PROTO__UDP] = {
            .open = NULL,
            .close = NULL,
            .send = test_case_execute_udp_send,
            .sess_close = test_case_udp_close,
        },
        [TEST_CASE_TYPE__CLIENT][L4_PROTO__TCP] = {
            .open = test_case_execute_tcp_open,
            .close = test_case_execute_tcp_close,
            .send = test_case_execute_tcp_send,
            .sess_close = test_case_tcp_close,
        },
        [TEST_CASE_TYPE__CLIENT][L4_PROTO__UDP] = {
            .open = test_case_execute_udp_open,
            .close = test_case_execute_udp_close,
            .send = test_case_execute_udp_send,
            .sess_close = test_case_udp_close,
        },
    };

    if (MSG_INVALID(msgid, msg, MSG_TEST_CASE_INIT))
        return -EINVAL;

    im = msg;

    eth_port = im->tcim_eth_port;
    tcid     = im->tcim_test_case_id;
    tc_type  = im->tcim_type;
    app_id   = im->tcim_app_id;
    l4_proto = im->tcim_l4_type;

    /* If the requested port is not handled by this core just ignore. */
    if (port_get_rx_queue_id(lcore, eth_port) == CORE_PORT_QINVALID)
        return 0;

    tc_info = TEST_GET_INFO(eth_port, tcid);

    /* If already configured then just ignore for now. */
    if (tc_info->tci_configured)
        return 0;

    /* Struct copy of the configuration. */
    *tc_info->tci_cfg = *im;

    if (tc_info->tci_cfg->tcim_tx_tstamp) {
        tstamp_tx_post_cb_t cb = NULL;

        /* TODO: set callback ipv4 recalc if defined(TPG_SW_CHECKSUMMING) or
         * when the NIC doesn't support checksum offload.
         * cb = ipv4_recalc_tstamp_cksum(mbuf, offset, size);
         */
        tstamp_start_tx(eth_port, port_get_tx_queue_id(lcore, eth_port), cb);
    }

    if (tc_info->tci_cfg->tcim_rx_tstamp) {
        tstamp_start_rx(eth_port, port_get_rx_queue_id(lcore, eth_port));
        test_case_latency_init(tc_info);

        /* Initialize latency buffer. */
        test_latency_state_init(tc_info->tci_latency_state,
                                tc_info->tci_cfg->tcim_latency.has_tcs_samples ?
                                tc_info->tci_cfg->tcim_latency.tcs_samples : 0);
    }

    /* Initialize operational part */
    test_case_init_state(lcore, tc_info->tci_cfg, &tc_info->tci_state,
                         &tc_info->tci_rate_timers);

    /* Initialize stats. */
    bzero(tc_info->tci_general_stats, sizeof(*tc_info->tci_general_stats));
    bzero(tc_info->tci_rate_stats, sizeof(*tc_info->tci_rate_stats));
    bzero(tc_info->tci_app_stats, sizeof(*tc_info->tci_app_stats));

    /* Let the application layer know that the test is starting. The application
     * will initialize its "global" per test case state.
     */
    APP_CALL(tc_start, tc_type, app_id)(tc_info->tci_cfg);

    /* Initialize the run callbacks. */
    test_case_init_callbacks(tc_info,
                             start_callbacks[tc_type][l4_proto].open,
                             start_callbacks[tc_type][l4_proto].close,
                             start_callbacks[tc_type][l4_proto].send,
                             start_callbacks[tc_type][l4_proto].sess_close);

    /* Initialize clients and servers. */

    /* WARNING: we could include test_case_start_tcp/udp_server/client in
     * the start_callbacks array but then the compiler would most likely
     * refuse to inline them inside the test_case_for_each_client/server
     * functions. To avoid that we do the (ugly) switch on l4 proto..
     */
    switch (tc_type) {
    case TEST_CASE_TYPE__CLIENT:
        switch (l4_proto) {
        case L4_PROTO__TCP:
            test_case_for_each_client(lcore, tc_info->tci_cfg,
                                      test_case_start_tcp_client,
                                      tc_info);
            break;
        case L4_PROTO__UDP:
            test_case_for_each_client(lcore, tc_info->tci_cfg,
                                      test_case_start_udp_client,
                                      tc_info);
            break;
        default:
            assert(false);
            return -EINVAL;
        }
        break;
    case TEST_CASE_TYPE__SERVER:
        switch (l4_proto) {
        case L4_PROTO__TCP:
            test_case_for_each_server(lcore, tc_info->tci_cfg,
                                      test_case_start_tcp_server,
                                      tc_info);
            break;
        case L4_PROTO__UDP:
            test_case_for_each_server(lcore, tc_info->tci_cfg,
                                      test_case_start_udp_server,
                                      tc_info);
            break;
        default:
            assert(false);
            return -EINVAL;
        }
        break;
    default:
        assert(false);
        return -EINVAL;
    }

    tc_info->tci_configured = true;

    return 0;
}

/*****************************************************************************
 * test_case_latency_init()
 ****************************************************************************/
void test_case_latency_init(test_case_info_t *tc_info)
{
    /* We don't bzero "test_oper_latency_state_t" we choose to keep recent
     * stats updated here instead than mgmt core.
     */
    bzero(&tc_info->tci_general_stats->tcs_latency_stats,
          sizeof(tc_info->tci_general_stats->tcs_latency_stats));
    tc_info->tci_general_stats->tcs_latency_stats.tcls_stats.ls_min_latency =
        UINT32_MAX;
    tc_info->tci_general_stats->tcs_latency_stats.tcls_sample_stats
        .ls_min_latency = UINT32_MAX;
}

/*****************************************************************************
 * test_case_start_cb()
 ****************************************************************************/
static int test_case_start_cb(uint16_t msgid, uint16_t lcore, void *msg)
{
    test_case_start_msg_t *sm;
    test_case_info_t      *tc_info;

    if (MSG_INVALID(msgid, msg, MSG_TEST_CASE_START))
        return -EINVAL;

    sm = msg;

    /* If the requested port is not handled by this core just ignore. */
    if (port_get_rx_queue_id(lcore, sm->tcsm_eth_port) == CORE_PORT_QINVALID) {
        RTE_LOG(ERR, USER1,
                "[%d:%s()] Received TestCase START on a core that's not handling this port!\n",
                rte_lcore_index(lcore), __func__);
        return 0;
    }

    tc_info = TEST_GET_INFO(sm->tcsm_eth_port, sm->tcsm_test_case_id);

    /* If already running then just ignore for now. */
    if (tc_info->tci_running) {
        RTE_LOG(ERR, USER1,
                "[%d:%s()] Received TestCase START for a test case already running on this port!\n",
                rte_lcore_index(lcore), __func__);
        return 0;
    }

    /* Store the initial start timestamp. */
    tc_info->tci_rate_stats->tcrs_start_time = rte_get_timer_cycles();

    /* Start the rate limiting engine. */
    test_case_rate_state_start(lcore, sm->tcsm_eth_port, sm->tcsm_test_case_id,
                               &tc_info->tci_state,
                               &tc_info->tci_rate_timers);

    /* Safe to mark the test as running. We shouldn't fail from this
     * point on..
     */
    tc_info->tci_running = true;

    return 0;
}

/*****************************************************************************
 * test_case_run_open_cb()
 ****************************************************************************/
static int test_case_run_open_cb(uint16_t msgid, uint16_t lcore __rte_unused,
                                 void *msg)
{
    test_case_run_msg_t *rm;
    test_case_info_t    *tc_info;

    if (MSG_INVALID(msgid, msg, MSG_TEST_CASE_RUN_OPEN))
        return -EINVAL;

    rm = msg;
    tc_info = TEST_GET_INFO(rm->tcrm_eth_port, rm->tcrm_test_case_id);
    return test_case_run_open_clients(tc_info, tc_info->tci_run_open_cb);
}

/*****************************************************************************
 * test_case_run_close_cb()
 ****************************************************************************/
static int test_case_run_close_cb(uint16_t msgid, uint16_t lcore __rte_unused,
                                  void *msg)
{
    test_case_run_msg_t *rm;
    test_case_info_t    *tc_info;

    if (MSG_INVALID(msgid, msg, MSG_TEST_CASE_RUN_CLOSE))
        return -EINVAL;

    rm = msg;
    tc_info = TEST_GET_INFO(rm->tcrm_eth_port, rm->tcrm_test_case_id);
    return test_case_run_close_clients(tc_info, tc_info->tci_run_close_cb);
}

/*****************************************************************************
 * test_case_run_send_cb()
 ****************************************************************************/
static int test_case_run_send_cb(uint16_t msgid, uint16_t lcore __rte_unused,
                                 void *msg)
{
    test_case_run_msg_t *rm;
    test_case_info_t    *tc_info;

    if (MSG_INVALID(msgid, msg, MSG_TEST_CASE_RUN_SEND))
        return -EINVAL;

    rm = msg;

    tc_info = TEST_GET_INFO(rm->tcrm_eth_port, rm->tcrm_test_case_id);
    return test_case_run_send_clients(tc_info, tc_info->tci_run_send_cb);
}

/*****************************************************************************
 * test_case_stop_cb()
 ****************************************************************************/
static int test_case_stop_cb(uint16_t msgid, uint16_t lcore,
                             void *msg)
{
    test_case_stop_msg_t *sm;
    test_case_info_t     *tc_info;
    test_oper_state_t    *tc_state;
    test_rate_timers_t   *tc_rate_timers;
    bool                  all_purged;
    tpg_test_case_type_t  tc_type;
    tpg_app_proto_t       app_id;

    if (MSG_INVALID(msgid, msg, MSG_TEST_CASE_STOP))
        return -EINVAL;

    sm = msg;

    tc_info = TEST_GET_INFO(sm->tcsm_eth_port, sm->tcsm_test_case_id);
    tc_state = &tc_info->tci_state;
    tc_rate_timers = &tc_info->tci_rate_timers;

    if (!tc_info->tci_configured || !tc_info->tci_running) {
        RTE_LOG(ERR, USER1,
                "[%d:%s()] Received TestCase STOP for a test case that's not running on this port!\n",
                rte_lcore_index(rte_lcore_id()), __func__);
        goto done;
    }

    /* If we didn't mark the TC as stopping do it now.
     * Notify all the tcbs that they need to go to CLOSED and get freed once
     * they get there.
     */
    if (!tc_info->tci_stopping) {
        /* Stop the rate limiting engine. */
        test_case_rate_state_stop(lcore, sm->tcsm_eth_port,
                                  sm->tcsm_test_case_id,
                                  tc_state,
                                  tc_rate_timers);

        tc_info->tci_stopping = true;
        all_purged = false;
    } else {
        /* Walk the tcbs for this test and purge them. */
        test_case_purge_cbs(tc_info, tc_info->tci_cfg);
        all_purged = true;
    }

    /* If we still have open/close/send messages being processed we should
     * repost this message to ourselves and wait until the open/close/send
     * messages are processed.
     */
    if (test_case_rate_state_running(tc_state) || !all_purged)
        return -EAGAIN;

    tc_info->tci_stopping = false;
    tc_info->tci_running = false;
    tc_info->tci_configured = false;

    if (tc_info->tci_cfg->tcim_tx_tstamp) {
        tstamp_stop_tx(sm->tcsm_eth_port,
                       port_get_rx_queue_id(lcore, sm->tcsm_eth_port));
    }

    if (tc_info->tci_cfg->tcim_rx_tstamp) {
        tstamp_stop_rx(sm->tcsm_eth_port,
                       port_get_rx_queue_id(lcore, sm->tcsm_eth_port));
    }

    tc_type = tc_info->tci_cfg->tcim_type;
    app_id = tc_info->tci_cfg->tcim_app_id;
    APP_CALL(tc_stop, tc_type, app_id)(tc_info->tci_cfg);

done:
    /* Notify the sender that we're done. */
    *sm->tcsm_done = true;

    return 0;
}

/*****************************************************************************
 * test_case_stats_req_cb()
 ****************************************************************************/
static int test_case_stats_req_cb(uint16_t msgid, uint16_t lcore __rte_unused,
                                  void *msg)
{
    test_case_stats_req_msg_t *sm;
    test_case_info_t          *tc_info;

    if (MSG_INVALID(msgid, msg, MSG_TEST_CASE_STATS_REQ))
        return -EINVAL;

    sm = msg;

    tc_info = TEST_GET_INFO(sm->tcsrm_eth_port, sm->tcsrm_test_case_id);

    /* Here we walk through the buffer in order to fill the recent stats */
    if (tc_info->tci_cfg->tcim_rx_tstamp) {
        tpg_test_case_latency_stats_t *tc_latency_stats;

        tc_latency_stats = &tc_info->tci_general_stats->tcs_latency_stats;

        test_update_recent_latency_stats(&tc_latency_stats->tcls_sample_stats,
                                         tc_info->tci_latency_state,
                                         &tc_info->tci_cfg->tcim_latency);
    }

    /* Struct copy the stats! */
    *sm->tcsrm_test_case_stats = *tc_info->tci_general_stats;
    *sm->tcsrm_test_case_app_stats = *tc_info->tci_app_stats;

    /* Clear the runtime stats. They're aggregated by the test manager.
     * Don't clear the start and end time for the gen stats!
     */
    if (tc_info->tci_cfg->tcim_rx_tstamp)
        test_case_latency_init(tc_info);

    switch (tc_info->tci_cfg->tcim_type) {
    case TEST_CASE_TYPE__SERVER:
        /* Clear the gen stats. */
        bzero(&tc_info->tci_general_stats->tcs_server,
              sizeof(tc_info->tci_general_stats->tcs_server));
        break;
    case TEST_CASE_TYPE__CLIENT:
        /* Clear the gen stats. */
        bzero(&tc_info->tci_general_stats->tcs_client,
              sizeof(tc_info->tci_general_stats->tcs_client));
        break;
    default:
        assert(false);
        return -EINVAL;
    }

    /* Clear the app stats. */
    bzero(tc_info->tci_app_stats, sizeof(*tc_info->tci_app_stats));
    return 0;
}

/*****************************************************************************
 * test_case_rates_stats_req_cb()
 ****************************************************************************/
static int test_case_rates_stats_req_cb(uint16_t msgid,
                                        uint16_t lcore __rte_unused,
                                        void *msg)
{
    test_case_rates_req_msg_t *sm;
    test_case_info_t          *tc_info;
    uint64_t                   now;

    if (MSG_INVALID(msgid, msg, MSG_TEST_CASE_RATES_REQ))
        return -EINVAL;

    sm = msg;

    tc_info = TEST_GET_INFO(sm->tcrrm_eth_port, sm->tcrrm_test_case_id);

    now = rte_get_timer_cycles();
    tc_info->tci_rate_stats->tcrs_end_time = now;

    /* Struct copy the stats! */
    *sm->tcrrm_test_case_rate_stats = *tc_info->tci_rate_stats;

    /* Clear the rates stats. They're aggregated by the test manager. */
    bzero(tc_info->tci_rate_stats, sizeof(*tc_info->tci_rate_stats));

    /* Store the new initial timestamp. */
    tc_info->tci_rate_stats->tcrs_start_time = now;

    return 0;
}

/*****************************************************************************
 * test_fix_recent_latency_stats()
 *      this fun should be call only for recent stats!
 ****************************************************************************/
static void
test_update_recent_latency_stats(tpg_latency_stats_t *stats,
                                 test_oper_latency_state_t *buffer,
                                 tpg_test_case_latency_t *tc_latency)
{
    uint32_t i;

    bzero(stats, sizeof(tpg_latency_stats_t));
    stats->ls_min_latency = UINT32_MAX;

    /* Here I walk trough my whole buffer in order to fix recent stats */
    for (i = 0; i < buffer->tols_actual_length; i++) {
        test_update_latency_stats(stats, buffer->tols_timestamps[i],
                                  tc_latency);
    }

}

/*****************************************************************************
 * test_init()
 ****************************************************************************/
bool test_init(void)
{
    int error;

    while (true) {

        /*
         * Register the handlers for our message types.
         */
        error = msg_register_handler(MSG_TEST_CASE_INIT,
                                     test_case_init_cb);
        if (error)
            break;

        error = msg_register_handler(MSG_TEST_CASE_START,
                                     test_case_start_cb);
        if (error)
            break;

        error = msg_register_handler(MSG_TEST_CASE_RUN_OPEN,
                                     test_case_run_open_cb);
        if (error)
            break;

        error = msg_register_handler(MSG_TEST_CASE_RUN_CLOSE,
                                     test_case_run_close_cb);
        if (error)
            break;

        error = msg_register_handler(MSG_TEST_CASE_RUN_SEND,
                                     test_case_run_send_cb);
        if (error)
            break;

        error = msg_register_handler(MSG_TEST_CASE_STOP,
                                     test_case_stop_cb);
        if (error)
            break;

        error = msg_register_handler(MSG_TEST_CASE_STATS_REQ,
                                     test_case_stats_req_cb);
        if (error)
            break;

        error = msg_register_handler(MSG_TEST_CASE_RATES_REQ,
                                     test_case_rates_stats_req_cb);
        if (error)
            break;

        tcp_notif_cb = test_tcp_udp_notif_handler;
        udp_notif_cb = test_tcp_udp_notif_handler;
        test_notif_cb = test_notif_handler;

        return true;
    }

    RTE_LOG(ERR, USER1, "Failed to register Tests msg handler: %s(%d)\n",
            rte_strerror(-error), -error);
    return false;
}

/*****************************************************************************
 * test_lcore_init_pool()
 * NOTES: this is a bit ugly but this has to be a macro due to the TPG message
 *        infra.
 ****************************************************************************/
#define test_lcore_init_pool(msgpool, name, count, lcore_id)                 \
    do {                                                                     \
        (msgpool) = rte_zmalloc_socket((name), (count) * sizeof(*(msgpool)), \
                                       RTE_CACHE_LINE_SIZE,                  \
                                       rte_lcore_to_socket_id(lcore_id));    \
        if ((msgpool) == NULL) {                                             \
            TPG_ERROR_ABORT("[%d:%s() Failed to allocate %s!\n",             \
                            rte_lcore_index((lcore_id)),                     \
                            __func__,                                        \
                            (name));                                         \
        }                                                                    \
    } while (0)

/*****************************************************************************
 * test_lcore_init()
 *      Notes: Initialize all the pointers within test_case_info_t objects.
 ****************************************************************************/
static void test_lcore_init_test_case_info(void)
{
    uint32_t eth_port;
    uint32_t tcid;

    for (eth_port = 0; eth_port < rte_eth_dev_count(); eth_port++) {
        for (tcid = 0; tcid < TPG_TEST_MAX_ENTRIES; tcid++) {
            test_case_info_t *tc_info = TEST_GET_INFO(eth_port, tcid);

            tc_info->tci_cfg = TEST_GET_CFG(eth_port, tcid);
            tc_info->tci_general_stats = TEST_GET_GEN_STATS(eth_port, tcid);
            tc_info->tci_rate_stats = TEST_GET_RATE_STATS(eth_port, tcid);
            tc_info->tci_app_stats = TEST_GET_APP_STATS(eth_port, tcid);
            tc_info->tci_latency_state = TEST_GET_LATENCY_STATE(eth_port, tcid);
        }
    }
}

/*****************************************************************************
 * test_lcore_init()
 ****************************************************************************/
void test_lcore_init(uint32_t lcore_id)
{
    test_lcore_init_pool(RTE_PER_LCORE(test_case_info),
                         "per_lcore_test_case_info",
                         rte_eth_dev_count() * TPG_TEST_MAX_ENTRIES,
                         lcore_id);

    test_lcore_init_pool(RTE_PER_LCORE(test_case_cfg),
                         "per_lcore_test_case_cfg",
                         rte_eth_dev_count() * TPG_TEST_MAX_ENTRIES,
                         lcore_id);

    test_lcore_init_pool(RTE_PER_LCORE(test_case_gen_stats),
                         "per_lcore_test_case_gen_stats",
                         rte_eth_dev_count() * TPG_TEST_MAX_ENTRIES,
                         lcore_id);
    test_lcore_init_pool(RTE_PER_LCORE(test_case_rate_stats),
                         "per_lcore_test_case_rate_stats",
                         rte_eth_dev_count() * TPG_TEST_MAX_ENTRIES,
                         lcore_id);
    test_lcore_init_pool(RTE_PER_LCORE(test_case_app_stats),
                         "per_lcore_test_case_app_stats",
                         rte_eth_dev_count() * TPG_TEST_MAX_ENTRIES,
                         lcore_id);

    test_lcore_init_pool(RTE_PER_LCORE(test_case_latency_state),
                         "per_lcore_test_case_latency_state",
                         rte_eth_dev_count() * TPG_TEST_MAX_ENTRIES,
                         lcore_id);

    test_lcore_init_pool(RTE_PER_LCORE(test_open_msgpool),
                         "per_lcore_open_msgpool",
                         rte_eth_dev_count() * TPG_TEST_MAX_ENTRIES,
                         lcore_id);
    test_lcore_init_pool(RTE_PER_LCORE(test_close_msgpool),
                         "per_lcore_close_msgpool",
                         rte_eth_dev_count() * TPG_TEST_MAX_ENTRIES,
                         lcore_id);
    test_lcore_init_pool(RTE_PER_LCORE(test_send_msgpool),
                         "per_lcore_send_msgpool",
                         rte_eth_dev_count() * TPG_TEST_MAX_ENTRIES,
                         lcore_id);

    test_lcore_init_pool(RTE_PER_LCORE(test_tmr_open_args),
                         "per_lcore_tmr_open_arg",
                         rte_eth_dev_count() * TPG_TEST_MAX_ENTRIES,
                         lcore_id);
    test_lcore_init_pool(RTE_PER_LCORE(test_tmr_close_args),
                         "per_lcore_tmr_close_arg",
                         rte_eth_dev_count() * TPG_TEST_MAX_ENTRIES,
                         lcore_id);
    test_lcore_init_pool(RTE_PER_LCORE(test_tmr_send_args),
                         "per_lcore_tmr_send_arg",
                         rte_eth_dev_count() * TPG_TEST_MAX_ENTRIES,
                         lcore_id);

    test_lcore_init_test_case_info();
}

