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
 *     tpg_test_mgmt_api.c
 *
 * Description:
 *     API to be used for managing the tests (configure/monitor/start/stop).
 *
 * Author:
 *     Dumitru Ceara, Eelco Chaudron
 *
 * Initial Created:
 *     05/13/2016
 *
 * Notes:
 *
 */

#include "tcp_generator.h"

/*****************************************************************************
 * Globals
 ****************************************************************************/
static const char *test_case_type_names[TEST_CASE_TYPE__MAX] = {
    [TEST_CASE_TYPE__CLIENT] = "client",
    [TEST_CASE_TYPE__SERVER] = "server",
};

/*****************************************************************************
 * Static Test Case default initializers
 ****************************************************************************/
/*****************************************************************************
 * test_init_server_default_criteria()
 ****************************************************************************/
static void test_init_server_default_criteria(tpg_server_t *cfg,
                                              tpg_test_criteria_t *crit)
{
    uint32_t servers_cnt;

    servers_cnt = TPG_IPV4_RANGE_SIZE(&cfg->srv_ips) *
                    TPG_PORT_RANGE_SIZE(&cfg->srv_l4.l4s_tcp_udp.tus_ports);
    *crit = CRIT_SRV_UP(servers_cnt);
}

/*****************************************************************************
 * test_init_client_default_criteria()
 ****************************************************************************/
static void test_init_client_default_criteria(tpg_client_t *cfg __rte_unused,
                                              tpg_test_criteria_t *crit)
{
    *crit = CRIT_RUN_TIME(GCFG_TEST_MAX_TC_RUNTIME);
}

/*****************************************************************************
 * test_init_server_defaults()
 ****************************************************************************/
static void test_init_server_defaults(tpg_test_case_t *cfg)
{
    cfg->tc_server.srv_ips = TPG_IPV4_RANGE(1, 1);

    cfg->tc_server.srv_l4.l4s_proto = L4_PROTO__TCP;
    cfg->tc_server.srv_l4.l4s_tcp_udp.tus_ports = TPG_PORT_RANGE(1, 1);

    cfg->tc_server.srv_app.as_app_proto = APP_PROTO__RAW;

    APP_SRV_CALL(default_cfg, APP_PROTO__RAW)(cfg);
}

/*****************************************************************************
 * test_init_client_defaults()
 ****************************************************************************/
static void test_init_client_defaults(tpg_test_case_t *cfg)
{
    cfg->tc_client.cl_src_ips = TPG_IPV4_RANGE(1, 1);
    cfg->tc_client.cl_dst_ips = TPG_IPV4_RANGE(1, 1);

    cfg->tc_client.cl_l4.l4c_proto = L4_PROTO__TCP;
    cfg->tc_client.cl_l4.l4c_tcp_udp.tuc_sports = TPG_PORT_RANGE(1, 1);
    cfg->tc_client.cl_l4.l4c_tcp_udp.tuc_dports = TPG_PORT_RANGE(1, 1);

    cfg->tc_client.cl_rates.rc_open_rate = TPG_RATE_INF();
    cfg->tc_client.cl_rates.rc_close_rate = TPG_RATE_INF();
    cfg->tc_client.cl_rates.rc_send_rate = TPG_RATE_INF();

    cfg->tc_client.cl_delays.dc_init_delay = TPG_DELAY_INF();
    cfg->tc_client.cl_delays.dc_uptime = TPG_DELAY_INF();
    cfg->tc_client.cl_delays.dc_downtime = TPG_DELAY_INF();

    cfg->tc_client.cl_app.ac_app_proto = APP_PROTO__RAW;

    APP_CL_CALL(default_cfg, APP_PROTO__RAW)(cfg);
}

/*****************************************************************************
 * test_init_server_sockopt_defaults()
 ****************************************************************************/
static void test_init_server_sockopt_defaults(sockopt_t *sockopt,
                                              const tpg_server_t *server_cfg)
{
    tpg_tcp_sockopt_t default_tcp_sockopt;

    switch (server_cfg->srv_l4.l4s_proto) {
    case L4_PROTO__TCP:
        tpg_xlate_default_TcpSockopt(&default_tcp_sockopt);
        tcp_store_sockopt(&sockopt->so_tcp, &default_tcp_sockopt);
        break;
    default:
        bzero(sockopt, sizeof(*sockopt));
        return;
    }
}

/*****************************************************************************
 * test_init_client_sockopt_defaults()
 ****************************************************************************/
static void test_init_client_sockopt_defaults(sockopt_t *sockopt,
                                              const tpg_client_t *client_cfg)
{
    tpg_tcp_sockopt_t default_tcp_sockopt;

    switch (client_cfg->cl_l4.l4c_proto) {
    case L4_PROTO__TCP:
        tpg_xlate_default_TcpSockopt(&default_tcp_sockopt);
        tcp_store_sockopt(&sockopt->so_tcp, &default_tcp_sockopt);
        break;
    default:
        bzero(sockopt, sizeof(*sockopt));
        return;
    }
}

/*****************************************************************************
 * test_init_sockopt_defaults()
 ****************************************************************************/
