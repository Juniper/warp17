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
 *     tpg_port.c
 *
 * Description:
 *     Handle setting up the physical Ethernet ports and create the desired
 *     receive and transmit queues.
 *
 * Author:
 *     Dumitru Ceara, Eelco Chaudron
 *
 * Initial Created:
 *     02/27/2015
 *
 * Notes:
 *
 */

/*****************************************************************************
 * Include files
 ****************************************************************************/
#include <unistd.h>

#include <rte_cycles.h>

#include "tcp_generator.h"

/*****************************************************************************
 * Global variables
 ****************************************************************************/
/* Define PORT global statistics. Each thread has its own set of locally
 * allocated stats which are accessible through STATS_GLOBAL(type, core, port).
 */
STATS_DEFINE(port_statistics_t);

port_port_cfg_t          *port_port_cfg; /* Array of [port] holding the ports core config */
port_core_cfg_t          *port_core_cfg; /* Array of [core] holding the q mappings. */
port_info_t              *port_dev_info; /* Array of [port] holding the port info. */

static link_rate_statistics_t *link_rate_statistics; /* Array of [port] */

/*****************************************************************************
 * For our application the hash key length of 40 bytes is more than
 * enough, even if we would support IPv6 (128+128+16+16 = 36 bytes),
 * however newer hardware requires more bit to initialize correctly.
 * So for now we just duplicate the key, and assume this is enough for
 * all hardware we support (if not we will panic ;)
 ****************************************************************************/
static uint8_t port_rss_key[] = {
 0x6d, 0x5a, 0x56, 0xda, 0x25, 0x5b, 0x0e, 0xc2,
 0x41, 0x67, 0x25, 0x3d, 0x43, 0xa3, 0x8f, 0xb0,
 0xd0, 0xca, 0x2b, 0xcb, 0xae, 0x7b, 0x30, 0xb4,
 0x77, 0xcb, 0x2d, 0xa3, 0x80, 0x30, 0xf2, 0x0c,
 0x6a, 0x42, 0xb7, 0x3b, 0xbe, 0xac, 0x01, 0xfa,

 0x6d, 0x5a, 0x56, 0xda, 0x25, 0x5b, 0x0e, 0xc2,
 0x41, 0x67, 0x25, 0x3d, 0x43, 0xa3, 0x8f, 0xb0,
 0xd0, 0xca, 0x2b, 0xcb, 0xae, 0x7b, 0x30, 0xb4,
 0x77, 0xcb, 0x2d, 0xa3, 0x80, 0x30, 0xf2, 0x0c,
 0x6a, 0x42, 0xb7, 0x3b, 0xbe, 0xac, 0x01, 0xfa,
};

/*****************************************************************************
 * port_get_global_rss_key()
 ****************************************************************************/
int port_get_global_rss_key(uint8_t ** const rss_key)
{
    if (rss_key == NULL)
        return 0;

    *rss_key = port_rss_key;
    return sizeof(port_rss_key);
}

/*****************************************************************************
 * port_adjust_reta()
 *  RETA size may be 0 in case we're running on a VF (e.g: for Intel 82599 10G)
 *  In that case make sure the user allocated only one core for that port.
 ****************************************************************************/
static bool port_adjust_reta(uint32_t port)
{
    /* Adjust reta_size. RETA size may be 0 in case we're running on a VF.
     * e.g: for Intel 82599 10G.
     */
    if (port_dev_info[port].pi_dev_info.reta_size) {
        port_dev_info[port].pi_adjusted_reta_size =
            port_dev_info[port].pi_dev_info.reta_size;
    } else {
        if (PORT_QCNT(port) > 1) {
            RTE_LOG(ERR, USER1, "ERROR: Detected reta_size == 0 "
                    "for port %"PRIu32"! "
                    "Are you running in a VM? Please allocate at "
                    "most one core per port!\n",
                    port);
            return false;
        }
        port_dev_info[port].pi_adjusted_reta_size = 1;
    }

    return true;
}

/*****************************************************************************
 * port_setup_reta_table()
 ****************************************************************************/
static void port_setup_reta_table(uint8_t port, int nr_of_queus)
{
    /*
     * We setup the HW's reta table in a round robbin fasion, i.e. if we
     * have 4 queue's in use it will be: 0 1 2 3 0 1 2 3 0 1 2 3 etc. etc.
     */
    int                              idx, shift, entry, ret;
    struct rte_eth_rss_reta_entry64 *reta_data;
    struct rte_eth_dev_info          dev_info;

    rte_eth_dev_info_get(port, &dev_info);

    if (dev_info.reta_size == 0)
        return;

    reta_data = rte_zmalloc("tmp_reta",
                            TPG_DIV_UP(dev_info.reta_size, RTE_RETA_GROUP_SIZE) *
                            sizeof(struct rte_eth_rss_reta_entry64),
                            0);

    for (entry = 0; entry < dev_info.reta_size; entry++) {
        idx = entry / RTE_RETA_GROUP_SIZE;
        shift = entry % RTE_RETA_GROUP_SIZE;
        reta_data[idx].mask |= (1ULL << shift);
        reta_data[idx].reta[shift] = entry % nr_of_queus;
    }

    ret = rte_eth_dev_rss_reta_update(port, reta_data, dev_info.reta_size);
    if (ret != 0) {
        TPG_ERROR_ABORT("Can't set RSS reta table for ethernet port %d, %d!\n",
                        port, ret);
    }

    rte_free(reta_data);
}

