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
 * Latency related local definitions
 ****************************************************************************/

/* Latency "tag" to let the destination know that we added a TX timestamp.
 * It expands to "WLATENCY" (without the null terminator).
 */
static const uint8_t raw_latency_magic[] = {
    0x57, 0x4c, 0x41, 0x54, 0x45, 0x4e, 0x43, 0x59
};

/* Make sure that the magic size matches the size of the magic field in the
 * timestamp payload.
 */
static_assert(sizeof(raw_latency_magic) ==
                    sizeof(((raw_latency_data_t *)NULL)->rld_magic),
              "Check rld_tstamp field size!");

/*****************************************************************************
 * Forward references.
 ****************************************************************************/
static cmdline_parse_ctx_t cli_ctx[];

/*****************************************************************************
 * Globals.
 ****************************************************************************/

/*
 * Shared (across lcore) template for RAW traffic.
 */
static RTE_DEFINE_PER_LCORE(struct rte_mbuf *, raw_mbuf_template);

/*****************************************************************************
 * raw_init_tstamp_cfg()
 ****************************************************************************/
static int raw_init_tstamp_cfg(raw_storage_t *raw_storage,
                               bool rx_tstamp, bool tx_tstamp)
{
    raw_tstamp_type_t tstamp_type = RAW_FLAG_TSTAMP_NONE;

    if (rx_tstamp && tx_tstamp)
        RAW_TSTAMP_SET(tstamp_type, RAW_FLAG_TSTAMP_RXTX);
    else if (rx_tstamp)
        RAW_TSTAMP_SET(tstamp_type, RAW_FLAG_TSTAMP_RX);
    else if (tx_tstamp)
        RAW_TSTAMP_SET(tstamp_type, RAW_FLAG_TSTAMP_TX);

    raw_storage->rst_tstamp = tstamp_type;

    return 0;
}

/*****************************************************************************
 * raw_clear_tstamp_cfg()
 ****************************************************************************/
static void raw_clear_tstamp_cfg(raw_storage_t *raw_storage)
{
    raw_storage->rst_tstamp = RAW_FLAG_TSTAMP_NONE;
}

/*****************************************************************************
 * raw_latency_data_init()
 *      Initialize the latency payload.
 ****************************************************************************/
static void raw_latency_data_init(raw_latency_data_t *latency_data)
{
    rte_memcpy(&latency_data->rld_magic, &raw_latency_magic[0],
               sizeof(latency_data->rld_magic));
}

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
    cfg->tc_app.app_raw_client.rc_req_plen = 0;
    cfg->tc_app.app_raw_client.rc_resp_plen = 0;
    TPG_XLATE_OPTIONAL_SET_FIELD(&cfg->tc_app.app_raw_client, rc_rx_tstamp,
                                 false);
    TPG_XLATE_OPTIONAL_SET_FIELD(&cfg->tc_app.app_raw_client, rc_tx_tstamp,
                                 false);
}

/*****************************************************************************
 * raw_server_default_cfg()
 ****************************************************************************/
void raw_server_default_cfg(tpg_test_case_t *cfg)
{
    cfg->tc_app.app_raw_server.rs_req_plen = 0;
    cfg->tc_app.app_raw_server.rs_resp_plen = 0;
    TPG_XLATE_OPTIONAL_SET_FIELD(&cfg->tc_app.app_raw_server, rs_rx_tstamp,
                                 false);
    TPG_XLATE_OPTIONAL_SET_FIELD(&cfg->tc_app.app_raw_server, rs_tx_tstamp,
                                 false);
}

/*****************************************************************************
 * raw_validate_cfg()
 ****************************************************************************/
static bool raw_validate_cfg(uint32_t eth_port, uint32_t test_case_id,
                             uint32_t incoming_len, bool rx_tstamp,
                             uint32_t outgoing_len, bool tx_tstamp,
                             printer_arg_t *printer_arg)
{
    if (rx_tstamp) {
        if (incoming_len < sizeof(raw_latency_data_t)) {
            tpg_printf(printer_arg,
                       "ERROR: RAW RX timestamping enabled so minimum RX size must be %lu!\n",
                       sizeof(raw_latency_data_t));
            return false;
        }
    }

    if (tx_tstamp) {
        tpg_ipv4_sockopt_t ipv4_sockopt;

        /* We don't support multiple TX timestamping types on the same test
         * case.
         */
        if (test_mgmt_get_ipv4_sockopt(eth_port, test_case_id, &ipv4_sockopt,
                                       printer_arg) == 0) {
            if (ipv4_sockopt.ip4so_tx_tstamp) {
                tpg_printf(printer_arg,
                           "ERROR: RAW TX timestamping and IP timestamping can't be both ON!\n");
                return false;
            }
        }

        if (outgoing_len < sizeof(raw_latency_data_t)) {
            tpg_printf(printer_arg,
                       "ERROR: RAW TX timestamping enabled so minimum TX size must be %lu!\n",
                       sizeof(raw_latency_data_t));
            return false;
        }

        /* TODO: validation is not done properly if the sockopts are changed
         * after the RAW config is applied!
         */
    }

    return true;
}

/*****************************************************************************
 * raw_validate_cfg()
 ****************************************************************************/
bool raw_client_validate_cfg(const tpg_test_case_t *cfg,
                             const tpg_app_t *app_cfg,
                             printer_arg_t *printer_arg)
{
    const tpg_raw_client_t *raw_client = &app_cfg->app_raw_client;

    return raw_validate_cfg(cfg->tc_eth_port, cfg->tc_id,
                            raw_client->rc_resp_plen,
                            TPG_XLATE_OPT_BOOL(raw_client, rc_rx_tstamp),
                            raw_client->rc_req_plen,
                            TPG_XLATE_OPT_BOOL(raw_client, rc_tx_tstamp),
                            printer_arg);
}

/*****************************************************************************
 * raw_validate_cfg()
 ****************************************************************************/
bool raw_server_validate_cfg(const tpg_test_case_t *cfg,
                             const tpg_app_t *app_cfg,
                             printer_arg_t *printer_arg)
{
    const tpg_raw_server_t *raw_server = &app_cfg->app_raw_server;

    return raw_validate_cfg(cfg->tc_eth_port, cfg->tc_id,
                            raw_server->rs_req_plen,
                            TPG_XLATE_OPT_BOOL(raw_server, rs_rx_tstamp),
                            raw_server->rs_resp_plen,
                            TPG_XLATE_OPT_BOOL(raw_server, rs_tx_tstamp),
                            printer_arg);
}

/*****************************************************************************
 * raw_client_print_cfg()
 ****************************************************************************/