static void test_init_sockopt_defaults(sockopt_t *sockopt,
                                       const tpg_test_case_t *te)
{
    /*
     * Setup L1 socket options, we currently only need to setup the
     * checksum offload flags. Now we copy them from the HW capabilities,
     * however later we can override them for example if we want to
     * force SW checksumming to introduce faults.
     */
    if ((port_dev_info[te->tc_eth_port].pi_dev_info.tx_offload_capa & DEV_TX_OFFLOAD_IPV4_CKSUM) != 0)
        sockopt->so_eth.ethso_tx_offload_ipv4_cksum = true;
    else
        sockopt->so_eth.ethso_tx_offload_ipv4_cksum = false;

    if ((port_dev_info[te->tc_eth_port].pi_dev_info.tx_offload_capa & DEV_TX_OFFLOAD_TCP_CKSUM) != 0)
        sockopt->so_eth.ethso_tx_offload_tcp_cksum = true;
    else
        sockopt->so_eth.ethso_tx_offload_tcp_cksum = false;

    if ((port_dev_info[te->tc_eth_port].pi_dev_info.tx_offload_capa & DEV_TX_OFFLOAD_UDP_CKSUM) != 0)
        sockopt->so_eth.ethso_tx_offload_udp_cksum = true;
    else
        sockopt->so_eth.ethso_tx_offload_udp_cksum = false;

    /*
     * Setup L4 socket options
     */
    switch (te->tc_type) {
    case TEST_CASE_TYPE__SERVER:
        test_init_server_sockopt_defaults(sockopt, &te->tc_server);
        break;
    case TEST_CASE_TYPE__CLIENT:
        test_init_client_sockopt_defaults(sockopt, &te->tc_client);
        break;
    default:
        return;
    }
}

/*****************************************************************************
 * Static Test case validate* functions.
 ****************************************************************************/
/*****************************************************************************
 * test_mgmt_validate_port_id()
 ****************************************************************************/
static bool test_mgmt_validate_port_id(uint32_t eth_port,
                                       printer_arg_t *printer_arg)
{
    if (eth_port >= rte_eth_dev_count()) {
        tpg_printf(printer_arg, "ERROR: Invalid ethernet port!\n");
        return false;
    }

    return true;
}

/*****************************************************************************
 * test_mgmt_validate_test_case_id()
 ****************************************************************************/
static bool test_mgmt_validate_test_case_id(uint32_t test_case_id,
                                            printer_arg_t *printer_arg)
{
    if (test_case_id >= TPG_TEST_MAX_ENTRIES) {
        tpg_printf(printer_arg, "ERROR: Invalid test case id!\n");
        return false;
    }

    return true;
}

/*****************************************************************************
 * test_mgmt_validate_ip_range()
 ****************************************************************************/
static bool
test_mgmt_validate_ip_range(const tpg_ip_range_t *range)
{
    return TPG_IP_GE(&range->ipr_end, &range->ipr_start);
}

/*****************************************************************************
 * test_mgmt_validate_l4port_range()
 ****************************************************************************/
static bool
test_mgmt_validate_l4port_range(const tpg_l4_port_range_t *range)
{
    return range->l4pr_start <= UINT16_MAX &&
           range->l4pr_end <= UINT16_MAX &&
           range->l4pr_end >= range->l4pr_start;
}

/*****************************************************************************
 * test_mgmt_validate_client_l4()
 ****************************************************************************/
static bool
test_mgmt_validate_client_l4(const tpg_l4_client_t *cfg,
                             printer_arg_t *printer_arg)
{
    switch (cfg->l4c_proto) {
    case L4_PROTO__TCP:
        /* Fallthrough. */
    case L4_PROTO__UDP:
        if (!test_mgmt_validate_l4port_range(&cfg->l4c_tcp_udp.tuc_sports)) {
            tpg_printf(printer_arg, "ERROR: Invalid L4 source port range!\n");
            return false;
        }

        if (!test_mgmt_validate_l4port_range(&cfg->l4c_tcp_udp.tuc_dports)) {
            tpg_printf(printer_arg, "ERROR: Invalid L4 dest port range!\n");
            return false;
        }
        break;
    default:
        return false;
    }

    return true;
}

/*****************************************************************************
 * test_mgmt_validate_client_app()
 ****************************************************************************/
static bool
test_mgmt_validate_client_app(const tpg_test_case_t *cfg,
                              printer_arg_t *printer_arg)
{
    tpg_app_proto_t app_id;

    app_id = cfg->tc_client.cl_app.ac_app_proto;
    return APP_CL_CALL(validate_cfg, app_id)(cfg, printer_arg);
}

/*****************************************************************************
 * test_mgmt_validate_test_case_client()
 ****************************************************************************/
static bool
test_mgmt_validate_test_case_client(const tpg_test_case_t *cfg,
                                    printer_arg_t *printer_arg)
{
    if (!test_mgmt_validate_ip_range(&cfg->tc_client.cl_src_ips)) {
        tpg_printf(printer_arg, "ERROR: Invalid source IP range!\n");
        return false;
    }

    if (!test_mgmt_validate_ip_range(&cfg->tc_client.cl_dst_ips)) {
        tpg_printf(printer_arg, "ERROR: Invalid destination IP range!\n");
        return false;
    }

    if (!test_mgmt_validate_client_l4(&cfg->tc_client.cl_l4, printer_arg))
        return false;

    if (cfg->tc_client.cl_app.ac_app_proto >= APP_PROTO__APP_PROTO_MAX) {
        tpg_printf(printer_arg, "ERROR: Invalid APP protocol type!\n");
        return false;
    }

    if (!test_mgmt_validate_client_app(cfg, printer_arg))
        return false;

    return true;
}

/*****************************************************************************
 * test_mgmt_validate_server_l4()
 ****************************************************************************/
static bool
test_mgmt_validate_server_l4(const tpg_l4_server_t *cfg,
                             printer_arg_t *printer_arg)
{
    switch (cfg->l4s_proto) {
    case L4_PROTO__TCP:
        /* Fallthrough. */
    case L4_PROTO__UDP:
        if (!test_mgmt_validate_l4port_range(&cfg->l4s_tcp_udp.tus_ports)) {
            tpg_printf(printer_arg, "ERROR: Invalid L4 port range!\n");
            return false;
        }
        break;
    default:
        return false;
    }

    return true;
}

/*****************************************************************************
 * test_mgmt_validate_server_app()
 ****************************************************************************/
static bool
test_mgmt_validate_server_app(const tpg_test_case_t *cfg,
                              printer_arg_t *printer_arg)
{
    tpg_app_proto_t app_id;

    app_id = cfg->tc_server.srv_app.as_app_proto;
    return APP_SRV_CALL(validate_cfg, app_id)(cfg, printer_arg);
}

