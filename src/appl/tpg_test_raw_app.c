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
 *     tpg_test_raw_app.c
 *
 * Description:
 *     RAW application implementation.
 *
 * Author:
 *     Dumitru Ceara, Eelco Chaudron
 *
 * Initial Created:
 *     02/22/2016
 *
 * Notes:
 *     The RAW application emulates request and response traffic. The client
 *     sends a request packet of a fixed configured size and waits for a fixed
 *     size response packet from the server. The user should configure the
 *     request/response size for both client and server test cases.
 *     The user has to make sure that the _request/response sizes match between
 *     clients and servers!
 *
 */

/*****************************************************************************
 * Include files
 ****************************************************************************/
#include "tcp_generator.h"

/*****************************************************************************
 * Local definitions
 ****************************************************************************/
#define RAW_DATA_TEMPLATE_SIZE GCFG_MBUF_PACKET_FRAGMENT_SIZE

/*****************************************************************************
 * Forward references.
 ****************************************************************************/
static cmdline_parse_ctx_t cli_ctx[];

/*****************************************************************************
 * Globals
 ****************************************************************************/
static RTE_DEFINE_PER_LCORE(struct rte_mbuf *, data_template);

/*****************************************************************************
 * raw_goto_state()
 ****************************************************************************/
static void raw_goto_state(raw_app_t *raw, raw_state_t state, uint16_t remaining)
{
    raw->ra_remaining_count = remaining;
    raw->ra_state = state;
}

/*****************************************************************************
 * raw_client_default_cfg()
 ****************************************************************************/
void raw_client_default_cfg(tpg_test_case_t *cfg)
{
    cfg->tc_client.cl_app.ac_raw.rc_req_plen = 0;
    cfg->tc_client.cl_app.ac_raw.rc_resp_plen = 0;
}

/*****************************************************************************
 * raw_server_default_cfg()
 ****************************************************************************/
void raw_server_default_cfg(tpg_test_case_t *cfg)
{
    cfg->tc_server.srv_app.as_raw.rs_req_plen = 0;
    cfg->tc_server.srv_app.as_raw.rs_resp_plen = 0;
}

/*****************************************************************************
 * raw_validate_cfg()
 ****************************************************************************/
bool raw_validate_cfg(const tpg_test_case_t *cfg __rte_unused,
                      printer_arg_t *printer_arg __rte_unused)
{
    /* Nothing to validate? */
    return true;
}

/*****************************************************************************
 * raw_client_print_cfg()
 ****************************************************************************/
void raw_client_print_cfg(const tpg_test_case_t *cfg,
                          printer_arg_t *printer_arg)
{
    tpg_printf(printer_arg, "RAW CLIENT:\n");
    tpg_printf(printer_arg, "%-16s : %"PRIu32"\n", "Request Len",
               cfg->tc_client.cl_app.ac_raw.rc_req_plen);
    tpg_printf(printer_arg, "%-16s : %"PRIu32"\n", "Response Len",
               cfg->tc_client.cl_app.ac_raw.rc_resp_plen);
}

/*****************************************************************************
 * raw_server_print_cfg()
 ****************************************************************************/
void raw_server_print_cfg(const tpg_test_case_t *cfg,
                          printer_arg_t *printer_arg)
{
    tpg_printf(printer_arg, "RAW SERVER:\n");
    tpg_printf(printer_arg, "%-16s : %"PRIu32"\n", "Request Len",
               cfg->tc_server.srv_app.as_raw.rs_req_plen);
    tpg_printf(printer_arg, "%-16s : %"PRIu32"\n", "Response Len",
               cfg->tc_server.srv_app.as_raw.rs_resp_plen);
}

/*****************************************************************************
 * raw_delete_cfg()
 ****************************************************************************/
void raw_delete_cfg(const tpg_test_case_t *cfg __rte_unused)
{
    /* Nothing allocated, nothing to do. */
}

/*****************************************************************************
 * raw_client_init()
 ****************************************************************************/
void raw_client_init(app_data_t *app_data, test_case_init_msg_t *init_msg)
{
    app_data->ad_raw.ra_req_size =
        init_msg->tcim_client.cl_app.ac_raw.rc_req_plen;
    app_data->ad_raw.ra_resp_size =
        init_msg->tcim_client.cl_app.ac_raw.rc_resp_plen;
}

/*****************************************************************************
 * raw_server_init()
 ****************************************************************************/
