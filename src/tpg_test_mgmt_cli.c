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
 *     tpg_test_mgmt_cli.c
 *
 * Description:
 *     Test manager CLI.
 *
 * Author:
 *     Dumitru Ceara, Eelco Chaudron
 *
 * Initial Created:
 *     22/12/2015
 *
 * Notes:
 *
 */

/*****************************************************************************
 * Include files
 ****************************************************************************/
#include "tcp_generator.h"

/****************************************************************************
 * Local definitions
 ****************************************************************************/
/****************************************************************************
 * Fill callbacks for generating CLIs that set optional fields in big
 * structures.
 * These macros are really ugly but they save about 1000 lines of copy/paste!
 ****************************************************************************/
#define OPT_FILL_TYPE_NAME(comp) comp ## _options_cli_cb_t

#define OPT_FILL_TYPEDEF(comp, type)                         \
    typedef struct OPT_FILL_TYPE_NAME(comp) {                \
        void (*opt_cb)(__typeof__(type) *dest, void *value); \
    } OPT_FILL_TYPE_NAME(comp)

#define OPT_FILL_CB(comp, field) comp ## _fill_ ## field
#define OPT_FILL_PARAM_NAME(comp, field) comp ## _fill_ ## field ## _param

#define OPT_FILL_CB_DEFINE(comp, type, field, field_type)        \
    static void OPT_FILL_CB(comp, field)(__typeof__(type) *dest, \
                                         void *value)            \
    {                                                            \
        bzero(dest, sizeof(*dest));                              \
        dest->field = *(__typeof__(field_type) *)value;          \
        dest->has_ ## field = true;                              \
    }

#define OPT_FILL_DEFINE(comp, type, field, field_type)                   \
    OPT_FILL_CB_DEFINE(comp, type, field, field_type)                    \
                                                                         \
    static OPT_FILL_TYPE_NAME(comp) OPT_FILL_PARAM_NAME(comp, field) = { \
        OPT_FILL_CB(comp, field)                                         \
    }

/****************************************************************************
 * - "start/stop tests "
 ****************************************************************************/
struct cmd_tests_start_stop_result {
    cmdline_fixed_string_t  start;
    cmdline_fixed_string_t  stop;
    cmdline_fixed_string_t  tests;
    cmdline_fixed_string_t  port_kw;
    uint32_t                port;
};

static cmdline_parse_token_string_t cmd_tests_start_T_start =
    TOKEN_STRING_INITIALIZER(struct cmd_tests_start_stop_result, start, "start");
static cmdline_parse_token_string_t cmd_tests_start_T_stop =
    TOKEN_STRING_INITIALIZER(struct cmd_tests_start_stop_result, stop, "stop");
static cmdline_parse_token_string_t cmd_tests_start_T_tests =
    TOKEN_STRING_INITIALIZER(struct cmd_tests_start_stop_result, tests, "tests");
static cmdline_parse_token_string_t cmd_tests_start_T_port_kw =
    TOKEN_STRING_INITIALIZER(struct cmd_tests_start_stop_result, port_kw, "port");
static cmdline_parse_token_num_t cmd_tests_start_T_port =
    TOKEN_NUM_INITIALIZER(struct cmd_tests_start_stop_result, port, UINT32);

static void cmd_tests_start_parsed(void *parsed_result,
                                   struct cmdline *cl,
                                   void *data __rte_unused)
{
    printer_arg_t                       parg;
    struct cmd_tests_start_stop_result *pr;
    struct rte_eth_link                 link;

    parg = TPG_PRINTER_ARG(cli_printer, cl);
    pr = parsed_result;

    /*
     * Use the nowait link call as rte_eth_link_get might block for 9 seconds.
     * Might be nice though if the user could specify a timeout and/or retry
     * count.
     */
    rte_eth_link_get_nowait(pr->port, &link);
    if (!link.link_status) {
        cmdline_printf(cl, "ERROR: Ehernet port %u: Link down!\n", pr->port);
        return;
    }

    if (test_mgmt_start_port(pr->port, &parg) == 0)
        cmdline_printf(cl, "Tests started on port %"PRIu32"\n", pr->port);
    else
        cmdline_printf(cl, "ERROR: Failed to start tests on port %"PRIu32"!\n",
                       pr->port);
}

static void cmd_tests_stop_parsed(void *parsed_result __rte_unused,
                                  struct cmdline *cl __rte_unused,
                                  void *data __rte_unused)
{
    printer_arg_t                       parg;
    struct cmd_tests_start_stop_result *pr;

    parg = TPG_PRINTER_ARG(cli_printer, cl);
    pr = parsed_result;

    if (test_mgmt_stop_port(pr->port, &parg) == 0)
        cmdline_printf(cl, "Tests stopped on port %"PRIu32"\n", pr->port);
    else
        cmdline_printf(cl, "ERROR: Failed to stop tests on port %"PRIu32"!\n",
                       pr->port);
}

cmdline_parse_inst_t cmd_tests_start = {
    .f = cmd_tests_start_parsed,
    .data = NULL,
    .help_str = "start tests port <eth_port>",
    .tokens = {
        (void *)&cmd_tests_start_T_start,
        (void *)&cmd_tests_start_T_tests,
        (void *)&cmd_tests_start_T_port_kw,
        (void *)&cmd_tests_start_T_port,
        NULL,
    },
};

cmdline_parse_inst_t cmd_tests_stop = {
    .f = cmd_tests_stop_parsed,
    .data = NULL,
    .help_str = "stop tests port <eth_port>",
    .tokens = {
        (void *)&cmd_tests_start_T_stop,
        (void *)&cmd_tests_start_T_tests,
        (void *)&cmd_tests_start_T_port_kw,
        (void *)&cmd_tests_start_T_port,
        NULL,
    },
};
/****************************************************************************
 * - "show link rate"
 ****************************************************************************/
struct cmd_show_link_rate_result {
    cmdline_fixed_string_t show;
    cmdline_fixed_string_t link;
    cmdline_fixed_string_t rate;
};

static cmdline_parse_token_string_t cmd_show_link_rate_T_show =
    TOKEN_STRING_INITIALIZER(struct cmd_show_link_rate_result, show, "show");
static cmdline_parse_token_string_t cmd_show_link_rate_T_link =
    TOKEN_STRING_INITIALIZER(struct cmd_show_link_rate_result, link, "link");
static cmdline_parse_token_string_t cmd_show_link_rate_T_rate =
    TOKEN_STRING_INITIALIZER(struct cmd_show_link_rate_result, rate, "rate");

static void cmd_show_link_rate_parsed(void *parsed_result __rte_unused,
                                      struct cmdline *cl __rte_unused,
                                      void *data __rte_unused)
{
    printer_arg_t parg;
    uint32_t      eth_port = 0;

    parg = TPG_PRINTER_ARG(cli_printer, cl);

    for (eth_port = 0; eth_port < rte_eth_dev_count(); eth_port++)
        test_show_link_rate(eth_port, &parg);
}

cmdline_parse_inst_t cmd_show_link_rate = {
    .f = cmd_show_link_rate_parsed,
    .data = NULL,
    .help_str = "show link rate",
    .tokens = {
        (void *)&cmd_show_link_rate_T_show,
        (void *)&cmd_show_link_rate_T_link,
        (void *)&cmd_show_link_rate_T_rate,
        NULL,
    },
};



/****************************************************************************
 * - "show tests ui"
 ****************************************************************************/
struct cmd_show_tests_ui_result {
    cmdline_fixed_string_t show;
    cmdline_fixed_string_t tests;
    cmdline_fixed_string_t ui;
};

static cmdline_parse_token_string_t cmd_show_tests_ui_T_show =
    TOKEN_STRING_INITIALIZER(struct cmd_show_tests_ui_result, show, "show");
static cmdline_parse_token_string_t cmd_show_tests_ui_T_tests =
    TOKEN_STRING_INITIALIZER(struct cmd_show_tests_ui_result, tests, "tests");
static cmdline_parse_token_string_t cmd_show_tests_ui_T_ui =
    TOKEN_STRING_INITIALIZER(struct cmd_show_tests_ui_result, ui, "ui");

static void cmd_show_tests_ui_parsed(void *parsed_result __rte_unused,
                                        struct cmdline *cl __rte_unused,
                                        void *data __rte_unused)
{
    test_init_stats_screen();
}

cmdline_parse_inst_t cmd_show_tests_ui = {
    .f = cmd_show_tests_ui_parsed,
    .data = NULL,
    .help_str = "show tests ui",
    .tokens = {
        (void *)&cmd_show_tests_ui_T_show,
        (void *)&cmd_show_tests_ui_T_tests,
        (void *)&cmd_show_tests_ui_T_ui,
        NULL,
    },
};

/****************************************************************************
 * - "show tests config"
 ****************************************************************************/
struct cmd_show_tests_config_result {
    cmdline_fixed_string_t  show;
    cmdline_fixed_string_t  tests;
    cmdline_fixed_string_t  config;
    cmdline_fixed_string_t  port_kw;
    uint32_t                port;
};

static cmdline_parse_token_string_t cmd_show_tests_config_T_show =
    TOKEN_STRING_INITIALIZER(struct cmd_show_tests_config_result, show, "show");
static cmdline_parse_token_string_t cmd_show_tests_config_T_tests =
    TOKEN_STRING_INITIALIZER(struct cmd_show_tests_config_result, tests, "tests");
static cmdline_parse_token_string_t cmd_show_tests_config_T_config =
    TOKEN_STRING_INITIALIZER(struct cmd_show_tests_config_result, config, "config");
static cmdline_parse_token_string_t cmd_show_tests_config_T_port_kw =
    TOKEN_STRING_INITIALIZER(struct cmd_show_tests_config_result, port_kw, "port");
static cmdline_parse_token_num_t cmd_show_tests_config_T_port =
    TOKEN_NUM_INITIALIZER(struct cmd_show_tests_config_result, port, UINT32);

static void cmd_show_tests_config_parsed(void *parsed_result, struct cmdline *cl,
                                         void *data __rte_unused)
{
    printer_arg_t                        parg;
    struct cmd_show_tests_config_result *pr;
    uint32_t                             tcid;

    parg = TPG_PRINTER_ARG(cli_printer, cl);
    pr = parsed_result;

    if (pr->port >= rte_eth_dev_count()) {
        cmdline_printf(cl, "ERROR: Port should be in the range 0..%"PRIu32"\n",
                       rte_eth_dev_count());
        return;
    }

    test_config_show_port(pr->port, &parg);

    for (tcid = 0; tcid < TPG_TEST_MAX_ENTRIES; tcid++) {
        tpg_test_case_t entry;

        if (test_mgmt_get_test_case_cfg(pr->port, tcid, &entry, NULL) != 0)
            continue;

        cmdline_printf(cl, "%-16s : %"PRIu32"\n", "Test Case Id", tcid);
        test_config_show_tc(&entry, &parg);
        cmdline_printf(cl, "\n\n");
    }
}

cmdline_parse_inst_t cmd_show_tests_config = {
    .f = cmd_show_tests_config_parsed,
    .data = NULL,
    .help_str = "show tests config port <eth_port>",
    .tokens = {
        (void *)&cmd_show_tests_config_T_show,
        (void *)&cmd_show_tests_config_T_tests,
        (void *)&cmd_show_tests_config_T_config,
        (void *)&cmd_show_tests_config_T_port_kw,
        (void *)&cmd_show_tests_config_T_port,
        NULL,
    },
};

/****************************************************************************
 * - "show tests state"
 ****************************************************************************/
struct cmd_show_tests_state_result {
    cmdline_fixed_string_t show;
    cmdline_fixed_string_t tests;
    cmdline_fixed_string_t state;
    cmdline_fixed_string_t port_kw;
    uint32_t               port;
};