void raw_client_print_cfg(const tpg_app_t *app_cfg,
                          printer_arg_t *printer_arg)
{
    tpg_printf(printer_arg, "RAW CLIENT:\n");
    tpg_printf(printer_arg, "%-15s: %"PRIu32"\n", "Request Len",
               app_cfg->app_raw_client.rc_req_plen);
    tpg_printf(printer_arg, "%-15s: %"PRIu32"\n", "Response Len",
               app_cfg->app_raw_client.rc_resp_plen);
    tpg_printf(printer_arg, "%-15s: %-3s\n",  "RX-TS",
               TPG_XLATE_OPT_BOOL(&app_cfg->app_raw_client, rc_rx_tstamp) ?
                                  "ON" : "OFF");
    tpg_printf(printer_arg, "%-15s: %-3s\n", "TX-TS",
               TPG_XLATE_OPT_BOOL(&app_cfg->app_raw_client, rc_tx_tstamp) ?
                                  "ON" : "OFF");
}

/*****************************************************************************
 * raw_server_print_cfg()
 ****************************************************************************/
void raw_server_print_cfg(const tpg_app_t *app_cfg,
                          printer_arg_t *printer_arg)
{
    tpg_printf(printer_arg, "RAW SERVER:\n");
    tpg_printf(printer_arg, "%-15s: %"PRIu32"\n", "Request Len",
               app_cfg->app_raw_server.rs_req_plen);
    tpg_printf(printer_arg, "%-15s: %"PRIu32"\n", "Response Len",
               app_cfg->app_raw_server.rs_resp_plen);
    tpg_printf(printer_arg, "%-15s: %-3s\n",  "RX-TS",
               TPG_XLATE_OPT_BOOL(&app_cfg->app_raw_server, rs_rx_tstamp) ?
                                  "ON" : "OFF");
    tpg_printf(printer_arg, "%-15s: %-3s\n", "TX-TS",
               TPG_XLATE_OPT_BOOL(&app_cfg->app_raw_server, rs_tx_tstamp) ?
                                  "ON" : "OFF");
}

/*****************************************************************************
 * raw_add_delete_cfg()
 ****************************************************************************/
void raw_add_delete_cfg(const tpg_test_case_t *cfg __rte_unused,
                        const tpg_app_t *app_cfg __rte_unused)
{
    /* Nothing allocated, nothing to do. */
}

/*****************************************************************************
 * raw_client_pkts_per_send()
 ****************************************************************************/
uint32_t raw_client_pkts_per_send(const tpg_test_case_t *cfg __rte_unused,
                                  const tpg_app_t *app_cfg,
                                  uint32_t max_pkt_size)
{
    return (app_cfg->app_raw_client.rc_req_plen + max_pkt_size - 1) /
                max_pkt_size;
}

/*****************************************************************************
 * raw_server_pkts_per_send()
 ****************************************************************************/
uint32_t raw_server_pkts_per_send(const tpg_test_case_t *cfg __rte_unused,
                                  const tpg_app_t *app_cfg,
                                  uint32_t max_pkt_size)
{
    return (app_cfg->app_raw_server.rs_resp_plen + max_pkt_size - 1) /
                max_pkt_size;
}

/*****************************************************************************
 * raw_conn_init()
 ****************************************************************************/
static void raw_conn_init(app_data_t *app_data, uint32_t req_len,
                          uint32_t resp_len)
{
    raw_tstamp_type_t tstamp_type;

    tstamp_type = app_data->ad_storage.ast_raw.rst_tstamp;

    /* If the config needs RX timestamping, enable for this session. */
    if (unlikely(RAW_TSTAMP_ISSET(tstamp_type, RAW_FLAG_TSTAMP_RX)))
        app_data->ad_raw.ra_rx_tstamp_size = sizeof(raw_latency_data_t);

    /* If the config needs TX timestamping, enable for this session. */
    if (unlikely(RAW_TSTAMP_ISSET(tstamp_type, RAW_FLAG_TSTAMP_TX))) {
        app_data->ad_raw.ra_tx_tstamp_size = sizeof(raw_latency_data_t);

        raw_latency_data_init(&app_data->ad_raw.ra_tx_tstamp);
    }

    app_data->ad_raw.ra_req_size = req_len;
    app_data->ad_raw.ra_resp_size = resp_len;
}

/*****************************************************************************
 * raw_client_init()
 ****************************************************************************/
void raw_client_init(app_data_t *app_data, const tpg_app_t *app_cfg)
{
    raw_conn_init(app_data, app_cfg->app_raw_client.rc_req_plen,
                  app_cfg->app_raw_client.rc_resp_plen);
}

/*****************************************************************************
 * raw_server_init()
 ****************************************************************************/
void raw_server_init(app_data_t *app_data, const tpg_app_t *app_cfg)
{
    /* Servers act exactly in the same way as clients except that requests and
     * responses are swapped.
     */
    raw_conn_init(app_data, app_cfg->app_raw_server.rs_resp_plen,
                  app_cfg->app_raw_server.rs_req_plen);
}

/*****************************************************************************
 * raw_tc_start()
 ****************************************************************************/
static void raw_tc_start(raw_storage_t *raw_storage, bool rx_tstamp,
                         bool tx_tstamp)
{
    int err;

    /* Initialize per test case timestamping configs. */
    err = raw_init_tstamp_cfg(raw_storage, rx_tstamp, tx_tstamp);
    if (err) {
        TPG_ERROR_ABORT("[%d:%s()] Failed to initialize RAW tstamp cfg. Error: %d(%s)\n",
                        rte_lcore_index(rte_lcore_id()), __func__,
                        -err, rte_strerror(-err));
    }
}

/*****************************************************************************
 * raw_client_tc_start()
 ****************************************************************************/
void raw_client_tc_start(const tpg_test_case_t *cfg __rte_unused,
                         const tpg_app_t *app_cfg,
                         app_storage_t *app_storage)
{
    const tpg_raw_client_t *client_cfg = &app_cfg->app_raw_client;

    raw_tc_start(&app_storage->ast_raw,
                 TPG_XLATE_OPT_BOOL(client_cfg, rc_rx_tstamp),
                 TPG_XLATE_OPT_BOOL(client_cfg, rc_tx_tstamp));
}

/*****************************************************************************
 * raw_server_tc_start()
 ****************************************************************************/
void raw_server_tc_start(const tpg_test_case_t *cfg __rte_unused,
                         const tpg_app_t *app_cfg,
                         app_storage_t *app_storage)
{
    const tpg_raw_server_t *server_cfg = &app_cfg->app_raw_server;

    raw_tc_start(&app_storage->ast_raw,
                 TPG_XLATE_OPT_BOOL(server_cfg, rs_rx_tstamp),
                 TPG_XLATE_OPT_BOOL(server_cfg, rs_tx_tstamp));
}

/*****************************************************************************
 * raw_tc_stop()
 ****************************************************************************/
void raw_tc_stop(const tpg_test_case_t *cfg __rte_unused,
                 const tpg_app_t *app_cfg __rte_unused,
                 app_storage_t *app_storage)
{
    /* Clear the timestamping configs. */
    raw_clear_tstamp_cfg(&app_storage->ast_raw);
}

/*****************************************************************************
 * raw_client_conn_up()
 ****************************************************************************/