void raw_server_init(app_data_t *app_data, test_case_init_msg_t *init_msg)
{
    /* Servers act exactly in the same way as clients except that requests and
     * responses are swapped.
     */
    app_data->ad_raw.ra_resp_size =
        init_msg->tcim_server.srv_app.as_raw.rs_req_plen;
    app_data->ad_raw.ra_req_size =
        init_msg->tcim_server.srv_app.as_raw.rs_resp_plen;
}

/*****************************************************************************
 * raw_tc_start()
 ****************************************************************************/
void raw_tc_start(test_case_init_msg_t *init_msg __rte_unused)
{
    /* RAW traffic is quite dumb so we don't need to initialize anything when
     * starting a testcase.
     */
}

/*****************************************************************************
 * raw_tc_stop()
 ****************************************************************************/
void raw_tc_stop(test_case_init_msg_t *init_msg __rte_unused)
{
    /* RAW traffic is quite dumb so we don't need to uninitialize anything when
     * stopping a testcase.
     */
}

/*****************************************************************************
 * raw_client_conn_up()
 ****************************************************************************/
void raw_client_conn_up(l4_control_block_t *l4, app_data_t *app_data,
                        tpg_test_case_app_stats_t *stats __rte_unused)
{
    raw_goto_state(&app_data->ad_raw, RAWS_SENDING,
                   app_data->ad_raw.ra_req_size);

    /* Might be that we just need to setup the connection and send no data.. */
    if (app_data->ad_raw.ra_req_size == 0)
        return;

    TEST_NOTIF(TEST_NOTIF_APP_CLIENT_SEND_START, l4, l4->l4cb_test_case_id,
               l4->l4cb_interface);
}

/*****************************************************************************
 * raw_server_conn_up()
 ****************************************************************************/
void raw_server_conn_up(l4_control_block_t *l4 __rte_unused,
                        app_data_t *app_data,
                        tpg_test_case_app_stats_t *stats __rte_unused)
{
    raw_goto_state(&app_data->ad_raw, RAWS_RECEIVING,
                   app_data->ad_raw.ra_resp_size);
}

/*****************************************************************************
 * raw_conn_down()
 ****************************************************************************/
void raw_conn_down(l4_control_block_t *l4 __rte_unused,
                  app_data_t *app_data __rte_unused,
                  tpg_test_case_app_stats_t *stats __rte_unused)
{
    /* Normally we should go through the state machine but there's nothing to
     * cleanup for RAW connections.
     */
}

/*****************************************************************************
 * raw_deliver_data()
 ****************************************************************************/
static void raw_deliver_data(app_data_t *app_data, struct rte_mbuf *rx_data)
{
    if (unlikely(rx_data->pkt_len > app_data->ad_raw.ra_remaining_count))
        app_data->ad_raw.ra_remaining_count = 0;
    else
        app_data->ad_raw.ra_remaining_count -= rx_data->pkt_len;
}

/*****************************************************************************
 * raw_client_deliver_data()
 ****************************************************************************/
uint32_t raw_client_deliver_data(l4_control_block_t *l4, app_data_t *app_data,
                                 tpg_test_case_app_stats_t *stats,
                                 struct rte_mbuf *rx_data)
{
    tpg_raw_stats_t *raw_stats = &stats->tcas_raw;

    raw_deliver_data(app_data, rx_data);
    if (app_data->ad_raw.ra_remaining_count == 0 &&
            app_data->ad_raw.ra_resp_size != 0) {
        INC_STATS(raw_stats, rsts_resp_cnt);
        TEST_NOTIF(TEST_NOTIF_APP_CLIENT_SEND_START, l4, l4->l4cb_test_case_id,
                   l4->l4cb_interface);
        raw_goto_state(&app_data->ad_raw, RAWS_SENDING,
                       app_data->ad_raw.ra_req_size);
    }
    return rx_data->pkt_len;
}

/*****************************************************************************
 * raw_server_deliver_data()
 ****************************************************************************/
uint32_t raw_server_deliver_data(l4_control_block_t *l4, app_data_t *app_data,
                                 tpg_test_case_app_stats_t *stats,
                                 struct rte_mbuf *rx_data)
{
    tpg_raw_stats_t *raw_stats = &stats->tcas_raw;

    raw_deliver_data(app_data, rx_data);
    if (app_data->ad_raw.ra_remaining_count == 0 &&
        app_data->ad_raw.ra_req_size != 0) {

        INC_STATS(raw_stats, rsts_req_cnt);
        TEST_NOTIF(TEST_NOTIF_APP_SERVER_SEND_START, l4, l4->l4cb_test_case_id,
                   l4->l4cb_interface);
        raw_goto_state(&app_data->ad_raw, RAWS_SENDING,
                       app_data->ad_raw.ra_req_size);
    }
    return rx_data->pkt_len;
}