/*****************************************************************************
 * test_mgmt_validate_test_case_server()
 ****************************************************************************/
static bool
test_mgmt_validate_test_case_server(const tpg_test_case_t *cfg,
                                    printer_arg_t *printer_arg)
{
    if (!test_mgmt_validate_ip_range(&cfg->tc_server.srv_ips)) {
        tpg_printf(printer_arg, "ERROR: Invalid IP range!\n");
        return false;
    }

    if (!test_mgmt_validate_server_l4(&cfg->tc_server.srv_l4, printer_arg))
        return false;

    if (cfg->tc_server.srv_app.as_app_proto >= APP_PROTO__APP_PROTO_MAX) {
        tpg_printf(printer_arg, "ERROR: Invalid APP protocol type!\n");
        return false;
    }

    if (!test_mgmt_validate_server_app(cfg, printer_arg))
        return false;

    return true;
}

/*****************************************************************************
 * test_mgmt_validate_test_case()
 ****************************************************************************/
static bool
test_mgmt_validate_test_case(const tpg_test_case_t *cfg,
                             printer_arg_t *printer_arg)
{
    if (!test_mgmt_validate_port_id(cfg->tc_eth_port, printer_arg) != 0)
        return false;

    if (!test_mgmt_validate_test_case_id(cfg->tc_id, printer_arg))
        return false;

    switch (cfg->tc_type) {
    case TEST_CASE_TYPE__SERVER:
        return test_mgmt_validate_test_case_server(cfg, printer_arg);
    case TEST_CASE_TYPE__CLIENT:
        return test_mgmt_validate_test_case_client(cfg, printer_arg);
    default:
        return false;
    }
}

/*****************************************************************************
 * test_mgmt_validate_port_options()
 ****************************************************************************/
static bool test_mgmt_validate_port_options(const tpg_port_options_t *options,
                                            printer_arg_t *printer_arg)
{
    if (options->po_mtu < PORT_MIN_MTU || options->po_mtu > PORT_MAX_MTU) {
        tpg_printf(printer_arg,
                   "ERROR: Invalid MTU value. Supported range: %u -> %u\n",
                   PORT_MIN_MTU,
                   PORT_MAX_MTU);
        return false;
    }

    return true;
}

/*****************************************************************************
 * test_mgmt_validate_tcp_sockopt()
 ****************************************************************************/
static bool test_mgmt_validate_tcp_sockopt(const tpg_tcp_sockopt_t *options,
                                           printer_arg_t *printer_arg)
{
    if (options->to_win_size > TCP_MAX_WINDOW_SIZE) {
        tpg_printf(printer_arg,
                   "ERROR: Invalid TCP window size. Max allowed: %u\n",
                   TCP_MAX_WINDOW_SIZE);
        return false;
    }

    if (options->to_syn_retry_cnt > TCP_MAX_RETRY_CNT) {
        tpg_printf(printer_arg,
                   "ERROR: Invalid TCP SYN retry count. Max allowed: %u\n",
                   TCP_MAX_RETRY_CNT);
        return false;
    }

    if (options->to_syn_ack_retry_cnt > TCP_MAX_RETRY_CNT) {
        tpg_printf(printer_arg,
                   "ERROR: Invalid TCP SYN/ACK retry count. Max allowed: %u\n",
                   TCP_MAX_RETRY_CNT);
        return false;
    }

    if (options->to_data_retry_cnt > TCP_MAX_RETRY_CNT) {
        tpg_printf(printer_arg,
                   "ERROR: Invalid TCP DATA retry count. Max allowed: %u\n",
                   TCP_MAX_RETRY_CNT);
        return false;
    }

    if (options->to_retry_cnt > TCP_MAX_RETRY_CNT) {
        tpg_printf(printer_arg,
                   "ERROR: Invalid TCP retry count. Max allowed: %u\n",
                   TCP_MAX_RETRY_CNT);
        return false;
    }

    if (options->to_rto > TCP_MAX_RTO_MS) {
        tpg_printf(printer_arg,
                   "ERROR: Invalid TCP retransmission timeout. Max allowed: %ums\n",
                   TCP_MAX_RTO_MS);
        return false;
    }

    if (options->to_fin_to > TCP_MAX_FIN_TO_MS) {
        tpg_printf(printer_arg,
                   "ERROR: Invalid TCP FIN timeout. Max allowed: %ums\n",
                   TCP_MAX_FIN_TO_MS);
        return false;
    }

    if (options->to_twait_to > TCP_MAX_TWAIT_TO_MS) {
        tpg_printf(printer_arg,
                   "ERROR: Invalid TCP TIME_WAIT timeout. Max allowed: %ums\n",
                   TCP_MAX_TWAIT_TO_MS);
        return false;
    }

    if (options->to_orphan_to > TCP_MAX_ORPHAN_TO_MS) {
        tpg_printf(printer_arg,
                   "ERROR: Invalid TCP orphan timeout. Max allowed: %ums\n",
                   TCP_MAX_ORPHAN_TO_MS);
        return false;
    }

    return true;
}

/*****************************************************************************
 * Static functions for checking API params.
 ****************************************************************************/
/*****************************************************************************
 * test_mgmt_add_port_cfg_check()
 ****************************************************************************/
static int test_mgmt_add_port_cfg_check(uint32_t eth_port,
                                        printer_arg_t *printer_arg)
{
    if (!test_mgmt_validate_port_id(eth_port, printer_arg))
        return -EINVAL;

    if (test_mgmt_get_port_env(eth_port)->te_test_running) {
        tpg_printf(printer_arg,
                   "ERROR: Test already running on port %"PRIu32"!\n",
                   eth_port);
        return -EALREADY;
    }

    return 0;
}

/*****************************************************************************
 * test_mgmt_add_test_case_check()
 ****************************************************************************/
