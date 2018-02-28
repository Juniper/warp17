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
 *     tpg_test_imix_app.c
 *
 * Description:
 *     IMIX application implementation.
 *
 * Author:
 *     Dumitru Ceara
 *
 * Initial Created:
 *     01/16/2018
 *
 * Notes:
 *
 */

/*****************************************************************************
 * Include files
 ****************************************************************************/
#include "tcp_generator.h"

/* Imix application "indirection" table built based on configured application
 * weights. Whenever we need to start a new application we just take the current
 * app index from the iat_app_indices table and advance it's position in a
 * circular way.
 */
typedef struct imix_app_table_s {

    uint16_t iat_app_indices[TPG_IMIX_MAX_TOTAL_APP_WEIGHT];

    uint32_t iat_count;
    uint32_t iat_last;

} imix_app_table_t;

/*****************************************************************************
 * Globals
 ****************************************************************************/

/* Array of imix_id; will store the global imix config */
static test_imix_group_t *imix_groups;

/* Array of imix_id; will store a per-lcore copy of imix config */
static RTE_DEFINE_PER_LCORE(tpg_imix_group_t *, imix_local_cfg);

#define IMIX_CFG_LOCAL_GET(imix_id) \
    (RTE_PER_LCORE(imix_local_cfg) + (imix_id))


/* Array of imix_id; will store a per-lcore storage table for each imix group */
static RTE_DEFINE_PER_LCORE(app_storage_t *, imix_storage_table);

#define IMIX_STORAGE_LOCAL_GET(imix_id) \
    (RTE_PER_LCORE(imix_storage_table) + imix_id * TPG_IMIX_MAX_APPS)

/* Array of imix_id; will store a per-lcore app table for each imix group */
static RTE_DEFINE_PER_LCORE(imix_app_table_t *, imix_app_table);

#define IMIX_APP_TABLE_LOCAL_GET(imix_id) \
    (RTE_PER_LCORE(imix_app_table) + imix_id)

/* Array of imix_id; will store the global imix stats */
static tpg_imix_app_stats_t *imix_stats;

#define IMIX_STATS_GET(imix_id) \
    (imix_stats + (imix_id))

/* Array of imix_id; will store a per-lcore copy of imix stats */
static RTE_DEFINE_PER_LCORE(tpg_imix_app_stats_t *, imix_local_stats);

#define IMIX_STATS_LOCAL_GET(imix_id) \
    (RTE_PER_LCORE(imix_local_stats) + (imix_id))

/* Max number of IMIX groups we track (for config and stats). We keep the last
 * one as temporary storage when stats need to be aggregated.
 */
#define IMIX_MAX_ENTRIES (TPG_IMIX_MAX_GROUPS + 1)

/* We forcefully use the last entry for temporary stats (at most one at a
 * single time). E.g., stats aggregation on the MGMT core.
 */
#define IMIX_GETS_STATS_TMP_GET_IMIX_ID() (IMIX_MAX_ENTRIES - 1)

/*****************************************************************************
 * Forward references.
 ****************************************************************************/
static cmdline_parse_ctx_t cli_ctx[];

static bool imix_cli_init(void);


/*****************************************************************************
 * imix_default_cfg()
 ****************************************************************************/
void imix_default_cfg(tpg_test_case_t *cfg __rte_unused)
{
}

/*****************************************************************************
 * imix_validate_cfg()
 ****************************************************************************/
bool imix_validate_cfg(const tpg_test_case_t *cfg, const tpg_app_t *app_cfg,
                       printer_arg_t *printer_arg)
{
    uint32_t           imix_id;
    test_imix_group_t *imix_group;
    tpg_imix_group_t  *imix_cfg;
    uint32_t           app_idx;

    imix_id = app_cfg->app_imix.imix_id;

    if (imix_id >= TPG_IMIX_MAX_GROUPS) {
        tpg_printf(printer_arg, "ERROR: Invalid IMIX id! Max allowed %u\n",
                   TPG_IMIX_MAX_GROUPS);
        return false;
    }

    imix_group = test_imix_get_env(imix_id);

    if (!imix_group->tig_configured) {
        tpg_printf(printer_arg, "ERROR: IMIX GROUP %u not configured!\n",
                   imix_id);
        return false;
    }

    if (imix_group->tig_referenced &&
            (imix_group->tig_owner_eth_port != cfg->tc_eth_port ||
             imix_group->tig_owner_tc_id != cfg->tc_id)) {
        tpg_printf(printer_arg, "ERROR: IMIX GROUP %u already used by a test case!\n",
                   imix_id);
        return false;
    }

    imix_cfg = &imix_group->tig_group;

    for (app_idx = 0; app_idx < imix_cfg->imix_apps_count; app_idx++) {
        tpg_app_t *app = &imix_cfg->imix_apps[app_idx].ia_app;

        if (!APP_CALL(validate_cfg, app->app_proto)(cfg, app, printer_arg))
            return false;
    }

    return true;
}

/*****************************************************************************
 * imix_print_cfg()
 ****************************************************************************/
void imix_print_cfg(const tpg_app_t *app_cfg, printer_arg_t *printer_arg)
{
    tpg_imix_group_t *imix_cfg;
    uint32_t          app_idx;

    imix_cfg = &test_imix_get_env(app_cfg->app_imix.imix_id)->tig_group;

    tpg_printf(printer_arg, "IMIX GROUP %"PRIu32":\n",
               app_cfg->app_imix.imix_id);

    for (app_idx = 0; app_idx < imix_cfg->imix_apps_count; app_idx++) {
        tpg_app_t *app = &imix_cfg->imix_apps[app_idx].ia_app;
        uint32_t   weight = imix_cfg->imix_apps[app_idx].ia_weight;

        tpg_printf(printer_arg, "IMIX Index %"PRIu32" (weight %u):\n", app_idx,
                   weight);
        APP_CALL(print_cfg, app->app_proto)(app, printer_arg);
        tpg_printf(printer_arg, "\n");
    }
}

/*****************************************************************************
 * imix_add_cfg()
 ****************************************************************************/
void imix_add_cfg(const tpg_test_case_t *cfg, const tpg_app_t *app_cfg)
{
    test_imix_group_t *imix_group;
    tpg_imix_group_t  *imix_cfg;
    uint32_t           app_idx;

    imix_group = test_imix_get_env(app_cfg->app_imix.imix_id);
    imix_cfg = &imix_group->tig_group;

    for (app_idx = 0; app_idx < imix_cfg->imix_apps_count; app_idx++) {
        tpg_app_t *app = &imix_cfg->imix_apps[app_idx].ia_app;

        APP_CALL(add_cfg, app->app_proto)(cfg, app);
    }

    /* Mark the IMIX group as being "referenced". */
    imix_group->tig_referenced = true;
    imix_group->tig_owner_eth_port = cfg->tc_eth_port;
    imix_group->tig_owner_tc_id = cfg->tc_id;
}