/*****************************************************************************
 * port_setup_port
 ****************************************************************************/
static bool port_setup_port(uint8_t port)
{
    int               rc;
    int               queue;
    uint16_t          number_of_rings;
    struct ether_addr mac_addr;

    struct rte_eth_conf default_port_config = {
        .rxmode = {
            .mq_mode        = ETH_MQ_RX_RSS,
            .max_rx_pkt_len = GCFG_MBUF_PACKET_FRAGMENT_SIZE,
            .split_hdr_size = 0,
            .header_split   = 0, /**< Header Split disabled */
            .hw_ip_checksum = 1, /**< IP checksum offload enabled */
            .hw_vlan_filter = 0, /**< VLAN filtering disabled */
            .jumbo_frame    = 0, /**< Jumbo Frame Support disabled */
            .hw_strip_crc   = 0, /**< CRC stripped by hardware */
        },
        .rx_adv_conf = {
            .rss_conf = {
                .rss_key = port_rss_key,
                .rss_key_len = sizeof(port_rss_key),
                .rss_hf = ETH_RSS_IPV4 | ETH_RSS_NONFRAG_IPV4_TCP | ETH_RSS_NONFRAG_IPV4_UDP,
            },
        },
        .txmode = {
            .mq_mode = ETH_MQ_TX_NONE,
        }
    };

    /* TODO: investigate what values we actually need? Can we make rx_conf and
     * tx_conf device independent?
     */
    struct rte_eth_rxconf rx_conf = {
        .rx_thresh = {
            .pthresh = 8,
            .hthresh = 8,
            .wthresh = 4,
        },
        .rx_free_thresh = 64,
        .rx_drop_en = 0
    };

    struct rte_eth_txconf tx_conf = {
        .tx_thresh = {
            .pthresh = 36,
            .hthresh = 0,
            .wthresh = 0,
        },
        .tx_free_thresh = 64,
        .tx_rs_thresh = 32,
    };

    RTE_LOG(INFO, USER1, "[%s()] Initializing Ethernet port %u.\n", __func__,
            port);

    number_of_rings = port_port_cfg[port].ppc_q_cnt;

    rte_eth_dev_info_get(port, &port_dev_info[port].pi_dev_info);
    default_port_config.rx_adv_conf.rss_conf.rss_key_len = port_dev_info[port].pi_dev_info.hash_key_size;
    if (default_port_config.rx_adv_conf.rss_conf.rss_key_len > sizeof(port_rss_key)) {
        RTE_LOG(ERR, USER1,
                "ERROR: Initializng RSS key for port %u, hardware size(%d), exceeds hash key length(%lu)!\n",
                port, default_port_config.rx_adv_conf.rss_conf.rss_key_len, sizeof(port_rss_key));
        return false;
    }

    if (number_of_rings > port_dev_info[port].pi_dev_info.max_rx_queues ||
        number_of_rings > port_dev_info[port].pi_dev_info.max_tx_queues) {
        RTE_LOG(ERR, USER1,
                "ERROR: Number of rx_/tx_rings(%d) is larget than hardware supports(%u/%u)!\n",
                number_of_rings, port_dev_info[port].pi_dev_info.max_rx_queues,
                port_dev_info[port].pi_dev_info.max_tx_queues);
        return false;
    }

    rc = rte_eth_dev_configure(port, number_of_rings, number_of_rings, &default_port_config);
    if (rc < 0) {
        if (rc == -EINVAL) {
            RTE_LOG(INFO, USER1, "WARNING: Virtual driver does not support multiple rings, "
                    "use single packet core!! Max rx\n");
        }
        RTE_LOG(ERR, USER1, "ERROR: Failed rte_eth_dev_configure(%d, %d, %d, ..,), returned %s(%d)!\n",
                port, number_of_rings, number_of_rings, rte_strerror(-rc), -rc);
        return false;
    }

    /*
     * On failure cases it might be nice to undo allocation,
     * however as we exit the application it should be fine for now.
     */
    for (queue = 0; queue < number_of_rings; queue++) {

        rc = rte_eth_rx_queue_setup(port, queue, TPG_ETH_DEV_RX_QUEUE_SIZE,
                                    port_get_socket(port, queue),
                                    &rx_conf,
                                    mem_get_mbuf_pool(port, queue));
        if (rc < 0) {
            RTE_LOG(ERR, USER1,
                    "ERROR: Failed rte_eth_rx_queue_setup(%u, %u, ...), returned %s(%d)!\n",
                    port, queue, rte_strerror(-rc), -rc);
            return false;
        }

        rc = rte_eth_tx_queue_setup(port, queue, TPG_ETH_DEV_TX_QUEUE_SIZE,
                                    port_get_socket(port, queue),
                                    &tx_conf);

        if (rc < 0) {
            RTE_LOG(ERR, USER1,
                    "ERROR: Failed rte_eth_tx_queue_setup(%u, %u, ...), returned %s(%d)!\n",
                    port, queue, rte_strerror(-rc), -rc);
            return false;
        }
    }

    rc  = rte_eth_dev_start(port);
    if (rc < 0) {
        RTE_LOG(ERR, USER1, "ERROR: Failed rte_eth_dev_start(%u), returned %d!\n",
                port, rc);
        return false;
    }

    rte_eth_macaddr_get(port, &mac_addr);
    RTE_LOG(INFO, USER1, "Ethernet port %u initialized with MAC address %02X:%02X:%02X:%02X:%02X:%02X\n",
            port,
            mac_addr.addr_bytes[0],
            mac_addr.addr_bytes[1],
            mac_addr.addr_bytes[2],
            mac_addr.addr_bytes[3],
            mac_addr.addr_bytes[4],
            mac_addr.addr_bytes[5]);

    rte_eth_dev_get_mtu(port, &port_dev_info[port].pi_mtu);

    port_setup_reta_table(port, number_of_rings);
    if (!port_adjust_reta(port))
        return false;

    return true;
}