void raw_client_conn_up(l4_control_block_t *l4, app_data_t *app_data,
                        tpg_app_stats_t *stats __rte_unused)
{
    raw_goto_state(&app_data->ad_raw, RAWS_SENDING,
                   app_data->ad_raw.ra_req_size);

    /* Might be that we just need to setup the connection and send no data.. */
    if (app_data->ad_raw.ra_req_size == 0)
        return;

    TEST_NOTIF(TEST_NOTIF_APP_SEND_START, l4);
}

/*****************************************************************************
 * raw_server_conn_up()
 ****************************************************************************/
void raw_server_conn_up(l4_control_block_t *l4 __rte_unused,
                        app_data_t *app_data,
                        tpg_app_stats_t *stats __rte_unused)
{
    raw_goto_state(&app_data->ad_raw, RAWS_RECEIVING,
                   app_data->ad_raw.ra_resp_size);
}

/*****************************************************************************
 * raw_conn_down()
 ****************************************************************************/
void raw_conn_down(l4_control_block_t *l4 __rte_unused,
                  app_data_t *app_data __rte_unused,
                  tpg_app_stats_t *stats __rte_unused)
{
    /* Normally we should go through the state machine but there's nothing to
     * cleanup for RAW connections.
     */
}

/*****************************************************************************
 * raw_deliver_data()
 ****************************************************************************/
static uint32_t
raw_deliver_data(l4_control_block_t *l4, raw_app_t *raw_app_data,
                 struct rte_mbuf *rx_data,
                 uint64_t rx_tstamp)
{
    uint32_t pkt_len = rx_data->pkt_len;

    if (unlikely(pkt_len > raw_app_data->ra_remaining_count))
        pkt_len = raw_app_data->ra_remaining_count;

    /* If we need to do RX timestamping and this is the beginning of an
     * incoming packet then check for latency info.
     */
    if (unlikely(raw_app_data->ra_rx_tstamp_size &&
            raw_app_data->ra_remaining_count == raw_app_data->ra_resp_size)) {

        if (likely(pkt_len >= raw_app_data->ra_rx_tstamp_size)) {
            raw_latency_data_t *latency_data;

            latency_data = rte_pktmbuf_mtod(rx_data, raw_latency_data_t *);

            /* If the latency magic is present then update latency stats. */
            if (likely(memcmp(&latency_data->rld_magic, &raw_latency_magic[0],
                       sizeof(latency_data->rld_magic)) == 0)) {
                uint64_t tx_tstamp;

                tx_tstamp =
                    TSTAMP_JOIN(rte_be_to_cpu_32(latency_data->rld_tstamp[1]),
                                rte_be_to_cpu_32(latency_data->rld_tstamp[0]));
                test_update_latency(l4, tx_tstamp, rx_tstamp);
            }
        } else {
            /* If we didn't receive enough for the RX tstamp data then ask
             * the stack to send the packet again when more data is available.
             */
            return 0;
        }
    }

    raw_app_data->ra_remaining_count -= pkt_len;

    return rx_data->pkt_len;
}

/*****************************************************************************
 * raw_client_deliver_data()
 ****************************************************************************/
uint32_t raw_client_deliver_data(l4_control_block_t *l4, app_data_t *app_data,
                                 tpg_app_stats_t *stats,
                                 struct rte_mbuf *rx_data,
                                 uint64_t rx_tstamp)
{
    tpg_raw_stats_t *raw_stats = &stats->as_raw;
    uint32_t         delivered;

    delivered = raw_deliver_data(l4, &app_data->ad_raw, rx_data, rx_tstamp);
    if (app_data->ad_raw.ra_remaining_count == 0) {
        INC_STATS(raw_stats, rsts_resp_cnt);

        if (app_data->ad_raw.ra_req_size != 0) {
            TEST_NOTIF(TEST_NOTIF_APP_SEND_START, l4);
            raw_goto_state(&app_data->ad_raw, RAWS_SENDING,
                           app_data->ad_raw.ra_req_size);
        } else {
            raw_goto_state(&app_data->ad_raw, RAWS_RECEIVING,
                           app_data->ad_raw.ra_resp_size);
        }
    }

    return delivered;
}

/*****************************************************************************
 * raw_server_deliver_data()
 ****************************************************************************/
uint32_t raw_server_deliver_data(l4_control_block_t *l4, app_data_t *app_data,
                                 tpg_app_stats_t *stats,
                                 struct rte_mbuf *rx_data,
                                 uint64_t rx_tstamp)
{
    tpg_raw_stats_t *raw_stats = &stats->as_raw;
    uint32_t         delivered;

    delivered = raw_deliver_data(l4, &app_data->ad_raw, rx_data, rx_tstamp);

    if (app_data->ad_raw.ra_remaining_count == 0) {
        INC_STATS(raw_stats, rsts_req_cnt);

        if (app_data->ad_raw.ra_req_size != 0) {
            TEST_NOTIF(TEST_NOTIF_APP_SEND_START, l4);
            raw_goto_state(&app_data->ad_raw, RAWS_SENDING,
                           app_data->ad_raw.ra_req_size);
        } else {
            raw_goto_state(&app_data->ad_raw, RAWS_RECEIVING,
                           app_data->ad_raw.ra_resp_size);
        }
    }

    return delivered;
}

/*****************************************************************************
 * raw_prepend_tstamp()
 ****************************************************************************/
static struct rte_mbuf *
raw_prepend_tstamp(l4_control_block_t *l4, raw_app_t *raw_app_data,
                   tpg_raw_stats_t *raw_stats,
                   struct rte_mbuf *tx_mbuf)
{
    if (unlikely(tx_mbuf == NULL))
        return NULL;

