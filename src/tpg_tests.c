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

typedef struct test_stats_s {

    tpg_gen_stats_t  ts_gen_stats;
    tpg_rate_stats_t ts_rate_stats;
    tpg_app_stats_t  ts_app_stats;

} test_stats_t;

/*
 * Array[port][tcid] holding the test case stats (gen, rate, app) for
 * testcases on a port.
 */
RTE_DEFINE_PER_LCORE(test_stats_t *, test_case_stats);

#define TEST_GET_STATS(port, tcid) \
    (&RTE_PER_LCORE(test_case_stats)[(port) * TPG_TEST_MAX_ENTRIES + (tcid)])

#define TEST_GET_GEN_STATS(port, tcid) \
    (&TEST_GET_STATS((port), (tcid))->ts_gen_stats)

#define TEST_GET_RATE_STATS(port, tcid) \
    (&TEST_GET_STATS((port), (tcid))->ts_rate_stats)

#define TEST_GET_APP_STATS(port, tcid) \
    (&TEST_GET_STATS((port), (tcid))->ts_app_stats)

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

RTE_DEFINE_PER_LCORE(test_run_msgpool_t *, test_open_msgpool);
RTE_DEFINE_PER_LCORE(test_run_msgpool_t *, test_close_msgpool);
RTE_DEFINE_PER_LCORE(test_run_msgpool_t *, test_send_msgpool);

#define TEST_GET_MSG_PTR(msgpool, port, tcid) \
    ((msgpool) +  (port) * TPG_TEST_MAX_ENTRIES + (tcid))

static RTE_DEFINE_PER_LCORE(test_tmr_arg_t *, test_tmr_open_args);
static RTE_DEFINE_PER_LCORE(test_tmr_arg_t *, test_tmr_close_args);
static RTE_DEFINE_PER_LCORE(test_tmr_arg_t *, test_tmr_send_args);

