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
 *     tpg_test_imix_app.h
 *
 * Description:
 *     Imix application state storage.
 *
 * Author:
 *     Dumitru Ceara
 *
 * Initial Created:
 *     01/18/2018
 *
 * Notes:
 *
 */

/*****************************************************************************
 * Multiple include protection
 ****************************************************************************/
#ifndef _H_TPG_TEST_IMIX_APP_
#define _H_TPG_TEST_IMIX_APP_

/*
 * Storage for imix group entries.
 */
typedef struct {
    tpg_imix_group_t tig_group;

    /* Bit flags. */
    uint32_t tig_configured : 1;
    uint32_t tig_referenced : 1; /* True if there's a test case
                                  * using this IMIX group.
                                  */

    /* Test case information in case tig_referenced == true. */
    uint32_t tig_owner_eth_port;
    uint32_t tig_owner_tc_id;
} test_imix_group_t;

/*****************************************************************************
 * "IMIX" externals.
 ****************************************************************************/

extern void imix_default_cfg(tpg_test_case_t *cfg);

extern bool imix_validate_cfg(const tpg_test_case_t *cfg,
                              const tpg_app_t *app_cfg,
                              printer_arg_t *printer_arg);

extern void imix_print_cfg(const tpg_app_t *app_cfg,
                           printer_arg_t *printer_arg);

extern void imix_add_cfg(const tpg_test_case_t *cfg,
                         const tpg_app_t *app_cfg);
extern void imix_delete_cfg(const tpg_test_case_t *cfg,
                            const tpg_app_t *app_cfg);

extern uint32_t imix_pkts_per_send(const tpg_test_case_t *cfg,
                                   const tpg_app_t *app_cfg,
                                   uint32_t max_pkt_size);

extern void imix_init_session(app_data_t *app_data, const tpg_app_t *app_cfg);

extern void imix_tc_start(const tpg_test_case_t *cfg, const tpg_app_t *app_cfg,
                          app_storage_t *app_storage);
extern void imix_tc_stop(const tpg_test_case_t *cfg, const tpg_app_t *app_cfg,
                         app_storage_t *app_storage);

extern void imix_conn_up(l4_control_block_t *l4, app_data_t *app_data,
                         tpg_app_stats_t *stats);

extern void imix_conn_down(l4_control_block_t *l4, app_data_t *app_data,
                           tpg_app_stats_t *stats);

extern uint32_t imix_deliver_data(l4_control_block_t *l4,
                                  app_data_t *app_data,
                                  tpg_app_stats_t *stats,
                                  struct rte_mbuf *rx_data,
                                  uint64_t rx_tstamp);

extern struct rte_mbuf *imix_send_data(l4_control_block_t *l4,
                                       app_data_t *app_data,
                                       tpg_app_stats_t *stats,
                                       uint32_t max_tx_size);

extern bool imix_data_sent(l4_control_block_t *l4, app_data_t *app_data,
                           tpg_app_stats_t *stats,
                           uint32_t bytes_sent);

extern void imix_stats_init_global(const tpg_app_t *app_cfg,
                                   tpg_app_stats_t *stats);
extern void imix_stats_init(const tpg_app_t *app_cfg, tpg_app_stats_t *stats);
extern void imix_stats_init_req(const tpg_app_t *app_cfg,
                                tpg_app_stats_t *stats);
extern void imix_stats_copy(tpg_app_stats_t *dest, const tpg_app_stats_t *src);
extern void imix_stats_add(tpg_app_stats_t *total,
                           const tpg_app_stats_t *elem);
extern void imix_stats_print(const tpg_app_stats_t *stats,
                             printer_arg_t *printer_arg);

extern bool imix_init(void);
extern void imix_lcore_init(uint32_t lcore_id);

extern test_imix_group_t    *test_imix_get_env(uint32_t imix_id);
extern tpg_imix_app_stats_t *test_imix_get_stats(uint32_t imix_id);

extern int imix_cli_set_app_cfg(uint32_t app_idx, const tpg_app_t *app_cfg,
                                printer_arg_t *printer_arg);
extern int imix_cli_get_app_cfg(uint32_t app_idx, tpg_app_t *app_cfg,
                                printer_arg_t *printer_arg);
extern int imix_cli_set_app_weight(uint32_t app_idx, uint32_t weight,
                                   printer_arg_t *printer_arg);
extern int imix_cli_delete_app_cfg(uint32_t app_idx,
                                   printer_arg_t *printer_arg);

#endif /* _H_TPG_TEST_IMIX_APP_ */