/*****************************************************************************
 * raw_send_data()
 ****************************************************************************/
struct rte_mbuf *raw_send_data(l4_control_block_t *l4 __rte_unused,
                               app_data_t *app_data,
                               tpg_test_case_app_stats_t *stats __rte_unused,
                               uint32_t max_tx_size)
{
    struct rte_mbuf *tx_mbuf;
    uint32_t         to_send;
    uint8_t         *template_data;
    phys_addr_t      template_data_phys;

    to_send = TPG_MIN(app_data->ad_raw.ra_remaining_count, max_tx_size);

    template_data = rte_pktmbuf_mtod(RTE_PER_LCORE(data_template), uint8_t *);
    template_data_phys = rte_pktmbuf_mtophys(RTE_PER_LCORE(data_template));
    tx_mbuf = data_chain_from_static_template(to_send, template_data,
                                              template_data_phys,
                                              RAW_DATA_TEMPLATE_SIZE);

    return tx_mbuf;
}

/*****************************************************************************
 * raw_data_sent()
 ****************************************************************************/
static void raw_data_sent(app_data_t *app_data, uint32_t bytes_sent)
{
    app_data->ad_raw.ra_remaining_count -= bytes_sent;
}

/*****************************************************************************
 * raw_client_data_sent()
 ****************************************************************************/
void raw_client_data_sent(l4_control_block_t *l4, app_data_t *app_data,
                          tpg_test_case_app_stats_t *stats,
                          uint32_t bytes_sent)
{
    tpg_raw_stats_t *raw_stats = &stats->tcas_raw;

    raw_data_sent(app_data, bytes_sent);
    if (app_data->ad_raw.ra_remaining_count == 0) {
        INC_STATS(raw_stats, rsts_req_cnt);

        if (app_data->ad_raw.ra_resp_size != 0) {
            TEST_NOTIF(TEST_NOTIF_APP_CLIENT_SEND_STOP, l4,
                       l4->l4cb_test_case_id,
                       l4->l4cb_interface);
            raw_goto_state(&app_data->ad_raw, RAWS_RECEIVING,
                           app_data->ad_raw.ra_resp_size);
        } else {
            TEST_NOTIF(TEST_NOTIF_APP_CLIENT_SEND_STOP, l4,
                       l4->l4cb_test_case_id,
                       l4->l4cb_interface);
            TEST_NOTIF(TEST_NOTIF_APP_CLIENT_SEND_START, l4,
                       l4->l4cb_test_case_id,
                       l4->l4cb_interface);
            raw_goto_state(&app_data->ad_raw, RAWS_SENDING,
                           app_data->ad_raw.ra_req_size);
        }
    }
}

/*****************************************************************************
 * raw_server_data_sent()
 ****************************************************************************/
void raw_server_data_sent(l4_control_block_t *l4, app_data_t *app_data,
                          tpg_test_case_app_stats_t *stats,
                          uint32_t bytes_sent)
{
    tpg_raw_stats_t *raw_stats = &stats->tcas_raw;

    raw_data_sent(app_data, bytes_sent);
    if (app_data->ad_raw.ra_remaining_count == 0) {
        INC_STATS(raw_stats, rsts_resp_cnt);

        TEST_NOTIF(TEST_NOTIF_APP_SERVER_SEND_STOP, l4, l4->l4cb_test_case_id,
                   l4->l4cb_interface);
        raw_goto_state(&app_data->ad_raw, RAWS_RECEIVING,
                       app_data->ad_raw.ra_resp_size);
    }
}

/*****************************************************************************
 * raw_stats_add()
 ****************************************************************************/
void raw_stats_add(tpg_test_case_app_stats_t *total,
                   const tpg_test_case_app_stats_t *elem)
{
    total->tcas_raw.rsts_req_cnt += elem->tcas_raw.rsts_req_cnt;
    total->tcas_raw.rsts_resp_cnt += elem->tcas_raw.rsts_resp_cnt;
}

/*****************************************************************************
 * raw_stats_print()
 ****************************************************************************/
