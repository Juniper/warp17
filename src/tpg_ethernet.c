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
 *     tpg_ethernet.c
 *
 * Description:
 *     Ethernet processing.
 *
 * Author:
 *     Dumitru Ceara, Eelco Chaudron
 *
 * Initial Created:
 *     03/03/2015
 *
 * Notes:
 *
 */

/*****************************************************************************
 * Include files
 ****************************************************************************/
#include "tcp_generator.h"

/*****************************************************************************
 * Global variables
 ****************************************************************************/
/* Define Ethernet global statistics. Each thread has its own set of locally
 * allocated stats which are accessible through STATS_GLOBAL(type, core, port).
 */
STATS_DEFINE(ethernet_statistics_t);

/*****************************************************************************
 * CLI commands
 *****************************************************************************
 * - "show ethernet statistics {details}"
 ****************************************************************************/
struct cmd_show_ethernet_statistics_result {
    cmdline_fixed_string_t show;
    cmdline_fixed_string_t ethernet;
    cmdline_fixed_string_t statistics;
    cmdline_fixed_string_t details;
};

static cmdline_parse_token_string_t cmd_show_ethernet_statistics_T_show =
    TOKEN_STRING_INITIALIZER(struct cmd_show_ethernet_statistics_result, show, "show");
static cmdline_parse_token_string_t cmd_show_ethernet_statistics_T_ethernet =
    TOKEN_STRING_INITIALIZER(struct cmd_show_ethernet_statistics_result, ethernet, "ethernet");
static cmdline_parse_token_string_t cmd_show_ethernet_statistics_T_statistics =
    TOKEN_STRING_INITIALIZER(struct cmd_show_ethernet_statistics_result, statistics, "statistics");
static cmdline_parse_token_string_t cmd_show_ethernet_statistics_T_details =
    TOKEN_STRING_INITIALIZER(struct cmd_show_ethernet_statistics_result, details, "details");

static void cmd_show_ethernet_statistics_parsed(void *parsed_result __rte_unused,
                                                struct cmdline *cl,
                                                void *data)
{
    int port;
    int core;
    int option = (intptr_t) data;

    for (port = 0; port < rte_eth_dev_count(); port++) {

        /*
         * Calculate totals first
         */
        ethernet_statistics_t  total_stats;
        ethernet_statistics_t *eth_stats;

        bzero(&total_stats, sizeof(total_stats));
        STATS_FOREACH_CORE(ethernet_statistics_t, port, core, eth_stats) {
            total_stats.es_etype_arp += eth_stats->es_etype_arp;
            total_stats.es_etype_ipv4 += eth_stats->es_etype_ipv4;
            total_stats.es_etype_ipv6 += eth_stats->es_etype_ipv6;
            total_stats.es_etype_vlan += eth_stats->es_etype_vlan;
            total_stats.es_etype_other += eth_stats->es_etype_other;
            total_stats.es_to_small_fragment += eth_stats->es_to_small_fragment;
        }

        /*
         * Display individual counters
         */
        cmdline_printf(cl, "Port %d ethernet statistics:\n", port);

        SHOW_64BIT_STATS("etype ARP  (0x0806)", ethernet_statistics_t,
                         es_etype_arp,
                         port,
                         option);

        SHOW_64BIT_STATS("etype IPv4 (0x0800)", ethernet_statistics_t,
                         es_etype_ipv4,
                         port,
                         option);

        SHOW_64BIT_STATS("etype IPv6 (0x86DD)", ethernet_statistics_t,
                         es_etype_ipv6,
                         port,
                         option);

        SHOW_64BIT_STATS("etype VLAN (0x8100)", ethernet_statistics_t,
                         es_etype_vlan,
                         port,
                         option);

        cmdline_printf(cl, "\n");

        SHOW_16BIT_STATS("small mbuf fragment", ethernet_statistics_t,
                         es_to_small_fragment,
                         port,
                         option);

        cmdline_printf(cl, "\n");
    }
}