/*****************************************************************************
 * imix_delete_cfg()
 ****************************************************************************/
void imix_delete_cfg(const tpg_test_case_t *cfg, const tpg_app_t *app_cfg)
{
    test_imix_group_t *imix_group;
    tpg_imix_group_t  *imix_cfg;
    uint32_t           app_idx;

    imix_group = test_imix_get_env(app_cfg->app_imix.imix_id);
    imix_cfg = &imix_group->tig_group;

    for (app_idx = 0; app_idx < imix_cfg->imix_apps_count; app_idx++) {
        tpg_app_t *app = &imix_cfg->imix_apps[app_idx].ia_app;

        APP_CALL(delete_cfg, app->app_proto)(cfg, app);
    }

    /* Reset "referenced" bit for imix group. */
    assert(imix_group->tig_referenced);
    imix_group->tig_referenced = false;
}

/*****************************************************************************
 * imix_pkts_per_send()
 ****************************************************************************/
uint32_t imix_pkts_per_send(const tpg_test_case_t *cfg,
                            const tpg_app_t *app_cfg,
                            uint32_t max_pkt_size)
{
    tpg_imix_group_t *imix_cfg;
    uint64_t          total_weight = 0;
    uint64_t          total_pkts_per_send = 0;
    uint32_t          app_idx;

    imix_cfg = &test_imix_get_env(app_cfg->app_imix.imix_id)->tig_group;

    for (app_idx = 0; app_idx < imix_cfg->imix_apps_count; app_idx++) {
        tpg_app_t *app = &imix_cfg->imix_apps[app_idx].ia_app;
        uint32_t   weight = imix_cfg->imix_apps[app_idx].ia_weight;

        total_weight += weight;
        total_pkts_per_send +=
            weight * APP_CALL(pkts_per_send, app->app_proto)(cfg, app,
                                                             max_pkt_size);
    }

    /* Return the weighted average (rounded up). */
    return (total_pkts_per_send + total_weight - 1) / total_weight;
}

/*****************************************************************************
 * imix_tc_start()
 ****************************************************************************/
void imix_tc_start(const tpg_test_case_t *cfg, const tpg_app_t *app_cfg,
                   app_storage_t *tc_app_storage __rte_unused)
{
    uint32_t          imix_id;
    tpg_imix_group_t *imix_cfg;
    app_storage_t    *app_storage;
    imix_app_table_t *app_table;
    uint32_t          app_idx;

    imix_id = app_cfg->app_imix.imix_id;
    imix_cfg = IMIX_CFG_LOCAL_GET(imix_id);
    app_storage = IMIX_STORAGE_LOCAL_GET(imix_id);
    app_table = IMIX_APP_TABLE_LOCAL_GET(imix_id);

    /* Store a copy of the config per lcore to avoid accessing non-local
     * memory.
     */
    *imix_cfg = test_imix_get_env(imix_id)->tig_group;

    /* Clear the app indirection table. */
    bzero(app_table, sizeof(*app_table));

    for (app_idx = 0; app_idx < imix_cfg->imix_apps_count; app_idx++) {
        tpg_app_t *app = &imix_cfg->imix_apps[app_idx].ia_app;
        uint32_t   weight = imix_cfg->imix_apps[app_idx].ia_weight;
        uint32_t   i;

        /* Build our app table based on the configured weight for apps in
         * this group.
         * WARNING: if the number of established sessions is small (comparable
         * with TPG_IMIX_MAX_TOTAL_APP_WEIGHT) then this distribution might not
         * work fine. We should probably compute the gcd of all weights and
         * normalize them...
         */
        for (i = 0; i < weight; i++) {
            assert(app_table->iat_count < TPG_IMIX_MAX_TOTAL_APP_WEIGHT);

            app_table->iat_app_indices[app_table->iat_count] = app_idx;
            app_table->iat_count++;
        }

        APP_CALL(tc_start, app->app_proto)(cfg, app, &app_storage[app_idx]);
    }
}

/*****************************************************************************
 * imix_tc_stop()
 ****************************************************************************/
void imix_tc_stop(const tpg_test_case_t *cfg, const tpg_app_t *app_cfg,
                  app_storage_t *tc_app_storage __rte_unused)
{
    uint32_t          imix_id;
    tpg_imix_group_t *imix_cfg;
    app_storage_t    *app_storage;
    uint32_t          app_idx;

    imix_id = app_cfg->app_imix.imix_id;
    imix_cfg = IMIX_CFG_LOCAL_GET(imix_id);
    app_storage = IMIX_STORAGE_LOCAL_GET(imix_id);

    for (app_idx = 0; app_idx < imix_cfg->imix_apps_count; app_idx++) {
        tpg_app_t *app = &imix_cfg->imix_apps[app_idx].ia_app;

        APP_CALL(tc_stop, app->app_proto)(cfg, app, &app_storage[app_idx]);
    }

    /* Cleanup the local imix config (just in case). */
    bzero(imix_cfg, sizeof(*imix_cfg));
}

/*****************************************************************************
 * imix_init_session()
 ****************************************************************************/
void imix_init_session(app_data_t *app_data, const tpg_app_t *app_cfg)
{
    /* For IMIX we delay a bit the call to INIT. The reason is that servers
     * only call app_init in LISTENING state. But we would also like to
     * distribute (according to the configured weights) the established server
     * sessions. To achieve that we delay the application initialization until
     * the underlying session gets established. This adds a bit of overhead
     * as we will reinitializa the application multiple times for clients but
     * the impact shouldn't be too high.
     */

    /* For now just make sure that the imix_id is properly stored in the
     * application data. Everything else we do in imix_conn_up.
     */
    app_data->ad_imix_id = app_cfg->app_imix.imix_id;
}

/*****************************************************************************
 * imix_conn_up()
 ****************************************************************************/