static int test_mgmt_add_test_case_check(uint32_t eth_port,
                                         uint32_t test_case_id,
                                         test_env_t **tenv,
                                         printer_arg_t *printer_arg)
{
    if (!test_mgmt_validate_port_id(eth_port, printer_arg))
        return -EINVAL;

    if (!test_mgmt_validate_test_case_id(test_case_id, printer_arg))
        return -EINVAL;

    *tenv = test_mgmt_get_port_env(eth_port);
    if ((*tenv)->te_test_running)
        return -EALREADY;

    if ((*tenv)->te_test_cases[test_case_id].state.teos_configured)
        return -EEXIST;

    return 0;
}

/*****************************************************************************
 * test_mgmt_del_test_case_check()
 ****************************************************************************/
static int test_mgmt_del_test_case_check(uint32_t eth_port,
                                         uint32_t test_case_id,
                                         test_env_t **tenv,
                                         printer_arg_t *printer_arg)
{
    if (!test_mgmt_validate_port_id(eth_port, printer_arg))
        return -EINVAL;

    if (!test_mgmt_validate_test_case_id(test_case_id, printer_arg))
        return -EINVAL;

    *tenv = test_mgmt_get_port_env(eth_port);
    if ((*tenv)->te_test_running)
        return -EALREADY;

    if (!(*tenv)->te_test_cases[test_case_id].state.teos_configured)
        return -ENOENT;

    return 0;
}

/*****************************************************************************
 * test_mgmt_update_test_case_check()
 ****************************************************************************/
static int test_mgmt_update_test_case_check(uint32_t eth_port,
                                            uint32_t test_case_id,
                                            tpg_test_case_type_t tc_type,
                                            test_env_t **tenv,
                                            printer_arg_t *printer_arg)
{
    if (!test_mgmt_validate_port_id(eth_port, printer_arg))
        return -EINVAL;

    if (!test_mgmt_validate_test_case_id(test_case_id, printer_arg))
        return -EINVAL;

    *tenv = test_mgmt_get_port_env(eth_port);
    if ((*tenv)->te_test_running) {
        tpg_printf(printer_arg, "ERROR: Test running on port %"PRIu32"!\n",
                   eth_port);
        return -EALREADY;
    }

    if (!(*tenv)->te_test_cases[test_case_id].state.teos_configured) {
        tpg_printf(printer_arg,
                   "ERROR: Test case %"PRIu32
                   " not configured on port %"PRIu32"!\n",
                   test_case_id,
                   eth_port);
        return -ENOENT;
    }

    if (tc_type != TEST_CASE_TYPE__MAX &&
            (*tenv)->te_test_cases[test_case_id].cfg.tc_type != tc_type) {
        tpg_printf(printer_arg,
                   "ERROR: Update not supported for %s test type!\n",
                   test_case_type_names[tc_type]);
        return -EINVAL;
    }

    return 0;
}

/*****************************************************************************
 * test_mgmt_get_test_case_check()
 ****************************************************************************/
static int test_mgmt_get_test_case_check(uint32_t eth_port,
                                         uint32_t test_case_id,
                                         tpg_test_case_type_t tc_type,
                                         test_env_t **tenv,
                                         printer_arg_t *printer_arg)
{
    if (!test_mgmt_validate_port_id(eth_port, printer_arg))
        return -EINVAL;

    if (!test_mgmt_validate_test_case_id(test_case_id, printer_arg))
        return -EINVAL;

    *tenv = test_mgmt_get_port_env(eth_port);
    if (!(*tenv)->te_test_cases[test_case_id].state.teos_configured) {
        tpg_printf(printer_arg,
                   "ERROR: Test case %"PRIu32
                   " not configured on port %"PRIu32"!\n",
                   test_case_id,
                   eth_port);
        return -EALREADY;
    }

    if (tc_type != TEST_CASE_TYPE__MAX &&
            (*tenv)->te_test_cases[test_case_id].cfg.tc_type != tc_type) {
        tpg_printf(printer_arg, "ERROR: Unexpected %s test type!\n",
                   test_case_type_names[tc_type]);
        return -EINVAL;
    }

    return 0;
}

/*****************************************************************************
 * test_mgmt_app_del_test_case()
 ****************************************************************************/
static void test_mgmt_app_del_test_case(const tpg_test_case_t *cfg)
{
    tpg_app_proto_t app_id;

    switch (cfg->tc_type) {
    case TEST_CASE_TYPE__SERVER:
        app_id = cfg->tc_server.srv_app.as_app_proto;
        APP_SRV_CALL(delete_cfg, app_id)(cfg);
        break;
    case TEST_CASE_TYPE__CLIENT:
        app_id = cfg->tc_client.cl_app.ac_app_proto;
        APP_CL_CALL(delete_cfg, app_id)(cfg);
        break;
    default:
        assert(false);
        break;
    }
}

/*****************************************************************************
 * test_mgmt_get_sockopt()
 ****************************************************************************/
static int test_mgmt_get_sockopt(uint32_t eth_port, uint32_t test_case_id,
                                 sockopt_t **out,
                                 test_env_t **tenv,
                                 printer_arg_t *printer_arg)
{
    int err;

    if (!out)
        return -EINVAL;

    err = test_mgmt_get_test_case_check(eth_port, test_case_id,
                                        TEST_CASE_TYPE__MAX,
                                        tenv,
                                        printer_arg);
    if (err != 0)
        return err;

    *out = &(*tenv)->te_test_cases[test_case_id].sockopt;
    return 0;
}

/*****************************************************************************
 * API Implementation
 ****************************************************************************/
/*****************************************************************************
 * test_init_defaults()
 ****************************************************************************/
void test_init_defaults(tpg_test_case_t *te, tpg_test_case_type_t type,
                        uint32_t eth_port,
                        uint32_t test_case_id)
{
    te->tc_type = type;
    te->tc_eth_port = eth_port;
    te->tc_id = test_case_id;

    switch (te->tc_type) {
    case TEST_CASE_TYPE__SERVER:
        test_init_server_defaults(te);
        test_init_server_default_criteria(&te->tc_server, &te->tc_criteria);
        break;
    case TEST_CASE_TYPE__CLIENT:
        test_init_client_defaults(te);
        test_init_client_default_criteria(&te->tc_client, &te->tc_criteria);
        break;
    default:
        return;
    }

    te->tc_async = false;
}

