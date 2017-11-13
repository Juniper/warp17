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
 *     tpg_test_app.h
 *
 * Description:
 *     Application state storage.
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
#ifndef _H_TPG_TEST_APP_
#define _H_TPG_TEST_APP_

/*****************************************************************************
 * Application data
 ****************************************************************************/
typedef struct app_data_s {

    tpg_app_proto_t   ad_type;

    union {
        raw_app_t     ad_raw;
        http_app_t    ad_http;
        generic_app_t ad_generic;
    };

} app_data_t;

/*****************************************************************************
 * API to be used by the lower level stack implementations (TCP/UDP) for:
 * - default config initialization
 * - test-case config validation
 * - test-case config printing
 * - test-case deletion
 * - test-case start/stop
 * - application data initialization
 * - connection UP/DOWN notifications
 * - RX data available notification
 * - TX data possible notification
 * - TX data confirmation
 * - stats aggregation/printing
 ****************************************************************************/
typedef void (*app_default_cfg_cb_t)(tpg_test_case_t *cfg);

typedef bool (*app_validate_cfg_cb_t)(const tpg_test_case_t *cfg,
                                      printer_arg_t *printer_arg);

typedef void (*app_print_cfg_cb_t)(const tpg_test_case_t *te,
                                   printer_arg_t *printer_arg);

typedef void (*app_delete_cfg_cb_t)(const tpg_test_case_t *cfg);

typedef void (*app_init_cb_t)(app_data_t *app_data,
                              test_case_init_msg_t *init_msg);

typedef void (*app_tc_start_stop_cb_t)(test_case_init_msg_t *init_msg);

typedef void (*app_conn_up_cb_t)(l4_control_block_t *l4,
                                 app_data_t *app_data,
                                 tpg_test_case_app_stats_t *stats);

typedef void (*app_conn_down_cb_t)(l4_control_block_t *l4,
                                   app_data_t *app_data,
                                   tpg_test_case_app_stats_t *stats);

typedef uint32_t (*app_deliver_cb_t)(l4_control_block_t *l4,
                                     app_data_t *app_data,
                                     tpg_test_case_app_stats_t *stats,
                                     struct rte_mbuf *data,
                                     uint64_t rx_tstamp);

/* WARNING: the application must guarantee that if any of the segments in the
 * returned mbuf are marked as "static" (through the DATA_SET_STATIC call) then
 * the buf_addr in the segment points to static data that can never be freed.
 * E.g.: fixed/constant headers.
 */
typedef struct rte_mbuf *(*app_send_cb_t)(l4_control_block_t *l4,
                                          app_data_t *app_data,
                                          tpg_test_case_app_stats_t *stats,
                                          uint32_t max_tx_size);

typedef void (*app_data_sent_cb_t)(l4_control_block_t *l4, app_data_t *app_data,
                                   tpg_test_case_app_stats_t *stats,
                                   uint32_t bytes_sent);

typedef void (*app_stats_add_cb_t)(tpg_test_case_app_stats_t *total,
                                   const tpg_test_case_app_stats_t *elem);

typedef void (*app_stats_print_cb_t)(const tpg_test_case_app_stats_t *stats,
                                     printer_arg_t *printer);

/*****************************************************************************
 * Helpers for initializing and using the callback arrays.
 ****************************************************************************/
#define DECLARE_APP_CB_ARRAY(type, name) \
    __typeof__(type) name[TEST_CASE_TYPE__MAX][APP_PROTO__APP_PROTO_MAX]

extern DECLARE_APP_CB_ARRAY(app_default_cfg_cb_t, app_default_cfg_handlers);
extern DECLARE_APP_CB_ARRAY(app_validate_cfg_cb_t, app_validate_cfg_handlers);
extern DECLARE_APP_CB_ARRAY(app_print_cfg_cb_t, app_print_cfg_handlers);
extern DECLARE_APP_CB_ARRAY(app_delete_cfg_cb_t, app_delete_cfg_handlers);
extern DECLARE_APP_CB_ARRAY(app_tc_start_stop_cb_t, app_tc_start_handlers);
extern DECLARE_APP_CB_ARRAY(app_tc_start_stop_cb_t, app_tc_stop_handlers);
extern DECLARE_APP_CB_ARRAY(app_init_cb_t, app_init_handlers);
extern DECLARE_APP_CB_ARRAY(app_conn_up_cb_t, app_conn_up_handlers);
extern DECLARE_APP_CB_ARRAY(app_conn_down_cb_t, app_conn_down_handlers);
extern DECLARE_APP_CB_ARRAY(app_deliver_cb_t, app_deliver_handlers);
extern DECLARE_APP_CB_ARRAY(app_send_cb_t, app_send_handlers);
extern DECLARE_APP_CB_ARRAY(app_data_sent_cb_t, app_data_sent_handlers);
extern DECLARE_APP_CB_ARRAY(app_stats_add_cb_t, app_stats_add_handlers);
extern DECLARE_APP_CB_ARRAY(app_stats_print_cb_t, app_stats_print_handlers);

#define DEFINE_APP_CLIENT_CB(app, cb) \
    [TEST_CASE_TYPE__CLIENT][app] = cb

#define DEFINE_APP_SERVER_CB(app, cb) \
    [TEST_CASE_TYPE__SERVER][app] = cb

#define DEFINE_APP_CB(app, client_cb, server_cb) \
    DEFINE_APP_CLIENT_CB(app, client_cb),        \
    DEFINE_APP_SERVER_CB(app, server_cb)

#define APP_CALL(name, type, app_id) \
    app_ ## name ## _handlers[type][app_id]

#define APP_CL_CALL(name, app_id) \
    APP_CALL(name, TEST_CASE_TYPE__CLIENT, app_id)

#define APP_SRV_CALL(name, app_id) \
    APP_CALL(name, TEST_CASE_TYPE__SERVER, app_id)

#endif /* _H_TPG_TEST_APP_ */