void raw_stats_print(const tpg_test_case_app_stats_t *stats,
                     printer_arg_t *printer_arg)
{
    tpg_printf(printer_arg, "%13s %13s\n", "Requests", "Replies");
    tpg_printf(printer_arg, "%13"PRIu32 " %13"PRIu32 "\n",
               stats->tcas_raw.rsts_req_cnt,
               stats->tcas_raw.rsts_resp_cnt);
}

/*****************************************************************************
 * raw_init()
 ****************************************************************************/
bool raw_init(void)
{
    /*
     * Add RAW module CLI commands
     */
    if (!cli_add_main_ctx(cli_ctx)) {
        RTE_LOG(ERR, USER1, "ERROR: Can't add RAW specific CLI commands!\n");
        return false;
    }

    return true;
}

/*****************************************************************************
 * raw_lcore_init()
 ****************************************************************************/
void raw_lcore_init(uint32_t lcore_id __rte_unused)
{
    RTE_PER_LCORE(data_template) = rte_pktmbuf_alloc(mem_get_mbuf_local_pool());
    if (RTE_PER_LCORE(data_template) == NULL)
        TPG_ERROR_ABORT("ERROR: %s!\n",
                        "Failed to allocate per core RAW data template");

    if (RTE_PER_LCORE(data_template)->buf_len < RAW_DATA_TEMPLATE_SIZE)
        TPG_ERROR_ABORT("ERROR: %s!\n",
                        "RAW template doesn't fit in a single segment");

    memset(rte_pktmbuf_mtod(RTE_PER_LCORE(data_template), uint8_t *), 42,
           RAW_DATA_TEMPLATE_SIZE);
}

/*****************************************************************************
 * CLI
 ****************************************************************************/

/****************************************************************************
 * - "set tests client raw port <port> test-case-id <tcid>
 *      data-req-plen <len> data-resp-plen <len>"
 ****************************************************************************/
 struct cmd_tests_set_app_raw_client_result {
    cmdline_fixed_string_t set;
    cmdline_fixed_string_t tests;
    cmdline_fixed_string_t client;
    cmdline_fixed_string_t raw;
    cmdline_fixed_string_t port_kw;
    uint32_t               port;
    cmdline_fixed_string_t tcid_kw;
    uint32_t               tcid;

    cmdline_fixed_string_t data_req_kw;
    uint32_t               data_req_plen;

    cmdline_fixed_string_t data_resp_kw;
    uint32_t               data_resp_plen;
};

static cmdline_parse_token_string_t cmd_tests_set_app_raw_client_T_set =
    TOKEN_STRING_INITIALIZER(struct cmd_tests_set_app_raw_client_result, set, "set");
static cmdline_parse_token_string_t cmd_tests_set_app_raw_client_T_tests =
    TOKEN_STRING_INITIALIZER(struct cmd_tests_set_app_raw_client_result, tests, "tests");
static cmdline_parse_token_string_t cmd_tests_set_app_raw_client_T_client =
    TOKEN_STRING_INITIALIZER(struct cmd_tests_set_app_raw_client_result, client, "client");
static cmdline_parse_token_string_t cmd_tests_set_app_raw_client_T_raw =
    TOKEN_STRING_INITIALIZER(struct cmd_tests_set_app_raw_client_result, raw, "raw");

static cmdline_parse_token_string_t cmd_tests_set_app_raw_client_T_port_kw =
    TOKEN_STRING_INITIALIZER(struct cmd_tests_set_app_raw_client_result, port_kw, "port");
static cmdline_parse_token_num_t cmd_tests_set_app_raw_client_T_port =
    TOKEN_NUM_INITIALIZER(struct cmd_tests_set_app_raw_client_result, port, UINT32);

static cmdline_parse_token_string_t cmd_tests_set_app_raw_client_T_tcid_kw =
    TOKEN_STRING_INITIALIZER(struct cmd_tests_set_app_raw_client_result, tcid_kw, "test-case-id");
static cmdline_parse_token_num_t cmd_tests_set_app_raw_client_T_tcid =
    TOKEN_NUM_INITIALIZER(struct cmd_tests_set_app_raw_client_result, tcid, UINT32);

static cmdline_parse_token_string_t cmd_tests_set_app_raw_client_T_data_req_kw =
    TOKEN_STRING_INITIALIZER(struct cmd_tests_set_app_raw_client_result, data_req_kw, "data-req-plen");
static cmdline_parse_token_num_t cmd_tests_set_app_raw_client_T_data_req_plen =
    TOKEN_NUM_INITIALIZER(struct cmd_tests_set_app_raw_client_result, data_req_plen, UINT32);