/*****************************************************************************
 * test_mgmt_add_port_cfg()
 ****************************************************************************/
int test_mgmt_add_port_cfg(uint32_t eth_port, const tpg_port_cfg_t *cfg,
                           printer_arg_t *printer_arg)
{
    int err;

    if (!cfg)
        return -EINVAL;

    err = test_mgmt_add_port_cfg_check(eth_port, printer_arg);
    if (err != 0)
        return err;

    /* Struct copy. */
    test_mgmt_get_port_env(eth_port)->te_port_cfg = *cfg;

    return 0;
}

/*****************************************************************************
 * test_mgmt_get_port_cfg()
 ****************************************************************************/
const tpg_port_cfg_t *test_mgmt_get_port_cfg(uint32_t eth_port,
                                             printer_arg_t *printer_arg)
{
    if (!test_mgmt_validate_port_id(eth_port, printer_arg) != 0)
        return NULL;

    return &test_mgmt_get_port_env(eth_port)->te_port_cfg;
}

/*****************************************************************************
 * test_mgmt_add_port_cfg_l3_intf()
 ****************************************************************************/
int test_mgmt_add_port_cfg_l3_intf(uint32_t eth_port,
                                   const tpg_l3_intf_t *l3_intf,
                                   printer_arg_t *printer_arg)
{
    tpg_port_cfg_t *pcfg;
    int             err;
    uint32_t        i;


    if (!l3_intf)
        return -EINVAL;

    err = test_mgmt_add_port_cfg_check(eth_port, printer_arg);
    if (err != 0)
        return err;

    pcfg = &test_mgmt_get_port_env(eth_port)->te_port_cfg;

    for (i = 0; i < pcfg->pc_l3_intfs_count; i++) {
        if (TPG_IP_EQ(&pcfg->pc_l3_intfs[i].l3i_ip, &l3_intf->l3i_ip) &&
                TPG_IP_EQ(&pcfg->pc_l3_intfs[i].l3i_mask,
                          &l3_intf->l3i_mask)) {
            tpg_printf(printer_arg,
                       "ERROR: L3 interface already configured!\n");
            return -EEXIST;
        }
    }

    if (pcfg->pc_l3_intfs_count == TPG_TEST_MAX_L3_INTF)
        return -ENOMEM;

    /* Struct copy. */
    pcfg->pc_l3_intfs[pcfg->pc_l3_intfs_count] = *l3_intf;
    pcfg->pc_l3_intfs_count++;
    return 0;
}

/*****************************************************************************
 * test_mgmt_add_port_cfg_l3_gw()
 ****************************************************************************/
int test_mgmt_add_port_cfg_l3_gw(uint32_t eth_port, tpg_ip_t *gw,
                                 printer_arg_t *printer_arg)
{
    int err;

    err = test_mgmt_add_port_cfg_check(eth_port, printer_arg);
    if (err != 0)
        return err;

    /* Struct copy. */
    test_mgmt_get_port_env(eth_port)->te_port_cfg.pc_def_gw = *gw;
    return 0;
}

/*****************************************************************************
 * test_mgmt_add_test_case()
 ****************************************************************************/
int test_mgmt_add_test_case(uint32_t eth_port, const tpg_test_case_t *cfg,
                            printer_arg_t *printer_arg)
{
    test_env_t *tenv;
    int         err;

    if (!cfg)
        return -EINVAL;

    err = test_mgmt_add_test_case_check(eth_port, cfg->tc_id, &tenv,
                                        printer_arg);
    if (err != 0)
        return err;

    if (!test_mgmt_validate_test_case(cfg, printer_arg))
        return -EINVAL;

    /* Struct copy. */
    tenv->te_test_cases[cfg->tc_id].cfg = *cfg;

    tenv->te_test_cases[cfg->tc_id].state.teos_configured = true;
    test_init_sockopt_defaults(&tenv->te_test_cases[cfg->tc_id].sockopt, cfg);
    tenv->te_test_cases_count++;
    return 0;
}

/*****************************************************************************
 * test_mgmt_del_test_case()
 ****************************************************************************/
int
test_mgmt_del_test_case(uint32_t eth_port, uint32_t test_case_id,
                        printer_arg_t *printer_arg)
{
    test_env_t *tenv;
    int         err;

    err = test_mgmt_del_test_case_check(eth_port, test_case_id, &tenv,
                                        printer_arg);
    if (err != 0)
        return err;

    /* Call the application delete callback to cleanup any memory that was
     * allocated for the application config.
     */
    test_mgmt_app_del_test_case(&tenv->te_test_cases[test_case_id].cfg);

    /* Unconfigure means just resetting the bit.. */
    tenv->te_test_cases[test_case_id].state.teos_configured = false;
    tenv->te_test_cases_count--;
    return 0;
}

/*****************************************************************************
 * test_mgmt_update_test_case()
 ****************************************************************************/