/*****************************************************************************
 * port_parse_mappings
 * pcore_mask should contain the core mask in the following format:
 * <port>.<hexadecimal_core_mask>
 ****************************************************************************/
static bool port_parse_mappings(char *pcore_mask, uint32_t pcore_mask_len)
{
    int       port_count    = rte_eth_dev_count();
    char     *core_mask     = pcore_mask;
    uint32_t  core_mask_len = pcore_mask_len;
    uint64_t  intmask;
    long int  port;
    int       core;

    while (*core_mask != '.' && core_mask_len) {
        core_mask++;
        core_mask_len--;
    }

    if (!core_mask_len)
        return false;

    /* We assume the memory won't be used after this call ends.
     * Remove the . and put a '\0' instead so we can parse the first part.
     */
    *core_mask = '\0';

    errno = 0;
    port = strtol(pcore_mask, NULL, 10);
    if (errno)
        return false;

    if (port > port_count)
        return false;

    /* skip the '\0' */
    core_mask++;
    core_mask_len--;

    /* Remove '0x' if present. */
    if (core_mask_len >= 2 && core_mask[0] == '0' &&
            (core_mask[1] == 'x' || core_mask[1] == 'X')) {
        core_mask += 2;
        core_mask_len -= 2;
    }

    errno = 0;
    intmask = strtoll(core_mask, NULL, 16);
    if (errno)
        return false;

    RTE_LCORE_FOREACH_SLAVE(core) {
        if (PORT_COREID_IN_MASK(intmask, core)) {
            if (!cfg_is_pkt_core(core)) {
                RTE_LOG(ERR, USER1, "Non-Packet cores shouldn't be assigned to ports!\n");
                return false;
            }
            port_core_cfg[core].pcc_qport_map[port] = port_port_cfg[port].ppc_q_cnt;

            PORT_ADD_CORE_TO_MASK(port_port_cfg[port].ppc_core_mask, core);
            if (port_port_cfg[port].ppc_q_cnt == 0)
                port_port_cfg[port].ppc_core_default = core;
            port_port_cfg[port].ppc_q_cnt++;
        }
    }

    return true;
}

/*****************************************************************************
 * port_start_ports()
 *****************************************************************************/
static bool port_start_ports(void)
{
    uint32_t            port;
    struct rte_eth_link link;

    /* First setup and start the ports. */
    for (port = 0; port < rte_eth_dev_count(); port++) {
        if (!port_setup_port(port))
            return false;
    }

    /* The rte_eth_dev_start API was already called by the port_setup_port
     * function but the link might still be negotiating. By calling
     * rte_eth_link_get we make sure that (at least) after init the ports have
     * finished their initialization.
     */
    for (port = 0; port < rte_eth_dev_count(); port++) {
        rte_eth_link_get(port, &link);
        if (!link.link_status) {
            RTE_LOG(ERR, USER1,
                    "ERROR: Ethernet port %"PRIu32" is DOWN at init!\n",
                    port);
        }
    }

    /* TODO: getting the link should be enough to guarantee that the LINK has
     * been negotiated but on some NICs (e.g., 82599) it seems that the link is
     * but we fail to receive packets for a while. Sleep for a bit for now.
     */
    sleep(5);
    return true;
}

/*****************************************************************************
 * port_get_socket()
 *****************************************************************************/
uint32_t port_get_socket(int port, int queue)
{
    uint32_t core;

    RTE_LCORE_FOREACH_SLAVE(core) {
        if (port_get_rx_queue_id(core, port) == queue)
            return rte_lcore_to_socket_id(core);
    }

    /* Shouldn't be reached! */
    assert(false);
    return 0;
}

/*****************************************************************************
 * port_link_info_get()
 *****************************************************************************/
void port_link_info_get(uint32_t port, struct rte_eth_link *link_info)
{
    rte_eth_link_get(port, link_info);
}

/*****************************************************************************
 * port_link_stats_get()
 *****************************************************************************/
void port_link_stats_get(uint32_t port, struct rte_eth_stats *total_link_stats)
{
    rte_eth_stats_get(port, total_link_stats);
}

/*****************************************************************************
 * port_link_rate_stats_get()
 *****************************************************************************/
