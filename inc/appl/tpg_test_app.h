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
 * Application storage (to be shared by all sessions on a test case)
 * WARNING: careful when sharing non-pointers. The data should be immutable
 * then as applications will store a copy of it!
 ****************************************************************************/
typedef union app_storage_u {

    raw_storage_t     ast_raw;
    http_storage_t    ast_http;
    generic_storage_t ast_generic;

} app_storage_t;

/*****************************************************************************
 * Application data
 ****************************************************************************/
typedef struct app_data_s {

    tpg_app_proto_t ad_type;

    /* In case this is an IMIX session we need to store the imix app index and
     * imix group id here. We can't use the union because the "real" app is not
     * imix.
     */
    uint16_t ad_imix_index;
    uint16_t ad_imix_id;

    /* Shared test cases application storage. */
    app_storage_t ad_storage;

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
 * - retrieving expected number of pkts sent per transaction
 * - application data initialization
 * - connection UP/DOWN notifications
 * - RX data available notification
 * - TX data possible notification
 * - TX data confirmation
 * - stats init/aggregation/printing
 ****************************************************************************/
typedef void (*app_default_cfg_cb_t)(tpg_test_case_t *cfg);

typedef bool (*app_validate_cfg_cb_t)(const tpg_test_case_t *cfg,
                                      const tpg_app_t *app_cfg,
                                      printer_arg_t *printer_arg);

typedef void (*app_print_cfg_cb_t)(const tpg_app_t *app_cfg,
                                   printer_arg_t *printer_arg);

typedef void (*app_add_delete_cfg_cb_t)(const tpg_test_case_t *cfg,
                                        const tpg_app_t *app_cfg);

typedef uint32_t (*app_pkts_per_send_cb_t)(const tpg_test_case_t *cfg,
                                           const tpg_app_t *app_cfg,
                                           uint32_t max_pkt_size);

typedef void (*app_init_cb_t)(app_data_t *app_data,
                              const tpg_app_t *app_cfg);

typedef void (*app_tc_start_stop_cb_t)(const tpg_test_case_t *cfg,
                                       const tpg_app_t *app_cfg,
                                       app_storage_t *app_storage);

typedef void (*app_conn_up_cb_t)(l4_control_block_t *l4,
                                 app_data_t *app_data,
                                 tpg_app_stats_t *stats);

typedef void (*app_conn_down_cb_t)(l4_control_block_t *l4,
                                   app_data_t *app_data,
                                   tpg_app_stats_t *stats);

typedef uint32_t (*app_deliver_cb_t)(l4_control_block_t *l4,
                                     app_data_t *app_data,
                                     tpg_app_stats_t *stats,
                                     struct rte_mbuf *data,
                                     uint64_t rx_tstamp);

/* WARNING: the application must guarantee that if any of the segments in the
 * returned mbuf are marked as "static" (through the DATA_SET_STATIC call) then
 * the buf_addr in the segment points to static data that can never be freed.
 * E.g.: fixed/constant headers.
 */
typedef struct rte_mbuf *(*app_send_cb_t)(l4_control_block_t *l4,
                                          app_data_t *app_data,
                                          tpg_app_stats_t *stats,
                                          uint32_t max_tx_size);

/* Returns true if the application sent a complete message, false otherwise. */
typedef bool (*app_data_sent_cb_t)(l4_control_block_t *l4, app_data_t *app_data,
                                   tpg_app_stats_t *stats,
                                   uint32_t bytes_sent);

typedef void (*app_stats_init_global_cb_t)(const tpg_app_t *app_cfg,
                                           tpg_app_stats_t *stats);

typedef void (*app_stats_init_cb_t)(const tpg_app_t *app_cfg,
                                    tpg_app_stats_t *stats);

typedef void (*app_stats_init_req_cb_t)(const tpg_app_t *app_cfg,
                                        tpg_app_stats_t *stats);

typedef void (*app_stats_copy_cb_t)(tpg_app_stats_t *dest,
                                    const tpg_app_stats_t *src);

typedef void (*app_stats_add_cb_t)(tpg_app_stats_t *total,
                                   const tpg_app_stats_t *elem);

typedef void (*app_stats_print_cb_t)(const tpg_app_stats_t *stats,
                                     printer_arg_t *printer);

/*****************************************************************************
 * Helpers for initializing and using the callback arrays.
 ****************************************************************************/
#define DECLARE_APP_CB_ARRAY(type, name) \
    __typeof__(type) name[APP_PROTO__APP_PROTO_MAX]

extern DECLARE_APP_CB_ARRAY(app_default_cfg_cb_t, app_default_cfg_handlers);
extern DECLARE_APP_CB_ARRAY(app_validate_cfg_cb_t, app_validate_cfg_handlers);
extern DECLARE_APP_CB_ARRAY(app_print_cfg_cb_t, app_print_cfg_handlers);
extern DECLARE_APP_CB_ARRAY(app_add_delete_cfg_cb_t, app_add_cfg_handlers);
extern DECLARE_APP_CB_ARRAY(app_add_delete_cfg_cb_t, app_delete_cfg_handlers);
extern DECLARE_APP_CB_ARRAY(app_pkts_per_send_cb_t, app_pkts_per_send_handlers);
extern DECLARE_APP_CB_ARRAY(app_tc_start_stop_cb_t, app_tc_start_handlers);
extern DECLARE_APP_CB_ARRAY(app_tc_start_stop_cb_t, app_tc_stop_handlers);
extern DECLARE_APP_CB_ARRAY(app_init_cb_t, app_init_handlers);
extern DECLARE_APP_CB_ARRAY(app_conn_up_cb_t, app_conn_up_handlers);
extern DECLARE_APP_CB_ARRAY(app_conn_down_cb_t, app_conn_down_handlers);
extern DECLARE_APP_CB_ARRAY(app_deliver_cb_t, app_deliver_handlers);
extern DECLARE_APP_CB_ARRAY(app_send_cb_t, app_send_handlers);
extern DECLARE_APP_CB_ARRAY(app_data_sent_cb_t, app_data_sent_handlers);
extern DECLARE_APP_CB_ARRAY(app_stats_init_global_cb_t,
                            app_stats_init_global_handlers);
extern DECLARE_APP_CB_ARRAY(app_stats_init_cb_t, app_stats_init_handlers);
extern DECLARE_APP_CB_ARRAY(app_stats_init_req_cb_t,
                            app_stats_init_req_handlers);
extern DECLARE_APP_CB_ARRAY(app_stats_copy_cb_t, app_stats_copy_handlers);
extern DECLARE_APP_CB_ARRAY(app_stats_add_cb_t, app_stats_add_handlers);
extern DECLARE_APP_CB_ARRAY(app_stats_print_cb_t, app_stats_print_handlers);

#define DEFINE_APP_CB(app, cb) \
    [app] = cb

#define APP_CALL(name, app_id) \
    app_ ## name ## _handlers[app_id]

#endif /* _H_TPG_TEST_APP_ */