void imix_conn_up(l4_control_block_t *l4, app_data_t *app_data,
                  tpg_app_stats_t *stats)
{
    uint32_t              imix_id;
    uint32_t              imix_index;
    tpg_imix_group_t     *imix_cfg;
    app_storage_t        *app_storage;
    imix_app_table_t     *app_table;
    tpg_imix_app_stats_t *imix_app_stats;
    tpg_app_stats_t      *app_stats;
    tpg_app_t            *app;

    assert(stats->as_imix.imix_id == app_data->ad_imix_id);

    imix_id = app_data->ad_imix_id;
    imix_cfg = IMIX_CFG_LOCAL_GET(imix_id);
    app_storage = IMIX_STORAGE_LOCAL_GET(imix_id);
    app_table = IMIX_APP_TABLE_LOCAL_GET(imix_id);
    imix_app_stats = IMIX_STATS_LOCAL_GET(imix_id);

    /* Initialize the application index (into the corresponding application
     * table) based on the weights-indices arrays we built for this imix
     * group.
     */
    imix_index = app_table->iat_app_indices[app_table->iat_last];
    app_table->iat_last++;
    app_table->iat_last %= app_table->iat_count;

    assert(imix_index < imix_cfg->imix_apps_count);
    app_data->ad_imix_index = imix_index;
    app = &imix_cfg->imix_apps[imix_index].ia_app;

    /* The storage needs to be reinitialized too because this is the only
     * moment when we know exactly what type of application will run on this
     * session. Hence the storage type has to match too..
     */
    app_data->ad_storage = app_storage[imix_index];

    APP_CALL(init, app->app_proto)(app_data, app);

    /* Now do the real conn_up.. */

    assert(imix_index < imix_cfg->imix_apps_count);
    app_stats = &imix_app_stats->ias_apps[imix_index];

    assert(app->app_proto == imix_app_stats->ias_app_protos[imix_index]);

    APP_CALL(conn_up, app->app_proto)(l4, app_data, app_stats);
}

/*****************************************************************************
 * imix_conn_down()
 ****************************************************************************/
void imix_conn_down(l4_control_block_t *l4, app_data_t *app_data,
                    tpg_app_stats_t *stats)
{
    uint32_t              imix_id;
    uint32_t              imix_index;
    tpg_imix_group_t     *imix_cfg;
    tpg_imix_app_stats_t *imix_app_stats;
    tpg_app_proto_t       app_proto;
    tpg_app_stats_t      *app_stats;

    imix_id = app_data->ad_imix_id;
    imix_index = app_data->ad_imix_index;

    assert(stats->as_imix.imix_id == imix_id);

    imix_cfg = IMIX_CFG_LOCAL_GET(imix_id);
    imix_app_stats = IMIX_STATS_LOCAL_GET(imix_id);

    assert(imix_index < imix_cfg->imix_apps_count);
    app_proto = imix_cfg->imix_apps[imix_index].ia_app.app_proto;
    app_stats = &imix_app_stats->ias_apps[imix_index];

    assert(app_proto == imix_app_stats->ias_app_protos[imix_index]);

    APP_CALL(conn_down, app_proto)(l4, app_data, app_stats);
}

/*****************************************************************************
 * imix_deliver_data()
 ****************************************************************************/
uint32_t imix_deliver_data(l4_control_block_t *l4, app_data_t *app_data,
                           tpg_app_stats_t *stats,
                           struct rte_mbuf *rx_data,
                           uint64_t rx_tstamp)
{
    uint32_t              imix_id;
    uint32_t              imix_index;
    tpg_imix_group_t     *imix_cfg;
    tpg_imix_app_stats_t *imix_app_stats;
    tpg_app_proto_t       app_proto;
    tpg_app_stats_t      *app_stats;

    imix_id = app_data->ad_imix_id;
    imix_index = app_data->ad_imix_index;

    assert(stats->as_imix.imix_id == imix_id);

    imix_cfg = IMIX_CFG_LOCAL_GET(imix_id);
    imix_app_stats = IMIX_STATS_LOCAL_GET(imix_id);

    assert(imix_index < imix_cfg->imix_apps_count);
    app_proto = imix_cfg->imix_apps[imix_index].ia_app.app_proto;
    app_stats = &imix_app_stats->ias_apps[imix_index];

    assert(app_proto == imix_app_stats->ias_app_protos[imix_index]);

    return APP_CALL(deliver, app_proto)(l4, app_data, app_stats, rx_data,
                                        rx_tstamp);
}

/*****************************************************************************
 * imix_send_data()
 ****************************************************************************/
struct rte_mbuf *imix_send_data(l4_control_block_t *l4, app_data_t *app_data,
                                tpg_app_stats_t *stats,
                                uint32_t max_tx_size)
{
    uint32_t              imix_id;
    uint32_t              imix_index;
    tpg_imix_group_t     *imix_cfg;
    tpg_imix_app_stats_t *imix_app_stats;
    tpg_app_proto_t       app_proto;
    tpg_app_stats_t      *app_stats;

    imix_id = app_data->ad_imix_id;
    imix_index = app_data->ad_imix_index;

    assert(stats->as_imix.imix_id == imix_id);

    imix_cfg = IMIX_CFG_LOCAL_GET(imix_id);
    imix_app_stats = IMIX_STATS_LOCAL_GET(imix_id);

    assert(imix_index < imix_cfg->imix_apps_count);
    app_proto = imix_cfg->imix_apps[imix_index].ia_app.app_proto;
    app_stats = &imix_app_stats->ias_apps[imix_index];

    assert(app_proto == imix_app_stats->ias_app_protos[imix_index]);

    return APP_CALL(send, app_proto)(l4, app_data, app_stats, max_tx_size);
}

/*****************************************************************************
 * imix_data_sent()
 ****************************************************************************/
bool imix_data_sent(l4_control_block_t *l4, app_data_t *app_data,
                    tpg_app_stats_t *stats,
                    uint32_t bytes_sent)
{
    uint32_t              imix_id;
    uint32_t              imix_index;
    tpg_imix_group_t     *imix_cfg;
    tpg_imix_app_stats_t *imix_app_stats;
    tpg_app_proto_t       app_proto;
    tpg_app_stats_t      *app_stats;

    imix_id = app_data->ad_imix_id;
    imix_index = app_data->ad_imix_index;

    assert(stats->as_imix.imix_id == imix_id);

    imix_cfg = IMIX_CFG_LOCAL_GET(imix_id);
    imix_app_stats = IMIX_STATS_LOCAL_GET(imix_id);

    assert(imix_index < imix_cfg->imix_apps_count);
    app_proto = imix_cfg->imix_apps[imix_index].ia_app.app_proto;
    app_stats = &imix_app_stats->ias_apps[imix_index];

    assert(app_proto == imix_app_stats->ias_app_protos[imix_index]);

    return APP_CALL(data_sent, app_proto)(l4, app_data, app_stats, bytes_sent);
}

/*****************************************************************************
 * imix_stats_init_global()
 ****************************************************************************/