void port_link_rate_stats_get(uint32_t port, struct rte_eth_stats *total_rstats)
{
    struct rte_eth_stats    estats;
    link_rate_statistics_t *lrstats;
    uint64_t                now;
    uint64_t                hz;
    uint64_t                time_diff;

    lrstats = &link_rate_statistics[port];
    bzero(total_rstats, sizeof(*total_rstats));

    now = rte_get_timer_cycles();
    hz = rte_get_timer_hz();
    time_diff = TPG_TIME_DIFF(now, lrstats->lrs_timestamp);

    rte_eth_stats_get(port, &estats);

    total_rstats->ipackets = (estats.ipackets - lrstats->lrs_estats.ipackets) *
                             hz / time_diff;

    total_rstats->ibytes = (estats.ibytes - lrstats->lrs_estats.ibytes) *
                           hz / time_diff;

    total_rstats->opackets = (estats.opackets - lrstats->lrs_estats.opackets) *
                             hz / time_diff;

    total_rstats->obytes = (estats.obytes - lrstats->lrs_estats.obytes) *
                           hz / time_diff;

    total_rstats->ierrors = (estats.ierrors - lrstats->lrs_estats.ierrors) *
                            hz / time_diff;

    total_rstats->oerrors = (estats.oerrors - lrstats->lrs_estats.oerrors) *
                            hz / time_diff;

    total_rstats->rx_nombuf = (estats.rx_nombuf - lrstats->lrs_estats.rx_nombuf) *
                              hz / time_diff;

    /* Update the stats. */
    rte_memcpy(&lrstats->lrs_estats, &estats, sizeof(lrstats->lrs_estats));
    lrstats->lrs_timestamp = now;
}

/*****************************************************************************
 * port_total_stats_get()
 *****************************************************************************/
void port_total_stats_get(uint32_t port, port_statistics_t *total_port_stats)
{
    const port_statistics_t *port_stats;
    uint32_t                 core;

    bzero(total_port_stats, sizeof(*total_port_stats));
    STATS_FOREACH_CORE(port_statistics_t, port, core, port_stats) {
        total_port_stats->ps_received_pkts += port_stats->ps_received_pkts;
        total_port_stats->ps_received_bytes += port_stats->ps_received_bytes;

        total_port_stats->ps_send_pkts += port_stats->ps_send_pkts;
        total_port_stats->ps_send_bytes += port_stats->ps_send_bytes;
        total_port_stats->ps_send_failure += port_stats->ps_send_failure;
        total_port_stats->ps_send_sim_failure += port_stats->ps_send_sim_failure;
    }
}

/*****************************************************************************
 * CLI commands
 *****************************************************************************
 * - "show port statistics {details}"
 ****************************************************************************/
struct cmd_show_port_statistics_result {
    cmdline_fixed_string_t show;
    cmdline_fixed_string_t port;
    cmdline_fixed_string_t statistics;
    cmdline_fixed_string_t details;
};

static cmdline_parse_token_string_t cmd_show_port_statistics_T_show =
    TOKEN_STRING_INITIALIZER(struct cmd_show_port_statistics_result, show, "show");
static cmdline_parse_token_string_t cmd_show_port_statistics_T_port =
    TOKEN_STRING_INITIALIZER(struct cmd_show_port_statistics_result, port, "port");
static cmdline_parse_token_string_t cmd_show_port_statistics_T_statistics =
    TOKEN_STRING_INITIALIZER(struct cmd_show_port_statistics_result, statistics, "statistics");
static cmdline_parse_token_string_t cmd_show_port_statistics_T_details =
    TOKEN_STRING_INITIALIZER(struct cmd_show_port_statistics_result, details, "details");

#define SHOW_ETH_STATS(counter)                        \
do {                                                   \
    if (estats.counter != 0)                           \
        cmdline_printf(cl, "  %-20s :  %20"PRIu64"\n", \
                       #counter,                       \
                       estats.counter);                \
} while (0)

#define SHOW_ETH_STATS_QUEUE(counter)                           \
do {                                                            \
    int i;                                                      \
    for (i = 0; i < RTE_ETHDEV_QUEUE_STAT_CNTRS; i++)           \
        if (estats.counter[i] != 0)                             \
            cmdline_printf(cl, "  %-16s[%2d] :  %20"PRIu64"\n", \
                           #counter, i,                         \
                           estats.counter[i]);                  \
} while (0)