    /* Prefill the tstamp header (only if TX ts is enabled and if this is
     * the beginning of an outgoing request/response).
     */
    if (unlikely(raw_app_data->ra_tx_tstamp_size &&
            raw_app_data->ra_req_size == raw_app_data->ra_remaining_count)) {

        struct rte_mbuf    *tstamp_mbuf;
        raw_latency_data_t *latency_data = &raw_app_data->ra_tx_tstamp;
        ptrdiff_t           latency_offset = RTE_PTR_DIFF(latency_data, l4);

        /* In order for timestamping to work (and scale) we need to be able
         * to access the physical memory address of the TX timestamp header
         * which is deduced from the L4 control block physical address.
         * Fail if that can't be done.
         */
        if (unlikely(l4->l4cb_phys_addr == RTE_BAD_PHYS_ADDR)) {
            INC_STATS(raw_stats, rsts_tstamp_no_phys_addr);
            return tx_mbuf;
        }

        /* To simplify our lives if the send window is not big enough we just
         * skip the timestamp..
         */
        if (unlikely(tx_mbuf->pkt_len < sizeof(*latency_data))) {
            INC_STATS(raw_stats, rsts_tstamp_no_win);
            return tx_mbuf;
        }

        /* Build a clone mbuf pointing to the same memory as latency_data. */
        /* TODO: this might not work well in case RAW is ran on top of TCP and
         * response sizes are set to 0 (i.e., the ra_tx_tstamp/latency_data will
         * be referenced in multiple requests that are stored in the TCP send
         * window). This is a corner case though.
         */
        tstamp_mbuf =
            data_chain_from_static_template(sizeof(*latency_data),
                                            (uint8_t *)latency_data,
                                            l4->l4cb_phys_addr + latency_offset,
                                            sizeof(*latency_data));

        if (unlikely(tstamp_mbuf == NULL)) {
            INC_STATS(raw_stats, rsts_tstamp_no_mbuf);
            return tx_mbuf;
        }

        /* Mark the packet payload for timestamping lower in the stack.
         * The timestamp info is includes the magic so skip that part and
         * store as offset the uint32 array.
         */
        tstamp_tx_pkt(tstamp_mbuf,
                      RTE_PTR_DIFF(&latency_data->rld_tstamp[0], latency_data),
                      sizeof(latency_data->rld_tstamp));

        if (unlikely(tx_mbuf->pkt_len == tstamp_mbuf->pkt_len)) {
            pkt_mbuf_free(tx_mbuf);
            return tstamp_mbuf;
        }

        /* Remove a bit of the template data to accommodate the timestamp and
         * keep the correct payload length.
         */
        assert(rte_pktmbuf_trim(tx_mbuf, tstamp_mbuf->pkt_len) == 0);
        tstamp_mbuf->next = tx_mbuf;
        tstamp_mbuf->pkt_len += tx_mbuf->pkt_len;
        tstamp_mbuf->nb_segs += tx_mbuf->nb_segs;

        return tstamp_mbuf;
    }

    return tx_mbuf;
}

/*****************************************************************************
 * raw_send_data()
 ****************************************************************************/