void imix_stats_init_global(const tpg_app_t *app_cfg, tpg_app_stats_t *stats)
{
    uint32_t              imix_id;
    tpg_imix_group_t     *imix_cfg;
    tpg_imix_app_stats_t *imix_app_stats;
    uint32_t              app_idx;

    imix_id = app_cfg->app_imix.imix_id;
    imix_cfg = &test_imix_get_env(imix_id)->tig_group;
    imix_app_stats = IMIX_STATS_GET(imix_id);

    stats->as_imix.imix_id = imix_id;

    imix_app_stats->ias_app_protos_count = imix_cfg->imix_apps_count;
    imix_app_stats->ias_apps_count = imix_cfg->imix_apps_count;

    for (app_idx = 0;
            app_idx < imix_app_stats->ias_app_protos_count;
            app_idx++) {
        tpg_app_proto_t app_id = imix_cfg->imix_apps[app_idx].ia_app.app_proto;

        imix_app_stats->ias_app_protos[app_idx] = app_id;

        APP_CALL(stats_init_global,
                 app_id)(&imix_cfg->imix_apps[app_idx].ia_app,
                         &imix_app_stats->ias_apps[app_idx]);
    }
}

/*****************************************************************************
 * imix_stats_init()
 ****************************************************************************/
void imix_stats_init(const tpg_app_t *app_cfg, tpg_app_stats_t *stats)
{
    uint32_t              imix_id;
    tpg_imix_group_t     *imix_cfg;
    tpg_imix_app_stats_t *imix_app_stats;
    uint32_t              app_idx;

    imix_id = app_cfg->app_imix.imix_id;
    imix_cfg = IMIX_CFG_LOCAL_GET(imix_id);
    imix_app_stats = IMIX_STATS_LOCAL_GET(imix_id);

    stats->as_imix.imix_id = imix_id;

    imix_app_stats->ias_app_protos_count = imix_cfg->imix_apps_count;
    imix_app_stats->ias_apps_count = imix_cfg->imix_apps_count;

    for (app_idx = 0;
            app_idx < imix_app_stats->ias_app_protos_count;
            app_idx++) {
        tpg_app_proto_t app_id = imix_cfg->imix_apps[app_idx].ia_app.app_proto;

        imix_app_stats->ias_app_protos[app_idx] = app_id;

        APP_CALL(stats_init, app_id)(&imix_cfg->imix_apps[app_idx].ia_app,
                                     &imix_app_stats->ias_apps[app_idx]);
    }
}

/*****************************************************************************
 * imix_stats_init_req()
 ****************************************************************************/
void imix_stats_init_req(const tpg_app_t *app_cfg, tpg_app_stats_t *stats)
{
    tpg_imix_group_t     *imix_cfg;
    tpg_imix_app_stats_t *imix_app_stats;
    uint32_t              app_idx;

    imix_cfg = &test_imix_get_env(app_cfg->app_imix.imix_id)->tig_group;

    /* Use the "special" imix-id we reserved for stats requests. */
    stats->as_imix.imix_id = IMIX_GETS_STATS_TMP_GET_IMIX_ID();

    imix_app_stats = IMIX_STATS_GET(stats->as_imix.imix_id);

    imix_app_stats->ias_app_protos_count = imix_cfg->imix_apps_count;
    imix_app_stats->ias_apps_count = imix_cfg->imix_apps_count;

    for (app_idx = 0; app_idx < imix_cfg->imix_apps_count; app_idx++) {
        tpg_app_proto_t app_id = imix_cfg->imix_apps[app_idx].ia_app.app_proto;

        imix_app_stats->ias_app_protos[app_idx] = app_id;

        APP_CALL(stats_init_req,
                 app_id)(&imix_cfg->imix_apps[app_idx].ia_app,
                         &imix_app_stats->ias_apps[app_idx]);
    }
}

/*****************************************************************************
 * imix_stats_copy()
 ****************************************************************************/
void imix_stats_copy(tpg_app_stats_t *dest, const tpg_app_stats_t *src)
{
    tpg_imix_app_stats_t *dest_app_stats;
    tpg_imix_app_stats_t *src_app_stats;
    uint32_t              app_idx;

    dest_app_stats = IMIX_STATS_GET(dest->as_imix.imix_id);
    src_app_stats = IMIX_STATS_LOCAL_GET(src->as_imix.imix_id);

    assert(dest_app_stats->ias_app_protos_count ==
                dest_app_stats->ias_apps_count);
    assert(dest_app_stats->ias_app_protos_count ==
                src_app_stats->ias_app_protos_count);
    assert(dest_app_stats->ias_apps_count ==
                src_app_stats->ias_apps_count);

    for (app_idx = 0; app_idx < dest_app_stats->ias_apps_count; app_idx++) {
        tpg_app_proto_t app_id = dest_app_stats->ias_app_protos[app_idx];

        assert(app_id == src_app_stats->ias_app_protos[app_idx]);

        APP_CALL(stats_copy, app_id)(&dest_app_stats->ias_apps[app_idx],
                                     &src_app_stats->ias_apps[app_idx]);
    }
}

/*****************************************************************************
 * imix_stats_add()
 ****************************************************************************/
void imix_stats_add(tpg_app_stats_t *total, const tpg_app_stats_t *elem)
{
    tpg_imix_app_stats_t *total_stats;
    tpg_imix_app_stats_t *elem_stats;
    uint32_t              app_idx;

    total_stats = IMIX_STATS_GET(total->as_imix.imix_id);
    elem_stats = IMIX_STATS_GET(elem->as_imix.imix_id);

    for (app_idx = 0; app_idx < total_stats->ias_app_protos_count; app_idx++) {
        tpg_app_proto_t app_id = total_stats->ias_app_protos[app_idx];

        assert(app_id == elem_stats->ias_app_protos[app_idx]);

        APP_CALL(stats_add, app_id)(&total_stats->ias_apps[app_idx],
                                    &elem_stats->ias_apps[app_idx]);
    }
}

/*****************************************************************************
 * imix_stats_print()
 ****************************************************************************/
void imix_stats_print(const tpg_app_stats_t *stats, printer_arg_t *printer_arg)
{
    tpg_imix_app_stats_t *imix_app_stats;
    uint32_t              app_idx;

    imix_app_stats = IMIX_STATS_GET(stats->as_imix.imix_id);

    for (app_idx = 0;
            app_idx < imix_app_stats->ias_app_protos_count;
            app_idx++) {
        tpg_app_proto_t app_id = imix_app_stats->ias_app_protos[app_idx];

        tpg_printf(printer_arg, "IMIX %"PRIu32":\n", app_idx);
        APP_CALL(stats_print, app_id)(&imix_app_stats->ias_apps[app_idx],
                                      printer_arg);
    }
}