static void cmd_show_port_statistics_parsed(void *parsed_result __rte_unused,
                                            struct cmdline *cl,
                                            void *data)
{
    int port;
    int option = (intptr_t) data;

    for (port = 0; port < rte_eth_dev_count(); port++) {

        /*
         * Calculate totals first
         */
        port_statistics_t        total_stats;

        port_total_stats_get(port, &total_stats);

        /*
         * Display individual counters
         */
        cmdline_printf(cl, "Port %d software statistics:\n", port);

        SHOW_64BIT_STATS("Received packets", port_statistics_t,
                         ps_received_pkts,
                         port,
                         option);

        SHOW_64BIT_STATS("Received bytes", port_statistics_t,
                         ps_received_bytes,
                         port,
                         option);

        cmdline_printf(cl, "\n");

        SHOW_64BIT_STATS("Sent packets", port_statistics_t,
                         ps_send_pkts,
                         port,
                         option);

        SHOW_64BIT_STATS("Sent bytes", port_statistics_t,
                         ps_send_bytes,
                         port,
                         option);

        SHOW_64BIT_STATS("Sent failures", port_statistics_t,
                         ps_send_failure,
                         port,
                         option);

        cmdline_printf(cl, "\n");

        SHOW_64BIT_STATS("Simulated failures", port_statistics_t,
                         ps_send_sim_failure,
                         port,
                         option);

        cmdline_printf(cl, "\n");
    }

    if (option == 'd') {

        for (port = 0; port < rte_eth_dev_count(); port++) {

            struct rte_eth_stats estats;

            port_link_stats_get(port, &estats);

            cmdline_printf(cl, "Port %d driver rte_eth_stats:\n", port);
            SHOW_ETH_STATS(ipackets);
            SHOW_ETH_STATS(opackets);
            SHOW_ETH_STATS(ibytes);
            SHOW_ETH_STATS(obytes);
            SHOW_ETH_STATS(imissed);
            SHOW_ETH_STATS(ierrors);
            SHOW_ETH_STATS(oerrors);
            SHOW_ETH_STATS(imcasts);
            SHOW_ETH_STATS(rx_nombuf);

            SHOW_ETH_STATS_QUEUE(q_ipackets);
            SHOW_ETH_STATS_QUEUE(q_opackets);
            SHOW_ETH_STATS_QUEUE(q_ibytes);
            SHOW_ETH_STATS_QUEUE(q_obytes);
            SHOW_ETH_STATS_QUEUE(q_errors);

            SHOW_ETH_STATS(ilbpackets);
            SHOW_ETH_STATS(olbpackets);
            SHOW_ETH_STATS(ilbbytes);
            SHOW_ETH_STATS(olbbytes);

            cmdline_printf(cl, "\n");
        }

        for (port = 0; port < rte_eth_dev_count(); port++) {

#define MAX_XSTATS_TO_DISPLAY 128

            int                   i, rc;
            struct rte_eth_xstats exstats[MAX_XSTATS_TO_DISPLAY];

            rc = rte_eth_xstats_get(port, exstats, MAX_XSTATS_TO_DISPLAY);

            cmdline_printf(cl, "Port %d driver rte_xeth_stats:\n", port);

            if (rc < 0) {
                cmdline_printf(cl, "  ERROR: Failed getting statistics, error %d\n", rc);
                continue;
            }

            if (rc > MAX_XSTATS_TO_DISPLAY) {
                cmdline_printf(cl, "  ERROR: More statistics available than we can display, number %d\n", rc);
                continue;
            }

            for (i = 0; i < rc; i++) {
                if (exstats[i].value != 0) {
                    cmdline_printf(cl, "  %-32s :  %20"PRIu64"\n",
                                   exstats[i].name, exstats[i].value);
                }
            }

            cmdline_printf(cl, "\n");
        }
    }
}

cmdline_parse_inst_t cmd_show_port_statistics = {
    .f = cmd_show_port_statistics_parsed,
    .data = NULL,
    .help_str = "show port statistics",
    .tokens = {
        (void *)&cmd_show_port_statistics_T_show,
        (void *)&cmd_show_port_statistics_T_port,
        (void *)&cmd_show_port_statistics_T_statistics,
        NULL,
    },
};

cmdline_parse_inst_t cmd_show_port_statistics_details = {
    .f = cmd_show_port_statistics_parsed,
    .data = (void *) (intptr_t) 'd',
    .help_str = "show port statistics details",
    .tokens = {
        (void *)&cmd_show_port_statistics_T_show,
        (void *)&cmd_show_port_statistics_T_port,
        (void *)&cmd_show_port_statistics_T_statistics,
        (void *)&cmd_show_port_statistics_T_details,
        NULL,
    },
};

/****************************************************************************
 * - "show port link"
 ****************************************************************************/
struct cmd_show_port_link_result {
    cmdline_fixed_string_t show;
    cmdline_fixed_string_t port;
    cmdline_fixed_string_t link;
};

static cmdline_parse_token_string_t cmd_show_port_link_T_show =
    TOKEN_STRING_INITIALIZER(struct cmd_show_port_link_result, show, "show");
static cmdline_parse_token_string_t cmd_show_port_link_T_port =
    TOKEN_STRING_INITIALIZER(struct cmd_show_port_link_result, port, "port");
static cmdline_parse_token_string_t cmd_show_port_link_T_link =
    TOKEN_STRING_INITIALIZER(struct cmd_show_port_link_result, link, "link");

static void cmd_show_port_link_parsed(void *parsed_result __rte_unused,
                                      struct cmdline *cl,
                                      void *data __rte_unused)
{
    int port;

    for (port = 0; port < rte_eth_dev_count(); port++) {
        struct rte_eth_link link;

        port_link_info_get(port, &link);

        if (link.link_status == 0) {
            cmdline_printf(cl, "Port %d linkstate %s\n",
                           port, "DOWN");
        } else {
            if (link.link_speed < 1000) {
                cmdline_printf(cl, "Port %d linkstate %s, speed %uMB, duplex %s\n",
                               port, "UP", link.link_speed,
                               link.link_duplex == ETH_LINK_HALF_DUPLEX ? "half" :
                               link.link_duplex == ETH_LINK_FULL_DUPLEX ? "full" :
                               link.link_duplex == ETH_LINK_AUTONEG_DUPLEX ? "auto" :
                               "???");

            } else {
                cmdline_printf(cl, "Port %d linkstate %s, speed %uG, duplex %s\n",
                               port, "UP", link.link_speed/1000,
                               link.link_duplex == ETH_LINK_HALF_DUPLEX ? "half" :
                               link.link_duplex == ETH_LINK_FULL_DUPLEX ? "full" :
                               link.link_duplex == ETH_LINK_AUTONEG_DUPLEX ? "auto" :
                               "???");
            }
        }
    }
}