static cmdline_parse_token_string_t cmd_show_tests_state_T_show =
    TOKEN_STRING_INITIALIZER(struct cmd_show_tests_state_result, show, "show");
static cmdline_parse_token_string_t cmd_show_tests_state_T_tests =
    TOKEN_STRING_INITIALIZER(struct cmd_show_tests_state_result, tests, "tests");
static cmdline_parse_token_string_t cmd_show_tests_state_T_config =
    TOKEN_STRING_INITIALIZER(struct cmd_show_tests_state_result, state, "state");
static cmdline_parse_token_string_t cmd_show_tests_state_T_port_kw =
    TOKEN_STRING_INITIALIZER(struct cmd_show_tests_state_result, port_kw, "port");
static cmdline_parse_token_num_t cmd_show_tests_state_T_port =
    TOKEN_NUM_INITIALIZER(struct cmd_show_tests_state_result, port, UINT32);

static void cmd_show_tests_state_parsed(void *parsed_result, struct cmdline *cl,
                                        void *data __rte_unused)
{
    printer_arg_t                       parg;
    struct cmd_show_tests_state_result *pr;

    parg = TPG_PRINTER_ARG(cli_printer, cl);
    pr = parsed_result;

    if (pr->port >= rte_eth_dev_count()) {
        cmdline_printf(cl, "ERROR: Port should be in the range 0..%"PRIu32"\n",
                       rte_eth_dev_count());
        return;
    }

    test_state_show_tcs_hdr(pr->port, &parg);
    test_state_show_tcs(pr->port, &parg);
}

cmdline_parse_inst_t cmd_show_tests_state = {
    .f = cmd_show_tests_state_parsed,
    .data = NULL,
    .help_str = "show tests state port <eth_port>",
    .tokens = {
        (void *)&cmd_show_tests_state_T_show,
        (void *)&cmd_show_tests_state_T_tests,
        (void *)&cmd_show_tests_state_T_config,
        (void *)&cmd_show_tests_state_T_port_kw,
        (void *)&cmd_show_tests_state_T_port,
        NULL,
    },
};

/****************************************************************************
 * - "show tests stats"
 ****************************************************************************/
struct cmd_show_tests_stats_result {
    cmdline_fixed_string_t show;
    cmdline_fixed_string_t tests;
    cmdline_fixed_string_t stats;
    cmdline_fixed_string_t port_kw;
    uint32_t               port;
    cmdline_fixed_string_t tcid_kw;
    uint32_t               tcid;
};

static cmdline_parse_token_string_t cmd_show_tests_stats_T_show =
    TOKEN_STRING_INITIALIZER(struct cmd_show_tests_stats_result, show, "show");
static cmdline_parse_token_string_t cmd_show_tests_stats_T_tests =
    TOKEN_STRING_INITIALIZER(struct cmd_show_tests_stats_result, tests,
                             "tests");
static cmdline_parse_token_string_t cmd_show_tests_stats_T_config =
    TOKEN_STRING_INITIALIZER(struct cmd_show_tests_stats_result, stats,
                             "stats");
static cmdline_parse_token_string_t cmd_show_tests_stats_T_port_kw =
    TOKEN_STRING_INITIALIZER(struct cmd_show_tests_stats_result, port_kw,
                             "port");
static cmdline_parse_token_num_t cmd_show_tests_stats_T_port =
    TOKEN_NUM_INITIALIZER(struct cmd_show_tests_stats_result, port, UINT32);
static cmdline_parse_token_string_t cmd_show_tests_stats_T_tcid_kw =
    TOKEN_STRING_INITIALIZER(struct cmd_show_tests_stats_result, tcid_kw,
                             "test-case-id");
static cmdline_parse_token_num_t cmd_show_tests_stats_T_tcid =
    TOKEN_NUM_INITIALIZER(struct cmd_show_tests_stats_result, tcid, UINT32);

static void cmd_show_tests_stats_parsed(void *parsed_result, struct cmdline *cl,
                                        void *data __rte_unused)
{
    printer_arg_t                       parg;
    struct cmd_show_tests_stats_result *pr;
    tpg_test_case_t                     test_case_cfg;

    parg = TPG_PRINTER_ARG(cli_printer, cl);
    pr = parsed_result;

    if (test_mgmt_get_test_case_cfg(pr->port, pr->tcid, &test_case_cfg,
                                    &parg) != 0)
        return;

    cmdline_printf(cl, "Port %"PRIu32", Test Case %"PRIu32" Statistics:\n",
                   pr->port,
                   pr->tcid);

    test_state_show_stats(&test_case_cfg, &parg);
}

cmdline_parse_inst_t cmd_show_tests_stats = {
    .f = cmd_show_tests_stats_parsed,
    .data = NULL,
    .help_str = "show tests stats port <eth_port> test-case-id <tcid>",
    .tokens = {
        (void *)&cmd_show_tests_stats_T_show,
        (void *)&cmd_show_tests_stats_T_tests,
        (void *)&cmd_show_tests_stats_T_config,
        (void *)&cmd_show_tests_stats_T_port_kw,
        (void *)&cmd_show_tests_stats_T_port,
        (void *)&cmd_show_tests_stats_T_tcid_kw,
        (void *)&cmd_show_tests_stats_T_tcid,
        NULL,
    },
};

/****************************************************************************
 * - "add tests l3_intf port <eth_port> ip <ip> mask <mask> gw <gw>"
 ****************************************************************************/
 struct cmd_tests_add_l3_intf_result {
    cmdline_fixed_string_t add;
    cmdline_fixed_string_t tests;
    cmdline_fixed_string_t l3_intf;
    cmdline_fixed_string_t port_kw;
    uint32_t               port;
    cmdline_fixed_string_t ip_kw;
    cmdline_ipaddr_t       ip;
    cmdline_fixed_string_t mask_kw;
    cmdline_ipaddr_t       mask;
};

static cmdline_parse_token_string_t cmd_tests_add_l3_intf_T_add =
    TOKEN_STRING_INITIALIZER(struct cmd_tests_add_l3_intf_result, add, "add");
static cmdline_parse_token_string_t cmd_tests_add_l3_intf_T_tests =
    TOKEN_STRING_INITIALIZER(struct cmd_tests_add_l3_intf_result, tests, "tests");
static cmdline_parse_token_string_t cmd_tests_add_l3_intf_T_l3_intf =
    TOKEN_STRING_INITIALIZER(struct cmd_tests_add_l3_intf_result, l3_intf, "l3_intf");
static cmdline_parse_token_string_t cmd_tests_add_l3_intf_T_port_kw =
    TOKEN_STRING_INITIALIZER(struct cmd_tests_add_l3_intf_result, port_kw, "port");
static cmdline_parse_token_num_t cmd_tests_add_l3_intf_T_port =
    TOKEN_NUM_INITIALIZER(struct cmd_tests_add_l3_intf_result, port, UINT32);
static cmdline_parse_token_string_t cmd_tests_add_l3_intf_T_ip_kw =
    TOKEN_STRING_INITIALIZER(struct cmd_tests_add_l3_intf_result, ip_kw, "ip");
static cmdline_parse_token_ipaddr_t cmd_tests_add_l3_intf_T_ip =
    TOKEN_IPADDR_INITIALIZER(struct cmd_tests_add_l3_intf_result, ip);
static cmdline_parse_token_string_t cmd_tests_add_l3_intf_T_mask_kw =
    TOKEN_STRING_INITIALIZER(struct cmd_tests_add_l3_intf_result, mask_kw, "mask");
static cmdline_parse_token_ipaddr_t cmd_tests_add_l3_intf_T_mask =
    TOKEN_IPADDR_INITIALIZER(struct cmd_tests_add_l3_intf_result, mask);

static void cmd_tests_add_l3_intf_parsed(void *parsed_result, struct cmdline *cl,
                                         void *data __rte_unused)
{
    printer_arg_t                        parg;
    struct cmd_tests_add_l3_intf_result *pr;
    tpg_l3_intf_t                        l3_intf;

    parg = TPG_PRINTER_ARG(cli_printer, cl);
    pr = parsed_result;

    if (pr->ip.family != AF_INET) {
        cmdline_printf(cl, "ERROR: IPv6 not supported yet!\n");
        return;
    }

    if (pr->ip.family != pr->mask.family) {
        cmdline_printf(cl, "ERROR: Mixing IPv4 and IPv6..\n");
        return;
    }

    l3_intf = (tpg_l3_intf_t){
        .l3i_ip = TPG_IPV4(rte_be_to_cpu_32(pr->ip.addr.ipv4.s_addr)),
        .l3i_mask = TPG_IPV4(rte_be_to_cpu_32(pr->mask.addr.ipv4.s_addr)),
        .l3i_count = 1 /* TODO: count not implemented yet! */
    };

    if (test_mgmt_add_port_cfg_l3_intf(pr->port, &l3_intf, &parg) == 0)
        cmdline_printf(cl, "L3 interface successfully added.\n");
    else
        cmdline_printf(cl, "ERROR: Failed to add L3 interface!\n");
}

cmdline_parse_inst_t cmd_tests_add_l3_intf = {
    .f = cmd_tests_add_l3_intf_parsed,
    .data = NULL,
    .help_str = "add tests l3_intf port <eth_port> ip <ip> mask <mask>",
    .tokens = {
        (void *)&cmd_tests_add_l3_intf_T_add,
        (void *)&cmd_tests_add_l3_intf_T_tests,
        (void *)&cmd_tests_add_l3_intf_T_l3_intf,
        (void *)&cmd_tests_add_l3_intf_T_port_kw,
        (void *)&cmd_tests_add_l3_intf_T_port,
        (void *)&cmd_tests_add_l3_intf_T_ip_kw,
        (void *)&cmd_tests_add_l3_intf_T_ip,
        (void *)&cmd_tests_add_l3_intf_T_mask_kw,
        (void *)&cmd_tests_add_l3_intf_T_mask,
        NULL,
    },
};

/****************************************************************************
 * - "add tests l3_gw port <eth_port> gw <gw>"
 ****************************************************************************/
 struct cmd_tests_add_l3_gw_result {
    cmdline_fixed_string_t add;
    cmdline_fixed_string_t tests;
    cmdline_fixed_string_t l3_gw;
    cmdline_fixed_string_t port_kw;
    uint32_t               port;
    cmdline_fixed_string_t gw_kw;
    cmdline_ipaddr_t       gw;
};

static cmdline_parse_token_string_t cmd_tests_add_l3_gw_T_add =
    TOKEN_STRING_INITIALIZER(struct cmd_tests_add_l3_gw_result, add, "add");
static cmdline_parse_token_string_t cmd_tests_add_l3_gw_T_tests =
    TOKEN_STRING_INITIALIZER(struct cmd_tests_add_l3_gw_result, tests, "tests");
static cmdline_parse_token_string_t cmd_tests_add_l3_gw_T_l3_intf =
    TOKEN_STRING_INITIALIZER(struct cmd_tests_add_l3_gw_result, l3_gw, "l3_gw");
static cmdline_parse_token_string_t cmd_tests_add_l3_gw_T_port_kw =
    TOKEN_STRING_INITIALIZER(struct cmd_tests_add_l3_gw_result, port_kw, "port");
static cmdline_parse_token_num_t cmd_tests_add_l3_gw_T_port =
    TOKEN_NUM_INITIALIZER(struct cmd_tests_add_l3_gw_result, port, UINT32);
static cmdline_parse_token_string_t cmd_tests_add_l3_gw_T_gw_kw =
    TOKEN_STRING_INITIALIZER(struct cmd_tests_add_l3_gw_result, gw_kw, "gw");