cmdline_parse_inst_t cmd_show_ethernet_statistics = {
    .f = cmd_show_ethernet_statistics_parsed,
    .data = NULL,
    .help_str = "show ethernet statistics",
    .tokens = {
        (void *)&cmd_show_ethernet_statistics_T_show,
        (void *)&cmd_show_ethernet_statistics_T_ethernet,
        (void *)&cmd_show_ethernet_statistics_T_statistics,
        NULL,
    },
};

cmdline_parse_inst_t cmd_show_ethernet_statistics_details = {
    .f = cmd_show_ethernet_statistics_parsed,
    .data = (void *) (intptr_t) 'd',
    .help_str = "show ethernet statistics details",
    .tokens = {
        (void *)&cmd_show_ethernet_statistics_T_show,
        (void *)&cmd_show_ethernet_statistics_T_ethernet,
        (void *)&cmd_show_ethernet_statistics_T_statistics,
        (void *)&cmd_show_ethernet_statistics_T_details,
        NULL,
    },
};

/*****************************************************************************
 * Main menu context
 ****************************************************************************/
static cmdline_parse_ctx_t cli_ctx[] = {
    &cmd_show_ethernet_statistics,
    &cmd_show_ethernet_statistics_details,
    NULL,
};

/*****************************************************************************
 * eth_init()
 ****************************************************************************/
bool eth_init(void)
{
    /*
     * Add port module CLI commands
     */
    if (!cli_add_main_ctx(cli_ctx)) {
        RTE_LOG(ERR, USER1,
                "ERROR: Can't add ethernet specific CLI commands!\n");
        return false;
    }

    /*
     * Allocate memory for ethernet statistics, and clear all of them
     */
    if (STATS_GLOBAL_INIT(ethernet_statistics_t, "eth_stats") == NULL) {
        RTE_LOG(ERR, USER1,
                "ERROR: Failed allocating ethernet statistics memory!\n");
        return false;
    }

    return true;
}

/*****************************************************************************
 * eth_lcore_init()
 ****************************************************************************/
void eth_lcore_init(uint32_t lcore_id)
{
    /* Init the local stats. */
    if (STATS_LOCAL_INIT(ethernet_statistics_t, "eth_stats",
                         lcore_id) == NULL) {
        TPG_ERROR_ABORT("[%d:%s() Failed to allocate per lcore eth stats!\n",
                        rte_lcore_index(lcore_id),
                        __func__);
    }
}

/*****************************************************************************
 * eth_build_eth_hdr()
 ****************************************************************************/
int eth_build_eth_hdr(struct rte_mbuf *mbuf, uint64_t dst_mac,
                      uint64_t src_mac, uint16_t ether_type)
{
    struct ether_hdr *eth;

    eth = (struct ether_hdr *) rte_pktmbuf_append(mbuf,
                                                  sizeof(struct ether_hdr));

    if (eth == NULL)
        return -ENOMEM;

    arp_uint64_to_mac(dst_mac, eth->d_addr.addr_bytes);

    if (src_mac == TPG_USE_PORT_MAC)
        rte_eth_macaddr_get(mbuf->port, &eth->s_addr);
    else
        arp_uint64_to_mac(src_mac, eth->s_addr.addr_bytes);

    eth->ether_type =  rte_cpu_to_be_16(ether_type);

    if (true) {
        /*
         * We assume hardware checksum calculation for ip/tcp, to
         * support this we need to set the correct l2 header size.
         */
        mbuf->l2_len = sizeof(struct ether_hdr);
    }

    return sizeof(struct ether_hdr);
}

/*****************************************************************************
 * eth_receive_pkt()
 *
 * Return the mbuf only if it needs to be free'ed back to the pool, if it was
 * consumed, or needed later (ip refrag), return NULL.
 ****************************************************************************/
struct rte_mbuf *eth_receive_pkt(packet_control_block_t *pcb,
                                 struct rte_mbuf *mbuf)
{
    uint16_t          etype;
    struct ether_hdr *eth_hdr;