cmdline_parse_inst_t cmd_show_port_link = {
    .f = cmd_show_port_link_parsed,
    .data = NULL,
    .help_str = "show port link",
    .tokens = {
        (void *)&cmd_show_port_link_T_show,
        (void *)&cmd_show_port_link_T_port,
        (void *)&cmd_show_port_link_T_link,
        NULL,
    },
};

/****************************************************************************
 * - "show port map"
 ****************************************************************************/
struct cmd_show_port_map_result {
    cmdline_fixed_string_t show;
    cmdline_fixed_string_t port;
    cmdline_fixed_string_t map;
};

static cmdline_parse_token_string_t cmd_show_port_map_T_show =
    TOKEN_STRING_INITIALIZER(struct cmd_show_port_map_result, show, "show");
static cmdline_parse_token_string_t cmd_show_port_map_T_port =
    TOKEN_STRING_INITIALIZER(struct cmd_show_port_map_result, port, "port");
static cmdline_parse_token_string_t cmd_show_port_map_T_map =
    TOKEN_STRING_INITIALIZER(struct cmd_show_port_map_result, map, "map");

static void cmd_show_port_map_parsed(void *parsed_result __rte_unused,
                                     struct cmdline *cl,
                                     void *data __rte_unused)
{
    uint32_t port;

    for (port = 0; port < rte_eth_dev_count(); port++) {
        uint32_t core;

        cmdline_printf(cl, "Port %u[socket: %d]:\n", port,
                       port_dev_info[port].pi_dev_info.pci_dev->numa_node);
        RTE_LCORE_FOREACH_SLAVE(core) {
            if (PORT_COREID_IN_MASK(port_port_cfg[port].ppc_core_mask, core)) {
                cmdline_printf(cl, "   Core %u[socket:%u] (Tx: %d, Rx: %d)\n",
                               core,
                               rte_lcore_to_socket_id(core),
                               port_get_tx_queue_id(core, port),
                               port_get_rx_queue_id(core, port));
            }
        }
        cmdline_printf(cl, "\n");
    }
}

cmdline_parse_inst_t cmd_show_port_map = {
    .f = cmd_show_port_map_parsed,
    .data = NULL,
    .help_str = "show port map",
    .tokens = {
        (void *)&cmd_show_port_map_T_show,
        (void *)&cmd_show_port_map_T_port,
        (void *)&cmd_show_port_map_T_map,
        NULL,
    },
};

/****************************************************************************
 * - "show port info"
 ****************************************************************************/
struct cmd_show_port_info_result {
    cmdline_fixed_string_t show;
    cmdline_fixed_string_t port;
    cmdline_fixed_string_t info;
};

static cmdline_parse_token_string_t cmd_show_port_info_T_show =
    TOKEN_STRING_INITIALIZER(struct cmd_show_port_info_result, show, "show");
static cmdline_parse_token_string_t cmd_show_port_info_T_port =
    TOKEN_STRING_INITIALIZER(struct cmd_show_port_info_result, port, "port");
static cmdline_parse_token_string_t cmd_show_port_info_T_info =
    TOKEN_STRING_INITIALIZER(struct cmd_show_port_info_result, info, "info");

