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

STATS_DEFINE(tpg_phy_statistics_t);

/*****************************************************************************
 * Globals
 ****************************************************************************/
static const char *test_case_type_names[TEST_CASE_TYPE__MAX] = {
    [TEST_CASE_TYPE__CLIENT] = TEST_CASE_CLIENT_STR,
    [TEST_CASE_TYPE__SERVER] = TEST_CASE_SERVER_STR,
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

    cfg->tc_init_delay = TPG_DELAY(0);
    cfg->tc_uptime = TPG_DELAY_INF();
    cfg->tc_downtime = TPG_DELAY_INF();

    cfg->tc_app.app_proto = APP_PROTO__RAW_SERVER;
    APP_CALL(default_cfg, APP_PROTO__RAW_SERVER)(cfg);
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

    cfg->tc_client.cl_mcast_src = false;

    cfg->tc_init_delay = TPG_DELAY(0);
    cfg->tc_uptime = TPG_DELAY_INF();
    cfg->tc_downtime = TPG_DELAY_INF();

    cfg->tc_app.app_proto = APP_PROTO__RAW_CLIENT;
    APP_CALL(default_cfg, APP_PROTO__RAW_CLIENT)(cfg);
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
        return;
    }
}

/*****************************************************************************
 * test_init_sockopt_defaults()
 ****************************************************************************/
