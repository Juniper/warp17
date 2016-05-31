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
 *     tpg_test_http_1_1_app.h
 *
 * Description:
 *     HTTP 1.1 application state storage.
 *
 * Author:
 *     Dumitru Ceara, Eelco Chaudron
 *
 * Initial Created:
 *     02/22/2016
 *
 * Notes:
 *
 */

/*****************************************************************************
 * Multiple include protection
 ****************************************************************************/
#ifndef _H_TPG_TEST_HTTP_1_1_APP_
#define _H_TPG_TEST_HTTP_1_1_APP_

/*****************************************************************************
 * HTTP state machine.
 * See tpg_test_http_1_1_sm.dot for the diagram. (xdot dot/tpg_test_http_1_1_sm.dot)
 ****************************************************************************/
typedef enum {

    HTTPS_CL_SEND_REQ,
    HTTPS_CL_PARSE_RESP_HDR,
    HTTPS_CL_RECV_RESP_BODY,

    HTTPS_SRV_PARSE_REQ_HDR,
    HTTPS_SRV_RECV_REQ_BODY,
    HTTPS_SRV_SEND_RESP,

    HTTPS_CLOSED,

    HTTPS_MAX_STATE

} http_state_t;

/*****************************************************************************
 * HTTP application client/server definitions
 ****************************************************************************/
typedef struct http_app_s {

    union {
        app_recv_ptr_t ha_recv;
        app_send_ptr_t ha_send;
    };

    uint32_t           ha_content_length;
    http_state_t       ha_state;

} http_app_t;

/*****************************************************************************
 * HTTP global stats
 ****************************************************************************/
typedef struct http_statistics_s {

    uint32_t hts_req_err;
    uint32_t hts_resp_err;

} http_statistics_t;

/*****************************************************************************
 * HTTP externals.
 ****************************************************************************/
extern void http_client_default_cfg(tpg_test_case_t *cfg);
extern void http_server_default_cfg(tpg_test_case_t *cfg);

extern bool http_client_validate_cfg(const tpg_test_case_t *cfg,
                                     printer_arg_t *printer_arg);
extern bool http_server_validate_cfg(const tpg_test_case_t *cfg,
                                     printer_arg_t *printer_arg);

extern void http_client_print_cfg(const tpg_test_case_t *cfg,
                                  printer_arg_t *printer_arg);
extern void http_server_print_cfg(const tpg_test_case_t *cfg,
                                  printer_arg_t *printer_arg);

extern void http_client_delete_cfg(const tpg_test_case_t *cfg);
extern void http_server_delete_cfg(const tpg_test_case_t *cfg);

extern void http_client_server_init(app_data_t *app_data,
                                    test_case_init_msg_t *init_msg);

extern void http_client_tc_start(test_case_init_msg_t *init_msg);
extern void http_server_tc_start(test_case_init_msg_t *init_msg);

extern void http_client_tc_stop(test_case_init_msg_t *init_msg);
extern void http_server_tc_stop(test_case_init_msg_t *init_msg);

extern void http_client_conn_up(l4_control_block_t *l4, app_data_t *app_data,
                                tpg_test_case_app_stats_t *stats);
extern void http_server_conn_up(l4_control_block_t *l4, app_data_t *app_data,
                                tpg_test_case_app_stats_t *stats);

extern void http_client_server_conn_down(l4_control_block_t *l4,
                                         app_data_t *app_data,
                                         tpg_test_case_app_stats_t *stats);

extern uint32_t http_client_deliver_data(l4_control_block_t *l4,
                                         app_data_t *app_data,
                                         tpg_test_case_app_stats_t *stats,
                                         struct rte_mbuf *rx_data);
extern uint32_t http_server_deliver_data(l4_control_block_t *l4,
                                         app_data_t *app_data,
                                         tpg_test_case_app_stats_t *stats,
                                         struct rte_mbuf *rx_data);

extern struct rte_mbuf *http_client_send_data(l4_control_block_t *l4,
                                              app_data_t *app_data,
                                              tpg_test_case_app_stats_t *stats,
                                              uint32_t max_tx_size);
extern struct rte_mbuf *http_server_send_data(l4_control_block_t *l4,
                                              app_data_t *app_data,
                                              tpg_test_case_app_stats_t *stats,
                                              uint32_t max_tx_size);

extern void http_client_data_sent(l4_control_block_t *l4, app_data_t *app_data,
                                  tpg_test_case_app_stats_t *stats,
                                  uint32_t bytes_sent);
extern void http_server_data_sent(l4_control_block_t *l4, app_data_t *app_data,
                                  tpg_test_case_app_stats_t *stats,
                                  uint32_t bytes_sent);

extern void http_stats_add(tpg_test_case_app_stats_t *total,
                           const tpg_test_case_app_stats_t *elem);
extern void http_stats_print(const tpg_test_case_app_stats_t *stats,
                             printer_arg_t *printer_arg);

extern bool http_init(void);
extern void http_lcore_init(uint32_t lcore_id);

#endif /* _H_TPG_TEST_HTTP_1_1_APP_ */