static cmdline_parse_token_ipaddr_t cmd_tests_add_l3_gw_T_gw =
    TOKEN_IPADDR_INITIALIZER(struct cmd_tests_add_l3_gw_result, gw);

static void cmd_tests_add_l3_gw_parsed(void *parsed_result, struct cmdline *cl,
                                       void *data __rte_unused)
{
    printer_arg_t                      parg;
    struct cmd_tests_add_l3_gw_result *pr;
    tpg_ip_t                           gw;

    parg = TPG_PRINTER_ARG(cli_printer, cl);
    pr = parsed_result;

    if (pr->gw.family != AF_INET) {
        cmdline_printf(cl, "ERROR: IPv6 not supported yet!\n");
        return;
    }

    gw = TPG_IPV4(rte_be_to_cpu_32(pr->gw.addr.ipv4.s_addr));
    if (test_mgmt_add_port_cfg_l3_gw(pr->port, &gw, &parg) == 0)
        cmdline_printf(cl, "Default gateway successfully added.\n");
    else
        cmdline_printf(cl, "ERROR: Failed to add default gateway!\n");
}

cmdline_parse_inst_t cmd_tests_add_l3_gw = {
    .f = cmd_tests_add_l3_gw_parsed,
    .data = NULL,
    .help_str = "add tests l3_gw port <eth_port> gw <gw>",
    .tokens = {
        (void *)&cmd_tests_add_l3_gw_T_add,
        (void *)&cmd_tests_add_l3_gw_T_tests,
        (void *)&cmd_tests_add_l3_gw_T_l3_intf,
        (void *)&cmd_tests_add_l3_gw_T_port_kw,
        (void *)&cmd_tests_add_l3_gw_T_port,
        (void *)&cmd_tests_add_l3_gw_T_gw_kw,
        (void *)&cmd_tests_add_l3_gw_T_gw,
        NULL,
    },
};

/****************************************************************************
 * - "add tests server tcp|udp port <eth_port>
      ips <ip_range> l4_ports <port_range>
      data-req-plen <len> data-resp-plen <len>"
 ****************************************************************************/
 struct cmd_tests_add_server_result {
    cmdline_fixed_string_t add;
    cmdline_fixed_string_t tests;
    cmdline_fixed_string_t tcp_udp;
    cmdline_fixed_string_t server;
    cmdline_fixed_string_t port_kw;
    uint32_t               port;
    cmdline_fixed_string_t tcid_kw;
    uint32_t               tcid;
    cmdline_fixed_string_t src;
    cmdline_ipaddr_t       src_low;
    cmdline_ipaddr_t       src_high;
    cmdline_fixed_string_t sports;
    uint16_t               sport_low;
    uint16_t               sport_high;

    cmdline_fixed_string_t data_req_plen_kw;
    uint16_t               data_req_plen;
    cmdline_fixed_string_t data_resp_plen_kw;
    uint16_t               data_resp_plen;
};

static cmdline_parse_token_string_t cmd_tests_add_server_T_add =
    TOKEN_STRING_INITIALIZER(struct cmd_tests_add_server_result, add, "add");
static cmdline_parse_token_string_t cmd_tests_add_server_T_tests =
    TOKEN_STRING_INITIALIZER(struct cmd_tests_add_server_result, tests, "tests");

static cmdline_parse_token_string_t cmd_tests_add_server_T_tcp_udp =
    TOKEN_STRING_INITIALIZER(struct cmd_tests_add_server_result, tcp_udp, "tcp#udp");

static cmdline_parse_token_string_t cmd_tests_add_server_T_server =
    TOKEN_STRING_INITIALIZER(struct cmd_tests_add_server_result, server, "server");
static cmdline_parse_token_string_t cmd_tests_add_server_T_port_kw =
    TOKEN_STRING_INITIALIZER(struct cmd_tests_add_server_result, port_kw, "port");
static cmdline_parse_token_num_t cmd_tests_add_server_T_port =
    TOKEN_NUM_INITIALIZER(struct cmd_tests_add_server_result, port, UINT32);

static cmdline_parse_token_string_t cmd_tests_add_server_T_tcid_kw =
    TOKEN_STRING_INITIALIZER(struct cmd_tests_add_server_result, tcid_kw, "test-case-id");
static cmdline_parse_token_num_t cmd_tests_add_server_T_tcid =
    TOKEN_NUM_INITIALIZER(struct cmd_tests_add_server_result, tcid, UINT32);

static cmdline_parse_token_string_t cmd_tests_add_server_T_src =
    TOKEN_STRING_INITIALIZER(struct cmd_tests_add_server_result, src, "src");

static cmdline_parse_token_ipaddr_t cmd_tests_add_server_T_src_low =
    TOKEN_IPADDR_INITIALIZER(struct cmd_tests_add_server_result, src_low);
static cmdline_parse_token_ipaddr_t cmd_tests_add_server_T_src_high =
    TOKEN_IPADDR_INITIALIZER(struct cmd_tests_add_server_result, src_high);

static cmdline_parse_token_string_t cmd_tests_add_server_T_sports =
    TOKEN_STRING_INITIALIZER(struct cmd_tests_add_server_result, sports, "sport");
static cmdline_parse_token_num_t cmd_tests_add_server_T_sport_low =
    TOKEN_NUM_INITIALIZER(struct cmd_tests_add_server_result, sport_low, UINT16);
static cmdline_parse_token_num_t cmd_tests_add_server_T_sport_high =
    TOKEN_NUM_INITIALIZER(struct cmd_tests_add_server_result, sport_high, UINT16);

static void cmd_tests_add_tcp_udp_server_parsed(void *parsed_result,
                                                struct cmdline *cl,
                                                void *data __rte_unused)
{
    printer_arg_t                       parg;
    struct cmd_tests_add_server_result *pr;
    tpg_test_case_t                     tc;

    parg = TPG_PRINTER_ARG(cli_printer, cl);
    pr = parsed_result;

    if (pr->src_low.family != AF_INET || pr->src_high.family != AF_INET) {
        cmdline_printf(cl, "ERROR: IPv6 not supported yet!\n");
        return;
    }

    test_init_defaults(&tc, TEST_CASE_TYPE__SERVER, pr->port, pr->tcid);

    if (strncmp(pr->tcp_udp, "tcp", strlen("tcp")) == 0)
        tc.tc_server.srv_l4.l4s_proto = L4_PROTO__TCP;
    else if (strncmp(pr->tcp_udp, "udp", strlen("udp")) == 0)
        tc.tc_server.srv_l4.l4s_proto = L4_PROTO__UDP;
    else
        assert(false);

    tc.tc_server.srv_ips =
        TPG_IPV4_RANGE(rte_be_to_cpu_32(pr->src_low.addr.ipv4.s_addr),
                       rte_be_to_cpu_32(pr->src_high.addr.ipv4.s_addr));
    tc.tc_server.srv_l4.l4s_tcp_udp.tus_ports =
        TPG_PORT_RANGE(pr->sport_low, pr->sport_high);

    if (test_mgmt_add_test_case(pr->port, &tc, &parg) == 0)
        cmdline_printf(cl,
                       "Test case %"PRIu32
                       " successfully added to port %"PRIu32"!\n",
                       pr->tcid, pr->port);
    else
        cmdline_printf(cl,
                       "ERROR: Failed to add test case %"PRIu32
                       " to port %"PRIu32"!\n",
                       pr->tcid, pr->port);
}

cmdline_parse_inst_t cmd_tests_add_tcp_udp_server = {
    .f = cmd_tests_add_tcp_udp_server_parsed,
    .data = NULL,
    .help_str = "add tests server tcp|udp port <eth_port> "
                "test-case-id <tcid> src <ip_range> sport <port_range>",
    .tokens = {
        (void *)&cmd_tests_add_server_T_add,
        (void *)&cmd_tests_add_server_T_tests,
        (void *)&cmd_tests_add_server_T_server,
        (void *)&cmd_tests_add_server_T_tcp_udp,
        (void *)&cmd_tests_add_server_T_port_kw,
        (void *)&cmd_tests_add_server_T_port,
        (void *)&cmd_tests_add_server_T_tcid_kw,
        (void *)&cmd_tests_add_server_T_tcid,
        (void *)&cmd_tests_add_server_T_src,
        (void *)&cmd_tests_add_server_T_src_low,
        (void *)&cmd_tests_add_server_T_src_high,
        (void *)&cmd_tests_add_server_T_sports,
        (void *)&cmd_tests_add_server_T_sport_low,
        (void *)&cmd_tests_add_server_T_sport_high,
        NULL,
    },
};

/****************************************************************************
 * - "add tests client tcp|udp port <eth_port> test-case-id <tcid>
        srcs <srcs_range> sports <sports>
        dests <rip> dports <rports>"
 ****************************************************************************/
 struct cmd_tests_add_client_result {
    cmdline_fixed_string_t add;
    cmdline_fixed_string_t tests;
    cmdline_fixed_string_t client;
    cmdline_fixed_string_t tcp_udp;
    cmdline_fixed_string_t port_kw;
    uint32_t               port;
    cmdline_fixed_string_t tcid_kw;
    uint32_t               tcid;

    cmdline_fixed_string_t srcs;
    cmdline_ipaddr_t       src_low;
    cmdline_ipaddr_t       src_high;
    cmdline_fixed_string_t sports;
    uint16_t               sport_low;
    uint16_t               sport_high;

    cmdline_fixed_string_t dests;
    cmdline_ipaddr_t       dest_low;
    cmdline_ipaddr_t       dest_high;
    cmdline_fixed_string_t dports;
    uint16_t               dport_low;
    uint16_t               dport_high;
};

static cmdline_parse_token_string_t cmd_tests_add_client_T_add =
    TOKEN_STRING_INITIALIZER(struct cmd_tests_add_client_result, add, "add");
static cmdline_parse_token_string_t cmd_tests_add_client_T_tests =
    TOKEN_STRING_INITIALIZER(struct cmd_tests_add_client_result, tests, "tests");

static cmdline_parse_token_string_t cmd_tests_add_client_T_client =
    TOKEN_STRING_INITIALIZER(struct cmd_tests_add_client_result, client, "client");
static cmdline_parse_token_string_t cmd_tests_add_client_T_tcp_udp =
    TOKEN_STRING_INITIALIZER(struct cmd_tests_add_client_result, tcp_udp, "tcp#udp");

static cmdline_parse_token_string_t cmd_tests_add_client_T_port_kw =
    TOKEN_STRING_INITIALIZER(struct cmd_tests_add_client_result, port_kw, "port");
static cmdline_parse_token_num_t cmd_tests_add_client_T_port =
    TOKEN_NUM_INITIALIZER(struct cmd_tests_add_client_result, port, UINT32);

static cmdline_parse_token_string_t cmd_tests_add_client_T_tcid_kw =
    TOKEN_STRING_INITIALIZER(struct cmd_tests_add_client_result, tcid_kw, "test-case-id");
static cmdline_parse_token_num_t cmd_tests_add_client_T_tcid =
    TOKEN_NUM_INITIALIZER(struct cmd_tests_add_client_result, tcid, UINT32);

static cmdline_parse_token_string_t cmd_tests_add_client_T_srcs =
    TOKEN_STRING_INITIALIZER(struct cmd_tests_add_client_result, srcs, "src");
static cmdline_parse_token_ipaddr_t cmd_tests_add_client_T_src_low =
    TOKEN_IPADDR_INITIALIZER(struct cmd_tests_add_client_result, src_low);
static cmdline_parse_token_ipaddr_t cmd_tests_add_client_T_src_high =
    TOKEN_IPADDR_INITIALIZER(struct cmd_tests_add_client_result, src_high);

static cmdline_parse_token_string_t cmd_tests_add_client_T_sports =
    TOKEN_STRING_INITIALIZER(struct cmd_tests_add_client_result, sports, "sport");
