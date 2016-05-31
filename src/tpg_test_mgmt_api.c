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

    if ((*tenv)->te_states[test_case_id].teos_configured)
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

    if (!(*tenv)->te_states[test_case_id].teos_configured)
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

    if (!(*tenv)->te_states[test_case_id].teos_configured) {
        tpg_printf(printer_arg,
                   "ERROR: Test case %"PRIu32
                   " not configured on port %"PRIu32"!\n",
                   test_case_id,
                   eth_port);
        return -ENOENT;
    }

    if (tc_type != TEST_CASE_TYPE__MAX &&
            (*tenv)->te_test_cases[test_case_id].tc_type != tc_type) {
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
    if (!(*tenv)->te_states[test_case_id].teos_configured) {
        tpg_printf(printer_arg,
                   "ERROR: Test case %"PRIu32
                   " not configured on port %"PRIu32"!\n",
                   test_case_id,
                   eth_port);
        return -EALREADY;
    }

    if (tc_type != TEST_CASE_TYPE__MAX &&
            (*tenv)->te_test_cases[test_case_id].tc_type != tc_type) {
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
    tenv->te_test_cases[cfg->tc_id] = *cfg;

    tenv->te_states[cfg->tc_id].teos_configured = true;
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
    test_mgmt_app_del_test_case(&tenv->te_test_cases[test_case_id]);

    /* Unconfigure means just resetting the bit.. */
    tenv->te_states[test_case_id].teos_configured = false;
    tenv->te_test_cases_count--;
    return 0;
}

/*****************************************************************************
 * test_mgmt_update_test_case_rate()
 ****************************************************************************/
int test_mgmt_update_test_case_rate(uint32_t eth_port,
                                    uint32_t test_case_id,
                                    tpg_rate_type_t rate_type,
                                    const tpg_rate_t *rate,
                                    printer_arg_t *printer_arg)
{
    test_env_t        *tenv;
    tpg_rate_client_t *app_rate_client;
    int                err;

    if (!rate)
        return -EINVAL;

    err = test_mgmt_update_test_case_check(eth_port, test_case_id,
                                           TEST_CASE_TYPE__CLIENT,
                                           &tenv,
                                           printer_arg);
    if (err != 0)
        return err;

    app_rate_client = &tenv->te_test_cases[test_case_id].tc_client.cl_rates;

    switch (rate_type) {
    case RATE_TYPE__OPEN:
        app_rate_client->rc_open_rate = *rate;
        break;
    case RATE_TYPE__SEND:
        app_rate_client->rc_send_rate = *rate;
        break;
    case RATE_TYPE__CLOSE:
        app_rate_client->rc_close_rate = *rate;
        break;
    default:
        return -EINVAL;
    }

    return 0;
}

/*****************************************************************************
 * test_mgmt_update_test_case_timeout()
 ****************************************************************************/
int test_mgmt_update_test_case_timeout(uint32_t eth_port,
                                       uint32_t test_case_id,
                                       tpg_delay_type_t timeout_type,
                                       const tpg_delay_t *timeout,
                                       printer_arg_t *printer_arg)
{
    test_env_t         *tenv;
    tpg_delay_client_t *app_delay_client;
    int                 err;

    if (!timeout)
        return -EINVAL;

    err = test_mgmt_update_test_case_check(eth_port, test_case_id,
                                           TEST_CASE_TYPE__CLIENT,
                                           &tenv,
                                           printer_arg);
    if (err != 0)
        return err;

    app_delay_client = &tenv->te_test_cases[test_case_id].tc_client.cl_delays;

    switch (timeout_type) {
    case DELAY_TYPE__INIT:
        app_delay_client->dc_init_delay = *timeout;
        break;
    case DELAY_TYPE__UPTIME:
        app_delay_client->dc_uptime = *timeout;
        break;
    case DELAY_TYPE__DOWNTIME:
        app_delay_client->dc_downtime = *timeout;
        break;
    default:
        return -EINVAL;
    }

    return 0;
}

/*****************************************************************************
 * test_mgmt_update_test_case_criteria()
 ****************************************************************************/
int
test_mgmt_update_test_case_criteria(uint32_t eth_port, uint32_t test_case_id,
                                    const tpg_test_criteria_t *criteria,
                                    printer_arg_t *printer_arg)
{
    test_env_t *tenv;
    int         err;

    err = test_mgmt_update_test_case_check(eth_port, test_case_id,
                                           TEST_CASE_TYPE__MAX,
                                           &tenv,
                                           printer_arg);
    if (err != 0)
        return err;

    /* Struct copy. */
    tenv->te_test_cases[test_case_id].tc_criteria = *criteria;

    return 0;
}

/*****************************************************************************
 * test_mgmt_update_test_case_async()
 ****************************************************************************/
int test_mgmt_update_test_case_async(uint32_t eth_port, uint32_t test_case_id,
                                     bool async,
                                     printer_arg_t *printer_arg)
{
    test_env_t *tenv;
    int         err;

    err = test_mgmt_update_test_case_check(eth_port, test_case_id,
                                           TEST_CASE_TYPE__MAX,
                                           &tenv,
                                           printer_arg);
    if (err != 0)
        return err;


    tenv->te_test_cases[test_case_id].tc_async = async;
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

    *out = tenv->te_test_cases[test_case_id];
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

    *out = tenv->te_test_cases[test_case_id].tc_client.cl_app;
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

    *out = tenv->te_test_cases[test_case_id].tc_server.srv_app;
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
    tmp_cfg = tenv->te_test_cases[test_case_id];
    tmp_cfg.tc_client.cl_app = *app_cl_cfg;

    if (!test_mgmt_validate_test_case_client(&tmp_cfg, printer_arg))
        return -EINVAL;

    /* Call the application delete callback to cleanup any memory that was
     * allocated for the application config.
     */
    test_mgmt_app_del_test_case(&tenv->te_test_cases[test_case_id]);

    /* Struct copy. */
    tenv->te_test_cases[test_case_id].tc_client.cl_app = *app_cl_cfg;

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
    tmp_cfg = tenv->te_test_cases[test_case_id];
    tmp_cfg.tc_server.srv_app = *app_srv_cfg;

    if (!test_mgmt_validate_test_case_server(&tmp_cfg, printer_arg))
        return -EINVAL;

    /* Call the application delete callback to cleanup any memory that was
     * allocated for the application config.
     */
    test_mgmt_app_del_test_case(&tenv->te_test_cases[test_case_id]);

    /* Struct copy. */
    tenv->te_test_cases[test_case_id].tc_server.srv_app = *app_srv_cfg;

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

    *out = tenv->te_states[test_case_id];
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