int test_mgmt_update_test_case(uint32_t eth_port, uint32_t test_case_id,
                               const tpg_update_arg_t *arg,
                               printer_arg_t *printer_arg)
{
    test_env_t           *tenv;
    tpg_test_case_type_t  tc_type = TEST_CASE_TYPE__MAX;
    tpg_test_case_t      *test_case;
    int                   err;

    if (!arg)
        return -EINVAL;

    if (arg->ua_rate_open || arg->ua_rate_send || arg->ua_rate_close ||
            arg->ua_init_delay || arg->ua_uptime || arg->ua_downtime)
        tc_type = TEST_CASE_TYPE__CLIENT;

    err = test_mgmt_update_test_case_check(eth_port, test_case_id, tc_type,
                                           &tenv,
                                           printer_arg);
    if (err != 0)
        return err;

    test_case = &tenv->te_test_cases[test_case_id].cfg;

    if (arg->ua_rate_open)
        test_case->tc_client.cl_rates.rc_open_rate = *arg->ua_rate_open;

    if (arg->ua_rate_send)
        test_case->tc_client.cl_rates.rc_send_rate = *arg->ua_rate_send;

    if (arg->ua_rate_close)
        test_case->tc_client.cl_rates.rc_close_rate = *arg->ua_rate_close;

    if (arg->ua_init_delay)
        test_case->tc_client.cl_delays.dc_init_delay = *arg->ua_init_delay;

    if (arg->ua_uptime)
        test_case->tc_client.cl_delays.dc_uptime = *arg->ua_uptime;

    if (arg->ua_downtime)
        test_case->tc_client.cl_delays.dc_downtime = *arg->ua_downtime;

    if (arg->ua_criteria)
        test_case->tc_criteria = *arg->ua_criteria;

    if (arg->has_ua_async)
        test_case->tc_async = arg->ua_async;

    return 0;
}

/*****************************************************************************
 * test_mgmt_get_test_case_cfg()
 ****************************************************************************/
int
test_mgmt_get_test_case_cfg(uint32_t eth_port, uint32_t test_case_id,
                            tpg_test_case_t *out,
                            printer_arg_t *printer_arg)
{
    int         err;
    test_env_t *tenv;

    if (!out)
        return -EINVAL;

    err = test_mgmt_get_test_case_check(eth_port, test_case_id,
                                        TEST_CASE_TYPE__MAX,
                                        &tenv,
                                        printer_arg);
    if (err != 0)
        return err;

    *out = tenv->te_test_cases[test_case_id].cfg;
    return 0;
}

/*****************************************************************************
 * test_mgmt_get_test_case_app_client_cfg()
 ****************************************************************************/
int test_mgmt_get_test_case_app_client_cfg(uint32_t eth_port,
                                           uint32_t test_case_id,
                                           tpg_app_client_t *out,
                                           printer_arg_t *printer_arg)
{
    int         err;
    test_env_t *tenv;

    if (!out)
        return -EINVAL;

    err = test_mgmt_get_test_case_check(eth_port, test_case_id,
                                        TEST_CASE_TYPE__CLIENT,
                                        &tenv,
                                        printer_arg);
    if (err != 0)
        return err;

    *out = tenv->te_test_cases[test_case_id].cfg.tc_client.cl_app;
    return 0;
}

/*****************************************************************************
 * test_mgmt_get_test_case_app_server_cfg()
 ****************************************************************************/
int test_mgmt_get_test_case_app_server_cfg(uint32_t eth_port,
                                           uint32_t test_case_id,
                                           tpg_app_server_t *out,
                                           printer_arg_t *printer_arg)
{
    int         err;
    test_env_t *tenv;

    if (!out)
        return -EINVAL;

    err = test_mgmt_get_test_case_check(eth_port, test_case_id,
                                        TEST_CASE_TYPE__SERVER,
                                        &tenv,
                                        printer_arg);
    if (err != 0)
        return err;

    *out = tenv->te_test_cases[test_case_id].cfg.tc_server.srv_app;
    return 0;
}

/*****************************************************************************
 * test_mgmt_update_test_case_app_client()
 ****************************************************************************/
int test_mgmt_update_test_case_app_client(uint32_t eth_port,
                                          uint32_t test_case_id,
                                          const tpg_app_client_t *app_cl_cfg,
                                          printer_arg_t *printer_arg)
{
    test_env_t      *tenv;
    tpg_test_case_t  tmp_cfg;
    int              err;

    if (!app_cl_cfg)
        return -EINVAL;

    err = test_mgmt_update_test_case_check(eth_port, test_case_id,
                                           TEST_CASE_TYPE__CLIENT,
                                           &tenv,
                                           printer_arg);
    if (err != 0)
        return err;

    /* Struct copy. */
    tmp_cfg = tenv->te_test_cases[test_case_id].cfg;
    tmp_cfg.tc_client.cl_app = *app_cl_cfg;

    if (!test_mgmt_validate_test_case_client(&tmp_cfg, printer_arg))
        return -EINVAL;

    /* Call the application delete callback to cleanup any memory that was
     * allocated for the application config.
     */
    test_mgmt_app_del_test_case(&tenv->te_test_cases[test_case_id].cfg);

    /* Struct copy. */
    tenv->te_test_cases[test_case_id].cfg.tc_client.cl_app = *app_cl_cfg;

    return 0;
}

/*****************************************************************************
 * test_mgmt_update_test_case_app_server()
 ****************************************************************************/
int test_mgmt_update_test_case_app_server(uint32_t eth_port,
                                          uint32_t test_case_id,
                                          const tpg_app_server_t *app_srv_cfg,
                                          printer_arg_t *printer_arg)
{
    test_env_t      *tenv;
    tpg_test_case_t  tmp_cfg;
    int              err;

    if (!app_srv_cfg)
        return -EINVAL;

    err = test_mgmt_update_test_case_check(eth_port, test_case_id,
                                           TEST_CASE_TYPE__SERVER,
                                           &tenv,
                                           printer_arg);
    if (err != 0)
        return err;

    /* Struct copy. */
    tmp_cfg = tenv->te_test_cases[test_case_id].cfg;
    tmp_cfg.tc_server.srv_app = *app_srv_cfg;

    if (!test_mgmt_validate_test_case_server(&tmp_cfg, printer_arg))
        return -EINVAL;

    /* Call the application delete callback to cleanup any memory that was
     * allocated for the application config.
     */
    test_mgmt_app_del_test_case(&tenv->te_test_cases[test_case_id].cfg);

    /* Struct copy. */
    tenv->te_test_cases[test_case_id].cfg.tc_server.srv_app = *app_srv_cfg;

    return 0;
}

/*****************************************************************************
 * test_mgmt_get_test_case_count()
 ****************************************************************************/