static void cmd_show_port_info_parsed(void *parsed_result __rte_unused,
                                      struct cmdline *cl,
                                      void *data __rte_unused)
{
    int port;

    cmdline_printf(cl, "                                       Queues max  rx      tx         rss offloads\n");
    cmdline_printf(cl, "Port Driver           PCI address  Soc rx    tx    offload offload    IPv4   IPv6  ex  2\n");
    cmdline_printf(cl, "---- ---------------- ------------ --- ----- ----- ------- ---------- ------+---------+--\n");

    for (port = 0; port < rte_eth_dev_count(); port++) {

        struct rte_eth_dev_info  dev_info;
        struct rte_pci_addr     *pci;

        rte_eth_dev_info_get(port, &dev_info);

        if (dev_info.pci_dev == NULL)
            continue;

        pci = &dev_info.pci_dev->addr;

        cmdline_printf(cl,
                       "%4u %-16.16s "PCI_PRI_FMT" %3d %5u %5u %c%c%c%c%c%c%c %c%c%c%c%c%c%c%c%c%c %c%c%c%c%c%c %c%c%c%c%c%c%c%c%c %c%c\n",
                       port, dev_info.driver_name,
                       pci->domain, pci->bus, pci->devid, pci->function,
                       dev_info.pci_dev->numa_node,
                       dev_info.max_rx_queues, dev_info.max_tx_queues,

                       (dev_info.rx_offload_capa & DEV_RX_OFFLOAD_VLAN_STRIP) ? 'v' : '-',
                       (dev_info.rx_offload_capa & DEV_RX_OFFLOAD_IPV4_CKSUM) ? '4' : '-',
                       (dev_info.rx_offload_capa & DEV_RX_OFFLOAD_UDP_CKSUM) ? 'u' : '-',
                       (dev_info.rx_offload_capa & DEV_RX_OFFLOAD_TCP_CKSUM) ? 't' : '-',
                       (dev_info.rx_offload_capa & DEV_RX_OFFLOAD_TCP_LRO) ? 'l' : '-',
                       (dev_info.rx_offload_capa & DEV_RX_OFFLOAD_QINQ_STRIP) ? 'q' : '-',
                       (dev_info.rx_offload_capa & ~(
                           DEV_RX_OFFLOAD_VLAN_STRIP |
                           DEV_RX_OFFLOAD_IPV4_CKSUM |
                           DEV_RX_OFFLOAD_UDP_CKSUM |
                           DEV_RX_OFFLOAD_TCP_CKSUM |
                           DEV_RX_OFFLOAD_TCP_LRO |
                           DEV_RX_OFFLOAD_QINQ_STRIP)) ? '+' : ' ',

                       (dev_info.tx_offload_capa & DEV_TX_OFFLOAD_VLAN_INSERT) ? 'v' : '-',
                       (dev_info.tx_offload_capa & DEV_TX_OFFLOAD_IPV4_CKSUM) ? '4' : '-',
                       (dev_info.tx_offload_capa & DEV_TX_OFFLOAD_UDP_CKSUM) ? 'u' : '-',
                       (dev_info.tx_offload_capa & DEV_TX_OFFLOAD_TCP_CKSUM) ? 't' : '-',
                       (dev_info.tx_offload_capa & DEV_TX_OFFLOAD_SCTP_CKSUM) ? 's' : '-',
                       (dev_info.tx_offload_capa & DEV_TX_OFFLOAD_TCP_TSO) ? 't' : '-',
                       (dev_info.tx_offload_capa & DEV_TX_OFFLOAD_UDP_TSO) ? 'T' : '-',
                       (dev_info.tx_offload_capa & DEV_TX_OFFLOAD_OUTER_IPV4_CKSUM) ? 'o' : '-',
                       (dev_info.tx_offload_capa & DEV_TX_OFFLOAD_QINQ_INSERT) ? 'q' : '-',
                       (dev_info.tx_offload_capa & ~(
                           DEV_TX_OFFLOAD_VLAN_INSERT |
                           DEV_TX_OFFLOAD_IPV4_CKSUM |
                           DEV_TX_OFFLOAD_UDP_CKSUM |
                           DEV_TX_OFFLOAD_TCP_CKSUM |
                           DEV_TX_OFFLOAD_SCTP_CKSUM |
                           DEV_TX_OFFLOAD_TCP_TSO |
                           DEV_TX_OFFLOAD_UDP_TSO |
                           DEV_TX_OFFLOAD_OUTER_IPV4_CKSUM |
                           DEV_TX_OFFLOAD_QINQ_INSERT)) ? '+' : ' ',

                       (dev_info.flow_type_rss_offloads & ETH_RSS_IPV4) ? '4' : '-',
                       (dev_info.flow_type_rss_offloads & ETH_RSS_FRAG_IPV4) ? 'f' : '-',
                       (dev_info.flow_type_rss_offloads & ETH_RSS_NONFRAG_IPV4_TCP) ? 't' : '-',
                       (dev_info.flow_type_rss_offloads & ETH_RSS_NONFRAG_IPV4_UDP) ? 'u' : '-',
                       (dev_info.flow_type_rss_offloads & ETH_RSS_NONFRAG_IPV4_SCTP) ? 's' : '-',
                       (dev_info.flow_type_rss_offloads & ETH_RSS_NONFRAG_IPV4_OTHER) ? 'o' : '-',
                       (dev_info.flow_type_rss_offloads & ETH_RSS_IPV6) ? '6' : '-',
                       (dev_info.flow_type_rss_offloads & ETH_RSS_FRAG_IPV6) ? 'f' : '-',
                       (dev_info.flow_type_rss_offloads & ETH_RSS_NONFRAG_IPV6_TCP) ? 't' : '-',
                       (dev_info.flow_type_rss_offloads & ETH_RSS_NONFRAG_IPV6_UDP) ? 'u' : '-',
                       (dev_info.flow_type_rss_offloads & ETH_RSS_NONFRAG_IPV6_SCTP) ? 's' : '-',
                       (dev_info.flow_type_rss_offloads & ETH_RSS_NONFRAG_IPV6_OTHER) ? 'o' : '-',
                       (dev_info.flow_type_rss_offloads & ETH_RSS_IPV6_EX) ? 'E' : '-',
                       (dev_info.flow_type_rss_offloads & ETH_RSS_IPV6_TCP_EX) ? 'T' : '-',
                       (dev_info.flow_type_rss_offloads & ETH_RSS_IPV6_UDP_EX) ? 'U' : '-',
                       (dev_info.flow_type_rss_offloads & ETH_RSS_L2_PAYLOAD) ? '2' : '-',
                       (dev_info.flow_type_rss_offloads & ~(
                           ETH_RSS_IPV4 |
                           ETH_RSS_FRAG_IPV4 |
                           ETH_RSS_NONFRAG_IPV4_TCP |
                           ETH_RSS_NONFRAG_IPV4_UDP |
                           ETH_RSS_NONFRAG_IPV4_SCTP |
                           ETH_RSS_NONFRAG_IPV4_OTHER |
                           ETH_RSS_IPV6 |
                           ETH_RSS_FRAG_IPV6 |
                           ETH_RSS_NONFRAG_IPV6_TCP |
                           ETH_RSS_NONFRAG_IPV6_UDP |
                           ETH_RSS_NONFRAG_IPV6_SCTP |
                           ETH_RSS_NONFRAG_IPV6_OTHER |
                           ETH_RSS_L2_PAYLOAD |
                           ETH_RSS_IPV6_EX |
                           ETH_RSS_IPV6_TCP_EX |
                           ETH_RSS_IPV6_UDP_EX)) ? '+' : ' ');
    }
}