static cmdline_parse_token_num_t cmd_tests_add_client_T_sport_low =
    TOKEN_NUM_INITIALIZER(struct cmd_tests_add_client_result, sport_low, UINT16);
static cmdline_parse_token_num_t cmd_tests_add_client_T_sport_high =
    TOKEN_NUM_INITIALIZER(struct cmd_tests_add_client_result, sport_high, UINT16);

static cmdline_parse_token_string_t cmd_tests_add_client_T_dests =
    TOKEN_STRING_INITIALIZER(struct cmd_tests_add_client_result, dests, "dest");
static cmdline_parse_token_ipaddr_t cmd_tests_add_client_T_dest_low =
    TOKEN_IPADDR_INITIALIZER(struct cmd_tests_add_client_result, dest_low);
static cmdline_parse_token_ipaddr_t cmd_tests_add_client_T_dest_high =
    TOKEN_IPADDR_INITIALIZER(struct cmd_tests_add_client_result, dest_high);

static cmdline_parse_token_string_t cmd_tests_add_client_T_dports =
    TOKEN_STRING_INITIALIZER(struct cmd_tests_add_client_result, dports, "dport");
static cmdline_parse_token_num_t cmd_tests_add_client_T_dport_low =
    TOKEN_NUM_INITIALIZER(struct cmd_tests_add_client_result, dport_low, UINT16);
static cmdline_parse_token_num_t cmd_tests_add_client_T_dport_high =
    TOKEN_NUM_INITIALIZER(struct cmd_tests_add_client_result, dport_high, UINT16);

static void cmd_tests_add_client_parsed(void *parsed_result, struct cmdline *cl,
                                        void *data __rte_unused)
{
    printer_arg_t                       parg;
    struct cmd_tests_add_client_result *pr;
    tpg_test_case_t                     tc;

    parg = TPG_PRINTER_ARG(cli_printer, cl);
    pr = parsed_result;

    if (pr->src_low.family != AF_INET ||
            pr->src_high.family != AF_INET ||
            pr->dest_low.family != AF_INET ||
            pr->dest_high.family != AF_INET) {
        cmdline_printf(cl, "ERROR: IPv6 not supported yet!\n");
        return;
    }

    test_init_defaults(&tc, TEST_CASE_TYPE__CLIENT, pr->port, pr->tcid);

    if (strncmp(pr->tcp_udp, "tcp", strlen("tcp")) == 0)
        tc.tc_client.cl_l4.l4c_proto = L4_PROTO__TCP;
    else if (strncmp(pr->tcp_udp, "udp", strlen("udp")) == 0)
        tc.tc_client.cl_l4.l4c_proto = L4_PROTO__UDP;

    tc.tc_client.cl_src_ips =
        TPG_IPV4_RANGE(rte_be_to_cpu_32(pr->src_low.addr.ipv4.s_addr),
                       rte_be_to_cpu_32(pr->src_high.addr.ipv4.s_addr));
    tc.tc_client.cl_l4.l4c_tcp_udp.tuc_sports =
        TPG_PORT_RANGE(pr->sport_low, pr->sport_high);

    tc.tc_client.cl_dst_ips =
        TPG_IPV4_RANGE(rte_be_to_cpu_32(pr->dest_low.addr.ipv4.s_addr),
                       rte_be_to_cpu_32(pr->dest_high.addr.ipv4.s_addr));
    tc.tc_client.cl_l4.l4c_tcp_udp.tuc_dports =
        TPG_PORT_RANGE(pr->dport_low, pr->dport_high);

    if (test_mgmt_add_test_case(pr->port, &tc, &parg) == 0)
        cmdline_printf(cl,
                       "Test case %"PRIu32
                       " successfully added to port %"PRIu32"!\n",
                       pr->tcid, pr->port);
    else
        cmdline_printf(cl,
                       "ERROR: Failed to add test case %"PRIu32
                       " to port %"PRIu32"!\n",
                       pr->tcid, pr->port);
}

cmdline_parse_inst_t cmd_tests_add_client = {
    .f = cmd_tests_add_client_parsed,
    .data = NULL,
    .help_str = "add tests client tcp|udp port <eth_port> test-case-id <tcid> "
                "src <ip-range> sport <l4-ports> "
                "dest <ip-range> dport <l4-ports>",
    .tokens = {
        (void *)&cmd_tests_add_client_T_add,
        (void *)&cmd_tests_add_client_T_tests,
        (void *)&cmd_tests_add_client_T_client,
        (void *)&cmd_tests_add_client_T_tcp_udp,
        (void *)&cmd_tests_add_client_T_port_kw,
        (void *)&cmd_tests_add_client_T_port,
        (void *)&cmd_tests_add_client_T_tcid_kw,
        (void *)&cmd_tests_add_client_T_tcid,

        (void *)&cmd_tests_add_client_T_srcs,
        (void *)&cmd_tests_add_client_T_src_low,
        (void *)&cmd_tests_add_client_T_src_high,

        (void *)&cmd_tests_add_client_T_sports,
        (void *)&cmd_tests_add_client_T_sport_low,
        (void *)&cmd_tests_add_client_T_sport_high,

        (void *)&cmd_tests_add_client_T_dests,
        (void *)&cmd_tests_add_client_T_dest_low,
        (void *)&cmd_tests_add_client_T_dest_high,

        (void *)&cmd_tests_add_client_T_dports,
        (void *)&cmd_tests_add_client_T_dport_low,
        (void *)&cmd_tests_add_client_T_dport_high,
        NULL,
    },
};

/****************************************************************************
 * - "del tests port <eth_port> test-case-id <tcid>
 ****************************************************************************/
struct cmd_tests_del_test_result {
    cmdline_fixed_string_t del;
    cmdline_fixed_string_t tests;
    cmdline_fixed_string_t port_kw;
    uint32_t               port;
    cmdline_fixed_string_t tcid_kw;
    uint32_t               tcid;
};

static cmdline_parse_token_string_t cmd_tests_del_test_T_del =
    TOKEN_STRING_INITIALIZER(struct cmd_tests_del_test_result, del, "del");
static cmdline_parse_token_string_t cmd_tests_del_test_T_tests =
    TOKEN_STRING_INITIALIZER(struct cmd_tests_del_test_result, tests, "tests");

static cmdline_parse_token_string_t cmd_tests_del_test_T_port_kw =
    TOKEN_STRING_INITIALIZER(struct cmd_tests_del_test_result, port_kw, "port");
static cmdline_parse_token_num_t cmd_tests_del_test_T_port =
    TOKEN_NUM_INITIALIZER(struct cmd_tests_del_test_result, port, UINT32);

static cmdline_parse_token_string_t cmd_tests_del_test_T_tcid_kw =
    TOKEN_STRING_INITIALIZER(struct cmd_tests_del_test_result, tcid_kw, "test-case-id");
static cmdline_parse_token_num_t cmd_tests_del_test_T_tcid =
    TOKEN_NUM_INITIALIZER(struct cmd_tests_del_test_result, tcid, UINT32);

static void cmd_tests_del_test_parsed(void *parsed_result, struct cmdline *cl,
                                      void *data __rte_unused)
{
    printer_arg_t                     parg;
    struct cmd_tests_del_test_result *pr;

    parg = TPG_PRINTER_ARG(cli_printer, cl);
    pr = parsed_result;

    if (test_mgmt_del_test_case(pr->port, pr->tcid, &parg) == 0)
        cmdline_printf(cl,
                       "Successfully deleted test case %"PRIu32
                       " from port %"PRIu32"\n",
                       pr->tcid,
                       pr->port);
    else
        cmdline_printf(cl,
                       "ERROR: Failed to delete test case %"PRIu32
                       " from port %"PRIu32"\n",
                       pr->tcid,
                       pr->port);
}

cmdline_parse_inst_t cmd_tests_del_test = {
    .f = cmd_tests_del_test_parsed,
    .data = NULL,
    .help_str = "del tests port <eth_port> test-case-id <tcid>",
    .tokens = {
        (void *)&cmd_tests_del_test_T_del,
        (void *)&cmd_tests_del_test_T_tests,
        (void *)&cmd_tests_del_test_T_port_kw,
        (void *)&cmd_tests_del_test_T_port,
        (void *)&cmd_tests_del_test_T_tcid_kw,
        (void *)&cmd_tests_del_test_T_tcid,
        NULL,
    },
};

/****************************************************************************
 * - "set tests rate port <eth_port> test-case-id <tcid>
 *      open|close|send <rate> | infinite"
 ****************************************************************************/
 struct cmd_tests_set_rate_result {
    cmdline_fixed_string_t set;
    cmdline_fixed_string_t tests;
    cmdline_fixed_string_t rate;
    cmdline_fixed_string_t port_kw;
    uint32_t               port;
    cmdline_fixed_string_t tcid_kw;
    uint32_t               tcid;

    cmdline_fixed_string_t rate_kw;
    uint32_t               rate_val;

    cmdline_fixed_string_t infinite;
};

static cmdline_parse_token_string_t cmd_tests_set_rate_T_set =
    TOKEN_STRING_INITIALIZER(struct cmd_tests_set_rate_result, set, "set");
static cmdline_parse_token_string_t cmd_tests_set_rate_T_tests =
    TOKEN_STRING_INITIALIZER(struct cmd_tests_set_rate_result, tests, "tests");
static cmdline_parse_token_string_t cmd_tests_set_rate_T_rate =
    TOKEN_STRING_INITIALIZER(struct cmd_tests_set_rate_result, rate, "rate");

static cmdline_parse_token_string_t cmd_tests_set_rate_T_port_kw =
    TOKEN_STRING_INITIALIZER(struct cmd_tests_set_rate_result, port_kw, "port");
static cmdline_parse_token_num_t cmd_tests_set_rate_T_port =
    TOKEN_NUM_INITIALIZER(struct cmd_tests_set_rate_result, port, UINT32);

static cmdline_parse_token_string_t cmd_tests_set_rate_T_tcid_kw =
    TOKEN_STRING_INITIALIZER(struct cmd_tests_set_rate_result, tcid_kw, "test-case-id");
static cmdline_parse_token_num_t cmd_tests_set_rate_T_tcid =
    TOKEN_NUM_INITIALIZER(struct cmd_tests_set_rate_result, tcid, UINT32);

static cmdline_parse_token_string_t cmd_tests_set_rate_T_rate_kw =
    TOKEN_STRING_INITIALIZER(struct cmd_tests_set_rate_result, rate_kw, "open#close#send");
static cmdline_parse_token_num_t cmd_tests_set_rate_T_rate_val =
    TOKEN_NUM_INITIALIZER(struct cmd_tests_set_rate_result, rate_val, UINT32);

static cmdline_parse_token_string_t cmd_tests_set_rate_T_infinite =
    TOKEN_STRING_INITIALIZER(struct cmd_tests_set_rate_result, infinite, "infinite");

static void cmd_tests_set_rate_parsed(void *parsed_result, struct cmdline *cl,
                                      void *data)
{
    printer_arg_t                     parg;
    struct cmd_tests_set_rate_result *pr;
    bool                              infinite = (((intptr_t) data) == 'i');
    tpg_update_arg_t                  update_arg;
    tpg_rate_t                        rate;

    tpg_xlate_default_UpdateArg(&update_arg);
    parg = TPG_PRINTER_ARG(cli_printer, cl);
    pr = parsed_result;

    if (infinite)
        rate = TPG_RATE_INF();
    else
        rate = TPG_RATE(pr->rate_val);

    if (strncmp(pr->rate_kw, "open", strlen("open")) == 0)
        update_arg.ua_rate_open = &rate;
    else if (strncmp(pr->rate_kw, "close", strlen("close")) == 0)
        update_arg.ua_rate_close = &rate;
    else if (strncmp(pr->rate_kw, "send", strlen("send")) == 0)
        update_arg.ua_rate_send = &rate;
    else
        assert(false);

    if (test_mgmt_update_test_case(pr->port, pr->tcid, &update_arg, &parg) == 0)
        cmdline_printf(cl, "Port %"PRIu32", Test Case %"PRIu32" updated!\n",
                       pr->port,
                       pr->tcid);
    else
        cmdline_printf(cl,
                       "ERROR: Failed updating test case %"PRIu32
                       " config on port %"PRIu32"\n",
                       pr->tcid,
                       pr->port);
}