uint32_t
test_mgmt_get_test_case_count(uint32_t eth_port)
{
    if (!test_mgmt_validate_port_id(eth_port, NULL))
        return 0;

    return test_mgmt_get_port_env(eth_port)->te_test_cases_count;
}

/*****************************************************************************
 * test_mgmt_get_test_case_state()
 ****************************************************************************/
int
test_mgmt_get_test_case_state(uint32_t eth_port, uint32_t test_case_id,
                              test_env_oper_state_t *out,
                              printer_arg_t *printer_arg)
{
    int         err;
    test_env_t *tenv;

    if (!out)
        return -EINVAL;

    err = test_mgmt_get_test_case_check(eth_port, test_case_id,
                                        TEST_CASE_TYPE__MAX,
                                        &tenv,
                                        printer_arg);
    if (err != 0)
        return err;

    *out = tenv->te_test_cases[test_case_id].state;
    return 0;
}

/*****************************************************************************
 * test_mgmt_set_port_options()
 ****************************************************************************/
int
test_mgmt_set_port_options(uint32_t eth_port, tpg_port_options_t *opts,
                           printer_arg_t *printer_arg)
{
    tpg_port_options_t old_opts;
    int                err;

    if (!opts)
        return -EINVAL;

    err = test_mgmt_add_port_cfg_check(eth_port, printer_arg);
    if (err != 0)
        return err;

    port_get_conn_options(eth_port, &old_opts);

    if (opts->has_po_mtu)
        old_opts.po_mtu = opts->po_mtu;

    if (!test_mgmt_validate_port_options(opts, printer_arg))
        return -EINVAL;

    err = port_set_conn_options(eth_port, opts);
    if (err != 0)
        return err;

    return 0;
}

/*****************************************************************************
 * test_mgmt_get_port_options()
 ****************************************************************************/
int test_mgmt_get_port_options(uint32_t eth_port, tpg_port_options_t *out,
                               printer_arg_t *printer_arg)
{
    if (!out)
        return -EINVAL;

    if (!test_mgmt_validate_port_id(eth_port, printer_arg))
        return -EINVAL;

    port_get_conn_options(eth_port, out);
    return 0;
}

/*****************************************************************************
 * test_mgmt_set_tcp_sockopt()
 ****************************************************************************/
int
test_mgmt_set_tcp_sockopt(uint32_t eth_port, uint32_t test_case_id,
                          const tpg_tcp_sockopt_t *opts,
                          printer_arg_t *printer_arg)
{
    tpg_tcp_sockopt_t  old_opts;
    test_env_t        *tenv;
    tpg_test_case_t   *te_cfg;
    int                err;

    if (!opts)
        return -EINVAL;

    err = test_mgmt_update_test_case_check(eth_port, test_case_id,
                                           TEST_CASE_TYPE__MAX,
                                           &tenv,
                                           printer_arg);
    if (err != 0)
        return err;

    te_cfg = &tenv->te_test_cases[test_case_id].cfg;

    /* Only valid for TCP. */
    if ((te_cfg->tc_type == TEST_CASE_TYPE__SERVER &&
            te_cfg->tc_server.srv_l4.l4s_proto != L4_PROTO__TCP) ||
        (te_cfg->tc_type == TEST_CASE_TYPE__CLIENT &&
            te_cfg->tc_client.cl_l4.l4c_proto != L4_PROTO__TCP)) {
        tpg_printf(printer_arg, "ERROR: Test case L4 type is not TCP!\n");
        return -EINVAL;
    }

    tcp_load_sockopt(&old_opts,
                     &tenv->te_test_cases[test_case_id].sockopt.so_tcp);

    if (opts->has_to_win_size)
        old_opts.to_win_size = opts->to_win_size;

    if (opts->has_to_syn_retry_cnt)
        old_opts.to_syn_retry_cnt = opts->to_syn_retry_cnt;

    if (opts->has_to_syn_ack_retry_cnt)
        old_opts.to_syn_ack_retry_cnt = opts->to_syn_ack_retry_cnt;

    if (opts->has_to_data_retry_cnt)
        old_opts.to_data_retry_cnt = opts->to_data_retry_cnt;

    if (opts->has_to_retry_cnt)
        old_opts.to_retry_cnt = opts->to_retry_cnt;

    if (opts->has_to_rto)
        old_opts.to_rto = opts->to_rto;

    if (opts->has_to_fin_to)
        old_opts.to_fin_to = opts->to_fin_to;

    if (opts->has_to_twait_to)
        old_opts.to_twait_to = opts->to_twait_to;

    if (opts->has_to_orphan_to)
        old_opts.to_orphan_to = opts->to_orphan_to;

    if (opts->has_to_skip_timewait)
        old_opts.to_skip_timewait = opts->to_skip_timewait;

    if (!test_mgmt_validate_tcp_sockopt(&old_opts, printer_arg))
        return -EINVAL;

    tcp_store_sockopt(&tenv->te_test_cases[test_case_id].sockopt.so_tcp,
                      &old_opts);
    return 0;
}

/*****************************************************************************
 * test_mgmt_get_tcp_sockopt()
 ****************************************************************************/
int test_mgmt_get_tcp_sockopt(uint32_t eth_port, uint32_t test_case_id,
                              tpg_tcp_sockopt_t *out,
                              printer_arg_t *printer_arg)
{
    test_env_t      *tenv;
    tpg_test_case_t *te_cfg;
    sockopt_t       *sockopt;
    int              err;

    if (!out)
        return -EINVAL;

    err = test_mgmt_get_sockopt(eth_port, test_case_id, &sockopt, &tenv,
                                printer_arg);
    if (err != 0)
        return err;

    te_cfg = &tenv->te_test_cases[test_case_id].cfg;

    /* Only valid for TCP. */
    if ((te_cfg->tc_type == TEST_CASE_TYPE__SERVER &&
            te_cfg->tc_server.srv_l4.l4s_proto != L4_PROTO__TCP) ||
        (te_cfg->tc_type == TEST_CASE_TYPE__CLIENT &&
            te_cfg->tc_client.cl_l4.l4c_proto != L4_PROTO__TCP)) {
        tpg_printf(printer_arg, "ERROR: Test case L4 type is not TCP!\n");
        return -EINVAL;
    }

    tcp_load_sockopt(out, &sockopt->so_tcp);
    return 0;
}
/*****************************************************************************
 * test_mgmt_clear_stats()
 ****************************************************************************/