/*****************************************************************************
 * imix_init()
 ****************************************************************************/
bool imix_init(void)
{
    uint32_t imix_id;

    /*
     * Add IMIX module CLI commands
     */
    if (!cli_add_main_ctx(cli_ctx)) {
        RTE_LOG(ERR, USER1, "ERROR: Can't add IMIX specific CLI commands!\n");
        return false;
    }

    imix_groups = rte_zmalloc("test_imix_env",
                              IMIX_MAX_ENTRIES * sizeof(*imix_groups),
                              0);
    if (!imix_groups) {
        RTE_LOG(ERR, USER1, "Failed allocating IMIX global config!\n");
        return false;
    }

    imix_stats = rte_zmalloc("imix_stats_global",
                             IMIX_MAX_ENTRIES * sizeof(*imix_stats),
                             0);
    if (!imix_stats) {
        RTE_LOG(ERR, USER1, "Failed allocating IMIX global stats!\n");
        return false;
    }

    /* Initialize the global IMIX stats structures (i.e., set imix-id). */
    for (imix_id = 0; imix_id < IMIX_MAX_ENTRIES; imix_id++)
        imix_stats[imix_id].ias_imix_id = imix_id;

    if (!imix_cli_init())
        return false;

    return true;
}

/*****************************************************************************
 * imix_lcore_init()
 ****************************************************************************/
void imix_lcore_init(uint32_t lcore_id)
{
    uint32_t imix_id;

    RTE_PER_LCORE(imix_local_cfg) =
        rte_zmalloc_socket("imix_local_cfg",
                           IMIX_MAX_ENTRIES *
                               sizeof(*RTE_PER_LCORE(imix_local_cfg)),
                           RTE_CACHE_LINE_SIZE,
                           rte_lcore_to_socket_id(lcore_id));
    if (RTE_PER_LCORE(imix_local_cfg) == NULL) {
        TPG_ERROR_ABORT("[%d:%s() Failed to allocate per lcore imix_local_cfg!\n",
                        rte_lcore_index(lcore_id),
                        __func__);
    }

    RTE_PER_LCORE(imix_storage_table) =
        rte_zmalloc_socket("imix_storage_table_local",
                           IMIX_MAX_ENTRIES * TPG_IMIX_MAX_APPS *
                                sizeof(*RTE_PER_LCORE(imix_storage_table)),
                           RTE_CACHE_LINE_SIZE,
                           rte_lcore_to_socket_id(lcore_id));
    if (RTE_PER_LCORE(imix_storage_table) == NULL) {
        TPG_ERROR_ABORT("[%d:%s() Failed to allocate per lcore imix_storage_table!\n",
                        rte_lcore_index(lcore_id),
                        __func__);
    }

    RTE_PER_LCORE(imix_app_table) =
        rte_zmalloc_socket("imix_app_table_local",
                           IMIX_MAX_ENTRIES *
                                sizeof(*RTE_PER_LCORE(imix_app_table)),
                           RTE_CACHE_LINE_SIZE,
                           rte_lcore_to_socket_id(lcore_id));
    if (RTE_PER_LCORE(imix_app_table) == NULL) {
        TPG_ERROR_ABORT("[%d:%s() Failed to allocate per lcore imix_app_table!\n",
                        rte_lcore_index(lcore_id),
                        __func__);
    }

    RTE_PER_LCORE(imix_local_stats) =
        rte_zmalloc_socket("imix_local_stats",
                           IMIX_MAX_ENTRIES *
                               sizeof(*RTE_PER_LCORE(imix_local_stats)),
                           RTE_CACHE_LINE_SIZE,
                           rte_lcore_to_socket_id(lcore_id));
    if (RTE_PER_LCORE(imix_local_stats) == NULL) {
        TPG_ERROR_ABORT("[%d:%s() Failed to allocate per lcore imix_local_stats!\n",
                        rte_lcore_index(lcore_id),
                        __func__);
    }

    /* Initialize the local IMIX stats structures (i.e., set imix-id). */
    for (imix_id = 0; imix_id < IMIX_MAX_ENTRIES; imix_id++)
        RTE_PER_LCORE(imix_local_stats)[imix_id].ias_imix_id = imix_id;
}

/*****************************************************************************
 * test_imix_get_env()
 ****************************************************************************/
test_imix_group_t *test_imix_get_env(uint32_t imix_id)
{
    return &imix_groups[imix_id];
}

/*****************************************************************************
 * test_imix_get_stats()
 ****************************************************************************/
tpg_imix_app_stats_t *test_imix_get_stats(uint32_t imix_id)
{
    return IMIX_STATS_GET(imix_id);
}

/*****************************************************************************
 * CLI
 ****************************************************************************/
/*****************************************************************************
 * Global IMIX CLI storage
 ****************************************************************************/
/*
 * We allow only as many entries in a group as fit in the id list bitmap.
 * TODO: if we ever need more than that we need to write a new parser..
 */
#define IMIX_MAX_APP_CLI_ENTRIES \
    (sizeof(container_of(NULL, cmdline_id_list_t, map)->map) * 8)

/*
 * Storage for CLI imix app entries. Array indexed by eth_port, test-id and
 * app index.
 */
typedef struct {
    tpg_imix_app_t imix_cli_app;

    /* Bit flags. */
    uint32_t imix_cli_configured : 1;
} test_imix_cli_app_t;

static test_imix_cli_app_t *imix_cli_app_cfg;

/*****************************************************************************
 * imix_cli_init()
 ****************************************************************************/
static bool imix_cli_init(void)
{
    imix_cli_app_cfg = rte_zmalloc("imix_cli_app_cfg",
                                        IMIX_MAX_APP_CLI_ENTRIES *
                                            sizeof(*imix_cli_app_cfg),
                                        0);
    if (!imix_cli_app_cfg) {
        RTE_LOG(ERR, USER1, "Failed allocating IMIX CLI storage!\n");
        return false;
    }

    return true;
}

/*****************************************************************************
 * imix_cli_set_app_cfg()
 ****************************************************************************/