cmdline_parse_inst_t cmd_tests_set_rate = {
    .f = cmd_tests_set_rate_parsed,
    .data = NULL,
    .help_str = "set tests rate port <eth_port> test-case-id <tcid> "
                "open|close|send <rate>",
    .tokens = {
        (void *)&cmd_tests_set_rate_T_set,
        (void *)&cmd_tests_set_rate_T_tests,
        (void *)&cmd_tests_set_rate_T_rate,
        (void *)&cmd_tests_set_rate_T_port_kw,
        (void *)&cmd_tests_set_rate_T_port,
        (void *)&cmd_tests_set_rate_T_tcid_kw,
        (void *)&cmd_tests_set_rate_T_tcid,
        (void *)&cmd_tests_set_rate_T_rate_kw,
        (void *)&cmd_tests_set_rate_T_rate_val,
        NULL,
    },
};

cmdline_parse_inst_t cmd_tests_set_rate_infinite = {
    .f = cmd_tests_set_rate_parsed,
    .data = (void *) (intptr_t) 'i',
    .help_str = "set tests rate port <eth_port> test-case-id <tcid> "
                "open|close|send infinite",
    .tokens = {
        (void *)&cmd_tests_set_rate_T_set,
        (void *)&cmd_tests_set_rate_T_tests,
        (void *)&cmd_tests_set_rate_T_rate,
        (void *)&cmd_tests_set_rate_T_port_kw,
        (void *)&cmd_tests_set_rate_T_port,
        (void *)&cmd_tests_set_rate_T_tcid_kw,
        (void *)&cmd_tests_set_rate_T_tcid,
        (void *)&cmd_tests_set_rate_T_rate_kw,
        (void *)&cmd_tests_set_rate_T_infinite,
        NULL,
    },
};

/****************************************************************************
 * - "set tests timeouts port <eth_port> test-case-id <tcid>
 *      init|uptime|downtime <timeout>|infinite
 ****************************************************************************/
 struct cmd_tests_set_timeouts_result {
    cmdline_fixed_string_t set;
    cmdline_fixed_string_t tests;
    cmdline_fixed_string_t timeouts;
    cmdline_fixed_string_t port_kw;
    uint32_t               port;
    cmdline_fixed_string_t tcid_kw;
    uint32_t               tcid;

    cmdline_fixed_string_t timeout_kw;
    uint32_t               timeout;

    cmdline_fixed_string_t infinite;
};

static cmdline_parse_token_string_t cmd_tests_set_timeouts_T_set =
    TOKEN_STRING_INITIALIZER(struct cmd_tests_set_timeouts_result, set, "set");
static cmdline_parse_token_string_t cmd_tests_set_timeouts_T_tests =
    TOKEN_STRING_INITIALIZER(struct cmd_tests_set_timeouts_result, tests, "tests");
static cmdline_parse_token_string_t cmd_tests_set_timeouts_T_timeouts =
    TOKEN_STRING_INITIALIZER(struct cmd_tests_set_timeouts_result, timeouts, "timeouts");

static cmdline_parse_token_string_t cmd_tests_set_timeouts_T_port_kw =
    TOKEN_STRING_INITIALIZER(struct cmd_tests_set_timeouts_result, port_kw, "port");
static cmdline_parse_token_num_t cmd_tests_set_timeouts_T_port =
    TOKEN_NUM_INITIALIZER(struct cmd_tests_set_timeouts_result, port, UINT32);

static cmdline_parse_token_string_t cmd_tests_set_timeouts_T_tcid_kw =
    TOKEN_STRING_INITIALIZER(struct cmd_tests_set_timeouts_result, tcid_kw, "test-case-id");
static cmdline_parse_token_num_t cmd_tests_set_timeouts_T_tcid =
    TOKEN_NUM_INITIALIZER(struct cmd_tests_set_timeouts_result, tcid, UINT32);

static cmdline_parse_token_string_t cmd_tests_set_timeouts_T_timeout_kw =
    TOKEN_STRING_INITIALIZER(struct cmd_tests_set_timeouts_result, timeout_kw, "init#uptime#downtime");
static cmdline_parse_token_num_t cmd_tests_set_timeouts_T_timeout =
    TOKEN_NUM_INITIALIZER(struct cmd_tests_set_timeouts_result, timeout, UINT32);

static cmdline_parse_token_string_t cmd_tests_set_timeouts_T_infinite =
    TOKEN_STRING_INITIALIZER(struct cmd_tests_set_timeouts_result, infinite, "infinite");

static void cmd_tests_set_timeouts_parsed(void *parsed_result,
                                          struct cmdline *cl,
                                          void *data)
{
    printer_arg_t                         parg;
    struct cmd_tests_set_timeouts_result *pr;
    bool                                  infinite;
    tpg_update_arg_t                      update_arg;
    tpg_delay_t                           timeout;

    tpg_xlate_default_UpdateArg(&update_arg);
    parg = TPG_PRINTER_ARG(cli_printer, cl);
    pr = parsed_result;
    infinite = (((intptr_t) data) == 'i');

    if (infinite)
        timeout = TPG_DELAY_INF();
    else
        timeout = TPG_DELAY(pr->timeout);

    if (strncmp(pr->timeout_kw, "init", strlen("init")) == 0)
        update_arg.ua_init_delay = &timeout;
    else if (strncmp(pr->timeout_kw, "uptime", strlen("uptime")) == 0)
        update_arg.ua_uptime = &timeout;
    else if (strncmp(pr->timeout_kw, "downtime", strlen("downtime")) == 0)
        update_arg.ua_downtime = &timeout;
    else
        assert(false);

    if (test_mgmt_update_test_case(pr->port, pr->tcid, &update_arg, &parg) == 0)
        cmdline_printf(cl, "Port %"PRIu32", Test Case %"PRIu32" updated!\n",
                       pr->port,
                       pr->tcid);
    else
        cmdline_printf(cl,
                       "ERROR: Failed updating test case %"PRIu32
                       " config on port %"PRIu32"\n",
                       pr->tcid,
                       pr->port);
}

cmdline_parse_inst_t cmd_tests_set_timeouts = {
    .f = cmd_tests_set_timeouts_parsed,
    .data = NULL,
    .help_str = "set tests timeouts port <eth_port> test-case-id <tcid> "
                "init|uptime|downtime <rate>",
    .tokens = {
        (void *)&cmd_tests_set_timeouts_T_set,
        (void *)&cmd_tests_set_timeouts_T_tests,
        (void *)&cmd_tests_set_timeouts_T_timeouts,
        (void *)&cmd_tests_set_timeouts_T_port_kw,
        (void *)&cmd_tests_set_timeouts_T_port,
        (void *)&cmd_tests_set_timeouts_T_tcid_kw,
        (void *)&cmd_tests_set_timeouts_T_tcid,
        (void *)&cmd_tests_set_timeouts_T_timeout_kw,
        (void *)&cmd_tests_set_timeouts_T_timeout,
        NULL,
    },
};

cmdline_parse_inst_t cmd_tests_set_timeouts_infinite = {
    .f = cmd_tests_set_timeouts_parsed,
    .data = (void *) (intptr_t) 'i',
    .help_str = "set tests timeouts port <eth_port> test-case-id <tcid> "
                "init|uptime|downtime infinite",
    .tokens = {
        (void *)&cmd_tests_set_timeouts_T_set,
        (void *)&cmd_tests_set_timeouts_T_tests,
        (void *)&cmd_tests_set_timeouts_T_timeouts,
        (void *)&cmd_tests_set_timeouts_T_port_kw,
        (void *)&cmd_tests_set_timeouts_T_port,
        (void *)&cmd_tests_set_timeouts_T_tcid_kw,
        (void *)&cmd_tests_set_timeouts_T_tcid,
        (void *)&cmd_tests_set_timeouts_T_timeout_kw,
        (void *)&cmd_tests_set_timeouts_T_infinite,
        NULL,
    },
};

/****************************************************************************
 * - "set tests criteria port <eth_port> test-case-id <tcid>
 *      run-time|servers-up|clients-up|clients-estab|data-MB <value>"
 ****************************************************************************/
 struct cmd_tests_set_criteria_result {
    cmdline_fixed_string_t set;
    cmdline_fixed_string_t tests;
    cmdline_fixed_string_t criteria;
    cmdline_fixed_string_t port_kw;
    uint32_t               port;
    cmdline_fixed_string_t tcid_kw;
    uint32_t               tcid;

    cmdline_fixed_string_t criteria_kw;
    uint32_t               criteria_val;
};

static cmdline_parse_token_string_t cmd_tests_set_criteria_T_set =
    TOKEN_STRING_INITIALIZER(struct cmd_tests_set_criteria_result, set, "set");
static cmdline_parse_token_string_t cmd_tests_set_criteria_T_tests =
    TOKEN_STRING_INITIALIZER(struct cmd_tests_set_criteria_result, tests, "tests");
static cmdline_parse_token_string_t cmd_tests_set_criteria_T_criteria =
    TOKEN_STRING_INITIALIZER(struct cmd_tests_set_criteria_result, criteria, "criteria");

static cmdline_parse_token_string_t cmd_tests_set_criteria_T_port_kw =
    TOKEN_STRING_INITIALIZER(struct cmd_tests_set_criteria_result, port_kw, "port");
static cmdline_parse_token_num_t cmd_tests_set_criteria_T_port =
    TOKEN_NUM_INITIALIZER(struct cmd_tests_set_criteria_result, port, UINT32);

static cmdline_parse_token_string_t cmd_tests_set_criteria_T_tcid_kw =
    TOKEN_STRING_INITIALIZER(struct cmd_tests_set_criteria_result, tcid_kw, "test-case-id");
static cmdline_parse_token_num_t cmd_tests_set_criteria_T_tcid =
    TOKEN_NUM_INITIALIZER(struct cmd_tests_set_criteria_result, tcid, UINT32);

static cmdline_parse_token_string_t cmd_tests_set_criteria_T_criteria_kw =
    TOKEN_STRING_INITIALIZER(struct cmd_tests_set_criteria_result, criteria_kw, "run-time#servers-up#clients-up#clients-estab#data-MB");
static cmdline_parse_token_num_t cmd_tests_set_criteria_T_criteria_val =
    TOKEN_NUM_INITIALIZER(struct cmd_tests_set_criteria_result, criteria_val, UINT32);