#define TEST_GET_TMR_ARG(type, port, tcid)   \
    (RTE_PER_LCORE(test_tmr_##type##_args) + \
     (port) * TPG_TEST_MAX_ENTRIES + (tcid))

/*
 * Per test-type and protocol callbacks.
 * TODO: ideally this should be dynamic and allow registration of other
 * protocols.
 */
static int      test_case_tcp_client_open(l4_control_block_t *l4_cb);
static uint32_t test_case_tcp_mtu(l4_control_block_t *l4_cb);
static int      test_case_tcp_send(l4_control_block_t *l4_cb,
                                   struct rte_mbuf *data_mbuf,
                                   uint32_t *data_sent);
static void     test_case_tcp_close(l4_control_block_t *l4_cb);
static void     test_case_tcp_purge(l4_control_block_t *l4_cb);

static int      test_case_udp_client_open(l4_control_block_t *l4_cb);
static uint32_t test_case_udp_mtu(l4_control_block_t *l4_cb);
static int      test_case_udp_send(l4_control_block_t *l4_cb,
                                   struct rte_mbuf *data_mbuf,
                                   uint32_t *data_sent);
static void     test_case_udp_close(l4_control_block_t *l4_cb);
static void     test_case_udp_purge(l4_control_block_t *l4_cb);


static struct {

    test_case_client_open_cb_t   open;
    test_case_client_close_cb_t  close;
    test_case_session_mtu_cb_t   mtu;
    test_case_session_send_cb_t  send;
    test_case_session_close_cb_t sess_close;
    test_case_session_purge_cb_t sess_purge;
    test_case_htable_walk_cb_t   sess_htable_walk;

} test_callbacks[TEST_CASE_TYPE__MAX][L4_PROTO__L4_PROTO_MAX] = {

    [TEST_CASE_TYPE__SERVER][L4_PROTO__TCP] = {
        .open = NULL,
        .close = NULL,
        .mtu = test_case_tcp_mtu,
        .send = test_case_tcp_send,
        .sess_close = test_case_tcp_close,
        .sess_purge = test_case_tcp_purge,
        .sess_htable_walk = tlkp_walk_tcb,
    },
    [TEST_CASE_TYPE__SERVER][L4_PROTO__UDP] = {
        .open = NULL,
        .close = NULL,
        .mtu = test_case_udp_mtu,
        .send = test_case_udp_send,
        .sess_close = test_case_udp_close,
        .sess_purge = test_case_udp_purge,
        .sess_htable_walk = tlkp_walk_ucb,
    },
    [TEST_CASE_TYPE__CLIENT][L4_PROTO__TCP] = {
        .open = test_case_tcp_client_open,
        .close = test_case_tcp_close,
        .mtu = test_case_tcp_mtu,
        .send = test_case_tcp_send,
        .sess_close = test_case_tcp_close,
        .sess_purge = test_case_tcp_purge,
        .sess_htable_walk = tlkp_walk_tcb,
    },
    [TEST_CASE_TYPE__CLIENT][L4_PROTO__UDP] = {
        .open = test_case_udp_client_open,
        .close = test_case_udp_close,
        .mtu = test_case_udp_mtu,
        .send = test_case_udp_send,
        .sess_close = test_case_udp_close,
        .sess_purge = test_case_udp_purge,
        .sess_htable_walk = tlkp_walk_ucb,
    },

};

/*****************************************************************************
 * Forward declarations
 ****************************************************************************/
static void test_update_recent_latency_stats(tpg_latency_stats_t *stats,
                                             test_oper_latency_state_t *buffer,
                                             tpg_test_case_latency_t *tc_latency);

static void test_case_latency_init(test_case_info_t *tc_info);

/*****************************************************************************
 * test_update_cksum_tstamp()
 *      this function uses incremental checksum from RFC1624 in order to update
 *      the checksum (Ipv4/TCP/UDP) after the timstamp is added
 *      https://tools.ietf.org/html/rfc1624
 ****************************************************************************/
static void test_update_cksum_tstamp(struct rte_mbuf *mbuf __rte_unused,
                                     struct rte_mbuf *mbuf_seg,
                                     uint32_t offset, uint32_t size)
{
    uint16_t        *tstamp;
    uint16_t        *cksum_ptr;
    uint16_t         cksum;
    uint32_t         cksum_32;
    uint32_t         offset_ck;

    offset_ck = DATA_GET_CKSUM_OFFSET(mbuf);

    /* Offset checksum works only with TPG_SW_CHECKSUMMING enabled! */
    if (!offset_ck)
        return;

    cksum_ptr = (uint16_t *) data_mbuf_mtod_offset(mbuf, offset_ck);
    tstamp = (uint16_t *) data_mbuf_mtod_offset(mbuf_seg, offset);

    /* This is needed otherwise the checksum calc won't work */
    cksum = ~(*cksum_ptr) & 0xFFFF;
    /* WARNING: those functions are private functions from dpdk library, they
     * may change in future!!!
     */

    cksum_32 = __rte_raw_cksum(tstamp, size, cksum);
    cksum = __rte_raw_cksum_reduce(cksum_32);
    cksum = (cksum == 0xFFFF) ? cksum : ~cksum;
    *cksum_ptr = cksum;
}

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
    uint32_t eth_port;
    uint32_t tc_id;
    uint32_t src_ip, dst_ip;
    uint16_t src_port, dst_port;
    uint32_t conn_hash;
    uint32_t rx_queue_id;
    const tpg_client_t *client_cfg;

    eth_port = cfg->tcim_test_case.tc_eth_port;
    tc_id = cfg->tcim_test_case.tc_id;
    rx_queue_id = port_get_rx_queue_id(lcore, eth_port);
    client_cfg = &cfg->tcim_test_case.tc_client;

    TPG_FOREACH_CB_IN_RANGE(&client_cfg->cl_src_ips,
                            &client_cfg->cl_dst_ips,
                            &client_cfg->cl_l4.l4c_tcp_udp.tuc_sports,
                            &client_cfg->cl_l4.l4c_tcp_udp.tuc_dports,
                            src_ip, dst_ip, src_port, dst_port) {
        conn_hash = tlkp_calc_connection_hash(dst_ip, src_ip, dst_port,
                                              src_port);
        if (tlkp_get_qindex_from_hash(conn_hash, eth_port) != rx_queue_id)
            continue;

        callback(lcore, eth_port, tc_id, src_ip, dst_ip, src_port,
                 dst_port, conn_hash, callback_arg);
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
    uint32_t eth_port;
    uint32_t tc_id;
    uint32_t src_ip;
    uint16_t src_port;

    const tpg_server_t *server_cfg;

    eth_port = cfg->tcim_test_case.tc_eth_port;
    tc_id = cfg->tcim_test_case.tc_id;
    server_cfg = &cfg->tcim_test_case.tc_server;

    TPG_IPV4_FOREACH(&server_cfg->srv_ips, src_ip) {
        TPG_PORT_FOREACH(&server_cfg->srv_l4.l4s_tcp_udp.tus_ports, src_port) {
            callback(lcore, eth_port, tc_id, src_ip, 0, src_port, 0, 0,
                     callback_arg);
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
                                   tpg_latency_stats_t *sample_stats)
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

        sample_stats->ls_sum_latency -= retain_tstamp;
        sample_stats->ls_samples_count--;
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
void test_update_latency(l4_control_block_t *l4_cb, uint64_t sent_tstamp,
                         uint64_t rcv_tstamp)
{
    test_case_info_t        *tc_info;
    tpg_gen_latency_stats_t *stats;
    int64_t                  latency;

    tc_info = TEST_GET_INFO(l4_cb->l4cb_interface, l4_cb->l4cb_test_case_id);
    stats = &tc_info->tci_gen_stats->gs_latency_stats;

    if (rcv_tstamp < sent_tstamp || sent_tstamp == 0 || rcv_tstamp == 0) {
        INC_STATS(stats, gls_invalid_lat);
        return;
    }

    latency = rcv_tstamp - sent_tstamp;

    /* Global stats */
    test_update_latency_stats(&stats->gls_stats, latency,
                              &tc_info->tci_cfg->tcim_test_case.tc_latency);

    /* Recent stats */
    if (tc_info->tci_latency_state->tols_length != 0) {
        test_latency_state_add(tc_info->tci_latency_state,
                               latency, &stats->gls_sample_stats);
    }
}


/*****************************************************************************
 * test_case_run_msg()
 ****************************************************************************/
int test_case_run_msg(uint32_t lcore_id,
                      uint32_t eth_port, uint32_t test_case_id,
                      test_run_msgpool_t *msgpool,
                      test_run_msg_type_t msg_type)
{
    int    error;
    msg_t *msgp;

    msgp = &TEST_GET_MSG_PTR(msgpool, eth_port, test_case_id)->msg;
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
 * test_case_tmr_cb()
 ****************************************************************************/
static void test_case_tmr_cb(struct rte_timer *tmr __rte_unused, void *arg)
{
    test_tmr_arg_t     *tmr_arg = arg;
    test_rate_state_t  *rate_state = tmr_arg->tta_rate_state;
    rate_limit_t       *rate_limit = tmr_arg->tta_rate_limit;
    uint32_t            in_progress_flag = tmr_arg->tta_rate_in_progress_flag;
    uint32_t            reached_flag = tmr_arg->tta_rate_reached_flag;
    test_run_msgpool_t *msg_pool = tmr_arg->tta_run_msg_pool;

    /* We step into a new time interval... "Advance" the rates. */
    rate_limit_advance_interval(rate_limit);

    /* Start from scratch (reset the "reached" flag).. */
    rate_state->trs_flags &= ~reached_flag;

    test_resched_runner(rate_state, tmr_arg->tta_eth_port,
                        tmr_arg->tta_test_case_id,
                        in_progress_flag, reached_flag,
                        msg_pool, tmr_arg->tta_run_msg_type);
}

/*****************************************************************************
 * Test notifications.
 ****************************************************************************/

/*****************************************************************************
 * test_sess_init()
 ****************************************************************************/
static void test_sess_init(l4_control_block_t *l4_cb,
                           test_case_info_t *tc_info)
{
    tpg_app_proto_t app_id = l4_cb->l4cb_app_data.ad_type;

    /* Struct copy the shared storage. */
    l4_cb->l4cb_app_data.ad_storage = tc_info->tci_app_storage;

    /* Initialize the application state. */
    APP_CALL(init, app_id)(&l4_cb->l4cb_app_data,
                           &tc_info->tci_cfg->tcim_test_case.tc_app);

    /* Initialize the test state machine for the new TCB. */
    test_sm_client_initialize(l4_cb, tc_info);
}

/*****************************************************************************
 * test_sess_record_start_time()
 ****************************************************************************/
static void test_sess_record_start_time(test_case_info_t *tc_info)
{
    if (unlikely(tc_info->tci_gen_stats->gs_start_time == 0))
        tc_info->tci_gen_stats->gs_start_time = rte_get_timer_cycles();
}

/*****************************************************************************
 * test_sess_record_end_time()
 ****************************************************************************/
static void test_sess_record_end_time(test_case_info_t *tc_info)
{
    /*
     * This is not really the end time.. but we can keep it
     * here for now for tracking how long it took to establish sessions.
     */
    tc_info->tci_gen_stats->gs_end_time = rte_get_timer_cycles();
}

/*****************************************************************************
 * test_sess_connecting()
 ****************************************************************************/
static void test_sess_connecting(l4_control_block_t *l4_cb,
                                 test_case_info_t *tc_info)
{
    test_sess_record_start_time(tc_info);
    test_sm_sess_connecting(l4_cb, tc_info);
}

/*****************************************************************************
 * test_sess_connected()
 ****************************************************************************/
static void test_sess_connected(l4_control_block_t *l4_cb,
                                test_case_info_t *tc_info)
{
    tc_info->tci_gen_stats->gs_estab++;

    /* Update rate per second. */
    tc_info->tci_rate_stats->rs_estab_per_s++;

    test_sess_record_end_time(tc_info);
    test_sm_sess_connected(l4_cb, tc_info);
}

/*****************************************************************************
 * test_sess_connected_imm()
 ****************************************************************************/
static void test_sess_connected_imm(l4_control_block_t *l4_cb,
                                    test_case_info_t *tc_info)
{
    /* We skipped "connecting" so let's record the start time. */
    test_sess_record_start_time(tc_info);
    test_sess_connected(l4_cb, tc_info);
}

/*****************************************************************************
 * test_sess_listen()
 ****************************************************************************/
static void test_sess_listen(l4_control_block_t *l4_cb,
                             test_case_info_t *tc_info)
{
    tpg_app_proto_t app_id = l4_cb->l4cb_app_data.ad_type;

    /* Struct copy the shared storage. */
    l4_cb->l4cb_app_data.ad_storage = tc_info->tci_app_storage;

    /* Initialize the application state. */
    APP_CALL(init, app_id)(&l4_cb->l4cb_app_data,
                           &tc_info->tci_cfg->tcim_test_case.tc_app);

    /* Initialize the test state machine for the listening TCB. */
    test_sm_listen_initialize(l4_cb, tc_info);
}

/*****************************************************************************
 * test_sess_server_connected()
 ****************************************************************************/
static void test_sess_server_connected(l4_control_block_t *l4_cb,
                                       test_case_info_t *tc_info)
{
    tc_info->tci_gen_stats->gs_estab++;

    /* Update rate per second. */
    tc_info->tci_rate_stats->rs_estab_per_s++;

    /* Initialize the test state machine for the new CB. */
    test_sm_server_initialize(l4_cb, tc_info);
}

/*****************************************************************************
 * test_sess_closing()
 ****************************************************************************/
static void test_sess_closing(l4_control_block_t *l4_cb,
                              test_case_info_t *tc_info)
{
    test_sm_sess_closing(l4_cb, tc_info);
}

/*****************************************************************************
 * test_sess_closed()
 ****************************************************************************/
static void test_sess_closed(l4_control_block_t *l4_cb,
                             test_case_info_t *tc_info)
{
    /* Update rate per second. */
    tc_info->tci_rate_stats->rs_closed_per_s++;

    test_sm_sess_closed(l4_cb, tc_info);
}

/*****************************************************************************
 * test_sess_win_available()
 ****************************************************************************/
static void test_sess_win_available(l4_control_block_t *l4_cb,
                                    test_case_info_t *tc_info)
{
    test_sm_app_send_win_avail(l4_cb, tc_info);
}

/*****************************************************************************
 * test_sess_win_unavailable()
 ****************************************************************************/
static void test_sess_win_unavailable(l4_control_block_t *l4_cb,
                                      test_case_info_t *tc_info)
{
    test_sm_app_send_win_unavail(l4_cb, tc_info);
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
                                       rate_limit_t *rl,
                                       uint32_t lcore_id)
{

    /* No need to start a periodic timer if rate-limiting is set to 0. */
    if (!rate_limit_interval_us(rl))
        return;

    rte_timer_reset(tmr, rate_limit_interval_us(rl) * cycles_per_us, PERIODICAL,
                    lcore_id,
                    test_case_tmr_cb,
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
    rate_state->trs_flags = 0;

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
    test_tmr_arg_t    *tmr_open_arg;
    test_tmr_arg_t    *tmr_close_arg;
    test_tmr_arg_t    *tmr_send_arg;

    tmr_open_arg =  TEST_GET_TMR_ARG(open, eth_port, test_case_id);
    tmr_close_arg = TEST_GET_TMR_ARG(close, eth_port, test_case_id);
    tmr_send_arg =  TEST_GET_TMR_ARG(send, eth_port, test_case_id);

    *tmr_open_arg = (test_tmr_arg_t) {
        .tta_lcore_id = lcore,
        .tta_eth_port = eth_port,
        .tta_test_case_id = test_case_id,
        .tta_rate_state = rate_state,
        .tta_rate_limit = &rate_state->trs_open,
        .tta_rate_in_progress_flag = TRS_FLAGS_OPEN_IN_PROGRESS,
        .tta_rate_reached_flag = TRS_FLAGS_OPEN_RATE_REACHED,
        .tta_run_msg_pool = RTE_PER_LCORE(test_open_msgpool),
        .tta_run_msg_type = TRMT_OPEN,
    };

    *tmr_close_arg = (test_tmr_arg_t) {
        .tta_lcore_id = lcore,
        .tta_eth_port = eth_port,
        .tta_test_case_id = test_case_id,
        .tta_rate_state = rate_state,
        .tta_rate_limit = &rate_state->trs_close,
        .tta_rate_in_progress_flag = TRS_FLAGS_CLOSE_IN_PROGRESS,
        .tta_rate_reached_flag = TRS_FLAGS_CLOSE_RATE_REACHED,
        .tta_run_msg_pool = RTE_PER_LCORE(test_close_msgpool),
        .tta_run_msg_type = TRMT_CLOSE,
    };

    *tmr_send_arg = (test_tmr_arg_t) {
        .tta_lcore_id = lcore,
        .tta_eth_port = eth_port,
        .tta_test_case_id = test_case_id,
        .tta_rate_state = rate_state,
        .tta_rate_limit = &rate_state->trs_send,
        .tta_rate_in_progress_flag = TRS_FLAGS_SEND_IN_PROGRESS,
        .tta_rate_reached_flag = TRS_FLAGS_SEND_RATE_REACHED,
        .tta_run_msg_pool = RTE_PER_LCORE(test_send_msgpool),
        .tta_run_msg_type = TRMT_SEND,
    };

    test_case_rate_start_timer(&rate_timers->trt_open_timer, tmr_open_arg,
                               &rate_state->trs_open,
                               lcore);

    test_case_rate_start_timer(&rate_timers->trt_close_timer, tmr_close_arg,
                               &rate_state->trs_close,
                               lcore);

    test_case_rate_start_timer(&rate_timers->trt_send_timer, tmr_send_arg,
                               &rate_state->trs_send,
                               lcore);
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

    return rate_state->trs_flags &
            (TRS_FLAGS_OPEN_IN_PROGRESS |
             TRS_FLAGS_CLOSE_IN_PROGRESS |
             TRS_FLAGS_SEND_IN_PROGRESS);
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
                                 test_case_client_open_cb_t client_open_cb,
                                 test_case_client_close_cb_t client_close_cb,
                                 test_case_session_mtu_cb_t mtu_cb,
                                 test_case_session_send_cb_t send_cb,
                                 test_case_session_close_cb_t close_cb,
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
    switch (im->tcim_test_case.tc_type) {
    case TEST_CASE_TYPE__CLIENT:

        /* Get local and total session count. Unfortunately there's no other
         * way to compute the number of local sessions than to walk the list..
         */
        test_case_for_each_client(lcore, im,
                                  test_case_init_state_client_counters_cb,
                                  &local_sessions);
        total_sessions =
            test_case_client_cfg_count(&im->tcim_test_case.tc_client);
        break;
    case TEST_CASE_TYPE__SERVER:
        /* We know that servers are created on all lcores
         * (i.e., local == total).
         */
        local_sessions =
            test_case_server_cfg_count(&im->tcim_test_case.tc_server);
        total_sessions =
            test_case_server_cfg_count(&im->tcim_test_case.tc_server);
        break;

    default:
        assert(false);
        break;
    }

    /* Initialize the rates. */
    test_case_rate_state_init(lcore, im->tcim_test_case.tc_eth_port,
                              im->tcim_test_case.tc_id,
                              ts, rate_timers, im,
                              total_sessions, local_sessions);

    /* Initialize the session callbacks. */
    ts->tos_client_open_cb = client_open_cb;
    ts->tos_client_close_cb = client_close_cb;
    ts->tos_session_mtu_cb = mtu_cb;
    ts->tos_session_send_cb = send_cb;
    ts->tos_session_close_cb = close_cb;
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
    tpg_app_proto_t      app_id;
    sockopt_t           *sockopt;
    int                  error;

    tc_info = arg;
    app_id = tc_info->tci_cfg->tcim_test_case.tc_app.app_proto;
    sockopt = &tc_info->tci_cfg->tcim_sockopt;

    /* We need to pass NULL in order for tcp_listen_v4 to allocate one
     * for us.
     */
    server_tcb = NULL;

    /* Listen on the specified address + port. */
    error = tcp_listen_v4(&server_tcb, eth_port, src_ip, src_port,
                          test_case_id,
                          app_id, sockopt,
                          TCG_CB_CONSUME_ALL_DATA);
    if (unlikely(error)) {
        test_notification(TEST_NOTIF_SESS_FAILED, NULL, eth_port,
                          test_case_id);
    } else {
        test_notification(TEST_NOTIF_SESS_UP, NULL, eth_port,
                          test_case_id);
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
    tpg_app_proto_t      app_id;
    sockopt_t           *sockopt;
    int                  error;

    tc_info = arg;
    app_id = tc_info->tci_cfg->tcim_test_case.tc_app.app_proto;
    sockopt = &tc_info->tci_cfg->tcim_sockopt;

    /* We need to pass NULL in order for udp_listen_v4 to allocate one
     * for us.
     */
    server_ucb = NULL;

    /* Listen on the first specified address + port. */
    error = udp_listen_v4(&server_ucb, eth_port, src_ip, src_port,
                          test_case_id,
                          app_id, sockopt,
                          0);
    if (unlikely(error)) {
        test_notification(TEST_NOTIF_SESS_FAILED, NULL, eth_port,
                          test_case_id);
    } else {
        test_notification(TEST_NOTIF_SESS_UP, NULL, eth_port,
                          test_case_id);
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
    app_id = tc_info->tci_cfg->tcim_test_case.tc_app.app_proto;
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

    test_sess_init(&tcb->tcb_l4, tc_info);
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
    app_id = tc_info->tci_cfg->tcim_test_case.tc_app.app_proto;
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

    test_sess_init(&ucb->ucb_l4, tc_info);
}

/*****************************************************************************
 * Static functions for Running test cases.
 ****************************************************************************/

/*****************************************************************************
 * test_case_tcp_client_open()
 ****************************************************************************/
static int test_case_tcp_client_open(l4_control_block_t *l4_cb)
{
    tcp_control_block_t *tcb = container_of(l4_cb, tcp_control_block_t, tcb_l4);

    return tcp_open_v4_connection(&tcb, tcb->tcb_l4.l4cb_interface,
                                  tcb->tcb_l4.l4cb_src_addr.ip_v4,
                                  tcb->tcb_l4.l4cb_src_port,
                                  tcb->tcb_l4.l4cb_dst_addr.ip_v4,
                                  tcb->tcb_l4.l4cb_dst_port,
                                  tcb->tcb_l4.l4cb_test_case_id,
                                  tcb->tcb_l4.l4cb_app_data.ad_type,
                                  NULL,
                                  TCG_CB_REUSE_CB);
}

/*****************************************************************************
 * test_case_tcp_mtu()
 ****************************************************************************/
static uint32_t test_case_tcp_mtu(l4_control_block_t *l4_cb)
{
    return TCB_AVAIL_SEND(container_of(l4_cb, tcp_control_block_t, tcb_l4));
}

/*****************************************************************************
 * test_case_tcp_send()
 ****************************************************************************/
static int
test_case_tcp_send(l4_control_block_t *l4_cb, struct rte_mbuf *data_mbuf,
                   uint32_t *data_sent)
{
    tcp_control_block_t *tcb = container_of(l4_cb, tcp_control_block_t, tcb_l4);

    return tcp_send_v4(tcb, data_mbuf, TCG_SEND_PSH, 0 /* Timeout */,
                       data_sent);
}

/*****************************************************************************
 * test_case_tcp_close()
 ****************************************************************************/
static void test_case_tcp_close(l4_control_block_t *l4_cb)
{
    tcp_close_connection(container_of(l4_cb, tcp_control_block_t, tcb_l4), 0);
}

/*****************************************************************************
 * test_case_udp_client_open()
 ****************************************************************************/
static int test_case_udp_client_open(l4_control_block_t *l4_cb)
{
    udp_control_block_t *ucb = container_of(l4_cb, udp_control_block_t, ucb_l4);

    return udp_open_v4_connection(&ucb, ucb->ucb_l4.l4cb_interface,
                                  ucb->ucb_l4.l4cb_src_addr.ip_v4,
                                  ucb->ucb_l4.l4cb_src_port,
                                  ucb->ucb_l4.l4cb_dst_addr.ip_v4,
                                  ucb->ucb_l4.l4cb_dst_port,
                                  ucb->ucb_l4.l4cb_test_case_id,
                                  ucb->ucb_l4.l4cb_app_data.ad_type,
                                  NULL,
                                  TCG_CB_REUSE_CB);
}

/*****************************************************************************
 * test_case_udp_mtu()
 ****************************************************************************/
static uint32_t test_case_udp_mtu(l4_control_block_t *l4_cb)
{
    return UCB_MTU(container_of(l4_cb, udp_control_block_t, ucb_l4));
}

/*****************************************************************************
 * test_case_udp_send()
 ****************************************************************************/
static int test_case_udp_send(l4_control_block_t *l4_cb,
                              struct rte_mbuf *data_mbuf,
                              uint32_t *data_sent)
{
    udp_control_block_t *ucb = container_of(l4_cb, udp_control_block_t, ucb_l4);

    return udp_send_v4(ucb, data_mbuf, data_sent);
}

/*****************************************************************************
 * test_case_udp_close()
 ****************************************************************************/
static void test_case_udp_close(l4_control_block_t *l4_cb)
{
    udp_close_v4(container_of(l4_cb, udp_control_block_t, ucb_l4));
}

/*****************************************************************************
 * test_case_tcp_purge()
 ****************************************************************************/
static void test_case_tcp_purge(l4_control_block_t *l4_cb)
{
    tcp_control_block_t *tcb = container_of(l4_cb, tcp_control_block_t, tcb_l4);

    if (tcb->tcb_state != TS_INIT && tcb->tcb_state != TS_CLOSED)
        tcp_close_connection(tcb, TCG_SILENT_CLOSE);

    if (!tcb->tcb_malloced)
        tcp_connection_cleanup(tcb);
}

/*****************************************************************************
 * test_case_udp_purge()
 ****************************************************************************/
static void test_case_udp_purge(l4_control_block_t *l4_cb)
{
    udp_control_block_t *ucb = container_of(l4_cb, udp_control_block_t, ucb_l4);

    if (ucb->ucb_state != US_INIT && ucb->ucb_state != US_CLOSED)
        udp_close_v4(ucb);

    if (!ucb->ucb_malloced)
        udp_connection_cleanup(ucb);
}

/*****************************************************************************
 * test_purge_list()
 ****************************************************************************/
static uint32_t test_purge_list(test_case_info_t *tc_info,
                                tlkp_test_cb_list_t *cb_list)
{
    tpg_test_case_type_t tc_type = tc_info->tci_cfg->tcim_test_case.tc_type;
    tpg_l4_proto_t       l4_proto = tc_info->tci_cfg->tcim_l4_type;
    uint32_t             purge_cnt = 0;

    while (!TEST_CBQ_EMPTY(cb_list)) {
        l4_control_block_t *l4_cb = TAILQ_FIRST(cb_list);

        purge_cnt++;

        /* No need to remove from the list. It should be done by the cleanup
         * function. Inform the state machine that we're purging the session.
         */
        test_sm_purge(l4_cb, tc_info);

        /* Call the corresponding callback to purge the session. */
        test_callbacks[tc_type][l4_proto].sess_purge(l4_cb);
    }

    return purge_cnt;
}

/*****************************************************************************
 * test_case_purge_cbs()
 ****************************************************************************/
static void test_case_purge_cbs(test_case_info_t *tc_info)
{
    tpg_test_case_type_t tc_type = tc_info->tci_cfg->tcim_test_case.tc_type;
    tpg_l4_proto_t       l4_proto = tc_info->tci_cfg->tcim_l4_type;

    uint32_t eth_port = tc_info->tci_cfg->tcim_test_case.tc_eth_port;
    uint32_t tc_id    = tc_info->tci_cfg->tcim_test_case.tc_id;
    uint32_t purge_cnt = 0;

    test_case_htable_walk_cb_t htable_walk_fn;

    bool purge_htable_cb(l4_control_block_t *l4_cb, void *arg __rte_unused)
    {
        if (l4_cb->l4cb_test_case_id != tc_id)
            return true;

        purge_cnt++;

        test_sm_purge(l4_cb, tc_info);
        test_callbacks[tc_type][l4_proto].sess_purge(l4_cb);
        return true;
    }

    purge_cnt += test_purge_list(tc_info, &tc_info->tci_state.tos_to_init_cbs);
    purge_cnt += test_purge_list(tc_info, &tc_info->tci_state.tos_to_open_cbs);
    purge_cnt += test_purge_list(tc_info, &tc_info->tci_state.tos_to_close_cbs);
    purge_cnt += test_purge_list(tc_info, &tc_info->tci_state.tos_to_send_cbs);
    purge_cnt += test_purge_list(tc_info, &tc_info->tci_state.tos_closed_cbs);

    htable_walk_fn = test_callbacks[tc_type][l4_proto].sess_htable_walk;

    htable_walk_fn(eth_port, purge_htable_cb, NULL);

    RTE_LOG(INFO, USER1,
            "Purged %u sessions on eth_port %"PRIu32" tcid %"PRIu32"\n",
            purge_cnt, eth_port, tc_id);
}

/*****************************************************************************
 * Test Message Handlers which will run on the packet threads.
 ****************************************************************************/
/*****************************************************************************
 * test_case_init_cb()
 ****************************************************************************/
static int test_case_init_cb(uint16_t msgid, uint16_t lcore, void *msg)
{
    test_case_init_msg_t    *im;
    uint32_t                 eth_port;
    uint32_t                 tcid;
    test_case_info_t        *tc_info;
    tpg_test_case_type_t     tc_type;
    tpg_l4_proto_t           l4_proto;
    tpg_app_proto_t          app_id;
    tpg_test_case_latency_t *test_latency;

    if (MSG_INVALID(msgid, msg, MSG_TEST_CASE_INIT))
        return -EINVAL;

    im = msg;

    eth_port = im->tcim_test_case.tc_eth_port;
    tcid     = im->tcim_test_case.tc_id;
    tc_type  = im->tcim_test_case.tc_type;
    app_id   = im->tcim_test_case.tc_app.app_proto;
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
    test_latency = &tc_info->tci_cfg->tcim_test_case.tc_latency;

    if (tc_info->tci_cfg->tcim_tx_tstamp) {
        tstamp_tx_post_cb_t  cb;
        sockopt_t           *sockopt;

        sockopt = &tc_info->tci_cfg->tcim_sockopt;
        if (!sockopt->so_eth.ethso_tx_offload_ipv4_cksum ||
                !sockopt->so_eth.ethso_tx_offload_tcp_cksum ||
                !sockopt->so_eth.ethso_tx_offload_udp_cksum)
            cb = test_update_cksum_tstamp;
        else
            cb = NULL;

        tstamp_start_tx(eth_port, port_get_tx_queue_id(lcore, eth_port),
                        cb);
    }

    if (tc_info->tci_cfg->tcim_rx_tstamp) {
        tstamp_start_rx(eth_port, port_get_rx_queue_id(lcore, eth_port));
        test_case_latency_init(tc_info);

        /* Initialize latency buffer. */
        test_latency_state_init(tc_info->tci_latency_state,
                                test_latency->has_tcs_samples ?
                                test_latency->tcs_samples : 0);
    }

    /* Initialize operational part and callbacks. */
    test_case_init_state(lcore, tc_info->tci_cfg, &tc_info->tci_state,
                         test_callbacks[tc_type][l4_proto].open,
                         test_callbacks[tc_type][l4_proto].close,
                         test_callbacks[tc_type][l4_proto].mtu,
                         test_callbacks[tc_type][l4_proto].send,
                         test_callbacks[tc_type][l4_proto].sess_close,
                         &tc_info->tci_rate_timers);

    /* Let the application layer know that the test is starting. The application
     * will initialize its "global" per test case state.
     */
    APP_CALL(tc_start, app_id)(&tc_info->tci_cfg->tcim_test_case,
                               &tc_info->tci_cfg->tcim_test_case.tc_app,
                               &tc_info->tci_app_storage);

    /* Initialize stats. */
    bzero(tc_info->tci_gen_stats, sizeof(*tc_info->tci_gen_stats));
    bzero(tc_info->tci_rate_stats, sizeof(*tc_info->tci_rate_stats));

    /* Initialize app stats. */
    APP_CALL(stats_init, app_id)(&tc_info->tci_cfg->tcim_test_case.tc_app,
                                 tc_info->tci_app_stats);

    /* Initialize clients and servers. */

    /* WARNING: we could include test_case_start_tcp/udp_server/client in
     * the test_callbacks array but then the compiler would most likely
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
    bzero(&tc_info->tci_gen_stats->gs_latency_stats,
          sizeof(tc_info->tci_gen_stats->gs_latency_stats));
    tc_info->tci_gen_stats->gs_latency_stats.gls_stats.ls_min_latency =
        UINT32_MAX;
    tc_info->tci_gen_stats->gs_latency_stats.gls_sample_stats .ls_min_latency =
        UINT32_MAX;
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
    tc_info->tci_rate_stats->rs_start_time = rte_get_timer_cycles();

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
 * test_case_rate_limit_update()
 *      Notes: Update a specific test case rate limit. If the desired rate
 *             was reached we stop resending the message. Otherwise, if there
 *             are still sessions on the control_block list waiting to
 *             execute an operation, resend the message (EAGAIN).
 ****************************************************************************/
static int test_case_rate_limit_update(test_rate_state_t *rate_state,
                                       rate_limit_t *rate_limit,
                                       tlkp_test_cb_list_t *cb_list,
                                       uint32_t rate_in_progress_flag,
                                       uint32_t rate_reached_flag,
                                       uint32_t consumed)
{
    rate_limit_consume(rate_limit, consumed);

    /* If we still didn't reach the expected rate for this
     * interval then we should repost if we still have cbs in the list.
     */
    if (likely(!rate_limit_reached(rate_limit))) {

        /* Rate not reached but no more sessions in queue:
         * Stop and mark the message as not in progress anymore.
         */
        if (TEST_CBQ_EMPTY(cb_list)) {
            rate_state->trs_flags &= ~rate_in_progress_flag;
            return 0;
        }
        return -EAGAIN;
    }

    /* Set the "reached" flag. */
    rate_state->trs_flags |= rate_reached_flag;

    /* Rate reached: Stop and mark the message as not in progress anymore. */
    rate_state->trs_flags &= ~rate_in_progress_flag;
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
    test_oper_state_t   *ts;
    uint32_t             max_open;
    uint32_t             open_cnt;
    int                  error;

    if (MSG_INVALID(msgid, msg, MSG_TEST_CASE_RUN_OPEN))
        return -EINVAL;

    rm = msg;
    tc_info = TEST_GET_INFO(rm->tcrm_eth_port, rm->tcrm_test_case_id);
    ts = &tc_info->tci_state;

    /* Check how many sessions we are allowed to open. */
    max_open = rate_limit_available(&ts->tos_rates.trs_open);

    /* Start a batch of clients from the to_open list. */
    for (open_cnt = 0;
            !TEST_CBQ_EMPTY(&ts->tos_to_open_cbs) && open_cnt < max_open;
            open_cnt++) {
        l4_control_block_t *l4_cb;

        l4_cb = TAILQ_FIRST(&ts->tos_to_open_cbs);

        error = ts->tos_client_open_cb(l4_cb);
        if (unlikely(error)) {
            TEST_NOTIF(TEST_NOTIF_SESS_FAILED, l4_cb);
            /* Readd to the open list and try again later. */
            TEST_CBQ_ADD_TO_OPEN(ts, l4_cb);
        } else {
            TEST_NOTIF(TEST_NOTIF_SESS_UP, l4_cb);
        }
    }

    TRACE_FMT(TST, DEBUG, "OPEN start cnt %"PRIu32, open_cnt);

    /* Update the rate limit and check if we have to open more (later). */
    return test_case_rate_limit_update(&ts->tos_rates, &ts->tos_rates.trs_open,
                                       &ts->tos_to_open_cbs,
                                       TRS_FLAGS_OPEN_IN_PROGRESS,
                                       TRS_FLAGS_OPEN_RATE_REACHED,
                                       open_cnt);
}

/*****************************************************************************
 * test_case_run_close_cb()
 ****************************************************************************/
static int test_case_run_close_cb(uint16_t msgid, uint16_t lcore __rte_unused,
                                  void *msg)
{
    test_case_run_msg_t *rm;
    test_case_info_t    *tc_info;
    test_oper_state_t   *ts;
    uint32_t             max_close;
    uint32_t             close_cnt;

    if (MSG_INVALID(msgid, msg, MSG_TEST_CASE_RUN_CLOSE))
        return -EINVAL;

    rm = msg;
    tc_info = TEST_GET_INFO(rm->tcrm_eth_port, rm->tcrm_test_case_id);
    ts = &tc_info->tci_state;

    /* Check how many sessions we are allowed to close. */
    max_close = rate_limit_available(&ts->tos_rates.trs_close);

    /* Stop a batch of clients from the to_close list. */
    for (close_cnt = 0;
            !TEST_CBQ_EMPTY(&ts->tos_to_close_cbs) && close_cnt < max_close;
            close_cnt++) {
        l4_control_block_t *l4_cb;

        l4_cb = TAILQ_FIRST(&ts->tos_to_close_cbs);
        ts->tos_client_close_cb(l4_cb);
    }

    TRACE_FMT(TST, DEBUG, "CLOSE start cnt %"PRIu32, close_cnt);

    /* Update the rate limit and check if we have to send more (later). */
    return test_case_rate_limit_update(&ts->tos_rates, &ts->tos_rates.trs_close,
                                       &ts->tos_to_close_cbs,
                                       TRS_FLAGS_CLOSE_IN_PROGRESS,
                                       TRS_FLAGS_CLOSE_RATE_REACHED,
                                       close_cnt);
}

/*****************************************************************************
 * test_case_run_send_cb()
 ****************************************************************************/
static int test_case_run_send_cb(uint16_t msgid, uint16_t lcore __rte_unused,
                                 void *msg)
{
    test_case_run_msg_t *rm;
    test_case_info_t    *tc_info;
    test_oper_state_t   *ts;
    test_rate_state_t   *rate_state;
    uint32_t             max_send;
    uint32_t             send_cnt;
    uint32_t             send_pkt_cnt;

    if (MSG_INVALID(msgid, msg, MSG_TEST_CASE_RUN_SEND))
        return -EINVAL;

    rm = msg;

    tc_info = TEST_GET_INFO(rm->tcrm_eth_port, rm->tcrm_test_case_id);
    ts = &tc_info->tci_state;
    rate_state = &ts->tos_rates;

    /* Check how many sessions are allowed to send traffic. */
    max_send = rate_limit_available(&rate_state->trs_send);

    for (send_cnt = 0, send_pkt_cnt = 0;
            !TEST_CBQ_EMPTY(&ts->tos_to_send_cbs) && send_pkt_cnt < max_send;
            send_pkt_cnt++) {
        int                 error;
        uint32_t            mtu;
        struct rte_mbuf    *data_mbuf;
        uint32_t            data_sent = 0;
        l4_control_block_t *l4_cb;
        tpg_app_proto_t     app_id;

        l4_cb = TAILQ_FIRST(&ts->tos_to_send_cbs);
        app_id = l4_cb->l4cb_app_data.ad_type;
        mtu = ts->tos_session_mtu_cb(l4_cb);

        data_mbuf = APP_CALL(send, app_id)(l4_cb, &l4_cb->l4cb_app_data,
                                           tc_info->tci_app_stats,
                                           mtu);
        if (unlikely(data_mbuf == NULL)) {
            TEST_NOTIF(TEST_NOTIF_DATA_NULL, l4_cb);

            if (test_sm_has_data_pending(l4_cb)) {
                /* Move at the end to try again later. */
                TEST_CBQ_REM_TO_SEND(ts, l4_cb);
                TEST_CBQ_ADD_TO_SEND(ts, l4_cb);
            }
            continue;
        }

        /* Try to send.
         * if we sent something then let the APP know
         * else if app still needs to send move at end of list
         * else do-nothing as the TEST state machine moved us already to
         * TSTS_NO_SND_WIN.
         */
        error = ts->tos_session_send_cb(l4_cb, data_mbuf, &data_sent);
        if (unlikely(error)) {
            TEST_NOTIF(TEST_NOTIF_DATA_FAILED, l4_cb);

            if (test_sm_has_data_pending(l4_cb)) {
                /* Move at the end to try again later. */
                TEST_CBQ_REM_TO_SEND(ts, l4_cb);
                TEST_CBQ_ADD_TO_SEND(ts, l4_cb);
                continue;
            }
        }

        if (likely(data_sent != 0)) {
            if (APP_CALL(data_sent, app_id)(l4_cb, &l4_cb->l4cb_app_data,
                                            tc_info->tci_app_stats,
                                            data_sent)) {
                /* We increment the sent count only if the application managed
                 * to transmit a complete message.
                 */
                send_cnt++;
            }

        }
    }
    TRACE_FMT(TST, DEBUG, "SEND data cnt %"PRIu32, send_cnt);

    /* Update the transaction send rate. */
    tc_info->tci_rate_stats->rs_data_per_s += send_cnt;

    /* Update the rate limiter with the number of individual sent packets
     * (not transactions!) and check if we have to send more (later).
     */
    return test_case_rate_limit_update(rate_state, &rate_state->trs_send,
                                       &ts->tos_to_send_cbs,
                                       TRS_FLAGS_SEND_IN_PROGRESS,
                                       TRS_FLAGS_SEND_RATE_REACHED,
                                       send_pkt_cnt);
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
        test_case_purge_cbs(tc_info);
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

    app_id = tc_info->tci_cfg->tcim_test_case.tc_app.app_proto;
    APP_CALL(tc_stop, app_id)(&tc_info->tci_cfg->tcim_test_case,
                              &tc_info->tci_cfg->tcim_test_case.tc_app,
                              &tc_info->tci_app_storage);

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
    tpg_app_proto_t            app_id;

    if (MSG_INVALID(msgid, msg, MSG_TEST_CASE_STATS_REQ))
        return -EINVAL;

    sm = msg;

    tc_info = TEST_GET_INFO(sm->tcsrm_eth_port, sm->tcsrm_test_case_id);

    app_id = tc_info->tci_cfg->tcim_test_case.tc_app.app_proto;

    /* Here we walk through the buffer in order to fill the recent stats */
    if (tc_info->tci_cfg->tcim_rx_tstamp) {
        tpg_test_case_latency_t *tc_latency;
        tpg_gen_latency_stats_t *tc_latency_stats;

        tc_latency = &tc_info->tci_cfg->tcim_test_case.tc_latency;
        tc_latency_stats = &tc_info->tci_gen_stats->gs_latency_stats;

        test_update_recent_latency_stats(&tc_latency_stats->gls_sample_stats,
                                         tc_info->tci_latency_state,
                                         tc_latency);
    }

    /* Struct copy the stats! */
    *sm->tcsrm_test_case_stats = *tc_info->tci_gen_stats;

    /* Ask the APP implementation to copy the stats for us. */
    APP_CALL(stats_copy, app_id)(sm->tcsrm_test_case_app_stats,
                                 tc_info->tci_app_stats);

    /* Clear the runtime stats. They're aggregated by the test manager.
     * Don't clear the start and end time for the gen stats!
     */
    if (tc_info->tci_cfg->tcim_rx_tstamp)
        test_case_latency_init(tc_info);

    tc_info->tci_gen_stats->gs_up = 0;
    tc_info->tci_gen_stats->gs_estab = 0;
    tc_info->tci_gen_stats->gs_down = 0;
    tc_info->tci_gen_stats->gs_failed = 0;
    tc_info->tci_gen_stats->gs_data_failed = 0;
    tc_info->tci_gen_stats->gs_data_null = 0;

    /* Clear the app stats. */
    APP_CALL(stats_init, app_id)(&tc_info->tci_cfg->tcim_test_case.tc_app,
                                 tc_info->tci_app_stats);
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
    tc_info->tci_rate_stats->rs_end_time = now;

    /* Struct copy the stats! */
    *sm->tcrrm_test_case_rate_stats = *tc_info->tci_rate_stats;

    /* Clear the rates stats. They're aggregated by the test manager. */
    bzero(tc_info->tci_rate_stats, sizeof(*tc_info->tci_rate_stats));

    /* Store the new initial timestamp. */
    tc_info->tci_rate_stats->rs_start_time = now;

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
            tc_info->tci_gen_stats = TEST_GET_GEN_STATS(eth_port, tcid);
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

    test_lcore_init_pool(RTE_PER_LCORE(test_case_stats),
                         "per_lcore_test_stats",
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

/****************************************************************************
 * test_notification()
 *
 * NOTE:
 *   This will be called from the packet processing core context.
 ****************************************************************************/
void test_notification(uint32_t notification, l4_control_block_t *l4_cb,
                       uint32_t eth_port, uint32_t test_case_id)
{
    test_case_info_t *tc_info = TEST_GET_INFO(eth_port, test_case_id);

    switch (notification) {

    case TEST_NOTIF_SESS_UP:
        tc_info->tci_gen_stats->gs_up++;
        break;
    case TEST_NOTIF_SESS_DOWN:
        tc_info->tci_gen_stats->gs_down++;
        break;
    case TEST_NOTIF_SESS_FAILED:
        tc_info->tci_gen_stats->gs_failed++;
        break;
    case TEST_NOTIF_DATA_FAILED:
        tc_info->tci_gen_stats->gs_data_failed++;
        break;
    case TEST_NOTIF_DATA_NULL:
        tc_info->tci_gen_stats->gs_data_null++;
        break;

    case TEST_NOTIF_TMR_FIRED:
        test_sm_tmr_to(l4_cb, tc_info);
        break;

    case TEST_NOTIF_SESS_CONNECTING:
        test_sess_connecting(l4_cb, tc_info);
        break;
    case TEST_NOTIF_SESS_CONNECTED:
        test_sess_connected(l4_cb, tc_info);
        break;
    case TEST_NOTIF_SESS_CONNECTED_IMM:
        test_sess_connected_imm(l4_cb, tc_info);
        break;
    case TEST_NOTIF_SESS_LISTEN:
        test_sess_listen(l4_cb, tc_info);
        break;
    case TEST_NOTIF_SESS_SRV_CONNECTED:
        test_sess_server_connected(l4_cb, tc_info);
        break;
    case TEST_NOTIF_SESS_CLOSING:
        test_sess_closing(l4_cb, tc_info);
        break;
    case TEST_NOTIF_SESS_CLOSED:
        test_sess_closed(l4_cb, tc_info);
        break;
    case TEST_NOTIF_SESS_WIN_AVAIL:
        test_sess_win_available(l4_cb, tc_info);
        break;
    case TEST_NOTIF_SESS_WIN_UNAVAIL:
        test_sess_win_unavailable(l4_cb, tc_info);
        break;

    case TEST_NOTIF_APP_SEND_START:
        test_sm_app_send_start(l4_cb, tc_info);
        break;
    case TEST_NOTIF_APP_SEND_STOP:
        test_sm_app_send_stop(l4_cb, tc_info);
        break;
    case TEST_NOTIF_APP_CLOSE:
        tc_info->tci_state.tos_session_close_cb(l4_cb);
        break;

    default:
        assert(false);
        break;
    }
}