static void test_init_sockopt_defaults(sockopt_t *sockopt,
                                       const tpg_test_case_t *te)
{
    tpg_ipv4_sockopt_t default_ipv4_sockopt;
    tpg_vlan_sockopt_t default_vlan_sockopt;

    bzero(sockopt, sizeof(*sockopt));

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
    * Setup L2 socket options.
    */
    tpg_xlate_default_VlanSockopt(&default_vlan_sockopt);
    vlan_store_sockopt(&sockopt->so_vlan, &default_vlan_sockopt);

    /*
     * Setup L3 socket options.
     */
    tpg_xlate_default_Ipv4Sockopt(&default_ipv4_sockopt);
    ipv4_store_sockopt(&sockopt->so_ipv4, &default_ipv4_sockopt);

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
 * test_mgmt_validate_ip_unicast_range()
 ****************************************************************************/
static bool test_mgmt_validate_ip_unicast_range(const tpg_ip_range_t *range)
{
    tpg_ip_t min_mcast_ip = TPG_IP_MCAST_MIN(&range->ipr_start.ip_version);
    tpg_ip_t max_mcast_ip = TPG_IP_MCAST_MAX(&range->ipr_start.ip_version);

    if (!test_mgmt_validate_ip_range(range))
        return false;

    if (TPG_IP_GE(&range->ipr_start, &min_mcast_ip) &&
            TPG_IP_GE(&max_mcast_ip, &range->ipr_end))
        return false;

    if (TPG_IP_GE(&range->ipr_end, &min_mcast_ip) &&
            TPG_IP_GE(&max_mcast_ip, &range->ipr_end))
        return false;

    if (TPG_IP_GE(&range->ipr_end, &max_mcast_ip) &&
            TPG_IP_GE(&min_mcast_ip, &range->ipr_start))
        return false;

    if (TPG_IP_BCAST(&range->ipr_end))
        return false;

    return true;
}

/*****************************************************************************
 * test_mgmt_validate_ip_mcast_range()
 ****************************************************************************/
static bool test_mgmt_validate_ip_mcast_range(const tpg_ip_range_t *range)
{
    return test_mgmt_validate_ip_range(range) &&
                TPG_IP_MCAST(&range->ipr_start) &&
                TPG_IP_MCAST(&range->ipr_end);
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

    if (cfg->tc_client.cl_mcast_src) {
        /* Validate Multicast IP ranges. */
        if (!test_mgmt_validate_ip_mcast_range(&cfg->tc_client.cl_dst_ips)) {
            tpg_printf(printer_arg,
                       "ERROR: Invalid destination IP Multicast range!\n");
            return false;
        }

        /* Only allow UDP clients to act as multicast sources. */
        if (cfg->tc_client.cl_l4.l4c_proto != L4_PROTO__UDP) {
            tpg_printf(printer_arg,
                       "ERROR: Only UDP multicast sources allowed!\n");
            return false;
        }
    } else {
        /* Validate Unicast Dest IP ranges. */
        if (!test_mgmt_validate_ip_unicast_range(&cfg->tc_client.cl_dst_ips)) {
            tpg_printf(printer_arg,
                       "ERROR: Invalid destination IP Unicast range!\n");
            return false;
        }
    }

    /* Validate Unicast SRC IP ranges. */
    if (!test_mgmt_validate_ip_unicast_range(&cfg->tc_client.cl_src_ips)) {
        tpg_printf(printer_arg,
                   "ERROR: Invalid source IP Unicast range!\n");
        return false;
    }

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
 * test_mgmt_validate_app()
 ****************************************************************************/
static bool
test_mgmt_validate_app(const tpg_test_case_t *cfg, printer_arg_t *printer_arg)
{
    tpg_app_proto_t app_id;

    app_id = cfg->tc_app.app_proto;

    if (app_id >= APP_PROTO__APP_PROTO_MAX) {
        tpg_printf(printer_arg, "ERROR: Invalid APP protocol type!\n");
        return false;
    }

    return APP_CALL(validate_cfg, app_id)(cfg, &cfg->tc_app, printer_arg);
}

/*****************************************************************************
 * test_mgmt_validate_latency()
 ****************************************************************************/
static bool test_mgmt_validate_latency(const tpg_test_case_t *cfg,
                                       printer_arg_t *printer_arg)
{
    if (!cfg->has_tc_latency)
        return true;

    if (cfg->tc_latency.tcs_samples > TPG_TSTAMP_SAMPLES_MAX_BUFSIZE) {
        tpg_printf(printer_arg, "ERROR: Max allowed samples count is %u!\n",
                   TPG_TSTAMP_SAMPLES_MAX_BUFSIZE);
        return false;
    }

    return true;
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

    /* Don't allow server test-case timeouts (i.e., init/uptime/downtime). */
    if (cfg->has_tc_init_delay || cfg->has_tc_uptime || cfg->has_tc_downtime) {
        tpg_printf(printer_arg,
                   "ERROR: test case timeouts not supported on server test cases!\n");
        return false;
    }

    return true;
}

/*****************************************************************************
 * test_mgmt_validate_test_case()
 ****************************************************************************/
static bool
test_mgmt_validate_test_case(const tpg_test_case_t *cfg,
                             printer_arg_t *printer_arg)
{
    if (!test_mgmt_validate_port_id(cfg->tc_eth_port, printer_arg))
        return false;

    if (!test_mgmt_validate_test_case_id(cfg->tc_id, printer_arg))
        return false;

    if (!test_mgmt_validate_latency(cfg, printer_arg))
        return false;

    if (!test_mgmt_validate_app(cfg, printer_arg))
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
                                            printer_arg_t *printer_arg,
                                            uint32_t port)
{
    if (options->has_po_mtu &&
            (options->po_mtu < PORT_MIN_MTU ||
             options->po_mtu > port_dev_info[port].pi_dev_info.max_rx_pktlen)) {
        tpg_printf(printer_arg,
                   "ERROR: Invalid MTU value. Supported range: %u -> %u\n",
                   PORT_MIN_MTU,
                   port_dev_info[port].pi_dev_info.max_rx_pktlen);
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
    if (options->has_to_win_size &&
            options->to_win_size > TCP_MAX_WINDOW_SIZE) {
        tpg_printf(printer_arg,
                   "ERROR: Invalid TCP window size. Max allowed: %u\n",
                   TCP_MAX_WINDOW_SIZE);
        return false;
    }

    if (options->has_to_syn_retry_cnt &&
            options->to_syn_retry_cnt > TCP_MAX_RETRY_CNT) {
        tpg_printf(printer_arg,
                   "ERROR: Invalid TCP SYN retry count. Max allowed: %u\n",
                   TCP_MAX_RETRY_CNT);
        return false;
    }

    if (options->has_to_syn_ack_retry_cnt &&
            options->to_syn_ack_retry_cnt > TCP_MAX_RETRY_CNT) {
        tpg_printf(printer_arg,
                   "ERROR: Invalid TCP SYN/ACK retry count. Max allowed: %u\n",
                   TCP_MAX_RETRY_CNT);
        return false;
    }

    if (options->has_to_data_retry_cnt &&
            options->to_data_retry_cnt > TCP_MAX_RETRY_CNT) {
        tpg_printf(printer_arg,
                   "ERROR: Invalid TCP DATA retry count. Max allowed: %u\n",
                   TCP_MAX_RETRY_CNT);
        return false;
    }

    if (options->has_to_retry_cnt &&
            options->to_retry_cnt > TCP_MAX_RETRY_CNT) {
        tpg_printf(printer_arg,
                   "ERROR: Invalid TCP retry count. Max allowed: %u\n",
                   TCP_MAX_RETRY_CNT);
        return false;
    }

    if (options->has_to_rto && options->to_rto > TCP_MAX_RTO_MS) {
        tpg_printf(printer_arg,
                   "ERROR: Invalid TCP retransmission timeout. Max allowed: %ums\n",
                   TCP_MAX_RTO_MS);
        return false;
    }

    if (options->has_to_fin_to && options->to_fin_to > TCP_MAX_FIN_TO_MS) {
        tpg_printf(printer_arg,
                   "ERROR: Invalid TCP FIN timeout. Max allowed: %ums\n",
                   TCP_MAX_FIN_TO_MS);
        return false;
    }

    if (options->has_to_twait_to &&
            options->to_twait_to > TCP_MAX_TWAIT_TO_MS) {
        tpg_printf(printer_arg,
                   "ERROR: Invalid TCP TIME_WAIT timeout. Max allowed: %ums\n",
                   TCP_MAX_TWAIT_TO_MS);
        return false;
    }

    if (options->has_to_orphan_to &&
            options->to_orphan_to > TCP_MAX_ORPHAN_TO_MS) {
        tpg_printf(printer_arg,
                   "ERROR: Invalid TCP orphan timeout. Max allowed: %ums\n",
                   TCP_MAX_ORPHAN_TO_MS);
        return false;
    }

    return true;
}

/*****************************************************************************
 * test_mgmt_validate_ipv4_sockopt()
 ****************************************************************************/
static bool
test_mgmt_validate_ipv4_sockopt(const tpg_ipv4_sockopt_t *options,
                                printer_arg_t *printer_arg)
{
    /* The only option we have for now is TOS and we allow any possible 8-bit
     * value in case the user wants to test with undefined TOS values.
     */
    if (options->has_ip4so_tos && options->ip4so_tos > UINT8_MAX) {
        tpg_printf(printer_arg,
                   "ERROR: Invalid IPv4 ToS. Max allowed: %u\n",
                   UINT8_MAX);
        return false;
    }

    return true;
}

/*****************************************************************************
 * test_mgmt_validate_vlan_sockopt()
 ****************************************************************************/
static bool
test_mgmt_validate_vlan_sockopt(const tpg_vlan_sockopt_t *options,
                                printer_arg_t *printer_arg)
{
     /* Basic validation for user provided vlan options */
    if (options->has_vlanso_id &&
            (options->vlanso_id < VLAN_MIN || options->vlanso_id > VLAN_MAX)) {
        tpg_printf(printer_arg,
                   "ERROR: Invalid value for vlan-id: Valid range are %u-%u\n",
                   VLAN_MIN, VLAN_MAX);
        return false;
    }

    if (options->has_vlanso_pri) {
        if (!options->has_vlanso_id) {
            tpg_printf(printer_arg,
                       "ERROR: VLAN PRI configured without VLAN ID!\n");
            return false;
        }

        if (options->vlanso_pri > 7) {
            tpg_printf(printer_arg,
                       "ERROR: Invalid value for vlan-pri: Valid range are %u-%u\n",
                       0, 7);
            return false;
        }
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
 * test_mgmt_app_add_test_case()
 ****************************************************************************/
static void test_mgmt_app_add_test_case(const tpg_test_case_t *cfg)
{
    APP_CALL(add_cfg, cfg->tc_app.app_proto)(cfg, &cfg->tc_app);
}

/*****************************************************************************
 * test_mgmt_app_del_test_case()
 ****************************************************************************/
static void test_mgmt_app_del_test_case(const tpg_test_case_t *cfg)
{
    APP_CALL(delete_cfg, cfg->tc_app.app_proto)(cfg, &cfg->tc_app);
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
 * test_mgmt_clear_tsm_statistics()
 ****************************************************************************/
static void test_mgmt_clear_tsm_statistics(uint32_t eth_port)
{
    uint32_t              core;
    tpg_tsm_statistics_t *stats;

    STATS_FOREACH_CORE(tpg_tsm_statistics_t, eth_port, core, stats) {
        stats->tsms_syn_to = 0;
        stats->tsms_synack_to = 0;
        stats->tsms_retry_to = 0;
        stats->tsms_retrans_bytes = 0;
        stats->tsms_missing_seq = 0;
        stats->tsms_snd_win_full = 0;
    }
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
    bzero(te, sizeof(*te));

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
    te->has_tc_latency = false;
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
    if (!test_mgmt_validate_port_id(eth_port, printer_arg))
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

    /* Call the application add callback to initialize any memory that will
     * be used for the application config.
     */
    test_mgmt_app_add_test_case(&tenv->te_test_cases[cfg->tc_id].cfg);

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

    if (arg->has_ua_rate_open || arg->has_ua_rate_close ||
            arg->has_ua_rate_send || arg->has_ua_init_delay ||
            arg->has_ua_uptime || arg->has_ua_downtime)
        tc_type = TEST_CASE_TYPE__CLIENT;

    err = test_mgmt_update_test_case_check(eth_port, test_case_id, tc_type,
                                           &tenv,
                                           printer_arg);
    if (err != 0)
        return err;

    test_case = &tenv->te_test_cases[test_case_id].cfg;

    if (arg->has_ua_rate_open)
        test_case->tc_client.cl_rates.rc_open_rate = arg->ua_rate_open;

    if (arg->has_ua_rate_close)
        test_case->tc_client.cl_rates.rc_close_rate = arg->ua_rate_close;

    if (arg->has_ua_rate_send)
        test_case->tc_client.cl_rates.rc_send_rate = arg->ua_rate_send;

    if (arg->has_ua_init_delay)
        test_case->tc_init_delay = arg->ua_init_delay;

    if (arg->has_ua_uptime)
        test_case->tc_uptime = arg->ua_uptime;

    if (arg->has_ua_downtime)
        test_case->tc_downtime = arg->ua_downtime;

    if (arg->has_ua_criteria)
        test_case->tc_criteria = arg->ua_criteria;

    if (arg->has_ua_async)
        test_case->tc_async = arg->ua_async;

    if (arg->has_ua_latency) {
        if (arg->ua_latency.has_tcs_samples &&
                arg->ua_latency.tcs_samples > TPG_TSTAMP_SAMPLES_MAX_BUFSIZE)
            return -EINVAL;

        test_case->has_tc_latency = arg->has_ua_latency;
        test_case->tc_latency = arg->ua_latency;
    }

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
int test_mgmt_get_test_case_app_cfg(uint32_t eth_port, uint32_t test_case_id,
                                    tpg_app_t *out,
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

    *out = tenv->te_test_cases[test_case_id].cfg.tc_app;
    return 0;
}

/*****************************************************************************
 * test_mgmt_update_test_case_app()
 ****************************************************************************/
int test_mgmt_update_test_case_app(uint32_t eth_port, uint32_t test_case_id,
                                   const tpg_app_t *app_cfg,
                                   printer_arg_t *printer_arg)
{
    test_env_t      *tenv;
    tpg_test_case_t  tmp_cfg;
    int              err;

    if (!app_cfg)
        return -EINVAL;

    err = test_mgmt_update_test_case_check(eth_port, test_case_id,
                                           TEST_CASE_TYPE__MAX,
                                           &tenv,
                                           printer_arg);
    if (err != 0)
        return err;

    /* Struct copy. */
    tmp_cfg = tenv->te_test_cases[test_case_id].cfg;
    tmp_cfg.tc_app = *app_cfg;

    if (!test_mgmt_validate_test_case(&tmp_cfg, printer_arg))
        return -EINVAL;

    /* Call the application delete callback to cleanup any memory that was
     * allocated for the application config.
     */
    test_mgmt_app_del_test_case(&tenv->te_test_cases[test_case_id].cfg);

    /* Struct copy. */
    tenv->te_test_cases[test_case_id].cfg.tc_app = *app_cfg;

    /* Call the application add callback to initialize any memory that will
     * be used for the application config.
     */
    test_mgmt_app_add_test_case(&tenv->te_test_cases[test_case_id].cfg);

    return 0;
}

/*****************************************************************************
 * test_mgmt_validate_imix_id()
 ****************************************************************************/
static bool test_mgmt_validate_imix_id(uint32_t imix_id,
                                       printer_arg_t *printer_arg)
{
    if (imix_id >= TPG_IMIX_MAX_GROUPS) {
        tpg_printf(printer_arg, "ERROR: Invalid IMIX id! Max allowed %u\n",
                   TPG_IMIX_MAX_GROUPS);
        return false;
    }

    return true;
}

/*****************************************************************************
 * test_mgmt_validate_imix_group()
 ****************************************************************************/
static bool test_mgmt_validate_imix_group(uint32_t imix_id,
                                          const tpg_imix_group_t *imix_cfg,
                                          printer_arg_t *printer_arg)
{
    uint32_t app_idx;
    uint32_t total_weight = 0;

    if (imix_cfg->imix_apps_count == 0) {
        tpg_printf(printer_arg,
                   "ERROR: IMIX GROUP %"PRIu32" configured without applications!\n",
                   imix_id);
        return false;
    }

    if (imix_cfg->imix_apps_count > TPG_IMIX_MAX_APPS) {
        tpg_printf(printer_arg,
                   "ERROR: IMIX GROUP %"PRIu32": Max apps allowed per IMIX group is: %u!\n",
                   imix_id,
                   TPG_IMIX_MAX_APPS);
        return false;
    }

    for (app_idx = 0; app_idx < imix_cfg->imix_apps_count; app_idx++) {
        uint32_t weight = imix_cfg->imix_apps[app_idx].ia_weight;

        if (weight == 0) {
            tpg_printf(printer_arg,
                       "ERROR: IMIX GROUP %"PRIu32": App index %"PRIu32" has weight 0!\n",
                       imix_id, app_idx);
            return false;
        }

        total_weight += weight;
    }

    if (total_weight > TPG_IMIX_MAX_TOTAL_APP_WEIGHT) {
        tpg_printf(printer_arg,
                   "ERROR: IMIX GROUP %"PRIu32": Total weight (%"PRIu32") above maximum limit (of )%"PRIu32")!\n",
                   imix_id, total_weight, TPG_IMIX_MAX_TOTAL_APP_WEIGHT);
        return false;
    }

    return true;
}

/*****************************************************************************
 * test_mgmt_add_imix_group()
 ****************************************************************************/
int test_mgmt_add_imix_group(uint32_t imix_id, const tpg_imix_group_t *imix_cfg,
                             printer_arg_t *printer_arg)
{
    test_imix_group_t *imix_group;

    if (!imix_cfg)
        return -EINVAL;

    if (!test_mgmt_validate_imix_id(imix_id, printer_arg))
        return -EINVAL;

    imix_group = test_imix_get_env(imix_id);

    if (imix_group->tig_configured) {
        tpg_printf(printer_arg,
                   "ERROR: IMIX GROUP %"PRIu32" already configured!\n",
                   imix_id);
        return -EEXIST;
    }

    if (!test_mgmt_validate_imix_group(imix_id, imix_cfg, printer_arg))
        return -EINVAL;

    /* Struct copy. */
    imix_group->tig_group = *imix_cfg;
    imix_group->tig_configured = true;

    tpg_printf(printer_arg, "Successfully added IMIX GROUP %"PRIu32"!\n",
               imix_id);

    return 0;
}

/*****************************************************************************
 * test_mgmt_del_imix_group()
 ****************************************************************************/
int test_mgmt_del_imix_group(uint32_t imix_id, printer_arg_t *printer_arg)
{
    test_imix_group_t *imix_group;

    if (!test_mgmt_validate_imix_id(imix_id, printer_arg))
        return -EINVAL;

    imix_group = test_imix_get_env(imix_id);

    if (!imix_group->tig_configured) {
        tpg_printf(printer_arg, "ERROR: IMIX GROUP %"PRIu32" not configured!\n",
                   imix_id);
        return -ENOENT;
    }

    if (imix_group->tig_referenced) {
        tpg_printf(printer_arg, "ERROR: IMIX GROUP %"PRIu32" still in use!\n",
                   imix_id);
        return -EBUSY;
    }

    /* Just reset the configured flag. */
    imix_group->tig_configured = false;

    tpg_printf(printer_arg, "Successfully deleted IMIX GROUP %"PRIu32"!\n",
               imix_id);
    return 0;
}

/*****************************************************************************
 * test_mgmt_get_imix_group()
 ****************************************************************************/
int test_mgmt_get_imix_group(uint32_t imix_id, tpg_imix_group_t *out,
                             printer_arg_t *printer_arg)
{
    test_imix_group_t *imix_group;

    if (!out)
        return -EINVAL;

    if (!test_mgmt_validate_imix_id(imix_id, printer_arg))
        return -EINVAL;

    imix_group = test_imix_get_env(imix_id);

    if (!imix_group->tig_configured) {
        tpg_printf(printer_arg, "ERROR: IMIX id %"PRIu32" not configured!\n",
                   imix_id);
        return -ENOENT;
    }

    /* Struct copy */
    *out = imix_group->tig_group;
    return 0;
}

/*****************************************************************************
 * test_mgmt_get_imix_stats()
 ****************************************************************************/
int test_mgmt_get_imix_stats(uint32_t imix_id, tpg_imix_app_stats_t *out,
                             printer_arg_t *printer_arg)
{
    test_imix_group_t *imix_group;

    if (!out)
        return -EINVAL;

    if (!test_mgmt_validate_imix_id(imix_id, printer_arg))
        return -EINVAL;

    imix_group = test_imix_get_env(imix_id);

    if (!imix_group->tig_configured) {
        tpg_printf(printer_arg, "ERROR: IMIX id %"PRIu32" not configured!\n",
                   imix_id);
        return -ENOENT;
    }

    /* Struct copy */
    *out = *test_imix_get_stats(imix_id);
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

    TPG_XLATE_OPTIONAL_COPY_FIELD(&old_opts, opts, po_mtu);

    if (!test_mgmt_validate_port_options(opts, printer_arg, eth_port))
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

    TPG_XLATE_OPTIONAL_COPY_FIELD(&old_opts, opts, to_win_size);
    TPG_XLATE_OPTIONAL_COPY_FIELD(&old_opts, opts, to_syn_retry_cnt);
    TPG_XLATE_OPTIONAL_COPY_FIELD(&old_opts, opts, to_syn_ack_retry_cnt);
    TPG_XLATE_OPTIONAL_COPY_FIELD(&old_opts, opts, to_data_retry_cnt);
    TPG_XLATE_OPTIONAL_COPY_FIELD(&old_opts, opts, to_retry_cnt);
    TPG_XLATE_OPTIONAL_COPY_FIELD(&old_opts, opts, to_rto);
    TPG_XLATE_OPTIONAL_COPY_FIELD(&old_opts, opts, to_fin_to);
    TPG_XLATE_OPTIONAL_COPY_FIELD(&old_opts, opts, to_twait_to);
    TPG_XLATE_OPTIONAL_COPY_FIELD(&old_opts, opts, to_orphan_to);
    TPG_XLATE_OPTIONAL_COPY_FIELD(&old_opts, opts, to_skip_timewait);
    TPG_XLATE_OPTIONAL_COPY_FIELD(&old_opts, opts, to_ack_delay);

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
 * test_mgmt_set_ipv4_sockopt()
 ****************************************************************************/
int
test_mgmt_set_ipv4_sockopt(uint32_t eth_port, uint32_t test_case_id,
                           const tpg_ipv4_sockopt_t *opts,
                           printer_arg_t *printer_arg)
{
    tpg_ipv4_sockopt_t  old_opts;
    test_env_t         *tenv;
    int                 err;

    if (!opts)
        return -EINVAL;

    err = test_mgmt_update_test_case_check(eth_port, test_case_id,
                                           TEST_CASE_TYPE__MAX,
                                           &tenv,
                                           printer_arg);
    if (err != 0)
        return err;

    ipv4_load_sockopt(&old_opts,
                      &tenv->te_test_cases[test_case_id].sockopt.so_ipv4);

    TPG_XLATE_OPTIONAL_COPY_FIELD(&old_opts, opts, ip4so_rx_tstamp);
    TPG_XLATE_OPTIONAL_COPY_FIELD(&old_opts, opts, ip4so_tx_tstamp);
    TPG_XLATE_OPTIONAL_COPY_FIELD(&old_opts, opts, ip4so_tos);

    if (!test_mgmt_validate_ipv4_sockopt(&old_opts, printer_arg))
        return -EINVAL;

    ipv4_store_sockopt(&tenv->te_test_cases[test_case_id].sockopt.so_ipv4,
                       &old_opts);
    return 0;
}

/*****************************************************************************
 * test_mgmt_get_ipv4_sockopt()
 ****************************************************************************/
int test_mgmt_get_ipv4_sockopt(uint32_t eth_port, uint32_t test_case_id,
                               tpg_ipv4_sockopt_t *out,
                               printer_arg_t *printer_arg)
{
    test_env_t *tenv;
    sockopt_t  *sockopt;
    int         err;

    if (!out)
        return -EINVAL;

    err = test_mgmt_get_sockopt(eth_port, test_case_id, &sockopt, &tenv,
                                printer_arg);
    if (err != 0)
        return err;

    ipv4_load_sockopt(out, &sockopt->so_ipv4);
    return 0;
}
/*****************************************************************************
 * test_mgmt_set_vlan_sockopt()
 ****************************************************************************/
int
test_mgmt_set_vlan_sockopt(uint32_t eth_port, uint32_t test_case_id,
                           const tpg_vlan_sockopt_t *opts,
                           printer_arg_t *printer_arg)
{

    tpg_vlan_sockopt_t  old_opts;
    test_env_t         *tenv;
    int                 err;

    if (!opts)
        return -EINVAL;

    err = test_mgmt_update_test_case_check(eth_port, test_case_id,
                                           TEST_CASE_TYPE__MAX,
                                           &tenv,
                                           printer_arg);
    if (err != 0)
        return err;

    vlan_load_sockopt(&old_opts,
                      &tenv->te_test_cases[test_case_id].sockopt.so_vlan);

    TPG_XLATE_OPTIONAL_COPY_FIELD(&old_opts, opts, vlanso_id);
    TPG_XLATE_OPTIONAL_COPY_FIELD(&old_opts, opts, vlanso_pri);

    if (!test_mgmt_validate_vlan_sockopt(&old_opts, printer_arg))
        return -EINVAL;

    vlan_store_sockopt(&tenv->te_test_cases[test_case_id].sockopt.so_vlan,
                       &old_opts);
    return 0;
}
/*****************************************************************************
 * test_mgmt_get_vlan_sockopt()
 ****************************************************************************/
int test_mgmt_get_vlan_sockopt(uint32_t eth_port, uint32_t test_case_id,
                               tpg_vlan_sockopt_t *out,
                               printer_arg_t *printer_arg)
{
    test_env_t *tenv;
    sockopt_t  *sockopt;
    int         err;

    if (!out)
        return -EINVAL;

    err = test_mgmt_get_sockopt(eth_port, test_case_id, &sockopt, &tenv,
                                printer_arg);
    if (err != 0)
        return err;

    vlan_load_sockopt(out, &sockopt->so_vlan);
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
                              tpg_gen_stats_t *out,
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
                                   tpg_rate_stats_t *out,
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
                                  tpg_app_stats_t *out,
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


/*****************************************************************************
 * test_mgmt_get_port_stats()
 *****************************************************************************/
int test_mgmt_get_port_stats(uint32_t eth_port,
                             tpg_port_statistics_t *total_stats,
                             printer_arg_t *printer_arg)
{
    tpg_port_statistics_t *port_stats;
    uint32_t               core;

    if (!test_mgmt_validate_port_id(eth_port, printer_arg))
        return -EINVAL;

    bzero(total_stats, sizeof(*total_stats));

    STATS_FOREACH_CORE(tpg_port_statistics_t, eth_port, core, port_stats) {
        total_stats->ps_received_pkts += port_stats->ps_received_pkts;
        total_stats->ps_received_bytes += port_stats->ps_received_bytes;
        total_stats->ps_sent_pkts += port_stats->ps_sent_pkts;
        total_stats->ps_sent_bytes += port_stats->ps_sent_bytes;
        total_stats->ps_sent_failure += port_stats->ps_sent_failure;
        total_stats->ps_received_ring_if_failed +=
            port_stats->ps_received_ring_if_failed;
        total_stats->ps_sent_sim_failure += port_stats->ps_sent_sim_failure;

    }

    return 0;
}

/*****************************************************************************
 * test_mgmt_get_phy_stats()
 *****************************************************************************/
int test_mgmt_get_phy_stats(uint32_t eth_port,
                            tpg_phy_statistics_t *total_stats,
                            printer_arg_t *printer_arg)
{
    struct rte_eth_link  link_info;
    struct rte_eth_stats phy_stats;

    if (!test_mgmt_validate_port_id(eth_port, printer_arg))
        return -EINVAL;

    port_link_stats_get(eth_port, &phy_stats);
    port_link_info_get(eth_port, &link_info);

    bzero(total_stats, sizeof(*total_stats));

    total_stats->pys_rx_pkts = phy_stats.ipackets;
    total_stats->pys_rx_bytes = phy_stats.ibytes;
    total_stats->pys_tx_pkts = phy_stats.opackets;
    total_stats->pys_tx_bytes = phy_stats.obytes;
    total_stats->pys_rx_errors = phy_stats.ierrors;
    total_stats->pys_tx_errors = phy_stats.oerrors;
    total_stats->pys_link_speed = link_info.link_speed;

    return 0;
}

/*****************************************************************************
 * test_mgmt_get_phy_rate_stats()
 *****************************************************************************/
int test_mgmt_get_phy_rate_stats(uint32_t eth_port,
                                 tpg_phy_statistics_t *rate_stats,
                                 printer_arg_t *printer_arg)
{
    struct rte_eth_link  link_info;
    struct rte_eth_stats phy_stats;

    if (!test_mgmt_validate_port_id(eth_port, printer_arg))
        return -EINVAL;

    port_link_rate_stats_get(eth_port, &phy_stats);
    port_link_info_get_nowait(eth_port, &link_info);

    bzero(rate_stats, sizeof(*rate_stats));

    rate_stats->pys_rx_pkts = phy_stats.ipackets;
    rate_stats->pys_rx_bytes = phy_stats.ibytes;
    rate_stats->pys_tx_pkts = phy_stats.opackets;
    rate_stats->pys_tx_bytes = phy_stats.obytes;
    rate_stats->pys_rx_errors = phy_stats.ierrors;
    rate_stats->pys_tx_errors = phy_stats.oerrors;
    rate_stats->pys_link_speed = link_info.link_speed;

    return 0;
}

/*****************************************************************************
 * test_mgmt_get_eth_stats()
 *****************************************************************************/
int test_mgmt_get_eth_stats(uint32_t eth_port,
                            tpg_eth_statistics_t *total_stats,
                            printer_arg_t *printer_arg)
{
    tpg_eth_statistics_t *eth_stats;
    uint32_t              core;

    if (!test_mgmt_validate_port_id(eth_port, printer_arg))
        return -EINVAL;

    bzero(total_stats, sizeof(*total_stats));

    STATS_FOREACH_CORE(tpg_eth_statistics_t, eth_port, core, eth_stats) {
        total_stats->es_etype_arp += eth_stats->es_etype_arp;
        total_stats->es_etype_ipv4 += eth_stats->es_etype_ipv4;
        total_stats->es_etype_ipv6 += eth_stats->es_etype_ipv6;
        total_stats->es_etype_vlan += eth_stats->es_etype_vlan;
        total_stats->es_etype_other += eth_stats->es_etype_other;
        total_stats->es_to_small_fragment += eth_stats->es_to_small_fragment;
        total_stats->es_no_tx_mbuf += eth_stats->es_no_tx_mbuf;
    }

    return 0;
}

/*****************************************************************************
 * test_mgmt_get_arp_stats()
 *****************************************************************************/
int test_mgmt_get_arp_stats(uint32_t eth_port,
                            tpg_arp_statistics_t *total_stats,
                            printer_arg_t *printer_arg)
{
    tpg_arp_statistics_t *arp_stats;
    uint32_t              core;

    if (!test_mgmt_validate_port_id(eth_port, printer_arg))
        return -EINVAL;

    bzero(total_stats, sizeof(*total_stats));

    STATS_FOREACH_CORE(tpg_arp_statistics_t, eth_port, core, arp_stats) {
        total_stats->as_received_req += arp_stats->as_received_req;
        total_stats->as_received_rep += arp_stats->as_received_rep;
        total_stats->as_received_other += arp_stats->as_received_other;
        total_stats->as_sent_req += arp_stats->as_sent_req;
        total_stats->as_sent_req_failed += arp_stats->as_sent_req_failed;
        total_stats->as_sent_rep += arp_stats->as_sent_rep;
        total_stats->as_sent_rep_failed += arp_stats->as_sent_rep_failed;
        total_stats->as_to_small_fragment += arp_stats->as_to_small_fragment;
        total_stats->as_invalid_hw_space += arp_stats->as_invalid_hw_space;
        total_stats->as_invalid_hw_len += arp_stats->as_invalid_hw_len;
        total_stats->as_invalid_proto_space +=
            arp_stats->as_invalid_proto_space;
        total_stats->as_invalid_proto_len += arp_stats->as_invalid_proto_len;
        total_stats->as_req_not_mine += arp_stats->as_req_not_mine;
    }

    return 0;
}

/*****************************************************************************
 * test_mgmt_get_route_stats()
 *****************************************************************************/
int test_mgmt_get_route_stats(uint32_t eth_port,
                              tpg_route_statistics_t *total_stats,
                              printer_arg_t *printer_arg)
{
    tpg_route_statistics_t *route_stats;
    uint32_t                core;

    if (!test_mgmt_validate_port_id(eth_port, printer_arg))
        return -EINVAL;

    bzero(total_stats, sizeof(*total_stats));

    STATS_FOREACH_CORE(tpg_route_statistics_t, eth_port, core, route_stats) {
        total_stats->rs_intf_add += route_stats->rs_intf_add;
        total_stats->rs_intf_del += route_stats->rs_intf_del;
        total_stats->rs_gw_add += route_stats->rs_gw_add;
        total_stats->rs_gw_del += route_stats->rs_gw_del;

        total_stats->rs_tbl_full += route_stats->rs_tbl_full;
        total_stats->rs_intf_nomem += route_stats->rs_intf_nomem;
        total_stats->rs_intf_notfound += route_stats->rs_intf_notfound;
        total_stats->rs_gw_nointf += route_stats->rs_gw_nointf;
        total_stats->rs_nh_not_found += route_stats->rs_nh_not_found;
        total_stats->rs_route_not_found += route_stats->rs_route_not_found;
    }

    return 0;
}

/*****************************************************************************
 * test_mgmt_get_ipv4_stats()
 *****************************************************************************/
int test_mgmt_get_ipv4_stats(uint32_t eth_port,
                             tpg_ipv4_statistics_t *total_stats,
                             printer_arg_t *printer_arg)
{
    tpg_ipv4_statistics_t *ipv4_stats;
    uint32_t               core;

    if (!test_mgmt_validate_port_id(eth_port, printer_arg))
        return -EINVAL;

    bzero(total_stats, sizeof(*total_stats));

    STATS_FOREACH_CORE(tpg_ipv4_statistics_t, eth_port, core, ipv4_stats) {
        total_stats->ips_received_pkts += ipv4_stats->ips_received_pkts;
        total_stats->ips_received_bytes += ipv4_stats->ips_received_bytes;
        total_stats->ips_protocol_icmp += ipv4_stats->ips_protocol_icmp;
        total_stats->ips_protocol_tcp += ipv4_stats->ips_protocol_tcp;
        total_stats->ips_protocol_udp += ipv4_stats->ips_protocol_udp;
        total_stats->ips_protocol_other += ipv4_stats->ips_protocol_other;
        total_stats->ips_to_small_fragment += ipv4_stats->ips_to_small_fragment;
        total_stats->ips_hdr_to_small += ipv4_stats->ips_hdr_to_small;
        total_stats->ips_invalid_checksum += ipv4_stats->ips_invalid_checksum;
        total_stats->ips_total_length_invalid +=
            ipv4_stats->ips_total_length_invalid;
        total_stats->ips_received_frags += ipv4_stats->ips_received_frags;

#ifndef _SPEEDY_PKT_PARSE_
        total_stats->ips_not_v4 += ipv4_stats->ips_not_v4;
        total_stats->ips_reserved_bit_set += ipv4_stats->ips_reserved_bit_set;
#endif

        total_stats->ips_invalid_pad += ipv4_stats->ips_invalid_pad;
    }

    return 0;
}

/*****************************************************************************
 * test_mgmt_get_udp_stats()
 *****************************************************************************/
int test_mgmt_get_udp_stats(uint32_t eth_port,
                            tpg_udp_statistics_t *total_stats,
                            printer_arg_t *printer_arg)
{
    tpg_udp_statistics_t *udp_stats;
    uint32_t              core;

    if (!test_mgmt_validate_port_id(eth_port, printer_arg))
        return -EINVAL;

    bzero(total_stats, sizeof(*total_stats));

    STATS_FOREACH_CORE(tpg_udp_statistics_t, eth_port, core, udp_stats) {
        total_stats->us_received_pkts += udp_stats->us_received_pkts;
        total_stats->us_received_bytes += udp_stats->us_received_bytes;
        total_stats->us_sent_pkts += udp_stats->us_sent_pkts;
        total_stats->us_sent_ctrl_bytes += udp_stats->us_sent_ctrl_bytes;
        total_stats->us_sent_data_bytes += udp_stats->us_sent_data_bytes;
        total_stats->us_ucb_malloced += udp_stats->us_ucb_malloced;
        total_stats->us_ucb_freed += udp_stats->us_ucb_freed;
        total_stats->us_ucb_not_found += udp_stats->us_ucb_not_found;
        total_stats->us_ucb_alloc_err += udp_stats->us_ucb_alloc_err;
        total_stats->us_to_small_fragment += udp_stats->us_to_small_fragment;
        total_stats->us_invalid_checksum += udp_stats->us_invalid_checksum;
        total_stats->us_failed_pkts += udp_stats->us_failed_pkts;
    }

    return 0;
}

/*****************************************************************************
 * test_mgmt_get_tcp_stats()
 *****************************************************************************/
int test_mgmt_get_tcp_stats(uint32_t eth_port,
                            tpg_tcp_statistics_t *total_stats,
                            printer_arg_t *printer_arg)
{
    tpg_tcp_statistics_t *tcp_stats;
    uint32_t              core;

    if (!test_mgmt_validate_port_id(eth_port, printer_arg))
        return -EINVAL;

    bzero(total_stats, sizeof(*total_stats));

    STATS_FOREACH_CORE(tpg_tcp_statistics_t, eth_port, core, tcp_stats) {
        total_stats->ts_received_pkts += tcp_stats->ts_received_pkts;
        total_stats->ts_received_bytes += tcp_stats->ts_received_bytes;
        total_stats->ts_sent_ctrl_pkts += tcp_stats->ts_sent_ctrl_pkts;
        total_stats->ts_sent_ctrl_bytes += tcp_stats->ts_sent_ctrl_bytes;
        total_stats->ts_sent_data_pkts += tcp_stats->ts_sent_data_pkts;
        total_stats->ts_sent_data_bytes += tcp_stats->ts_sent_data_bytes;
        total_stats->ts_tcb_malloced += tcp_stats->ts_tcb_malloced;
        total_stats->ts_tcb_freed += tcp_stats->ts_tcb_freed;
        total_stats->ts_tcb_not_found += tcp_stats->ts_tcb_not_found;
        total_stats->ts_tcb_alloc_err += tcp_stats->ts_tcb_alloc_err;
        total_stats->ts_to_small_fragment += tcp_stats->ts_to_small_fragment;
        total_stats->ts_hdr_to_small += tcp_stats->ts_hdr_to_small;
        total_stats->ts_invalid_checksum += tcp_stats->ts_invalid_checksum;
        total_stats->ts_failed_ctrl_pkts += tcp_stats->ts_failed_ctrl_pkts;
        total_stats->ts_failed_data_pkts += tcp_stats->ts_failed_data_pkts;
        total_stats->ts_failed_data_clone += tcp_stats->ts_failed_data_clone;

#ifndef _SPEEDY_PKT_PARSE_
        total_stats->ts_reserved_bit_set += tcp_stats->ts_reserved_bit_set;
#endif
    }

    return 0;
}

/*****************************************************************************
 * test_mgmt_get_tsm_stats()
 *****************************************************************************/
int test_mgmt_get_tsm_stats(uint32_t eth_port,
                            tpg_tsm_statistics_t *total_stats,
                            printer_arg_t *printer_arg)
{
    tpg_tsm_statistics_t *tsm_stats;
    uint32_t              core;

    if (!test_mgmt_validate_port_id(eth_port, printer_arg))
        return -EINVAL;

    bzero(total_stats, sizeof(*total_stats));
    total_stats->tsms_tcb_states_count = TS_MAX_STATE;

    STATS_FOREACH_CORE(tpg_tsm_statistics_t, eth_port, core, tsm_stats) {
        int i;

        for (i = 0; i < TS_MAX_STATE; i++)
            total_stats->tsms_tcb_states[i] += tsm_stats->tsms_tcb_states[i];

        total_stats->tsms_syn_to += tsm_stats->tsms_syn_to;
        total_stats->tsms_synack_to += tsm_stats->tsms_synack_to;
        total_stats->tsms_retry_to += tsm_stats->tsms_retry_to;
        total_stats->tsms_retrans_bytes += tsm_stats->tsms_retrans_bytes;
        total_stats->tsms_missing_seq += tsm_stats->tsms_missing_seq;
        total_stats->tsms_snd_win_full += tsm_stats->tsms_snd_win_full;

    }

    return 0;
}

/*****************************************************************************
 * test_mgmt_get_msg_stats()
 *****************************************************************************/
int test_mgmt_get_msg_stats(tpg_msg_statistics_t *total_stats,
                            printer_arg_t *printer_arg __rte_unused)
{
    tpg_msg_statistics_t *msg_stats;
    uint32_t              core;

    bzero(total_stats, sizeof(*total_stats));

    STATS_FOREACH_CORE(tpg_msg_statistics_t, 0, core, msg_stats) {
        total_stats->ms_rcvd += msg_stats->ms_rcvd;
        total_stats->ms_snd += msg_stats->ms_snd;
        total_stats->ms_poll += msg_stats->ms_poll;

        total_stats->ms_err += msg_stats->ms_err;
        total_stats->ms_proc_err += msg_stats->ms_proc_err;

        total_stats->ms_alloc += msg_stats->ms_alloc;
        total_stats->ms_alloc_err += msg_stats->ms_alloc_err;
        total_stats->ms_free += msg_stats->ms_free;
    }

    return 0;
}

/*****************************************************************************
 * test_mgmt_get_timer_stats()
 *****************************************************************************/
int test_mgmt_get_timer_stats(uint32_t eth_port,
                              tpg_timer_statistics_t *total_stats,
                              printer_arg_t *printer_arg)
{
    tpg_timer_statistics_t *timer_stats;
    uint32_t                core;

    if (!test_mgmt_validate_port_id(eth_port, printer_arg))
        return -EINVAL;

    bzero(total_stats, sizeof(*total_stats));
    STATS_FOREACH_CORE(tpg_timer_statistics_t, eth_port, core, timer_stats) {
        total_stats->tts_rto_set += timer_stats->tts_rto_set;
        total_stats->tts_rto_cancelled += timer_stats->tts_rto_cancelled;
        total_stats->tts_rto_fired += timer_stats->tts_rto_fired;

        total_stats->tts_slow_set += timer_stats->tts_slow_set;
        total_stats->tts_slow_cancelled += timer_stats->tts_slow_cancelled;
        total_stats->tts_slow_fired += timer_stats->tts_slow_fired;

        total_stats->tts_test_set += timer_stats->tts_test_set;
        total_stats->tts_test_cancelled += timer_stats->tts_test_cancelled;
        total_stats->tts_test_fired += timer_stats->tts_test_fired;

        total_stats->tts_rto_failed += timer_stats->tts_rto_failed;
        total_stats->tts_slow_failed += timer_stats->tts_slow_failed;
        total_stats->tts_l4cb_null += timer_stats->tts_l4cb_null;
        total_stats->tts_l4cb_invalid_flags +=
            timer_stats->tts_l4cb_invalid_flags;
        total_stats->tts_timeout_overflow +=
            timer_stats->tts_timeout_overflow;
    }

    return 0;
}

/*****************************************************************************
 * test_mgmt_clear_stats()
 ****************************************************************************/
int test_mgmt_clear_statistics(uint32_t eth_port, printer_arg_t *printer_arg)
{
    if (!test_mgmt_validate_port_id(eth_port, printer_arg))
        return -EINVAL;

    /* Clear PORT stats. */
    STATS_CLEAR(tpg_port_statistics_t, eth_port);

    /* Clear PHY stats. */
    rte_eth_stats_reset(eth_port);
    rte_eth_xstats_reset(eth_port);

    /* Clear ETHERNET stats. */
    STATS_CLEAR(tpg_eth_statistics_t, eth_port);

    /* Clear ARP stats. */
    STATS_CLEAR(tpg_arp_statistics_t, eth_port);

    /* Clear ROUTE stats. */
    STATS_CLEAR(tpg_route_statistics_t, eth_port);

    /* Clear IPv4 stats. */
    STATS_CLEAR(tpg_ipv4_statistics_t, eth_port);

    /* Clear UDP stats. */
    STATS_CLEAR(tpg_udp_statistics_t, eth_port);

    /* Clear TCP stats. */
    STATS_CLEAR(tpg_tcp_statistics_t, eth_port);

    /* Clear TSM stats.
     * However, we should only clean stats and not the number of TCBs in
     * each state. Therefore we can't use STATS_CLEAR.
     */
    test_mgmt_clear_tsm_statistics(eth_port);

    /* Clear MSG stats. */
    STATS_CLEAR(tpg_msg_statistics_t, eth_port);

    /* Clear TIMER stats. */
    STATS_CLEAR(tpg_timer_statistics_t, eth_port);

    return 0;
}

/*****************************************************************************
 * test_mgmt_rx_tstamp_enabled()
 ****************************************************************************/
bool test_mgmt_rx_tstamp_enabled(const tpg_test_case_t *entry)
{
    const test_env_t *tenv = test_mgmt_get_port_env(entry->tc_eth_port);
    const sockopt_t  *sockopt = &tenv->te_test_cases[entry->tc_id].sockopt;

    if (sockopt->so_ipv4.ip4so_rx_tstamp)
        return true;

    switch (entry->tc_app.app_proto) {
    case APP_PROTO__RAW_CLIENT:
        return TPG_XLATE_OPT_BOOL(&entry->tc_app.app_raw_client, rc_rx_tstamp);
    case APP_PROTO__RAW_SERVER:
        return TPG_XLATE_OPT_BOOL(&entry->tc_app.app_raw_server, rs_rx_tstamp);
    default:
        return false;
    }
}

/*****************************************************************************
 * test_mgmt_tx_tstamp_enabled()
 ****************************************************************************/
bool test_mgmt_tx_tstamp_enabled(const tpg_test_case_t *entry)
{
    const test_env_t *tenv = test_mgmt_get_port_env(entry->tc_eth_port);
    const sockopt_t  *sockopt = &tenv->te_test_cases[entry->tc_id].sockopt;

    if (sockopt->so_ipv4.ip4so_tx_tstamp)
        return true;

    switch (entry->tc_app.app_proto) {
    case APP_PROTO__RAW_CLIENT:
        return TPG_XLATE_OPT_BOOL(&entry->tc_app.app_raw_client, rc_tx_tstamp);
    case APP_PROTO__RAW_SERVER:
        return TPG_XLATE_OPT_BOOL(&entry->tc_app.app_raw_server, rs_tx_tstamp);
    default:
        return false;
    }
}