static cmdline_parse_token_string_t cmd_tests_set_app_raw_client_T_data_resp_kw =
    TOKEN_STRING_INITIALIZER(struct cmd_tests_set_app_raw_client_result, data_resp_kw, "data-resp-plen");
static cmdline_parse_token_num_t cmd_tests_set_app_raw_client_T_data_resp_plen =
    TOKEN_NUM_INITIALIZER(struct cmd_tests_set_app_raw_client_result, data_resp_plen, UINT32);

static void cmd_tests_set_app_raw_client_parsed(void *parsed_result,
                                                struct cmdline *cl,
                                                void *data __rte_unused)
{
    printer_arg_t                               parg;
    struct cmd_tests_set_app_raw_client_result *pr;
    tpg_app_client_t                            app_client_cfg;
    int                                         err;

    parg = TPG_PRINTER_ARG(cli_printer, cl);
    pr = parsed_result;

    app_client_cfg.ac_app_proto = APP_PROTO__RAW;
    app_client_cfg.ac_raw.rc_req_plen = pr->data_req_plen;
    app_client_cfg.ac_raw.rc_resp_plen = pr->data_resp_plen;

    err = test_mgmt_update_test_case_app_client(pr->port, pr->tcid,
                                                &app_client_cfg,
                                                &parg);
    if (err == 0) {
        cmdline_printf(cl, "Port %"PRIu32", Test Case %"PRIu32" updated!\n",
                       pr->port,
                       pr->tcid);
    } else {
        cmdline_printf(cl,
                       "ERROR: Failed updating test case %"PRIu32
                       " config on port %"PRIu32"\n",
                       pr->tcid,
                       pr->port);
    }
}

cmdline_parse_inst_t cmd_tests_set_app_raw_client = {
    .f = cmd_tests_set_app_raw_client_parsed,
    .data = NULL,
    .help_str = "set tests client raw port <port> test-case-id <tcid>"
                "data-req-plen <len> data-resp-plen <len>",
    .tokens = {
        (void *)&cmd_tests_set_app_raw_client_T_set,
        (void *)&cmd_tests_set_app_raw_client_T_tests,
        (void *)&cmd_tests_set_app_raw_client_T_client,
        (void *)&cmd_tests_set_app_raw_client_T_raw,
        (void *)&cmd_tests_set_app_raw_client_T_port_kw,
        (void *)&cmd_tests_set_app_raw_client_T_port,
        (void *)&cmd_tests_set_app_raw_client_T_tcid_kw,
        (void *)&cmd_tests_set_app_raw_client_T_tcid,
        (void *)&cmd_tests_set_app_raw_client_T_data_req_kw,
        (void *)&cmd_tests_set_app_raw_client_T_data_req_plen,
        (void *)&cmd_tests_set_app_raw_client_T_data_resp_kw,
        (void *)&cmd_tests_set_app_raw_client_T_data_resp_plen,
        NULL,
    },
};


/****************************************************************************
 * - "set tests server raw port <port> test-case-id <tcid>
 *      data-req-plen <len> data-resp-plen <len>"
 ****************************************************************************/
 struct cmd_tests_set_app_raw_server_result {
    cmdline_fixed_string_t set;
    cmdline_fixed_string_t tests;
    cmdline_fixed_string_t server;
    cmdline_fixed_string_t raw;
    cmdline_fixed_string_t port_kw;
    uint32_t               port;
    cmdline_fixed_string_t tcid_kw;
    uint32_t               tcid;

    cmdline_fixed_string_t data_req_kw;
    uint32_t               data_req_plen;

    cmdline_fixed_string_t data_resp_kw;
    uint32_t               data_resp_plen;
};

static cmdline_parse_token_string_t cmd_tests_set_app_raw_server_T_set =
    TOKEN_STRING_INITIALIZER(struct cmd_tests_set_app_raw_server_result, set, "set");
static cmdline_parse_token_string_t cmd_tests_set_app_raw_server_T_tests =
    TOKEN_STRING_INITIALIZER(struct cmd_tests_set_app_raw_server_result, tests, "tests");
static cmdline_parse_token_string_t cmd_tests_set_app_raw_server_T_server =
    TOKEN_STRING_INITIALIZER(struct cmd_tests_set_app_raw_server_result, server, "server");
static cmdline_parse_token_string_t cmd_tests_set_app_raw_server_T_raw =
    TOKEN_STRING_INITIALIZER(struct cmd_tests_set_app_raw_server_result, raw, "raw");