    if (unlikely(rte_pktmbuf_data_len(mbuf) < sizeof(struct ether_hdr))) {
        RTE_LOG(DEBUG, USER2, "[%d:%s()] ERR: mbuf fragment to small for ether_hdr!\n",
                pcb->pcb_core_index, __func__);

        INC_STATS(STATS_LOCAL(ethernet_statistics_t, pcb->pcb_port),
                  es_to_small_fragment);
        return mbuf;
    }

    eth_hdr = rte_pktmbuf_mtod(mbuf, struct ether_hdr *);
    etype = rte_be_to_cpu_16(eth_hdr->ether_type);

    PKT_TRACE(pcb, ETH, DEBUG, "dst=%02X:%02X:%02X:%02X:%02X:%02X, src=%02X:%02X:%02X:%02X:%02X:%02X, etype=0x%4.4X",
              eth_hdr->d_addr.addr_bytes[0],
              eth_hdr->d_addr.addr_bytes[1],
              eth_hdr->d_addr.addr_bytes[2],
              eth_hdr->d_addr.addr_bytes[3],
              eth_hdr->d_addr.addr_bytes[4],
              eth_hdr->d_addr.addr_bytes[5],
              eth_hdr->s_addr.addr_bytes[0],
              eth_hdr->s_addr.addr_bytes[1],
              eth_hdr->s_addr.addr_bytes[2],
              eth_hdr->s_addr.addr_bytes[3],
              eth_hdr->s_addr.addr_bytes[4],
              eth_hdr->s_addr.addr_bytes[5],
              etype);

    /*
     * Pop all vlan tags to determine protocol type
     */
    if (unlikely(etype == ETHER_TYPE_VLAN)) {

        INC_STATS(STATS_LOCAL(ethernet_statistics_t, pcb->pcb_port),
                  es_etype_vlan);

        rte_pktmbuf_adj(mbuf, sizeof(struct ether_hdr));

        do {
            struct vlan_hdr *tag_hdr;

            tag_hdr = rte_pktmbuf_mtod(mbuf, struct vlan_hdr *);

            if (unlikely(rte_pktmbuf_data_len(mbuf) < sizeof(struct vlan_hdr))) {
                RTE_LOG(DEBUG, USER2, "[%d:%s()] ERR: mbuf fragment to small for vlan_hdr!\n",
                        pcb->pcb_core_index, __func__);

                INC_STATS(STATS_LOCAL(ethernet_statistics_t, pcb->pcb_port),
                          es_to_small_fragment);
                return mbuf;
            }

            etype = rte_be_to_cpu_16(tag_hdr->eth_proto);

            PKT_TRACE(pcb, ETH, DEBUG, "VLAN pop: vlan_tci=%4.4X, etype=%4.4X",
                      rte_be_to_cpu_16(tag_hdr->vlan_tci), etype);

            rte_pktmbuf_adj(mbuf, sizeof(struct vlan_hdr));

        } while (etype == ETHER_TYPE_VLAN);

    } else {
        rte_pktmbuf_adj(mbuf, sizeof(struct ether_hdr));
    }

    eth_hdr = NULL; /* so if we ever decide to use it will quickly core */

    /*
     * Update stats, and execute protocol handler
     */
    switch (etype) {

    case ETHER_TYPE_IPv4:
        INC_STATS(STATS_LOCAL(ethernet_statistics_t, pcb->pcb_port),
                  es_etype_ipv4);
        mbuf = ipv4_receive_pkt(pcb, mbuf);
        break;

    case ETHER_TYPE_ARP:
        INC_STATS(STATS_LOCAL(ethernet_statistics_t, pcb->pcb_port),
                  es_etype_arp);
        mbuf = arp_receive_pkt(pcb, mbuf);
        break;

    case ETHER_TYPE_IPv6:
        /* TODO: handle IPv6 */
        INC_STATS(STATS_LOCAL(ethernet_statistics_t, pcb->pcb_port),
                  es_etype_ipv6);
        break;

    default:
        INC_STATS(STATS_LOCAL(ethernet_statistics_t, pcb->pcb_port),
                  es_etype_other);
        break;
    }

    return mbuf;
}