cmdline_parse_inst_t cmd_show_port_info = {
    .f = cmd_show_port_info_parsed,
    .data = NULL,
    .help_str = "show port info",
    .tokens = {
        (void *)&cmd_show_port_info_T_show,
        (void *)&cmd_show_port_info_T_port,
        (void *)&cmd_show_port_info_T_info,
        NULL,
    },
};

/*****************************************************************************
 * Main menu context
 ****************************************************************************/
static cmdline_parse_ctx_t cli_ctx[] = {
    &cmd_show_port_info,
    &cmd_show_port_map,
    &cmd_show_port_link,
    &cmd_show_port_statistics,
    &cmd_show_port_statistics_details,
    NULL,
};

/*****************************************************************************
 * port_init()
 ****************************************************************************/
bool port_init(void)
{
    uint32_t   port;
    uint32_t   core;
    char     **qmap;

    if (rte_eth_dev_count() < 1) {
        RTE_LOG(ERR, USER1, "ERROR: Requered ethernet interfaces not available!\n");
        return false;
    }

    /*
     * Add port module CLI commands
     */
    if (!cli_add_main_ctx(cli_ctx)) {
        RTE_LOG(ERR, USER1, "ERROR: Can't add port specific CLI commands!\n");
        return false;
    }

    /*
     * Allocate memory for port statistics, and clear all of them
     */
    if (STATS_GLOBAL_INIT(port_statistics_t, "port_stats") == NULL) {
        RTE_LOG(ERR, USER1,
                "ERROR: Failed allocating PORT statistics memory!\n");
        return false;
    }

    /*
     * Allocate memory for link rate stats and initialize all of them.
     */
    link_rate_statistics = rte_malloc("link_rate_stats",
                                      rte_eth_dev_count() * sizeof(*link_rate_statistics),
                                      0);

    if (link_rate_statistics == NULL) {
        RTE_LOG(ERR, USER1, "ERROR: Failed allocating link rate statistics memory!\n");
        return false;
    }

    for (port = 0; port < rte_eth_dev_count(); port++) {
        bzero(&link_rate_statistics[port].lrs_estats,
              sizeof(link_rate_statistics[port].lrs_estats));
        link_rate_statistics[port].lrs_timestamp = rte_get_timer_cycles();
    }

    /*
     * Allocate memory for the port-core mappings and clear them.
     */

    port_port_cfg = rte_zmalloc("port_port_cfg",
                                rte_eth_dev_count() * sizeof(*port_port_cfg),
                                0);

    if (port_port_cfg == NULL) {
        RTE_LOG(ERR, USER1, "ERROR: Failed allocating port-core mappings memory!\n");
        return false;
    }

    port_core_cfg = rte_zmalloc("port_core_cfg",
                                RTE_MAX_LCORE * sizeof(*port_core_cfg),
                                0);

    /*
     * Allocate memory for the port-core mappings and clear them.
     */
    if (port_core_cfg == NULL) {
        RTE_LOG(ERR, USER1, "ERROR: Failed allocating core-port mappings memory!\n");
        return false;
    }

    RTE_LCORE_FOREACH_SLAVE(core) {
        port_core_cfg[core].pcc_qport_map = rte_zmalloc("port_core_cfg.pcc_qport_map",
                                                        rte_eth_dev_count() *
                                                            sizeof(*port_core_cfg[core].pcc_qport_map),
                                                        0);
        if (!port_core_cfg[core].pcc_qport_map) {
            RTE_LOG(ERR, USER1, "ERROR: Failed allocating core-port q mappings memory!\n");
            return false;
        }

        for (port = 0; port < rte_eth_dev_count(); port++)
            port_core_cfg[core].pcc_qport_map[port] = CORE_PORT_QINVALID;
    }

    /*
     * Allocate memory for the eth dev info.
     */

    port_dev_info = rte_zmalloc("port_dev_info",
                                rte_eth_dev_count() * sizeof(*port_dev_info),
                                0);

    if (port_dev_info == NULL) {
        RTE_LOG(ERR, USER1, "ERROR: Failed allocating port dev info memory!\n");
        return false;
    }

    RTE_LOG(INFO, USER1, "Found %d Ethernet ports to initialize.\n",
            rte_eth_dev_count());

    /*
     * Initialize all available ethernet ports.
     */
    for (qmap = cfg_get_qmap(); *qmap; qmap++) {
        if (!port_parse_mappings(*qmap, strlen(*qmap)))
            rte_exit(EXIT_FAILURE, "ERROR: Failed initializing qmap %s!\n", *qmap);
    }

    if (!port_start_ports())
        return false;

    RTE_LOG(INFO, USER1, "Started %d Ethernet ports.\n", rte_eth_dev_count());

    return true;
}

/*****************************************************************************
 * port_lcore_init()
 ****************************************************************************/
void port_lcore_init(uint32_t lcore_id)
{
    /* Init the local stats. */
    if (STATS_LOCAL_INIT(port_statistics_t, "port_stats", lcore_id) == NULL) {
        TPG_ERROR_ABORT("[%d:%s() Failed to allocate per lcore port_stats!\n",
                        rte_lcore_index(lcore_id),
                        __func__);
    }
}