struct rte_mbuf *raw_send_data(l4_control_block_t *l4,
                               app_data_t *app_data,
                               tpg_app_stats_t *stats,
                               uint32_t max_tx_size)
{
    struct rte_mbuf *template;
    struct rte_mbuf *tx_mbuf;
    uint32_t         to_send;
    uint8_t         *template_data;
    phys_addr_t      template_data_phys;

    to_send = TPG_MIN(app_data->ad_raw.ra_remaining_count, max_tx_size);

    template = RTE_PER_LCORE(raw_mbuf_template);
    template_data = rte_pktmbuf_mtod(template, uint8_t *);
    template_data_phys = rte_pktmbuf_mtophys(template);
    tx_mbuf = data_chain_from_static_template(to_send, template_data,
                                              template_data_phys,
                                              RAW_DATA_TEMPLATE_SIZE);

    return raw_prepend_tstamp(l4, &app_data->ad_raw, &stats->as_raw, tx_mbuf);
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
bool raw_client_data_sent(l4_control_block_t *l4, app_data_t *app_data,
                          tpg_app_stats_t *stats,
                          uint32_t bytes_sent)
{
    tpg_raw_stats_t *raw_stats = &stats->as_raw;

    raw_data_sent(app_data, bytes_sent);
    if (app_data->ad_raw.ra_remaining_count == 0) {
        INC_STATS(raw_stats, rsts_req_cnt);

        if (app_data->ad_raw.ra_resp_size != 0) {
            TEST_NOTIF(TEST_NOTIF_APP_SEND_STOP, l4);
            raw_goto_state(&app_data->ad_raw, RAWS_RECEIVING,
                           app_data->ad_raw.ra_resp_size);
        } else {
            TEST_NOTIF(TEST_NOTIF_APP_SEND_STOP, l4);
            TEST_NOTIF(TEST_NOTIF_APP_SEND_START, l4);
            raw_goto_state(&app_data->ad_raw, RAWS_SENDING,
                           app_data->ad_raw.ra_req_size);
        }

        return true;
    }

    return false;
}

/*****************************************************************************
 * raw_server_data_sent()
 ****************************************************************************/
bool raw_server_data_sent(l4_control_block_t *l4, app_data_t *app_data,
                          tpg_app_stats_t *stats,
                          uint32_t bytes_sent)
{
    tpg_raw_stats_t *raw_stats = &stats->as_raw;

    raw_data_sent(app_data, bytes_sent);
    if (app_data->ad_raw.ra_remaining_count == 0) {
        INC_STATS(raw_stats, rsts_resp_cnt);

        TEST_NOTIF(TEST_NOTIF_APP_SEND_STOP, l4);
        raw_goto_state(&app_data->ad_raw, RAWS_RECEIVING,
                       app_data->ad_raw.ra_resp_size);
        return true;
    }

    return false;
}

/*****************************************************************************
 * raw_stats_init()
 ****************************************************************************/
void raw_stats_init(const tpg_app_t *app_cfg __rte_unused,
                    tpg_app_stats_t *stats)
{
    bzero(stats, sizeof(*stats));
}

/*****************************************************************************
 * raw_stats_copy()
 ****************************************************************************/
void raw_stats_copy(tpg_app_stats_t *dest, const tpg_app_stats_t *src)
{
    *dest = *src;
}

/*****************************************************************************
 * raw_stats_add()
 ****************************************************************************/
void raw_stats_add(tpg_app_stats_t *total, const tpg_app_stats_t *elem)
{
    total->as_raw.rsts_req_cnt += elem->as_raw.rsts_req_cnt;
    total->as_raw.rsts_resp_cnt += elem->as_raw.rsts_resp_cnt;

    total->as_raw.rsts_tstamp_no_phys_addr +=
        elem->as_raw.rsts_tstamp_no_phys_addr;
    total->as_raw.rsts_tstamp_no_mbuf +=
        elem->as_raw.rsts_tstamp_no_mbuf;
    total->as_raw.rsts_tstamp_no_win +=
        elem->as_raw.rsts_tstamp_no_win;
}

/*****************************************************************************
 * raw_stats_print()
 ****************************************************************************/
void raw_stats_print(const tpg_app_stats_t *stats, printer_arg_t *printer_arg)
{
    tpg_printf(printer_arg, "%13s %13s %13s %13s %13s\n",
               "Requests", "Replies", "TsNoPhys", "TsNoMbuf", "TsNoWin");
    tpg_printf(printer_arg,
               "%13"PRIu64 " %13"PRIu64 " %13"PRIu32 " %13"PRIu32 " %13"PRIu32
               "\n",
               stats->as_raw.rsts_req_cnt,
               stats->as_raw.rsts_resp_cnt,
               stats->as_raw.rsts_tstamp_no_phys_addr,
               stats->as_raw.rsts_tstamp_no_mbuf,
               stats->as_raw.rsts_tstamp_no_win);
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
    struct rte_mbuf *msg_mbuf;

    RTE_PER_LCORE(raw_mbuf_template) =
        pkt_mbuf_alloc(mem_get_mbuf_local_pool());
    if (RTE_PER_LCORE(raw_mbuf_template) == NULL) {
        TPG_ERROR_ABORT("ERROR: Failed to allocate RAW template!\n");
        return;
    }

    msg_mbuf = RTE_PER_LCORE(raw_mbuf_template);

    if (msg_mbuf->buf_len < RAW_DATA_TEMPLATE_SIZE)
        TPG_ERROR_ABORT("ERROR: %s!\n",
                        "RAW template doesn't fit in a single segment");

    /* Initialize the template with some random values. */
    memset(rte_pktmbuf_mtod(msg_mbuf, uint8_t *), 42, RAW_DATA_TEMPLATE_SIZE);
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
    cmdline_fixed_string_t imix;
    cmdline_fixed_string_t client;
    cmdline_fixed_string_t raw;
    cmdline_fixed_string_t port_kw;
    uint32_t               port;
    cmdline_fixed_string_t tcid_kw;
    uint32_t               tcid;
    cmdline_fixed_string_t app_index_kw;
    uint32_t               app_index;

    cmdline_fixed_string_t data_req_kw;
    uint32_t               data_req_plen;

    cmdline_fixed_string_t data_resp_kw;
    uint32_t               data_resp_plen;

    cmdline_fixed_string_t rx_tstamp_kw;
    cmdline_fixed_string_t tx_tstamp_kw;
};

static cmdline_parse_token_string_t cmd_tests_set_app_raw_client_T_set =
    TOKEN_STRING_INITIALIZER(struct cmd_tests_set_app_raw_client_result, set, "set");
static cmdline_parse_token_string_t cmd_tests_set_app_raw_client_T_tests =
    TOKEN_STRING_INITIALIZER(struct cmd_tests_set_app_raw_client_result, tests, "tests");
static cmdline_parse_token_string_t cmd_tests_set_app_raw_client_T_imix =
    TOKEN_STRING_INITIALIZER(struct cmd_tests_set_app_raw_client_result, imix, "imix");
static cmdline_parse_token_string_t cmd_tests_set_app_raw_client_T_client =
    TOKEN_STRING_INITIALIZER(struct cmd_tests_set_app_raw_client_result, client,
                             TEST_CASE_CLIENT_CLI_STR);
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

static cmdline_parse_token_string_t cmd_tests_set_app_raw_client_T_app_index_kw =
    TOKEN_STRING_INITIALIZER(struct cmd_tests_set_app_raw_client_result, app_index_kw, "app-index");
static cmdline_parse_token_num_t cmd_tests_set_app_raw_client_T_app_index =
    TOKEN_NUM_INITIALIZER(struct cmd_tests_set_app_raw_client_result, app_index, UINT32);

static cmdline_parse_token_string_t cmd_tests_set_app_raw_client_T_data_req_kw =
    TOKEN_STRING_INITIALIZER(struct cmd_tests_set_app_raw_client_result, data_req_kw, "data-req-plen");
static cmdline_parse_token_num_t cmd_tests_set_app_raw_client_T_data_req_plen =
    TOKEN_NUM_INITIALIZER(struct cmd_tests_set_app_raw_client_result, data_req_plen, UINT32);

static cmdline_parse_token_string_t cmd_tests_set_app_raw_client_T_data_resp_kw =
    TOKEN_STRING_INITIALIZER(struct cmd_tests_set_app_raw_client_result, data_resp_kw, "data-resp-plen");
static cmdline_parse_token_num_t cmd_tests_set_app_raw_client_T_data_resp_plen =
    TOKEN_NUM_INITIALIZER(struct cmd_tests_set_app_raw_client_result, data_resp_plen, UINT32);

static cmdline_parse_token_string_t cmd_tests_set_app_raw_client_T_rx_tstamp_kw =
    TOKEN_STRING_INITIALIZER(struct cmd_tests_set_app_raw_client_result, rx_tstamp_kw, "rx-timestamp");
static cmdline_parse_token_string_t cmd_tests_set_app_raw_client_T_tx_tstamp_kw =
    TOKEN_STRING_INITIALIZER(struct cmd_tests_set_app_raw_client_result, tx_tstamp_kw, "tx-timestamp");

static void cmd_tests_set_app_raw_client_parsed(void *parsed_result,
                                                struct cmdline *cl,
                                                void *data)
{
    printer_arg_t    parg;
    tpg_app_t        app_cfg;
    int              err;
    raw_flags_type_t flags;

    struct cmd_tests_set_app_raw_client_result *pr;

    parg = TPG_PRINTER_ARG(cli_printer, cl);
    pr = parsed_result;
    flags = (raw_flags_type_t)(uintptr_t)data;

    bzero(&app_cfg, sizeof(app_cfg));

    app_cfg.app_proto = APP_PROTO__RAW_CLIENT;
    app_cfg.app_raw_client.rc_req_plen = pr->data_req_plen;
    app_cfg.app_raw_client.rc_resp_plen = pr->data_resp_plen;

    if (RAW_TSTAMP_ISSET(flags, RAW_FLAG_TSTAMP_RX)) {
        TPG_XLATE_OPTIONAL_SET_FIELD(&app_cfg.app_raw_client, rc_rx_tstamp,
                                     true);
    }

    if (RAW_TSTAMP_ISSET(flags, RAW_FLAG_TSTAMP_TX)) {
        TPG_XLATE_OPTIONAL_SET_FIELD(&app_cfg.app_raw_client, rc_tx_tstamp,
                                     true);
    }

    if (!RAW_IMIX_ISSET(flags)) {
        err = test_mgmt_update_test_case_app(pr->port, pr->tcid, &app_cfg,
                                             &parg);
        if (err == 0) {
            cmdline_printf(cl, "Port %"PRIu32", Test Case %"PRIu32" updated!\n",
                           pr->port,
                           pr->tcid);
        } else {
            cmdline_printf(cl, "ERROR: Failed updating test case %"PRIu32
                           " config on port %"PRIu32"\n",
                           pr->tcid,
                           pr->port);
        }
    } else {
        err = imix_cli_set_app_cfg(pr->app_index, &app_cfg, &parg);

        if (err == 0) {
            cmdline_printf(cl, "IMIX app-index %"PRIu32" updated!\n",
                           pr->app_index);
        } else {
            cmdline_printf(cl,
                           "ERROR: Failed updating IMIX app-index %"PRIu32"!\n",
                           pr->app_index);
        }
    }
}

cmdline_parse_inst_t cmd_tests_set_app_raw_client = {
    .f = cmd_tests_set_app_raw_client_parsed,
    .data = (void *)(uintptr_t)RAW_FLAG_TSTAMP_NONE,
    .help_str = "set tests client raw port <port> test-case-id <tcid> "
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

cmdline_parse_inst_t cmd_tests_set_app_raw_client_imix = {
    .f = cmd_tests_set_app_raw_client_parsed,
    .data = (void *)(uintptr_t)RAW_FLAG_IMIX,
    .help_str = "set tests imix app-index <app-index> client raw "
                "data-req-plen <len> data-resp-plen <len>",
    .tokens = {
        (void *)&cmd_tests_set_app_raw_client_T_set,
        (void *)&cmd_tests_set_app_raw_client_T_tests,
        (void *)&cmd_tests_set_app_raw_client_T_imix,
        (void *)&cmd_tests_set_app_raw_client_T_app_index_kw,
        (void *)&cmd_tests_set_app_raw_client_T_app_index,
        (void *)&cmd_tests_set_app_raw_client_T_client,
        (void *)&cmd_tests_set_app_raw_client_T_raw,
        (void *)&cmd_tests_set_app_raw_client_T_data_req_kw,
        (void *)&cmd_tests_set_app_raw_client_T_data_req_plen,
        (void *)&cmd_tests_set_app_raw_client_T_data_resp_kw,
        (void *)&cmd_tests_set_app_raw_client_T_data_resp_plen,
        NULL,
    },
};

cmdline_parse_inst_t cmd_tests_set_app_raw_client_rx_tstamp = {
    .f = cmd_tests_set_app_raw_client_parsed,
    .data = (void *)(uintptr_t)RAW_FLAG_TSTAMP_RX,
    .help_str = "set tests client raw port <port> test-case-id <tcid> "
                "data-req-plen <len> data-resp-plen <len> rx-timestamp",
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
        (void *)&cmd_tests_set_app_raw_client_T_rx_tstamp_kw,
        NULL,
    },
};

cmdline_parse_inst_t cmd_tests_set_app_raw_client_rx_tstamp_imix = {
    .f = cmd_tests_set_app_raw_client_parsed,
    .data = (void *)(uintptr_t)(RAW_FLAG_TSTAMP_RX | RAW_FLAG_IMIX),
    .help_str = "set tests imix app-index <app-index> client raw "
                "data-req-plen <len> data-resp-plen <len> rx-timestamp",
    .tokens = {
        (void *)&cmd_tests_set_app_raw_client_T_set,
        (void *)&cmd_tests_set_app_raw_client_T_tests,
        (void *)&cmd_tests_set_app_raw_client_T_imix,
        (void *)&cmd_tests_set_app_raw_client_T_app_index_kw,
        (void *)&cmd_tests_set_app_raw_client_T_app_index,
        (void *)&cmd_tests_set_app_raw_client_T_client,
        (void *)&cmd_tests_set_app_raw_client_T_raw,
        (void *)&cmd_tests_set_app_raw_client_T_data_req_kw,
        (void *)&cmd_tests_set_app_raw_client_T_data_req_plen,
        (void *)&cmd_tests_set_app_raw_client_T_data_resp_kw,
        (void *)&cmd_tests_set_app_raw_client_T_data_resp_plen,
        (void *)&cmd_tests_set_app_raw_client_T_rx_tstamp_kw,
        NULL,
    },
};

cmdline_parse_inst_t cmd_tests_set_app_raw_client_tx_tstamp = {
    .f = cmd_tests_set_app_raw_client_parsed,
    .data = (void *)(uintptr_t)RAW_FLAG_TSTAMP_TX,
    .help_str = "set tests client raw port <port> test-case-id <tcid> "
                "data-req-plen <len> data-resp-plen <len> tx-timestamp",
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
        (void *)&cmd_tests_set_app_raw_client_T_tx_tstamp_kw,
        NULL,
    },
};