static void cmd_tests_set_criteria_parsed(void *parsed_result,
                                          struct cmdline *cl,
                                          void *data __rte_unused)
{
    printer_arg_t                         parg;
    struct cmd_tests_set_criteria_result *pr;
    tpg_update_arg_t                      update_arg;
    tpg_test_criteria_t                   criteria;

    tpg_xlate_default_UpdateArg(&update_arg);
    parg = TPG_PRINTER_ARG(cli_printer, cl);
    pr = parsed_result;

    if (strncmp(pr->criteria_kw, "run-time", strlen("run-time")) == 0)
        criteria = CRIT_RUN_TIME(pr->criteria_val);
    else if (strncmp(pr->criteria_kw, "servers-up", strlen("servers-up")) == 0)
        criteria = CRIT_SRV_UP(pr->criteria_val);
    else if (strncmp(pr->criteria_kw, "clients-up", strlen("clients-up")) == 0)
        criteria = CRIT_CL_UP(pr->criteria_val);
    else if (strncmp(pr->criteria_kw, "clients-estab", strlen("clients-estab")) == 0)
        criteria = CRIT_CL_ESTAB(pr->criteria_val);
    else if (strncmp(pr->criteria_kw, "data-MB", strlen("data-MB")) == 0)
        criteria = CRIT_DATA_MB(pr->criteria_val);
    else
        assert(false);

    update_arg.ua_criteria = &criteria;

    if (test_mgmt_update_test_case(pr->port, pr->tcid, &update_arg, &parg) == 0)
        cmdline_printf(cl, "Port %"PRIu32", Test Case %"PRIu32" updated!\n",
                       pr->port,
                       pr->tcid);
    else
        cmdline_printf(cl,
                       "ERROR: Failed updating test case %"PRIu32
                       " config on port %"PRIu32"\n",
                       pr->tcid,
                       pr->port);
}

cmdline_parse_inst_t cmd_tests_set_criteria = {
    .f = cmd_tests_set_criteria_parsed,
    .data = NULL,
    .help_str = "set tests criteria port <eth_port> test-case-id <tcid> "
                 "run-time|servers-up|clients-up|clients-estab|data-MB <value>",
    .tokens = {
        (void *)&cmd_tests_set_criteria_T_set,
        (void *)&cmd_tests_set_criteria_T_tests,
        (void *)&cmd_tests_set_criteria_T_criteria,
        (void *)&cmd_tests_set_criteria_T_port_kw,
        (void *)&cmd_tests_set_criteria_T_port,
        (void *)&cmd_tests_set_criteria_T_tcid_kw,
        (void *)&cmd_tests_set_criteria_T_tcid,
        (void *)&cmd_tests_set_criteria_T_criteria_kw,
        (void *)&cmd_tests_set_criteria_T_criteria_val,
        NULL,
    },
};

/****************************************************************************
 * - "set tests async port <eth_port> test-case-id <tcid>""
 * - "set tests no-async port <eth_port> test-case-id <tcid>""
 ****************************************************************************/
 struct cmd_tests_set_async_result {
    cmdline_fixed_string_t set;
    cmdline_fixed_string_t tests;
    cmdline_fixed_string_t async;
    cmdline_fixed_string_t noasync;
    cmdline_fixed_string_t port_kw;
    uint32_t               port;
    cmdline_fixed_string_t tcid_kw;
    uint32_t               tcid;
};

static cmdline_parse_token_string_t cmd_tests_set_async_T_set =
    TOKEN_STRING_INITIALIZER(struct cmd_tests_set_async_result, set, "set");
static cmdline_parse_token_string_t cmd_tests_set_async_T_tests =
    TOKEN_STRING_INITIALIZER(struct cmd_tests_set_async_result, tests, "tests");
static cmdline_parse_token_string_t cmd_tests_set_async_T_async =
    TOKEN_STRING_INITIALIZER(struct cmd_tests_set_async_result, async, "async");
static cmdline_parse_token_string_t cmd_tests_set_async_T_noasync =
    TOKEN_STRING_INITIALIZER(struct cmd_tests_set_async_result, noasync, "no-async");

static cmdline_parse_token_string_t cmd_tests_set_async_T_port_kw =
    TOKEN_STRING_INITIALIZER(struct cmd_tests_set_async_result, port_kw, "port");
static cmdline_parse_token_num_t cmd_tests_set_async_T_port =
    TOKEN_NUM_INITIALIZER(struct cmd_tests_set_async_result, port, UINT32);

static cmdline_parse_token_string_t cmd_tests_set_async_T_tcid_kw =
    TOKEN_STRING_INITIALIZER(struct cmd_tests_set_async_result, tcid_kw, "test-case-id");
static cmdline_parse_token_num_t cmd_tests_set_async_T_tcid =
    TOKEN_NUM_INITIALIZER(struct cmd_tests_set_async_result, tcid, UINT32);

OPT_FILL_TYPEDEF(test_case, tpg_update_arg_t);

OPT_FILL_CB_DEFINE(test_case, tpg_update_arg_t, ua_async, bool);

static void cmd_tests_set_async_parsed(void *parsed_result,
                                       struct cmdline *cl,
                                       void *data)
{
    printer_arg_t                      parg;
    struct cmd_tests_set_async_result *pr;
    tpg_update_arg_t                   update_arg;
    bool                               async = ((intptr_t)data);

    tpg_xlate_default_UpdateArg(&update_arg);
    parg = TPG_PRINTER_ARG(cli_printer, cl);
    pr = parsed_result;
    OPT_FILL_CB(test_case, ua_async)(&update_arg, &async);

    if (test_mgmt_update_test_case(pr->port, pr->tcid, &update_arg, &parg) == 0)
        cmdline_printf(cl, "Port %"PRIu32", Test Case %"PRIu32" updated!\n",
                       pr->port,
                       pr->tcid);
    else
        cmdline_printf(cl,
                       "ERROR: Failed updating test case %"PRIu32
                       " config on port %"PRIu32"\n",
                       pr->tcid,
                       pr->port);
}

cmdline_parse_inst_t cmd_tests_set_async = {
    .f = cmd_tests_set_async_parsed,
    .data = (void *) (intptr_t) true,
    .help_str = "set tests async port <eth_port> test-case-id <tcid>",
    .tokens = {
        (void *)&cmd_tests_set_async_T_set,
        (void *)&cmd_tests_set_async_T_tests,
        (void *)&cmd_tests_set_async_T_async,
        (void *)&cmd_tests_set_async_T_port_kw,
        (void *)&cmd_tests_set_async_T_port,
        (void *)&cmd_tests_set_async_T_tcid_kw,
        (void *)&cmd_tests_set_async_T_tcid,
        NULL,
    },
};

cmdline_parse_inst_t cmd_tests_set_noasync = {
    .f = cmd_tests_set_async_parsed,
    .data = (void *) (intptr_t) false,
    .help_str = "set tests noasync port <eth_port> test-case-id <tcid>",
    .tokens = {
        (void *)&cmd_tests_set_async_T_set,
        (void *)&cmd_tests_set_async_T_tests,
        (void *)&cmd_tests_set_async_T_noasync,
        (void *)&cmd_tests_set_async_T_port_kw,
        (void *)&cmd_tests_set_async_T_port,
        (void *)&cmd_tests_set_async_T_tcid_kw,
        (void *)&cmd_tests_set_async_T_tcid,
        NULL,
    },
};

/****************************************************************************
 * - "set tests port <eth_port> mtu <mtu_value>"
 ****************************************************************************/
 struct cmd_tests_set_mtu_result {
    cmdline_fixed_string_t set;
    cmdline_fixed_string_t tests;
    cmdline_fixed_string_t port_kw;
    uint32_t               port;
    cmdline_fixed_string_t mtu_kw;
    uint16_t               mtu;
};

static cmdline_parse_token_string_t cmd_tests_set_mtu_T_set =
    TOKEN_STRING_INITIALIZER(struct cmd_tests_set_mtu_result, set, "set");
static cmdline_parse_token_string_t cmd_tests_set_mtu_T_tests =
    TOKEN_STRING_INITIALIZER(struct cmd_tests_set_mtu_result, tests, "tests");

static cmdline_parse_token_string_t cmd_tests_set_mtu_T_port_kw =
    TOKEN_STRING_INITIALIZER(struct cmd_tests_set_mtu_result, port_kw, "port");
static cmdline_parse_token_num_t cmd_tests_set_mtu_T_port =
    TOKEN_NUM_INITIALIZER(struct cmd_tests_set_mtu_result, port, UINT32);

static cmdline_parse_token_string_t cmd_tests_set_mtu_T_mtu_kw =
    TOKEN_STRING_INITIALIZER(struct cmd_tests_set_mtu_result, mtu_kw, "mtu");
static cmdline_parse_token_num_t cmd_tests_set_mtu_T_mtu =
    TOKEN_NUM_INITIALIZER(struct cmd_tests_set_mtu_result, mtu, UINT16);

OPT_FILL_TYPEDEF(port, tpg_port_options_t);

OPT_FILL_DEFINE(port, tpg_port_options_t, po_mtu, uint16_t);

static void cmd_tests_set_mtu_parsed(void *parsed_result, struct cmdline *cl,
                                     void *data)
{
    printer_arg_t                    parg;
    struct cmd_tests_set_mtu_result *pr;
    OPT_FILL_TYPE_NAME(port)        *fill_param = data;
    tpg_port_options_t               port_opts;

    parg = TPG_PRINTER_ARG(cli_printer, cl);
    pr = parsed_result;
    fill_param->opt_cb(&port_opts, &pr->mtu);

    if (test_mgmt_set_port_options(pr->port, &port_opts, &parg) == 0)
        cmdline_printf(cl, "Port %"PRIu32" MTU updated!\n",
                       pr->port);
    else
        cmdline_printf(cl,
                       "ERROR: Failed updating MTU config on port %"PRIu32"\n",
                       pr->port);
}

cmdline_parse_inst_t cmd_tests_set_mtu = {
    .f = cmd_tests_set_mtu_parsed,
    .data = (void *) &OPT_FILL_PARAM_NAME(port, po_mtu),
    .help_str = "set tests mtu port <eth_port> <mtu-value>",
    .tokens = {
        (void *)&cmd_tests_set_mtu_T_set,
        (void *)&cmd_tests_set_mtu_T_tests,
        (void *)&cmd_tests_set_mtu_T_mtu_kw,
        (void *)&cmd_tests_set_mtu_T_port_kw,
        (void *)&cmd_tests_set_mtu_T_port,
        (void *)&cmd_tests_set_mtu_T_mtu,
        NULL,
    },
};

/****************************************************************************
 * - "set tests tcp-options port <eth_port> test-case-id <tcid> option value"
 ****************************************************************************/
struct cmd_tests_set_tcp_opts_result {
    cmdline_fixed_string_t set;
    cmdline_fixed_string_t tests;
    cmdline_fixed_string_t tcp_options;
    cmdline_fixed_string_t port_kw;
    uint32_t               port;
    cmdline_fixed_string_t tcid_kw;
    uint32_t               tcid;
    cmdline_fixed_string_t win_size;
    cmdline_fixed_string_t syn_retry;
    cmdline_fixed_string_t syn_ack_retry;
    cmdline_fixed_string_t data_retry;
    cmdline_fixed_string_t retry;
    cmdline_fixed_string_t rto;
    cmdline_fixed_string_t fin_to;
    cmdline_fixed_string_t twait_to;
    cmdline_fixed_string_t orphan_to;
    cmdline_fixed_string_t twait_skip;

    union {
        uint32_t opt_val_32;
        uint8_t  opt_val_8;
        bool     opt_val_bool;
    } opt_u;
};

static cmdline_parse_token_string_t cmd_tests_set_tcp_opts_T_set =
    TOKEN_STRING_INITIALIZER(struct cmd_tests_set_tcp_opts_result, set, "set");
static cmdline_parse_token_string_t cmd_tests_set_tcp_opts_T_tests =
    TOKEN_STRING_INITIALIZER(struct cmd_tests_set_tcp_opts_result, tests, "tests");
static cmdline_parse_token_string_t cmd_tests_set_tcp_opts_T_tcp_options =
    TOKEN_STRING_INITIALIZER(struct cmd_tests_set_tcp_opts_result, tcp_options, "tcp-options");

static cmdline_parse_token_string_t cmd_tests_set_tcp_opts_T_port_kw =
    TOKEN_STRING_INITIALIZER(struct cmd_tests_set_tcp_opts_result, port_kw, "port");