int imix_cli_set_app_cfg(uint32_t app_idx, const tpg_app_t *app_cfg,
                         printer_arg_t *printer_arg)
{
    test_imix_cli_app_t *app_cli_cfg;

    if (app_idx >= IMIX_MAX_APP_CLI_ENTRIES) {
        tpg_printf(printer_arg, "ERROR: Max allowed app index is %lu!\n",
                   IMIX_MAX_APP_CLI_ENTRIES);
        return -EINVAL;
    }

    app_cli_cfg = &imix_cli_app_cfg[app_idx];
    app_cli_cfg->imix_cli_app.ia_app = *app_cfg;
    app_cli_cfg->imix_cli_configured = true;

    return 0;
}

/*****************************************************************************
 * imix_cli_set_app_weight()
 ****************************************************************************/
int imix_cli_set_app_weight(uint32_t app_idx, uint32_t weight,
                            printer_arg_t *printer_arg)
{
    test_imix_cli_app_t *app_cli_cfg;

    if (app_idx >= IMIX_MAX_APP_CLI_ENTRIES) {
        tpg_printf(printer_arg, "ERROR: Max allowed IMIX APP index is %lu!\n",
                   IMIX_MAX_APP_CLI_ENTRIES);
        return -EINVAL;
    }

    app_cli_cfg = &imix_cli_app_cfg[app_idx];

    if (!app_cli_cfg->imix_cli_configured) {
        tpg_printf(printer_arg,
                   "ERROR: IMIX App index %"PRIu32" needs to be configured first!\n",
                   app_idx);
        return -EINVAL;
    }

    app_cli_cfg->imix_cli_app.ia_weight = weight;

    return 0;
}

/*****************************************************************************
 * imix_cli_get_app_cfg()
 ****************************************************************************/
int imix_cli_get_app_cfg(uint32_t app_idx, tpg_app_t *app_cfg,
                         printer_arg_t *printer_arg)
{
    test_imix_cli_app_t *app_cli_cfg;

    if (app_idx >= IMIX_MAX_APP_CLI_ENTRIES) {
        tpg_printf(printer_arg, "ERROR: Max allowed app index is %lu!\n",
                   IMIX_MAX_APP_CLI_ENTRIES);
        return -EINVAL;
    }

    app_cli_cfg = &imix_cli_app_cfg[app_idx];
    if (!app_cli_cfg->imix_cli_configured) {
        tpg_printf(printer_arg,
                   "ERROR: IMIX App index %"PRIu32" not configured!\n",
                   app_idx);
        return -ENOENT;
    }

    *app_cfg = app_cli_cfg->imix_cli_app.ia_app;

    return 0;
}

/*****************************************************************************
 * imix_cli_delete_app_cfg()
 ****************************************************************************/
int imix_cli_delete_app_cfg(uint32_t app_idx, printer_arg_t *printer_arg)
{
    test_imix_cli_app_t *app_cli_cfg;

    if (app_idx >= IMIX_MAX_APP_CLI_ENTRIES) {
        tpg_printf(printer_arg, "ERROR: Max allowed app index is %lu!\n",
                   IMIX_MAX_APP_CLI_ENTRIES);
        return -EINVAL;
    }

    app_cli_cfg = &imix_cli_app_cfg[app_idx];
    bzero(&app_cli_cfg->imix_cli_app, sizeof(app_cli_cfg->imix_cli_app));
    app_cli_cfg->imix_cli_configured = false;

    return 0;
}

/*****************************************************************************
 * IMIX CLI
 ****************************************************************************/

/*****************************************************************************
 * add tests imix-id <imix-id> app <list-of-imix-app-indices>
 ****************************************************************************/
struct cmd_imix_add_result {
    cmdline_fixed_string_t add;
    cmdline_fixed_string_t tests;
    cmdline_fixed_string_t imix_id_kw;
    uint32_t               imix_id;
    cmdline_fixed_string_t app;
    cmdline_id_list_t      id_list;
};

static cmdline_parse_token_string_t cmd_imix_add_T_add =
    TOKEN_STRING_INITIALIZER(struct cmd_imix_add_result, add, "add");
static cmdline_parse_token_string_t cmd_imix_add_T_tests =
    TOKEN_STRING_INITIALIZER(struct cmd_imix_add_result, tests, "tests");
static cmdline_parse_token_string_t cmd_imix_add_T_imix_id_kw =
    TOKEN_STRING_INITIALIZER(struct cmd_imix_add_result, imix_id_kw, "imix-id");
static cmdline_parse_token_num_t cmd_imix_add_T_imix_id =
    TOKEN_NUM_INITIALIZER(struct cmd_imix_add_result, imix_id, UINT32);
static cmdline_parse_token_string_t cmd_imix_add_T_app =
    TOKEN_STRING_INITIALIZER(struct cmd_imix_add_result, app, "app");
static cmdline_parse_token_id_list_t cmd_imix_add_T_id_list =
    TOKEN_ID_LIST_INITIALIZER(struct cmd_imix_add_result, id_list);

static void cmd_tests_add_imix_parsed(void *parsed_result,
                                      struct cmdline *cl,
                                      void *data __rte_unused)
{
    struct cmd_imix_add_result *pr = parsed_result;

    int              err;
    uint32_t         app_idx;
    tpg_imix_group_t imix_group;
    printer_arg_t    parg = TPG_PRINTER_ARG(cli_printer, cl);

    bzero(&imix_group, sizeof(imix_group));

    for (app_idx = 0; app_idx < IMIX_MAX_APP_CLI_ENTRIES; app_idx++) {
        test_imix_cli_app_t *storage;

        if (!(pr->id_list.map & (1 << app_idx)))
            continue;

        if (imix_group.imix_apps_count == TPG_IMIX_MAX_APPS) {
            cmdline_printf(cl,
                           "ERROR: Failed creating IMIX group!\n"
                           "Max apps allowed per IMIX group is %u.\n",
                           TPG_IMIX_MAX_APPS);
            return;
        }

        storage = &imix_cli_app_cfg[app_idx];

        if (!storage->imix_cli_configured) {
            cmdline_printf(cl,
                           "ERROR: Failed creating IMIX group!\n"
                           "App index %u doesn't exist!\n",
                           app_idx);
            return;
        }

        /* Struct copy. */
        imix_group.imix_apps[imix_group.imix_apps_count] = storage->imix_cli_app;
        imix_group.imix_apps_count++;
    }

    imix_group.imix_id = pr->imix_id;

    err = test_mgmt_add_imix_group(pr->imix_id, &imix_group, &parg);
    if (err != 0) {
        cmdline_printf(cl, "Failed to create IMIX group!\n");
        return;
    }
}