cmdline_parse_inst_t cmd_tests_set_app_raw_client_tx_tstamp_imix = {
    .f = cmd_tests_set_app_raw_client_parsed,
    .data = (void *)(uintptr_t)(RAW_FLAG_TSTAMP_TX | RAW_FLAG_IMIX),
    .help_str = "set tests imix app-index <app-index> client raw "
                "data-req-plen <len> data-resp-plen <len> tx-timestamp",
    .tokens = {
        (void *)&cmd_tests_set_app_raw_client_T_set,
        (void *)&cmd_tests_set_app_raw_client_T_tests,
        (void *)&cmd_tests_set_app_raw_client_T_imix,
        (void *)&cmd_tests_set_app_raw_client_T_app_index_kw,
        (void *)&cmd_tests_set_app_raw_client_T_app_index,
        (void *)&cmd_tests_set_app_raw_client_T_client,
        (void *)&cmd_tests_set_app_raw_client_T_raw,
        (void *)&cmd_tests_set_app_raw_client_T_data_req_kw,
        (void *)&cmd_tests_set_app_raw_client_T_data_req_plen,
        (void *)&cmd_tests_set_app_raw_client_T_data_resp_kw,
        (void *)&cmd_tests_set_app_raw_client_T_data_resp_plen,
        (void *)&cmd_tests_set_app_raw_client_T_tx_tstamp_kw,
        NULL,
    },
};

cmdline_parse_inst_t cmd_tests_set_app_raw_client_rxtx_tstamp = {
    .f = cmd_tests_set_app_raw_client_parsed,
    .data = (void *)(uintptr_t)RAW_FLAG_TSTAMP_RXTX,
    .help_str = "set tests client raw port <port> test-case-id <tcid> "
                "data-req-plen <len> data-resp-plen <len> rx-timestamp tx-timestamp",
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
        (void *)&cmd_tests_set_app_raw_client_T_rx_tstamp_kw,
        (void *)&cmd_tests_set_app_raw_client_T_tx_tstamp_kw,
        NULL,
    },
};