static cmdline_parse_token_num_t cmd_tests_set_tcp_opts_T_port =
    TOKEN_NUM_INITIALIZER(struct cmd_tests_set_tcp_opts_result, port, UINT32);

static cmdline_parse_token_string_t cmd_tests_set_tcp_opts_T_tcid_kw =
    TOKEN_STRING_INITIALIZER(struct cmd_tests_set_tcp_opts_result, tcid_kw, "test-case-id");
static cmdline_parse_token_num_t cmd_tests_set_tcp_opts_T_tcid =
    TOKEN_NUM_INITIALIZER(struct cmd_tests_set_tcp_opts_result, tcid, UINT32);

static cmdline_parse_token_string_t cmd_tests_set_tcp_opts_T_win_size =
    TOKEN_STRING_INITIALIZER(struct cmd_tests_set_tcp_opts_result, win_size, "win-size");
static cmdline_parse_token_string_t cmd_tests_set_tcp_opts_T_syn_retry =
    TOKEN_STRING_INITIALIZER(struct cmd_tests_set_tcp_opts_result, syn_retry, "syn-retry");
static cmdline_parse_token_string_t cmd_tests_set_tcp_opts_T_syn_ack_retry =
    TOKEN_STRING_INITIALIZER(struct cmd_tests_set_tcp_opts_result, syn_ack_retry, "syn-ack-retry");
static cmdline_parse_token_string_t cmd_tests_set_tcp_opts_T_data_retry =
    TOKEN_STRING_INITIALIZER(struct cmd_tests_set_tcp_opts_result, data_retry, "data-retry");
static cmdline_parse_token_string_t cmd_tests_set_tcp_opts_T_retry =
    TOKEN_STRING_INITIALIZER(struct cmd_tests_set_tcp_opts_result, retry, "retry");
static cmdline_parse_token_string_t cmd_tests_set_tcp_opts_T_rto =
    TOKEN_STRING_INITIALIZER(struct cmd_tests_set_tcp_opts_result, rto, "rto");
static cmdline_parse_token_string_t cmd_tests_set_tcp_opts_T_fin_to =
    TOKEN_STRING_INITIALIZER(struct cmd_tests_set_tcp_opts_result, fin_to, "fin-to");
static cmdline_parse_token_string_t cmd_tests_set_tcp_opts_T_twait_to =
    TOKEN_STRING_INITIALIZER(struct cmd_tests_set_tcp_opts_result, twait_to, "twait-to");
static cmdline_parse_token_string_t cmd_tests_set_tcp_opts_T_orphan_to =
    TOKEN_STRING_INITIALIZER(struct cmd_tests_set_tcp_opts_result, orphan_to, "orphan-to");
static cmdline_parse_token_string_t cmd_tests_set_tcp_opts_T_twait_skip =
    TOKEN_STRING_INITIALIZER(struct cmd_tests_set_tcp_opts_result, twait_skip, "twait-skip");

static cmdline_parse_token_num_t cmd_tests_set_tcp_opts_T_opt_val_32 =
    TOKEN_NUM_INITIALIZER(struct cmd_tests_set_tcp_opts_result, opt_u.opt_val_32, UINT32);
static cmdline_parse_token_num_t cmd_tests_set_tcp_opts_T_opt_val_8 =
    TOKEN_NUM_INITIALIZER(struct cmd_tests_set_tcp_opts_result, opt_u.opt_val_8, UINT8);
static cmdline_parse_token_num_t cmd_tests_set_tcp_opts_T_opt_val_bool =
    TOKEN_NUM_INITIALIZER(struct cmd_tests_set_tcp_opts_result, opt_u.opt_val_bool, UINT8);


OPT_FILL_TYPEDEF(tcp, tpg_tcp_sockopt_t);

OPT_FILL_DEFINE(tcp, tpg_tcp_sockopt_t, to_win_size, uint32_t);
OPT_FILL_DEFINE(tcp, tpg_tcp_sockopt_t, to_syn_retry_cnt, uint8_t);
OPT_FILL_DEFINE(tcp, tpg_tcp_sockopt_t, to_syn_ack_retry_cnt, uint8_t);
OPT_FILL_DEFINE(tcp, tpg_tcp_sockopt_t, to_data_retry_cnt, uint8_t);
OPT_FILL_DEFINE(tcp, tpg_tcp_sockopt_t, to_retry_cnt, uint8_t);
OPT_FILL_DEFINE(tcp, tpg_tcp_sockopt_t, to_rto, uint32_t);
OPT_FILL_DEFINE(tcp, tpg_tcp_sockopt_t, to_fin_to, uint32_t);
OPT_FILL_DEFINE(tcp, tpg_tcp_sockopt_t, to_twait_to, uint32_t);
OPT_FILL_DEFINE(tcp, tpg_tcp_sockopt_t, to_orphan_to, uint32_t);
OPT_FILL_DEFINE(tcp, tpg_tcp_sockopt_t, to_skip_timewait, bool);

static void cmd_tests_set_tcp_opts_parsed(void *parsed_result,
                                          struct cmdline *cl,
                                          void *data)
{
    printer_arg_t                         parg;
    struct cmd_tests_set_tcp_opts_result *pr;
    OPT_FILL_TYPE_NAME(tcp)              *fill_param = data;
    tpg_tcp_sockopt_t                     tcp_sockopt;

    parg = TPG_PRINTER_ARG(cli_printer, cl);
    pr = parsed_result;
    fill_param->opt_cb(&tcp_sockopt, &pr->opt_u);

    if (test_mgmt_set_tcp_sockopt(pr->port, pr->tcid, &tcp_sockopt, &parg) == 0)
        cmdline_printf(cl,
                       "Port %"PRIu32", Test Case %"PRIu32" TCP Socket Options updated!\n",
                       pr->port,
                       pr->tcid);
    else
        cmdline_printf(cl,
                       "ERROR: Failed updating test case %"PRIu32
                       " TCP Socket Options on port %"PRIu32"\n",
                       pr->tcid,
                       pr->port);
}

cmdline_parse_inst_t cmd_tests_set_tcp_opts_win_size = {
    .f = cmd_tests_set_tcp_opts_parsed,
    .data = (void *) &OPT_FILL_PARAM_NAME(tcp, to_win_size),
    .help_str = "set tests tcp-options port <eth_port> test-case-id <tcid> win-size <size>",
    .tokens = {
        (void *)&cmd_tests_set_tcp_opts_T_set,
        (void *)&cmd_tests_set_tcp_opts_T_tests,
        (void *)&cmd_tests_set_tcp_opts_T_tcp_options,
        (void *)&cmd_tests_set_tcp_opts_T_port_kw,
        (void *)&cmd_tests_set_tcp_opts_T_port,
        (void *)&cmd_tests_set_tcp_opts_T_tcid_kw,
        (void *)&cmd_tests_set_tcp_opts_T_tcid,
        (void *)&cmd_tests_set_tcp_opts_T_win_size,
        (void *)&cmd_tests_set_tcp_opts_T_opt_val_32,
        NULL,
    },
};

cmdline_parse_inst_t cmd_tests_set_tcp_opts_syn_retry = {
    .f = cmd_tests_set_tcp_opts_parsed,
    .data = (void *) &OPT_FILL_PARAM_NAME(tcp, to_syn_retry_cnt),
    .help_str = "set tests tcp-options port <eth_port> test-case-id <tcid> syn-retry <cnt>",
    .tokens = {
        (void *)&cmd_tests_set_tcp_opts_T_set,
        (void *)&cmd_tests_set_tcp_opts_T_tests,
        (void *)&cmd_tests_set_tcp_opts_T_tcp_options,
        (void *)&cmd_tests_set_tcp_opts_T_port_kw,
        (void *)&cmd_tests_set_tcp_opts_T_port,
        (void *)&cmd_tests_set_tcp_opts_T_tcid_kw,
        (void *)&cmd_tests_set_tcp_opts_T_tcid,
        (void *)&cmd_tests_set_tcp_opts_T_syn_retry,
        (void *)&cmd_tests_set_tcp_opts_T_opt_val_8,
        NULL,
    },
};

cmdline_parse_inst_t cmd_tests_set_tcp_opts_syn_ack_retry = {
    .f = cmd_tests_set_tcp_opts_parsed,
    .data = (void *) &OPT_FILL_PARAM_NAME(tcp, to_syn_ack_retry_cnt),
    .help_str = "set tests tcp-options port <eth_port> test-case-id <tcid> syn-ack-retry <cnt>",
    .tokens = {
        (void *)&cmd_tests_set_tcp_opts_T_set,
        (void *)&cmd_tests_set_tcp_opts_T_tests,
        (void *)&cmd_tests_set_tcp_opts_T_tcp_options,
        (void *)&cmd_tests_set_tcp_opts_T_port_kw,
        (void *)&cmd_tests_set_tcp_opts_T_port,
        (void *)&cmd_tests_set_tcp_opts_T_tcid_kw,
        (void *)&cmd_tests_set_tcp_opts_T_tcid,
        (void *)&cmd_tests_set_tcp_opts_T_syn_ack_retry,
        (void *)&cmd_tests_set_tcp_opts_T_opt_val_8,
        NULL,
    },
};

cmdline_parse_inst_t cmd_tests_set_tcp_opts_data_retry = {
    .f = cmd_tests_set_tcp_opts_parsed,
    .data = (void *) &OPT_FILL_PARAM_NAME(tcp, to_data_retry_cnt),
    .help_str = "set tests tcp-options port <eth_port> test-case-id <tcid> data-retry <cnt>",
    .tokens = {
        (void *)&cmd_tests_set_tcp_opts_T_set,
        (void *)&cmd_tests_set_tcp_opts_T_tests,
        (void *)&cmd_tests_set_tcp_opts_T_tcp_options,
        (void *)&cmd_tests_set_tcp_opts_T_port_kw,
        (void *)&cmd_tests_set_tcp_opts_T_port,
        (void *)&cmd_tests_set_tcp_opts_T_tcid_kw,
        (void *)&cmd_tests_set_tcp_opts_T_tcid,
        (void *)&cmd_tests_set_tcp_opts_T_data_retry,
        (void *)&cmd_tests_set_tcp_opts_T_opt_val_8,
        NULL,
    },
};

cmdline_parse_inst_t cmd_tests_set_tcp_opts_retry = {
    .f = cmd_tests_set_tcp_opts_parsed,
    .data = (void *) &OPT_FILL_PARAM_NAME(tcp, to_retry_cnt),
    .help_str = "set tests tcp-options port <eth_port> test-case-id <tcid> retry <cnt>",
    .tokens = {
        (void *)&cmd_tests_set_tcp_opts_T_set,
        (void *)&cmd_tests_set_tcp_opts_T_tests,
        (void *)&cmd_tests_set_tcp_opts_T_tcp_options,
        (void *)&cmd_tests_set_tcp_opts_T_port_kw,
        (void *)&cmd_tests_set_tcp_opts_T_port,
        (void *)&cmd_tests_set_tcp_opts_T_tcid_kw,
        (void *)&cmd_tests_set_tcp_opts_T_tcid,
        (void *)&cmd_tests_set_tcp_opts_T_retry,
        (void *)&cmd_tests_set_tcp_opts_T_opt_val_8,
        NULL,
    },
};