cmdline_parse_inst_t cmd_tests_add_imix = {
    .f = cmd_tests_add_imix_parsed,
    .data = NULL,
    .help_str = "add tests imix-id <id> app <app-id-list>",
    .tokens = {
        (void *)&cmd_imix_add_T_add,
        (void *)&cmd_imix_add_T_tests,
        (void *)&cmd_imix_add_T_imix_id_kw,
        (void *)&cmd_imix_add_T_imix_id,
        (void *)&cmd_imix_add_T_app,
        (void *)&cmd_imix_add_T_id_list,
        NULL,
    },
};

/*****************************************************************************
 * del tests imix-id <imix-id>
 ****************************************************************************/
struct cmd_imix_del_result {
    cmdline_fixed_string_t del;
    cmdline_fixed_string_t tests;
    cmdline_fixed_string_t imix_id_kw;
    uint32_t               imix_id;
};

static cmdline_parse_token_string_t cmd_imix_del_T_del =
    TOKEN_STRING_INITIALIZER(struct cmd_imix_del_result, del, "del");
static cmdline_parse_token_string_t cmd_imix_del_T_tests =
    TOKEN_STRING_INITIALIZER(struct cmd_imix_del_result, tests, "tests");
static cmdline_parse_token_string_t cmd_imix_del_T_imix_id_kw =
    TOKEN_STRING_INITIALIZER(struct cmd_imix_del_result, imix_id_kw, "imix-id");
static cmdline_parse_token_num_t cmd_imix_del_T_imix_id =
    TOKEN_NUM_INITIALIZER(struct cmd_imix_del_result, imix_id, UINT32);

static void cmd_tests_del_imix_parsed(void *parsed_result,
                                      struct cmdline *cl,
                                      void *data __rte_unused)
{
    struct cmd_imix_del_result *pr = parsed_result;

    int           err;
    printer_arg_t parg = TPG_PRINTER_ARG(cli_printer, cl);

    err = test_mgmt_del_imix_group(pr->imix_id, &parg);
    if (err != 0)
        cmdline_printf(cl, "Failed to delete IMIX group!\n");
}

cmdline_parse_inst_t cmd_tests_del_imix = {
    .f = cmd_tests_del_imix_parsed,
    .data = NULL,
    .help_str = "del tests imix-id <id>",
    .tokens = {
        (void *)&cmd_imix_del_T_del,
        (void *)&cmd_imix_del_T_tests,
        (void *)&cmd_imix_del_T_imix_id_kw,
        (void *)&cmd_imix_del_T_imix_id,
        NULL,
    },
};


/*****************************************************************************
 * set tests imix port <port> test-case-id <tcid> imix-id <imix-id>
 ****************************************************************************/
struct cmd_imix_set_result {
    cmdline_fixed_string_t set;
    cmdline_fixed_string_t tests;
    cmdline_fixed_string_t imix;
    cmdline_fixed_string_t port_kw;
    uint32_t               port;
    cmdline_fixed_string_t tcid_kw;
    uint32_t               tcid;
    cmdline_fixed_string_t imix_id_kw;
    uint32_t               imix_id;
};

static cmdline_parse_token_string_t cmd_imix_set_T_set =
    TOKEN_STRING_INITIALIZER(struct cmd_imix_set_result, set, "set");
static cmdline_parse_token_string_t cmd_imix_set_T_tests =
    TOKEN_STRING_INITIALIZER(struct cmd_imix_set_result, tests, "tests");
static cmdline_parse_token_string_t cmd_imix_set_T_imix =
    TOKEN_STRING_INITIALIZER(struct cmd_imix_set_result, imix, "imix");
static cmdline_parse_token_string_t cmd_imix_set_T_port_kw =
    TOKEN_STRING_INITIALIZER(struct cmd_imix_set_result, port_kw, "port");
static cmdline_parse_token_num_t cmd_imix_set_T_port =
    TOKEN_NUM_INITIALIZER(struct cmd_imix_set_result, port, UINT32);
static cmdline_parse_token_string_t cmd_imix_set_T_tcid_kw =
    TOKEN_STRING_INITIALIZER(struct cmd_imix_set_result, tcid, "test-case-id");
static cmdline_parse_token_num_t cmd_imix_set_T_tcid =
    TOKEN_NUM_INITIALIZER(struct cmd_imix_set_result, tcid, UINT32);
static cmdline_parse_token_string_t cmd_imix_set_T_imix_id_kw =
    TOKEN_STRING_INITIALIZER(struct cmd_imix_set_result, imix_id_kw, "imix-id");
static cmdline_parse_token_num_t cmd_imix_set_T_imix_id =
    TOKEN_NUM_INITIALIZER(struct cmd_imix_set_result, imix_id, UINT32);

static void cmd_tests_set_imix_parsed(void *parsed_result,
                                      struct cmdline *cl,
                                      void *data __rte_unused)
{
    tpg_app_t                   app_cfg;
    struct cmd_imix_set_result *pr = parsed_result;

    printer_arg_t parg = TPG_PRINTER_ARG(cli_printer, cl);

    bzero(&app_cfg, sizeof(app_cfg));

    app_cfg.app_proto = APP_PROTO__IMIX;
    app_cfg.app_imix.imix_id = pr->imix_id;

    if (test_mgmt_update_test_case_app(pr->port, pr->tcid, &app_cfg, &parg)) {
        cmdline_printf(cl,
                       "ERROR: Failed updating test case %"PRIu32
                       " config on port %"PRIu32"\n",
                       pr->tcid,
                       pr->port);
    } else {
        cmdline_printf(cl, "Port %"PRIu32", Test Case %"PRIu32" updated!\n",
                       pr->port,
                       pr->tcid);
    }
}

cmdline_parse_inst_t cmd_tests_set_imix = {
    .f = cmd_tests_set_imix_parsed,
    .data = NULL,
    .help_str = "set tests imix port <eth-port> test-case-id <tcid> imix-id <id>",
    .tokens = {
        (void *)&cmd_imix_set_T_set,
        (void *)&cmd_imix_set_T_tests,
        (void *)&cmd_imix_set_T_imix,
        (void *)&cmd_imix_set_T_port_kw,
        (void *)&cmd_imix_set_T_port,
        (void *)&cmd_imix_set_T_tcid_kw,
        (void *)&cmd_imix_set_T_tcid,
        (void *)&cmd_imix_set_T_imix_id_kw,
        (void *)&cmd_imix_set_T_imix_id,
        NULL,
    },
};

/*****************************************************************************
 * set tests imix app-index <app-index> weight <weight>
 ****************************************************************************/
struct cmd_imix_set_weight_result {
    cmdline_fixed_string_t set;
    cmdline_fixed_string_t tests;
    cmdline_fixed_string_t imix;
    cmdline_fixed_string_t app_index_kw;
    uint32_t               app_index;
    cmdline_fixed_string_t weight_kw;
    uint32_t               weight;
};

