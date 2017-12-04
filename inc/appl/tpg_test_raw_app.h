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
 *     tpg_test_raw_app.h
 *
 * Description:
 *     RAW application state storage.
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
#ifndef _H_TPG_TEST_RAW_APP_
#define _H_TPG_TEST_RAW_APP_

/*****************************************************************************
 * "RAW" state machine
 * See tpg_test_raw_sm.dot for the diagram. (xdot dot/tpg_test_raw_sm.dot)
 ****************************************************************************/
typedef enum {

    RAWS_SENDING,
    RAWS_RECEIVING,

    RAWS_MAX_STATE

} raw_state_t;

/* Latency payload to be embedded in RAW packets. */
typedef struct raw_latency_data_s {

    uint64_t rld_magic;
    uint32_t rld_tstamp[2];

} __attribute__((__packed__)) raw_latency_data_t;

/*****************************************************************************
 * "RAW" application (random fixed size request response)
 ****************************************************************************/
typedef struct raw_app_s {

    uint16_t    ra_req_size;
    uint16_t    ra_resp_size;

    uint16_t    ra_remaining_count;
    uint8_t     ra_rx_tstamp_size; /* 0 if no tx timestamping enabled. */
    uint8_t     ra_tx_tstamp_size; /* 0 if no rx timestamping enabled. */

    raw_latency_data_t ra_tx_tstamp; /* Storage for the outgoing timestamp. */

    raw_state_t ra_state;

} raw_app_t;

/*****************************************************************************
 * "RAW" externals.
 ****************************************************************************/
extern void raw_client_default_cfg(tpg_test_case_t *cfg);
extern void raw_server_default_cfg(tpg_test_case_t *cfg);

extern bool raw_client_validate_cfg(const tpg_test_case_t *cfg,
                                    printer_arg_t *printer_arg);
extern bool raw_server_validate_cfg(const tpg_test_case_t *cfg,
                                    printer_arg_t *printer_arg);

extern void raw_client_print_cfg(const tpg_test_case_t *cfg,
                                 printer_arg_t *printer_arg);
extern void raw_server_print_cfg(const tpg_test_case_t *cfg,
                                 printer_arg_t *printer_arg);

extern void raw_delete_cfg(const tpg_test_case_t *cfg);

extern void raw_client_init(app_data_t *app_data, test_case_init_msg_t *init_msg);
extern void raw_server_init(app_data_t *app_data, test_case_init_msg_t *init_msg);

extern void raw_client_tc_start(test_case_init_msg_t *init_msg);
extern void raw_server_tc_start(test_case_init_msg_t *init_msg);
extern void raw_tc_stop(test_case_init_msg_t *init_msg);

extern void raw_client_conn_up(l4_control_block_t *l4, app_data_t *app_data,
                               tpg_test_case_app_stats_t *stats);
extern void raw_server_conn_up(l4_control_block_t *l4, app_data_t *app_data,
                               tpg_test_case_app_stats_t *stats);

extern void raw_conn_down(l4_control_block_t *l4, app_data_t *app_data,
                          tpg_test_case_app_stats_t *stats);

extern uint32_t raw_client_deliver_data(l4_control_block_t *l4,
                                        app_data_t *app_data,
                                        tpg_test_case_app_stats_t *stats,
                                        struct rte_mbuf *rx_data,
                                        uint64_t rx_tstamp);
extern uint32_t raw_server_deliver_data(l4_control_block_t *l4,
                                        app_data_t *app_data,
                                        tpg_test_case_app_stats_t *stats,
                                        struct rte_mbuf *rx_data,
                                        uint64_t rx_tstamp);

extern struct rte_mbuf *raw_send_data(l4_control_block_t *l4,
                                      app_data_t *app_data,
                                      tpg_test_case_app_stats_t *stats,
                                      uint32_t max_tx_size);

extern bool raw_client_data_sent(l4_control_block_t *l4, app_data_t *app_data,
                                 tpg_test_case_app_stats_t *stats,
                                 uint32_t bytes_sent);
extern bool raw_server_data_sent(l4_control_block_t *l4, app_data_t *app_data,
                                 tpg_test_case_app_stats_t *stats,
                                 uint32_t bytes_sent);

extern void raw_stats_add(tpg_test_case_app_stats_t *total,
                          const tpg_test_case_app_stats_t *elem);
extern void raw_stats_print(const tpg_test_case_app_stats_t *stats,
                            printer_arg_t *printer_arg);

extern bool raw_init(void);
extern void raw_lcore_init(uint32_t lcore_id);

#endif /* _H_TPG_TEST_RAW_APP_ */