cmdline_parse_inst_t cmd_tests_set_tcp_opts_rto = {
    .f = cmd_tests_set_tcp_opts_parsed,
    .data = (void *) &OPT_FILL_PARAM_NAME(tcp, to_rto),
    .help_str = "set tests tcp-options port <eth_port> test-case-id <tcid> rto <rto_ms>",
    .tokens = {
        (void *)&cmd_tests_set_tcp_opts_T_set,
        (void *)&cmd_tests_set_tcp_opts_T_tests,
        (void *)&cmd_tests_set_tcp_opts_T_tcp_options,
        (void *)&cmd_tests_set_tcp_opts_T_port_kw,
        (void *)&cmd_tests_set_tcp_opts_T_port,
        (void *)&cmd_tests_set_tcp_opts_T_tcid_kw,
        (void *)&cmd_tests_set_tcp_opts_T_tcid,
        (void *)&cmd_tests_set_tcp_opts_T_rto,
        (void *)&cmd_tests_set_tcp_opts_T_opt_val_32,
        NULL,
    },
};

cmdline_parse_inst_t cmd_tests_set_tcp_opts_fin_to = {
    .f = cmd_tests_set_tcp_opts_parsed,
    .data = (void *) &OPT_FILL_PARAM_NAME(tcp, to_fin_to),
    .help_str = "set tests tcp-options port <eth_port> test-case-id <tcid> fin-to <fin_to_ms>",
    .tokens = {
        (void *)&cmd_tests_set_tcp_opts_T_set,
        (void *)&cmd_tests_set_tcp_opts_T_tests,
        (void *)&cmd_tests_set_tcp_opts_T_tcp_options,
        (void *)&cmd_tests_set_tcp_opts_T_port_kw,
        (void *)&cmd_tests_set_tcp_opts_T_port,
        (void *)&cmd_tests_set_tcp_opts_T_tcid_kw,
        (void *)&cmd_tests_set_tcp_opts_T_tcid,
        (void *)&cmd_tests_set_tcp_opts_T_fin_to,
        (void *)&cmd_tests_set_tcp_opts_T_opt_val_32,
        NULL,
    },
};

cmdline_parse_inst_t cmd_tests_set_tcp_opts_twait_to = {
    .f = cmd_tests_set_tcp_opts_parsed,
    .data = (void *) &OPT_FILL_PARAM_NAME(tcp, to_twait_to),
    .help_str = "set tests tcp-options port <eth_port> test-case-id <tcid> twait-to <twait_to_ms>",
    .tokens = {
        (void *)&cmd_tests_set_tcp_opts_T_set,
        (void *)&cmd_tests_set_tcp_opts_T_tests,
        (void *)&cmd_tests_set_tcp_opts_T_tcp_options,
        (void *)&cmd_tests_set_tcp_opts_T_port_kw,
        (void *)&cmd_tests_set_tcp_opts_T_port,
        (void *)&cmd_tests_set_tcp_opts_T_tcid_kw,
        (void *)&cmd_tests_set_tcp_opts_T_tcid,
        (void *)&cmd_tests_set_tcp_opts_T_twait_to,
        (void *)&cmd_tests_set_tcp_opts_T_opt_val_32,
        NULL,
    },
};

cmdline_parse_inst_t cmd_tests_set_tcp_opts_orphan_to = {
    .f = cmd_tests_set_tcp_opts_parsed,
    .data = (void *) &OPT_FILL_PARAM_NAME(tcp, to_orphan_to),
    .help_str = "set tests tcp-options port <eth_port> test-case-id <tcid> orphan-to <orphan_to_us>",
    .tokens = {
        (void *)&cmd_tests_set_tcp_opts_T_set,
        (void *)&cmd_tests_set_tcp_opts_T_tests,
        (void *)&cmd_tests_set_tcp_opts_T_tcp_options,
        (void *)&cmd_tests_set_tcp_opts_T_port_kw,
        (void *)&cmd_tests_set_tcp_opts_T_port,
        (void *)&cmd_tests_set_tcp_opts_T_tcid_kw,
        (void *)&cmd_tests_set_tcp_opts_T_tcid,
        (void *)&cmd_tests_set_tcp_opts_T_orphan_to,
        (void *)&cmd_tests_set_tcp_opts_T_opt_val_32,
        NULL,
    },
};

cmdline_parse_inst_t cmd_tests_set_tcp_opts_twait_skip = {
    .f = cmd_tests_set_tcp_opts_parsed,
    .data = (void *) &OPT_FILL_PARAM_NAME(tcp, to_skip_timewait),
    .help_str = "set tests tcp-options port <eth_port> test-case-id <tcid> twait-skip <true|false>",
    .tokens = {
        (void *)&cmd_tests_set_tcp_opts_T_set,
        (void *)&cmd_tests_set_tcp_opts_T_tests,
        (void *)&cmd_tests_set_tcp_opts_T_tcp_options,
        (void *)&cmd_tests_set_tcp_opts_T_port_kw,
        (void *)&cmd_tests_set_tcp_opts_T_port,
        (void *)&cmd_tests_set_tcp_opts_T_tcid_kw,
        (void *)&cmd_tests_set_tcp_opts_T_tcid,
        (void *)&cmd_tests_set_tcp_opts_T_twait_skip,
        (void *)&cmd_tests_set_tcp_opts_T_opt_val_bool,
        NULL,
    },
};

/****************************************************************************
 * - "show tests tcp-options port <eth_port> test-case-id <tcid>"
 ****************************************************************************/
 struct cmd_tests_show_tcp_opts_result {
    cmdline_fixed_string_t show;
    cmdline_fixed_string_t tests;
    cmdline_fixed_string_t tcp_options;
    cmdline_fixed_string_t port_kw;
    uint32_t               port;
    cmdline_fixed_string_t tcid_kw;
    uint32_t               tcid;
};

static cmdline_parse_token_string_t cmd_tests_show_tcp_opts_T_set =
    TOKEN_STRING_INITIALIZER(struct cmd_tests_show_tcp_opts_result, show, "show");
static cmdline_parse_token_string_t cmd_tests_show_tcp_opts_T_tests =
    TOKEN_STRING_INITIALIZER(struct cmd_tests_show_tcp_opts_result, tests, "tests");
static cmdline_parse_token_string_t cmd_tests_show_tcp_opts_T_tcp_options =
    TOKEN_STRING_INITIALIZER(struct cmd_tests_show_tcp_opts_result, tcp_options, "tcp-options");

static cmdline_parse_token_string_t cmd_tests_show_tcp_opts_T_port_kw =
    TOKEN_STRING_INITIALIZER(struct cmd_tests_show_tcp_opts_result, port_kw, "port");
static cmdline_parse_token_num_t cmd_tests_show_tcp_opts_T_port =
    TOKEN_NUM_INITIALIZER(struct cmd_tests_show_tcp_opts_result, port, UINT32);

static cmdline_parse_token_string_t cmd_tests_show_tcp_opts_T_tcid_kw =
    TOKEN_STRING_INITIALIZER(struct cmd_tests_show_tcp_opts_result, tcid_kw, "test-case-id");
static cmdline_parse_token_num_t cmd_tests_show_tcp_opts_T_tcid =
    TOKEN_NUM_INITIALIZER(struct cmd_tests_show_tcp_opts_result, tcid, UINT32);

static void cmd_tests_show_tcp_opts_parsed(void *parsed_result,
                                           struct cmdline *cl,
                                           void *data __rte_unused)
{
    printer_arg_t                          parg;
    struct cmd_tests_show_tcp_opts_result *pr;
    tpg_tcp_sockopt_t                      tcp_sockopt;

    parg = TPG_PRINTER_ARG(cli_printer, cl);
    pr = parsed_result;

    if (test_mgmt_get_tcp_sockopt(pr->port, pr->tcid, &tcp_sockopt, &parg) != 0)
        return;

    cmdline_printf(cl, "WIN   SYN SYN/ACK DATA RETRY RTO(ms) FIN(ms) TW(ms)  ORP(ms) TW-SKIP\n");
    cmdline_printf(cl, "----- --- ------- ---- ----- ------- ------- ------- ------- -------\n");
    cmdline_printf(cl, "%5u %3u %7u %4u %5u %7u %7u %7u %7u %7u\n",
                   tcp_sockopt.to_win_size,
                   tcp_sockopt.to_syn_retry_cnt,
                   tcp_sockopt.to_syn_ack_retry_cnt,
                   tcp_sockopt.to_data_retry_cnt,
                   tcp_sockopt.to_retry_cnt,
                   tcp_sockopt.to_rto,
                   tcp_sockopt.to_fin_to,
                   tcp_sockopt.to_twait_to,
                   tcp_sockopt.to_orphan_to,
                   tcp_sockopt.to_skip_timewait);
    cmdline_printf(cl, "\n\n");
}

cmdline_parse_inst_t cmd_tests_show_tcp_opts = {
    .f = cmd_tests_show_tcp_opts_parsed,
    .data = NULL,
    .help_str = "show tests tcp-options port <eth_port> test-case-id <tcid>",
    .tokens = {
        (void *)&cmd_tests_show_tcp_opts_T_set,
        (void *)&cmd_tests_show_tcp_opts_T_tests,
        (void *)&cmd_tests_show_tcp_opts_T_tcp_options,
        (void *)&cmd_tests_show_tcp_opts_T_port_kw,
        (void *)&cmd_tests_show_tcp_opts_T_port,
        (void *)&cmd_tests_show_tcp_opts_T_tcid_kw,
        (void *)&cmd_tests_show_tcp_opts_T_tcid,
        NULL,
    },
};

/****************************************************************************
 * - "exit"
 ****************************************************************************/
struct cmd_exit_result {
    cmdline_fixed_string_t exit;
};

static cmdline_parse_token_string_t cmd_exit_T_exit =
    TOKEN_STRING_INITIALIZER(struct cmd_exit_result, exit, "exit");

static void cmd_exit_parsed(void *parsed_result __rte_unused,
                            struct cmdline *cl,
                            void *data __rte_unused)
{
    cmdline_quit(cl);
}

cmdline_parse_inst_t cmd_exit = {
    .f = cmd_exit_parsed,
    .data = NULL,
    .help_str = "exit",
    .tokens = {
        (void *)&cmd_exit_T_exit,
        NULL,
    },
};

/*****************************************************************************
 * Main menu context
 ****************************************************************************/
static cmdline_parse_ctx_t cli_ctx[] = {
    &cmd_tests_start,
    &cmd_tests_stop,
    &cmd_show_tests_ui,
    &cmd_show_link_rate,
    &cmd_show_tests_config,
    &cmd_show_tests_state,
    &cmd_show_tests_stats,
    &cmd_tests_add_l3_intf,
    &cmd_tests_add_l3_gw,
    &cmd_tests_add_tcp_udp_server,
    &cmd_tests_add_client,
    &cmd_tests_del_test,
    &cmd_tests_set_rate,
    &cmd_tests_set_rate_infinite,
    &cmd_tests_set_timeouts,
    &cmd_tests_set_timeouts_infinite,
    &cmd_tests_set_criteria,
    &cmd_tests_set_noasync,
    &cmd_tests_set_async,
    &cmd_tests_set_mtu,
    &cmd_tests_set_tcp_opts_win_size,
    &cmd_tests_set_tcp_opts_syn_retry,
    &cmd_tests_set_tcp_opts_syn_ack_retry,
    &cmd_tests_set_tcp_opts_data_retry,
    &cmd_tests_set_tcp_opts_retry,
    &cmd_tests_set_tcp_opts_rto,
    &cmd_tests_set_tcp_opts_fin_to,
    &cmd_tests_set_tcp_opts_twait_to,
    &cmd_tests_set_tcp_opts_orphan_to,
    &cmd_tests_set_tcp_opts_twait_skip,
    &cmd_tests_show_tcp_opts,
    &cmd_exit,
    NULL,
};

/*****************************************************************************
 * test_mgmt_cli_init()
 ****************************************************************************/
bool test_mgmt_cli_init(void)
{
    return cli_add_main_ctx(cli_ctx);
}