static cmdline_parse_token_string_t cmd_imix_set_weight_T_set =
    TOKEN_STRING_INITIALIZER(struct cmd_imix_set_weight_result, set, "set");
static cmdline_parse_token_string_t cmd_imix_set_weight_T_tests =
    TOKEN_STRING_INITIALIZER(struct cmd_imix_set_weight_result, tests, "tests");
static cmdline_parse_token_string_t cmd_imix_set_weight_T_imix =
    TOKEN_STRING_INITIALIZER(struct cmd_imix_set_weight_result, imix, "imix");
static cmdline_parse_token_string_t cmd_imix_set_weight_T_app_index_kw =
    TOKEN_STRING_INITIALIZER(struct cmd_imix_set_weight_result, app_index_kw, "app-index");
static cmdline_parse_token_num_t cmd_imix_set_weight_T_app_index =
    TOKEN_NUM_INITIALIZER(struct cmd_imix_set_weight_result, app_index, UINT32);
static cmdline_parse_token_string_t cmd_imix_set_weight_T_weight_kw =
    TOKEN_STRING_INITIALIZER(struct cmd_imix_set_weight_result, weight_kw, "weight");
static cmdline_parse_token_num_t cmd_imix_set_weight_T_weight =
    TOKEN_NUM_INITIALIZER(struct cmd_imix_set_weight_result, weight, UINT32);

static void cmd_tests_set_imix_weight_parsed(void *parsed_result,
                                             struct cmdline *cl,
                                             void *data __rte_unused)
{
    struct cmd_imix_set_weight_result *pr = parsed_result;

    printer_arg_t parg = TPG_PRINTER_ARG(cli_printer, cl);

    if (imix_cli_set_app_weight(pr->app_index, pr->weight, &parg)) {
        cmdline_printf(cl,
                       "ERROR: Failed to update weight for IMIX app index %"PRIu32"!\n",
                       pr->app_index);
    } else {
        cmdline_printf(cl, "IMIX app index %"PRIu32" weight updated!\n",
                       pr->app_index);
    }
}

cmdline_parse_inst_t cmd_tests_set_imix_weight = {
    .f = cmd_tests_set_imix_weight_parsed,
    .data = NULL,
    .help_str = "set tests imix app-index <app-index> weight <weight>",
    .tokens = {
        (void *)&cmd_imix_set_weight_T_set,
        (void *)&cmd_imix_set_weight_T_tests,
        (void *)&cmd_imix_set_weight_T_imix,
        (void *)&cmd_imix_set_weight_T_app_index_kw,
        (void *)&cmd_imix_set_weight_T_app_index,
        (void *)&cmd_imix_set_weight_T_weight_kw,
        (void *)&cmd_imix_set_weight_T_weight,
        NULL,
    },
};

/*****************************************************************************
 * show tests imix <imix-id>
 ****************************************************************************/
struct cmd_imix_show_group_result {
    cmdline_fixed_string_t show;
    cmdline_fixed_string_t tests;
    cmdline_fixed_string_t imix;
    uint32_t               imix_id;
};

static cmdline_parse_token_string_t cmd_imix_show_group_T_show =
    TOKEN_STRING_INITIALIZER(struct cmd_imix_show_group_result, show, "show");
static cmdline_parse_token_string_t cmd_imix_show_group_T_tests =
    TOKEN_STRING_INITIALIZER(struct cmd_imix_show_group_result, tests, "tests");
static cmdline_parse_token_string_t cmd_imix_show_group_T_imix =
    TOKEN_STRING_INITIALIZER(struct cmd_imix_show_group_result, imix, "imix");
static cmdline_parse_token_num_t cmd_imix_show_group_T_imix_id =
    TOKEN_NUM_INITIALIZER(struct cmd_imix_show_group_result, imix_id, UINT32);

static void cmd_tests_show_imix_group_parsed(void *parsed_result,
                                             struct cmdline *cl,
                                             void *data)
{
    struct cmd_imix_show_group_result *pr = parsed_result;
    int                                option = (intptr_t) data;
    uint32_t                           imix_id;

    printer_arg_t parg = TPG_PRINTER_ARG(cli_printer, cl);

    tpg_imix_group_t imix_group;
    uint32_t         i;
    uint32_t         app_idx;

    if (option == 'i')
        imix_id = pr->imix_id;
    else
        imix_id = TPG_IMIX_MAX_GROUPS;

    for (i = 0; i < TPG_IMIX_MAX_GROUPS; i++) {
        if (imix_id != TPG_IMIX_MAX_GROUPS && imix_id != i)
            continue;

        if (test_mgmt_get_imix_group(i, &imix_group, NULL) != 0)
            continue;

        cmdline_printf(cl, "IMIX ID: %"PRIu32"\n", i);
        for (app_idx = 0; app_idx < imix_group.imix_apps_count; app_idx++) {
            tpg_imix_app_t *imix_app = &imix_group.imix_apps[app_idx];

            cmdline_printf(cl, "IMIX APP: %"PRIu32"\n", app_idx);
            APP_CALL(print_cfg,
                     imix_app->ia_app.app_proto)(&imix_app->ia_app, &parg);
            cmdline_printf(cl, "\n");
        }

        cmdline_printf(cl, "\n");
    }
}

cmdline_parse_inst_t cmd_tests_show_imix_group = {
    .f = cmd_tests_show_imix_group_parsed,
    .data = (void *) (intptr_t) 'i',
    .help_str = "show tests imix <imix-id>",
    .tokens = {
        (void *)&cmd_imix_show_group_T_show,
        (void *)&cmd_imix_show_group_T_tests,
        (void *)&cmd_imix_show_group_T_imix,
        (void *)&cmd_imix_show_group_T_imix_id,
        NULL,
    },
};

cmdline_parse_inst_t cmd_tests_show_imix = {
    .f = cmd_tests_show_imix_group_parsed,
    .data = NULL,
    .help_str = "show tests imix",
    .tokens = {
        (void *)&cmd_imix_show_group_T_show,
        (void *)&cmd_imix_show_group_T_tests,
        (void *)&cmd_imix_show_group_T_imix,
        NULL,
    },
};

static cmdline_parse_ctx_t cli_ctx[] = {
    &cmd_tests_add_imix,
    &cmd_tests_del_imix,
    &cmd_tests_set_imix,
    &cmd_tests_set_imix_weight,
    &cmd_tests_show_imix_group,
    &cmd_tests_show_imix,
    NULL,
};