cmdline_parse_inst_t cmd_tests_set_app_raw_client_rxtx_tstamp_imix = {
    .f = cmd_tests_set_app_raw_client_parsed,
    .data = (void *)(uintptr_t)(RAW_FLAG_TSTAMP_RXTX | RAW_FLAG_IMIX),
    .help_str = "set tests imix app-index <app-index> client raw "
                "data-req-plen <len> data-resp-plen <len> rx-timestamp tx-timestamp",
    .tokens = {
        (void *)&cmd_tests_set_app_raw_client_T_set,
        (void *)&cmd_tests_set_app_raw_client_T_tests,
        (void *)&cmd_tests_set_app_raw_client_T_imix,
        (void *)&cmd_tests_set_app_raw_client_T_app_index_kw,
        (void *)&cmd_tests_set_app_raw_client_T_app_index,
        (void *)&cmd_tests_set_app_raw_client_T_client,
        (void *)&cmd_tests_set_app_raw_client_T_raw,
        (void *)&cmd_tests_set_app_raw_client_T_data_req_kw,
        (void *)&cmd_tests_set_app_raw_client_T_data_req_plen,
        (void *)&cmd_tests_set_app_raw_client_T_data_resp_kw,
        (void *)&cmd_tests_set_app_raw_client_T_data_resp_plen,
        (void *)&cmd_tests_set_app_raw_client_T_rx_tstamp_kw,
        (void *)&cmd_tests_set_app_raw_client_T_tx_tstamp_kw,
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
    cmdline_fixed_string_t imix;
    cmdline_fixed_string_t server;
    cmdline_fixed_string_t raw;
    cmdline_fixed_string_t port_kw;
    uint32_t               port;
    cmdline_fixed_string_t tcid_kw;
    uint32_t               tcid;
    cmdline_fixed_string_t app_index_kw;
    uint32_t               app_index;

    cmdline_fixed_string_t data_req_kw;
    uint32_t               data_req_plen;

    cmdline_fixed_string_t data_resp_kw;
    uint32_t               data_resp_plen;

    cmdline_fixed_string_t rx_tstamp_kw;
    cmdline_fixed_string_t tx_tstamp_kw;
};

static cmdline_parse_token_string_t cmd_tests_set_app_raw_server_T_set =
    TOKEN_STRING_INITIALIZER(struct cmd_tests_set_app_raw_server_result, set, "set");
static cmdline_parse_token_string_t cmd_tests_set_app_raw_server_T_tests =
    TOKEN_STRING_INITIALIZER(struct cmd_tests_set_app_raw_server_result, tests, "tests");
static cmdline_parse_token_string_t cmd_tests_set_app_raw_server_T_imix =
    TOKEN_STRING_INITIALIZER(struct cmd_tests_set_app_raw_server_result, imix, "imix");
static cmdline_parse_token_string_t cmd_tests_set_app_raw_server_T_server =
    TOKEN_STRING_INITIALIZER(struct cmd_tests_set_app_raw_server_result, server,
                             TEST_CASE_SERVER_CLI_STR);
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

static cmdline_parse_token_string_t cmd_tests_set_app_raw_server_T_app_index_kw =
    TOKEN_STRING_INITIALIZER(struct cmd_tests_set_app_raw_server_result, app_index_kw, "app-index");
static cmdline_parse_token_num_t cmd_tests_set_app_raw_server_T_app_index =
    TOKEN_NUM_INITIALIZER(struct cmd_tests_set_app_raw_server_result, app_index, UINT32);

static cmdline_parse_token_string_t cmd_tests_set_app_raw_server_T_data_req_kw =
    TOKEN_STRING_INITIALIZER(struct cmd_tests_set_app_raw_server_result, data_req_kw, "data-req-plen");
static cmdline_parse_token_num_t cmd_tests_set_app_raw_server_T_data_req_plen =
    TOKEN_NUM_INITIALIZER(struct cmd_tests_set_app_raw_server_result, data_req_plen, UINT32);

static cmdline_parse_token_string_t cmd_tests_set_app_raw_server_T_data_resp_kw =
    TOKEN_STRING_INITIALIZER(struct cmd_tests_set_app_raw_server_result, data_resp_kw, "data-resp-plen");
static cmdline_parse_token_num_t cmd_tests_set_app_raw_server_T_data_resp_plen =
    TOKEN_NUM_INITIALIZER(struct cmd_tests_set_app_raw_server_result, data_resp_plen, UINT32);

static cmdline_parse_token_string_t cmd_tests_set_app_raw_server_T_rx_tstamp_kw =
    TOKEN_STRING_INITIALIZER(struct cmd_tests_set_app_raw_server_result, rx_tstamp_kw, "rx-timestamp");
static cmdline_parse_token_string_t cmd_tests_set_app_raw_server_T_tx_tstamp_kw =
    TOKEN_STRING_INITIALIZER(struct cmd_tests_set_app_raw_server_result, tx_tstamp_kw, "tx-timestamp");

static void cmd_tests_set_app_raw_server_parsed(void *parsed_result,
                                                struct cmdline *cl,
                                                void *data)
{
    printer_arg_t    parg;
    tpg_app_t        app_cfg;
    int              err;
    raw_flags_type_t flags;

    struct cmd_tests_set_app_raw_server_result *pr;

    parg = TPG_PRINTER_ARG(cli_printer, cl);
    pr = parsed_result;
    flags = (raw_flags_type_t)(uintptr_t)data;

    bzero(&app_cfg, sizeof(app_cfg));

    app_cfg.app_proto = APP_PROTO__RAW_SERVER;
    app_cfg.app_raw_server.rs_req_plen = pr->data_req_plen;
    app_cfg.app_raw_server.rs_resp_plen = pr->data_resp_plen;

    if (RAW_TSTAMP_ISSET(flags, RAW_FLAG_TSTAMP_RX)) {
        TPG_XLATE_OPTIONAL_SET_FIELD(&app_cfg.app_raw_server, rs_rx_tstamp,
                                     true);
    }

    if (RAW_TSTAMP_ISSET(flags, RAW_FLAG_TSTAMP_TX)) {
        TPG_XLATE_OPTIONAL_SET_FIELD(&app_cfg.app_raw_server, rs_tx_tstamp,
                                     true);
    }

    if (!RAW_IMIX_ISSET(flags)) {
        err = test_mgmt_update_test_case_app(pr->port, pr->tcid, &app_cfg,
                                             &parg);
        if (err == 0) {
            cmdline_printf(cl, "Port %"PRIu32", Test Case %"PRIu32" updated!\n",
                           pr->port,
                           pr->tcid);
        } else {
            cmdline_printf(cl, "ERROR: Failed updating test case %"PRIu32
                           " config on port %"PRIu32"\n",
                           pr->tcid,
                           pr->port);
        }
    } else {
        err = imix_cli_set_app_cfg(pr->app_index, &app_cfg, &parg);

        if (err == 0) {
            cmdline_printf(cl, "IMIX app-index %"PRIu32" updated!\n",
                           pr->app_index);
        } else {
            cmdline_printf(cl,
                           "ERROR: Failed updating IMIX app-index %"PRIu32"!\n",
                           pr->app_index);
        }
    }
}

cmdline_parse_inst_t cmd_tests_set_app_raw_server = {
    .f = cmd_tests_set_app_raw_server_parsed,
    .data = (void *)(uintptr_t)RAW_FLAG_TSTAMP_NONE,
    .help_str = "set tests server raw port <port> test-case-id <tcid> "
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

cmdline_parse_inst_t cmd_tests_set_app_raw_server_imix = {
    .f = cmd_tests_set_app_raw_server_parsed,
    .data = (void *)(uintptr_t)RAW_FLAG_IMIX,
    .help_str = "set tests imix app-index <app-index> server raw "
                "data-req-plen <len> data-resp-plen <len>",
    .tokens = {
        (void *)&cmd_tests_set_app_raw_server_T_set,
        (void *)&cmd_tests_set_app_raw_server_T_tests,
        (void *)&cmd_tests_set_app_raw_server_T_imix,
        (void *)&cmd_tests_set_app_raw_server_T_app_index_kw,
        (void *)&cmd_tests_set_app_raw_server_T_app_index,
        (void *)&cmd_tests_set_app_raw_server_T_server,
        (void *)&cmd_tests_set_app_raw_server_T_raw,
        (void *)&cmd_tests_set_app_raw_server_T_data_req_kw,
        (void *)&cmd_tests_set_app_raw_server_T_data_req_plen,
        (void *)&cmd_tests_set_app_raw_server_T_data_resp_kw,
        (void *)&cmd_tests_set_app_raw_server_T_data_resp_plen,
        NULL,
    },
};

cmdline_parse_inst_t cmd_tests_set_app_raw_server_rx_tstamp = {
    .f = cmd_tests_set_app_raw_server_parsed,
    .data = (void *)(uintptr_t)RAW_FLAG_TSTAMP_RX,
    .help_str = "set tests server raw port <port> test-case-id <tcid> "
                "data-req-plen <len> data-resp-plen <len> rx-timestamp",
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
        (void *)&cmd_tests_set_app_raw_server_T_rx_tstamp_kw,
        NULL,
    },
};

cmdline_parse_inst_t cmd_tests_set_app_raw_server_rx_tstamp_imix = {
    .f = cmd_tests_set_app_raw_server_parsed,
    .data = (void *)(uintptr_t)(RAW_FLAG_TSTAMP_RX | RAW_FLAG_IMIX),
    .help_str = "set tests imix app-index <app-index> server raw "
                "data-req-plen <len> data-resp-plen <len> rx-timestamp",
    .tokens = {
        (void *)&cmd_tests_set_app_raw_server_T_set,
        (void *)&cmd_tests_set_app_raw_server_T_tests,
        (void *)&cmd_tests_set_app_raw_server_T_imix,
        (void *)&cmd_tests_set_app_raw_server_T_app_index_kw,
        (void *)&cmd_tests_set_app_raw_server_T_app_index,
        (void *)&cmd_tests_set_app_raw_server_T_server,
        (void *)&cmd_tests_set_app_raw_server_T_raw,
        (void *)&cmd_tests_set_app_raw_server_T_data_req_kw,
        (void *)&cmd_tests_set_app_raw_server_T_data_req_plen,
        (void *)&cmd_tests_set_app_raw_server_T_data_resp_kw,
        (void *)&cmd_tests_set_app_raw_server_T_data_resp_plen,
        (void *)&cmd_tests_set_app_raw_server_T_rx_tstamp_kw,
        NULL,
    },
};

cmdline_parse_inst_t cmd_tests_set_app_raw_server_tx_tstamp = {
    .f = cmd_tests_set_app_raw_server_parsed,
    .data = (void *)(uintptr_t)RAW_FLAG_TSTAMP_TX,
    .help_str = "set tests server raw port <port> test-case-id <tcid> "
                "data-req-plen <len> data-resp-plen <len> tx-timestamp",
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
        (void *)&cmd_tests_set_app_raw_server_T_tx_tstamp_kw,
        NULL,
    },
};