int test_mgmt_clear_stats(uint32_t eth_port, printer_arg_t *printer_arg)
{
    if (!test_mgmt_validate_port_id(eth_port, printer_arg)){
        tpg_printf(printer_arg,
                   "ERROR: Invalid port %"PRIu32"!\n",
                   eth_port);
        return -EINVAL;
    }

    if (test_mgmt_get_port_env(eth_port)->te_test_running){
        tpg_printf(printer_arg,
                   "ERROR: Test already running on port %"PRIu32"!\n",
                   eth_port);
        return -EALREADY;
    }

    /* Clearing MSG stats */
    msg_total_stats_clear(eth_port);

    /* Clearing ARP stats */
    arp_total_stats_clear(eth_port);
    /* Clearing ROUTE stats */
    route_total_stats_clear(eth_port);
    /* Clearing TIMER stats */
    timer_total_stats_clear(eth_port);

    /* Clearing TCP stats */
    tcp_total_stats_clear(eth_port);
    /* Clearing TSM stats */
    tsm_total_stats_clear(eth_port);

    /* Clearing UDP stats */
    udp_total_stats_clear(eth_port);

    /* Clearing IPv4 stats */
    ipv4_total_stats_clear(eth_port);

    /* Clearing PORT stats */
    port_total_stats_clear(eth_port);
    /* Clearing ETHERNET stats */
    eth_total_stats_clear(eth_port);

    return 0;
}

/*****************************************************************************
 * test_mgmt_start_port()
 ****************************************************************************/
int test_mgmt_start_port(uint32_t eth_port, printer_arg_t *printer_arg)
{
    msg_t *msgp;
    MSG_LOCAL_DEFINE(test_start_msg_t, start_msg);

    if (!test_mgmt_validate_port_id(eth_port, printer_arg))
        return -EINVAL;

    if (test_mgmt_get_port_env(eth_port)->te_test_running)
        return -EALREADY;

    if (test_mgmt_get_test_case_count(eth_port) == 0)
        return -ENOENT;

    msgp = MSG_LOCAL(start_msg);
    msg_init(msgp, MSG_TEST_MGMT_START_TEST, cfg_get_test_mgmt_core(), 0);

    MSG_INNER(test_start_msg_t, msgp)->tssm_eth_port = eth_port;

    /*
     * Will wait for the message to be processed. We need to do it because we
     * read the data in the test test_cases that are updated by the test
     * management thread.
     */
    return msg_send(msgp, 0);
}

/*****************************************************************************
 * test_mgmt_stop_port()
 ****************************************************************************/
int test_mgmt_stop_port(uint32_t eth_port, printer_arg_t *printer_arg)
{
    msg_t *msgp;
    MSG_LOCAL_DEFINE(test_stop_msg_t, stop_msg);

    if (!test_mgmt_validate_port_id(eth_port, printer_arg))
        return -EINVAL;

    if (!test_mgmt_get_port_env(eth_port)->te_test_running)
        return -ENOENT;

    msgp = MSG_LOCAL(stop_msg);
    msg_init(msgp, MSG_TEST_MGMT_STOP_TEST, cfg_get_test_mgmt_core(), 0);

    MSG_INNER(test_stop_msg_t, msgp)->tssm_eth_port = eth_port;

    /* Send the message and wait for it to be processed. */
    return msg_send(msgp, 0);
}

/*****************************************************************************
 * test_mgmt_get_test_case_stats()
 ****************************************************************************/
int
test_mgmt_get_test_case_stats(uint32_t eth_port, uint32_t test_case_id,
                              tpg_test_case_stats_t *out,
                              printer_arg_t *printer_arg)
{
    int         err;
    test_env_t *tenv;

    if (!out)
        return -EINVAL;

    err = test_mgmt_get_test_case_check(eth_port, test_case_id,
                                        TEST_CASE_TYPE__MAX,
                                        &tenv,
                                        printer_arg);
    if (err != 0)
        return err;

    *out = *test_mgmt_get_stats(eth_port, test_case_id);
    return 0;
}

/*****************************************************************************
 * test_mgmt_get_test_case_rate_stats()
 ****************************************************************************/
int
test_mgmt_get_test_case_rate_stats(uint32_t eth_port, uint32_t test_case_id,
                                   tpg_test_case_rate_stats_t *out,
                                   printer_arg_t *printer_arg)
{
    int         err;
    test_env_t *tenv;

    if (!out)
        return -EINVAL;

    err = test_mgmt_get_test_case_check(eth_port, test_case_id,
                                        TEST_CASE_TYPE__MAX,
                                        &tenv,
                                        printer_arg);
    if (err != 0)
        return err;

    *out = *test_mgmt_get_rate_stats(eth_port, test_case_id);
    return 0;
}

/*****************************************************************************
 * test_mgmt_get_test_case_app_stats()
 ****************************************************************************/
int
test_mgmt_get_test_case_app_stats(uint32_t eth_port, uint32_t test_case_id,
                                  tpg_test_case_app_stats_t *out,
                                  printer_arg_t *printer_arg)
{
    int         err;
    test_env_t *tenv;

    if (!out)
        return -EINVAL;

    err = test_mgmt_get_test_case_check(eth_port, test_case_id,
                                        TEST_CASE_TYPE__MAX,
                                        &tenv,
                                        printer_arg);
    if (err != 0)
        return err;

    *out = *test_mgmt_get_app_stats(eth_port, test_case_id);
    return 0;
}