static cmdline_parse_token_string_t cmd_tests_set_app_raw_server_T_port_kw =
    TOKEN_STRING_INITIALIZER(struct cmd_tests_set_app_raw_server_result, port_kw, "port");
static cmdline_parse_token_num_t cmd_tests_set_app_raw_server_T_port =
    TOKEN_NUM_INITIALIZER(struct cmd_tests_set_app_raw_server_result, port, UINT32);

static cmdline_parse_token_string_t cmd_tests_set_app_raw_server_T_tcid_kw =
    TOKEN_STRING_INITIALIZER(struct cmd_tests_set_app_raw_server_result, tcid_kw, "test-case-id");
static cmdline_parse_token_num_t cmd_tests_set_app_raw_server_T_tcid =
    TOKEN_NUM_INITIALIZER(struct cmd_tests_set_app_raw_server_result, tcid, UINT32);

static cmdline_parse_token_string_t cmd_tests_set_app_raw_server_T_data_req_kw =
    TOKEN_STRING_INITIALIZER(struct cmd_tests_set_app_raw_server_result, data_req_kw, "data-req-plen");
static cmdline_parse_token_num_t cmd_tests_set_app_raw_server_T_data_req_plen =
    TOKEN_NUM_INITIALIZER(struct cmd_tests_set_app_raw_server_result, data_req_plen, UINT32);

static cmdline_parse_token_string_t cmd_tests_set_app_raw_server_T_data_resp_kw =
    TOKEN_STRING_INITIALIZER(struct cmd_tests_set_app_raw_server_result, data_resp_kw, "data-resp-plen");
static cmdline_parse_token_num_t cmd_tests_set_app_raw_server_T_data_resp_plen =
    TOKEN_NUM_INITIALIZER(struct cmd_tests_set_app_raw_server_result, data_resp_plen, UINT32);

static void cmd_tests_set_app_raw_server_parsed(void *parsed_result,
                                                struct cmdline *cl,
                                                void *data __rte_unused)
{
    printer_arg_t                               parg;
    struct cmd_tests_set_app_raw_server_result *pr;
    tpg_app_server_t                            app_server_cfg;
    int                                         err;

    parg = TPG_PRINTER_ARG(cli_printer, cl);
    pr = parsed_result;

    app_server_cfg.as_app_proto = APP_PROTO__RAW;
    app_server_cfg.as_raw.rs_req_plen = pr->data_req_plen;
    app_server_cfg.as_raw.rs_resp_plen = pr->data_resp_plen;

    err = test_mgmt_update_test_case_app_server(pr->port, pr->tcid,
                                                &app_server_cfg,
                                                &parg);
    if (err == 0) {
        cmdline_printf(cl, "Port %"PRIu32", Test Case %"PRIu32" updated!\n",
                       pr->port,
                       pr->tcid);
    } else {
        cmdline_printf(cl,
                       "ERROR: Failed updating test case %"PRIu32
                       " config on port %"PRIu32"\n",
                       pr->tcid,
                       pr->port);
    }
}

cmdline_parse_inst_t cmd_tests_set_app_raw_server = {
    .f = cmd_tests_set_app_raw_server_parsed,
    .data = NULL,
    .help_str = "set tests server raw port <port> test-case-id <tcid>"
                "data-req-plen <len> data-resp-plen <len>",
    .tokens = {
        (void *)&cmd_tests_set_app_raw_server_T_set,
        (void *)&cmd_tests_set_app_raw_server_T_tests,
        (void *)&cmd_tests_set_app_raw_server_T_server,
        (void *)&cmd_tests_set_app_raw_server_T_raw,
        (void *)&cmd_tests_set_app_raw_server_T_port_kw,
        (void *)&cmd_tests_set_app_raw_server_T_port,
        (void *)&cmd_tests_set_app_raw_server_T_tcid_kw,
        (void *)&cmd_tests_set_app_raw_server_T_tcid,
        (void *)&cmd_tests_set_app_raw_server_T_data_req_kw,
        (void *)&cmd_tests_set_app_raw_server_T_data_req_plen,
        (void *)&cmd_tests_set_app_raw_server_T_data_resp_kw,
        (void *)&cmd_tests_set_app_raw_server_T_data_resp_plen,
        NULL,
    },
};

/*****************************************************************************
 * Main menu context
 ****************************************************************************/
static cmdline_parse_ctx_t cli_ctx[] = {
    &cmd_tests_set_app_raw_client,
    &cmd_tests_set_app_raw_server,
    NULL,
};