cmdline_parse_inst_t cmd_tests_set_app_raw_server_tx_tstamp_imix = {
    .f = cmd_tests_set_app_raw_server_parsed,
    .data = (void *)(uintptr_t)(RAW_FLAG_TSTAMP_TX | RAW_FLAG_IMIX),
    .help_str = "set tests imix app-index <app-index> server raw "
                "data-req-plen <len> data-resp-plen <len> tx-timestamp",
    .tokens = {
        (void *)&cmd_tests_set_app_raw_server_T_set,
        (void *)&cmd_tests_set_app_raw_server_T_tests,
        (void *)&cmd_tests_set_app_raw_server_T_imix,
        (void *)&cmd_tests_set_app_raw_server_T_app_index_kw,
        (void *)&cmd_tests_set_app_raw_server_T_app_index,
        (void *)&cmd_tests_set_app_raw_server_T_server,
        (void *)&cmd_tests_set_app_raw_server_T_raw,
        (void *)&cmd_tests_set_app_raw_server_T_data_req_kw,
        (void *)&cmd_tests_set_app_raw_server_T_data_req_plen,
        (void *)&cmd_tests_set_app_raw_server_T_data_resp_kw,
        (void *)&cmd_tests_set_app_raw_server_T_data_resp_plen,
        (void *)&cmd_tests_set_app_raw_server_T_tx_tstamp_kw,
        NULL,
    },
};

cmdline_parse_inst_t cmd_tests_set_app_raw_server_rxtx_tstamp = {
    .f = cmd_tests_set_app_raw_server_parsed,
    .data = (void *)(uintptr_t)RAW_FLAG_TSTAMP_RXTX,
    .help_str = "set tests server raw port <port> test-case-id <tcid>"
                "data-req-plen <len> data-resp-plen <len> rx-timestamp tx-timestamp",
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
        (void *)&cmd_tests_set_app_raw_server_T_rx_tstamp_kw,
        (void *)&cmd_tests_set_app_raw_server_T_tx_tstamp_kw,
        NULL,
    },
};

cmdline_parse_inst_t cmd_tests_set_app_raw_server_rxtx_tstamp_imix = {
    .f = cmd_tests_set_app_raw_server_parsed,
    .data = (void *)(uintptr_t)(RAW_FLAG_TSTAMP_RXTX | RAW_FLAG_IMIX),
    .help_str = "set tests imix app-index <app-index> server raw "
                "data-req-plen <len> data-resp-plen <len> rx-timestamp tx-timestamp",
    .tokens = {
        (void *)&cmd_tests_set_app_raw_server_T_set,
        (void *)&cmd_tests_set_app_raw_server_T_tests,
        (void *)&cmd_tests_set_app_raw_server_T_imix,
        (void *)&cmd_tests_set_app_raw_server_T_app_index_kw,
        (void *)&cmd_tests_set_app_raw_server_T_app_index,
        (void *)&cmd_tests_set_app_raw_server_T_server,
        (void *)&cmd_tests_set_app_raw_server_T_raw,
        (void *)&cmd_tests_set_app_raw_server_T_data_req_kw,
        (void *)&cmd_tests_set_app_raw_server_T_data_req_plen,
        (void *)&cmd_tests_set_app_raw_server_T_data_resp_kw,
        (void *)&cmd_tests_set_app_raw_server_T_data_resp_plen,
        (void *)&cmd_tests_set_app_raw_server_T_rx_tstamp_kw,
        (void *)&cmd_tests_set_app_raw_server_T_tx_tstamp_kw,
        NULL,
    },
};

/*****************************************************************************
 * Main menu context
 ****************************************************************************/
static cmdline_parse_ctx_t cli_ctx[] = {
    &cmd_tests_set_app_raw_client,
    &cmd_tests_set_app_raw_client_imix,
    &cmd_tests_set_app_raw_client_rx_tstamp,
    &cmd_tests_set_app_raw_client_rx_tstamp_imix,
    &cmd_tests_set_app_raw_client_tx_tstamp,
    &cmd_tests_set_app_raw_client_tx_tstamp_imix,
    &cmd_tests_set_app_raw_client_rxtx_tstamp,
    &cmd_tests_set_app_raw_client_rxtx_tstamp_imix,
    &cmd_tests_set_app_raw_server,
    &cmd_tests_set_app_raw_server_imix,
    &cmd_tests_set_app_raw_server_rx_tstamp,
    &cmd_tests_set_app_raw_server_rx_tstamp_imix,
    &cmd_tests_set_app_raw_server_tx_tstamp,
    &cmd_tests_set_app_raw_server_tx_tstamp_imix,
    &cmd_tests_set_app_raw_server_rxtx_tstamp,
    &cmd_tests_set_app_raw_server_rxtx_tstamp_imix,
    NULL,
};

