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
 *     tpg_arp.c
 *
 * Description:
 *     ARP processing
 *
 * Author:
 *     Dumitru Ceara, Eelco Chaudron
 *
 * Initial Created:
 *     03/03/2015
 *
 * Notes:
 *     We take advantage of the fact that L2 packets, including ARP, are
 *     are currently not load balanced. As long as this is true, all ARP
 *     packets are handled by the first core in the reta table.
 *     This way we do not need to lock any of the data structures, as they are
 *     only changed by a single core.
 *     In addition any control messages needing to handle add and/or deletes
 *     of ARP entries are also processed by the first core for the port.
 *
 *     The only corner case that has issues is when deleting an ARP entry.
 *     Currently we do not delete ARP entries, only the L3 interface one
 *     when it gets deleted. So don't do this while tests are running ;)
 *
 *     If in the future we need L2 RSS, we should add a write only lock,
 *     and it should just work (the above corner case will still apply)
 */

/*****************************************************************************
 * Include files
 ****************************************************************************/
#include <rte_arp.h>

#include "tcp_generator.h"

/*****************************************************************************
 * Global variables
 ****************************************************************************/
/* Define ARP global statistics. Each thread has its own set of locally
 * allocated stats which are accessible through STATS_GLOBAL(type, core, port).
 */
STATS_DEFINE(tpg_arp_statistics_t);

/* ARP tables should be stored per port in memory allocated from the same socket
 * as the port. For global access we use an array of arrays indexed by port
 * (arp_per_port_tables). Each entry is an array of ARP entries allocated from
 * memory on the same socket as the port.
 */
static arp_entry_t **arp_per_port_tables;
/* Each lcore stores a clone of the global array (but allocated in its own
 * local memory). Assuming that in the ideal case ports are only handled by
 * cores that are on the same socket as the ports, this should give the fastest
 * memory access.
 */
static RTE_DEFINE_PER_LCORE(arp_entry_t **, arp_local_per_port_tables);

static uint8_t arp_bcast_addr[ETHER_ADDR_LEN] = {
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff
};

/*****************************************************************************
 * CLI commands
 *****************************************************************************
 * - "show arp entries"
 ****************************************************************************/
struct cmd_show_arp_entries_result {
    cmdline_fixed_string_t show;
    cmdline_fixed_string_t arp;
    cmdline_fixed_string_t entries;
    cmdline_fixed_string_t details;
};

static cmdline_parse_token_string_t cmd_show_arp_entries_T_show =
    TOKEN_STRING_INITIALIZER(struct cmd_show_arp_entries_result, show, "show");
static cmdline_parse_token_string_t cmd_show_arp_entries_T_arp =
    TOKEN_STRING_INITIALIZER(struct cmd_show_arp_entries_result, arp, "arp");
static cmdline_parse_token_string_t cmd_show_arp_entries_T_statistics =
    TOKEN_STRING_INITIALIZER(struct cmd_show_arp_entries_result, entries, "entries");

static void cmd_show_arp_entries_parsed(void *parsed_result __rte_unused,
                                        struct cmdline *cl,
                                        void *data __rte_unused)
{
    int port;

    for (port = 0; port < rte_eth_dev_count(); port++) {

        int          entry;
        arp_entry_t *port_entries = arp_per_port_tables[port];

        cmdline_printf(cl, "ARP table for port %u:\n\n", port);
        cmdline_printf(cl, "IPv4             MAC address        Age         Flags  Vlan-id\n");
        cmdline_printf(cl, "---------------  -----------------  ----------  -----  -------\n");

        /*
         *   This really kills performance, but we assume this is only done
         *   for trouble shooting ;)
         */

        for (entry = 0; entry < TPG_ARP_PORT_TABLE_SIZE; entry++) {

            if (arp_is_entry_in_use(&port_entries[entry])) {
                char ip_str[16];
                char mac_str[18];
                char flag_str[] = "u-";
                int  age = -1;

                snprintf(ip_str, sizeof(ip_str), TPG_IPV4_PRINT_FMT,
                         TPG_IPV4_PRINT_ARGS(port_entries[entry].ae_ip_address));

                if (ARP_IS_FLAG_SET(&port_entries[entry], TPG_ARP_FLAG_INCOMPLETE)) {

                    flag_str[1] = 'i';
                    snprintf(mac_str, sizeof(mac_str), "<incomplete>");

                } else {
                    if (ARP_IS_FLAG_SET(&port_entries[entry], TPG_ARP_FLAG_LOCAL))
                        flag_str[1] = 'l';

                    snprintf(mac_str, sizeof(mac_str), "%02X:%02X:%02X:%02X:%02X:%02X",
                             (uint8_t) (port_entries[entry].ae_mac_flags >> 40) & 0xff,
                             (uint8_t) (port_entries[entry].ae_mac_flags >> 32) & 0xff,
                             (uint8_t) (port_entries[entry].ae_mac_flags >> 24) & 0xff,
                             (uint8_t) (port_entries[entry].ae_mac_flags >> 16) & 0xff,
                             (uint8_t) (port_entries[entry].ae_mac_flags >>  8) & 0xff,
                             (uint8_t) (port_entries[entry].ae_mac_flags >>  0) & 0xff);
                }

                cmdline_printf(cl, "%15s  %17s  %10d  %5s %7"PRIu16"\n",
                               ip_str, mac_str, age, flag_str,
                               port_entries[entry].ae_vlan_id);
            }
        }

        cmdline_printf(cl, "\n");
    }

}

cmdline_parse_inst_t cmd_show_arp_entries = {
    .f = cmd_show_arp_entries_parsed,
    .data = NULL,
    .help_str = "show arp entries",
    .tokens = {
        (void *)&cmd_show_arp_entries_T_show,
        (void *)&cmd_show_arp_entries_T_arp,
        (void *)&cmd_show_arp_entries_T_statistics,
        NULL,
    },
};

/*****************************************************************************
 * - "show arp statistics {details}"
 ****************************************************************************/
struct cmd_show_arp_statistics_result {
    cmdline_fixed_string_t show;
    cmdline_fixed_string_t arp;
    cmdline_fixed_string_t statistics;
    cmdline_fixed_string_t details;
};

static cmdline_parse_token_string_t cmd_show_arp_statistics_T_show =
    TOKEN_STRING_INITIALIZER(struct cmd_show_arp_statistics_result, show, "show");
static cmdline_parse_token_string_t cmd_show_arp_statistics_T_arp =
    TOKEN_STRING_INITIALIZER(struct cmd_show_arp_statistics_result, arp, "arp");
static cmdline_parse_token_string_t cmd_show_arp_statistics_T_statistics =
    TOKEN_STRING_INITIALIZER(struct cmd_show_arp_statistics_result, statistics, "statistics");
static cmdline_parse_token_string_t cmd_show_arp_statistics_T_details =
    TOKEN_STRING_INITIALIZER(struct cmd_show_arp_statistics_result, details, "details");

static void cmd_show_arp_statistics_parsed(void *parsed_result __rte_unused,
                                           struct cmdline *cl,
                                           void *data)
{
    int           port;
    int           option = (intptr_t) data;
    printer_arg_t parg = TPG_PRINTER_ARG(cli_printer, cl);

    for (port = 0; port < rte_eth_dev_count(); port++) {

        /*
         * Calculate totals first
         */
        tpg_arp_statistics_t total_stats;

        if (test_mgmt_get_arp_stats(port, &total_stats, &parg) != 0)
            continue;

        /*
         * Display individual counters
         */
        cmdline_printf(cl, "Port %d ARP statistics:\n", port);

        SHOW_64BIT_STATS("Received Requests", tpg_arp_statistics_t,
                         as_received_req,
                         port,
                         option);

        SHOW_64BIT_STATS("Received Replies", tpg_arp_statistics_t,
                         as_received_rep,
                         port,
                         option);

        SHOW_64BIT_STATS("Received \"other\"", tpg_arp_statistics_t,
                         as_received_other,
                         port,
                         option);

        SHOW_64BIT_STATS("Sent Requests", tpg_arp_statistics_t,
                         as_sent_req, port,
                         option);

        SHOW_64BIT_STATS("Sent Requests Failed", tpg_arp_statistics_t,
                         as_sent_req_failed,
                         port,
                         option);

        SHOW_64BIT_STATS("Sent Replies", tpg_arp_statistics_t,
                         as_sent_req, port,
                         option);

        SHOW_64BIT_STATS("Sent Replies Failed", tpg_arp_statistics_t,
                         as_sent_req_failed,
                         port,
                         option);

        cmdline_printf(cl, "\n");

        SHOW_64BIT_STATS("Request not mine", tpg_arp_statistics_t,
                         as_req_not_mine,
                         port,
                         option);

        cmdline_printf(cl, "\n");

        SHOW_32BIT_STATS("Small mbuf fragment", tpg_arp_statistics_t,
                         as_to_small_fragment,
                         port,
                         option);

        SHOW_32BIT_STATS("Invalid hw space", tpg_arp_statistics_t,
                         as_invalid_hw_space,
                         port,
                         option);

        SHOW_32BIT_STATS("Invalid hw length", tpg_arp_statistics_t,
                         as_invalid_hw_len,
                         port,
                         option);

        SHOW_32BIT_STATS("Invalid proto space", tpg_arp_statistics_t,
                         as_invalid_proto_space,
                         port,
                         option);

        SHOW_32BIT_STATS("Invalid proto length", tpg_arp_statistics_t,
                         as_invalid_proto_len,
                         port,
                         option);

        cmdline_printf(cl, "\n");
    }

}

cmdline_parse_inst_t cmd_show_arp_statistics = {
    .f = cmd_show_arp_statistics_parsed,
    .data = NULL,
    .help_str = "show arp statistics",
    .tokens = {
        (void *)&cmd_show_arp_statistics_T_show,
        (void *)&cmd_show_arp_statistics_T_arp,
        (void *)&cmd_show_arp_statistics_T_statistics,
        NULL,
    },
};

cmdline_parse_inst_t cmd_show_arp_statistics_details = {
    .f = cmd_show_arp_statistics_parsed,
    .data = (void *) (intptr_t) 'd',
    .help_str = "show arp statistics details",
    .tokens = {
        (void *)&cmd_show_arp_statistics_T_show,
        (void *)&cmd_show_arp_statistics_T_arp,
        (void *)&cmd_show_arp_statistics_T_statistics,
        (void *)&cmd_show_arp_statistics_T_details,
        NULL,
    },
};

/*****************************************************************************
 * Main menu context
 ****************************************************************************/
static cmdline_parse_ctx_t cli_ctx[] = {
    &cmd_show_arp_entries,
    &cmd_show_arp_statistics,
    &cmd_show_arp_statistics_details,
    NULL,
};

/*****************************************************************************
 * arp_init()
 ****************************************************************************/
bool arp_init(void)
{
    uint32_t port;

    /*
     * Add ARP module CLI commands
     */
    if (!cli_add_main_ctx(cli_ctx)) {
        RTE_LOG(ERR, USER1, "ERROR: Can't add ARP specific CLI commands!\n");
        return false;
    }

    /*
     * Allocate memory for ARP statistics, and clear all of them
     */
    if (STATS_GLOBAL_INIT(tpg_arp_statistics_t, "arp_stats") == NULL) {
        RTE_LOG(ERR, USER1,
                "ERROR: Failed allocating ARP statistics memory!\n");
        return false;
    }

    arp_per_port_tables =
        rte_zmalloc_socket("arp_tables",
                           rte_eth_dev_count() * sizeof(*arp_per_port_tables),
                           0,
                           rte_lcore_to_socket_id(rte_lcore_id()));
    if (arp_per_port_tables == NULL) {
        RTE_LOG(ERR, USER1,
                "ERROR: Failed allocating global ARP table memory!\n");
        return false;
    }

    for (port = 0; port < rte_eth_dev_count(); port++) {
        uint32_t port_socket;

        port_socket = port_dev_info[port].pi_numa_node;
        arp_per_port_tables[port] =
            rte_zmalloc_socket("arp_table_port",
                               TPG_ARP_PORT_TABLE_SIZE *
                               sizeof(*arp_per_port_tables[port]),
                               RTE_CACHE_LINE_SIZE,
                               port_socket);
        if (arp_per_port_tables[port] == NULL) {
            RTE_LOG(ERR, USER1,
                    "ERROR: Failed allocating per port ARP table memory!\n");
            return false;
        }
    }

    return true;
}

/*****************************************************************************
 * arp_lcore_init()
 ****************************************************************************/
void arp_lcore_init(uint32_t lcore_id)
{
    uint32_t port;

    RTE_PER_LCORE(arp_local_per_port_tables) =
        rte_zmalloc_socket("arp_local_tables",
                           rte_eth_dev_count() *
                           sizeof(*RTE_PER_LCORE(arp_local_per_port_tables)),
                           RTE_CACHE_LINE_SIZE,
                           rte_lcore_to_socket_id(lcore_id));
    if (RTE_PER_LCORE(arp_local_per_port_tables) == NULL) {
        TPG_ERROR_ABORT("[%d:%s() Failed to allocate per lcore arp_tables!\n",
                        rte_lcore_index(lcore_id),
                        __func__);
    }

    for (port = 0; port < rte_eth_dev_count(); port++) {
        RTE_PER_LCORE(arp_local_per_port_tables)[port] =
            arp_per_port_tables[port];
    }

    /* Init the local stats. */
    if (STATS_LOCAL_INIT(tpg_arp_statistics_t, "arp_stats", lcore_id) == NULL) {
        TPG_ERROR_ABORT("[%d:%s() Failed to allocate per lcore arp_stats!\n",
                        rte_lcore_index(lcore_id),
                        __func__);
    }
}

/*****************************************************************************
 * arp_update_entry()
 ****************************************************************************/
static bool arp_update_entry(uint32_t port, uint32_t ip, uint64_t mac, bool local,
                             uint32_t vlan_id)
{
    int          i;
    bool         rc = true;
    arp_entry_t *update_entry = NULL;
    arp_entry_t *free_entry = NULL;
    arp_entry_t *port_entries = RTE_PER_LCORE(arp_local_per_port_tables)[port];

    for (i = 0; i < TPG_ARP_PORT_TABLE_SIZE; i++) {
        if (!arp_is_entry_in_use(&port_entries[i])) {
            if (free_entry == NULL)
                free_entry = &port_entries[i];

            continue;
        }

        if (ip == port_entries[i].ae_ip_address) {
            update_entry = &port_entries[i];
            break;
        }
    }

    if (i >= TPG_ARP_PORT_TABLE_SIZE && free_entry == NULL) {

        RTE_LOG(ERR, USER2, "[%d:%s()] DBG: ARP table for port %d full!\n",
                rte_lcore_index(rte_lcore_id()),  __func__, port);

        rc = false;

    } else {

        if (free_entry != NULL) {
            /*
             * Use new entry, and add it...
             */
            update_entry = free_entry;
            update_entry->ae_ip_address = ip;
            update_entry->ae_mac_flags = 0;
            if (local)
                update_entry->ae_mac_flags |= TPG_ARP_FLAG_LOCAL;
        }

        arp_set_mac_in_entry_as_uint64(update_entry, mac);
        update_entry->ae_vlan_id = vlan_id;

        if (free_entry != NULL)
            arp_set_entry_in_use(update_entry);

        RTE_LOG(DEBUG, USER2,
                "[%d:%s()] DBG: %s ARP entry for IP %u.%u.%u.%u (local = %d) on port %d\n",
                rte_lcore_index(rte_lcore_id()),  __func__,
                free_entry == update_entry ? "Added" : "Updated",
                (update_entry->ae_ip_address >> 24) & 0xff,
                (update_entry->ae_ip_address >> 16) & 0xff,
                (update_entry->ae_ip_address >>  8) & 0xff,
                (update_entry->ae_ip_address >>  0) & 0xff,
                local,
                port);

    }

    return rc;
}

/*****************************************************************************
 * arp_delete_entry()
 * NOTE: this is really slow but we assume we only do ARP processing in the
 *       beginning of the tests!
 ****************************************************************************/
static bool arp_delete_entry(uint32_t port, uint32_t ip)
{
    int          i;
    arp_entry_t *port_entries = RTE_PER_LCORE(arp_local_per_port_tables)[port];

    for (i = 0; i < TPG_ARP_PORT_TABLE_SIZE; i++) {
        if (arp_is_entry_in_use(&port_entries[i]) &&
            port_entries[i].ae_ip_address == ip) {

            arp_clear_entry_in_use(&port_entries[i]);
            return true;
        }
    }

    return false;
}

/*****************************************************************************
 * arp_ipv4_to_uint8()
 ****************************************************************************/
static inline void arp_ipv4_to_uint8(uint32_t ip, uint8_t *mem)
{
    mem[0] = ip >> 24;
    mem[1] = ip >> 16;
    mem[2] = ip >> 8;
    mem[3] = ip;
}

/*****************************************************************************
 * arp_send_arp_reply()
 ****************************************************************************/
static bool arp_send_arp_reply(uint32_t port, uint32_t sip, uint32_t dip,
                               uint8_t *mac, uint16_t vlan_tci)
{
    struct rte_mbuf  *mbuf;
    struct arp_hdr   *arp;
    struct ether_hdr *eth;
    struct vlan_hdr  *tag_hdr;
    uint16_t          offset = 0;

    /*
     * Malloc MBUF and construct the ARP packet
     */

    mbuf = pkt_mbuf_alloc(mem_get_mbuf_local_pool());
    if (unlikely(mbuf == NULL)) {

        RTE_LOG(DEBUG, USER2, "[%d:%s()] ERR: Failed mbuf alloc for ARP reply on port %d\n",
                rte_lcore_index(rte_lcore_id()),  __func__, port);

        return false;
    }

    eth = rte_pktmbuf_mtod(mbuf, struct ether_hdr *);
    offset += sizeof(struct ether_hdr);

    /*
     * Here we assume mbuf segment is big enough to hold ethetnet, Vlan
     * and arp header
     */
    if (VLAN_TAG_ID(vlan_tci) == 0) {
        mbuf->data_len = sizeof(struct ether_hdr) + sizeof(struct arp_hdr);
        eth->ether_type = rte_cpu_to_be_16(ETHER_TYPE_ARP);
    } else {
        tag_hdr =
            (struct vlan_hdr *)rte_pktmbuf_mtod_offset(mbuf, struct vlan_hdr *,
                                                       offset);
        tag_hdr->vlan_tci = rte_cpu_to_be_16(vlan_tci);
        tag_hdr->eth_proto = rte_cpu_to_be_16(ETHER_TYPE_ARP);
        mbuf->data_len = sizeof(struct ether_hdr) + sizeof(struct vlan_hdr)
                                                  + sizeof(struct arp_hdr);
        mbuf->l2_len = sizeof(struct ether_hdr) + sizeof(struct vlan_hdr);
        offset += sizeof(struct vlan_hdr);
        eth->ether_type = rte_cpu_to_be_16(ETHER_TYPE_VLAN);
    }
    mbuf->pkt_len = mbuf->data_len;

    arp = (struct arp_hdr *)rte_pktmbuf_mtod_offset(mbuf, struct arp_hdr *,
                                                    offset);

    rte_memcpy(&eth->d_addr, mac, ETHER_ADDR_LEN);
    rte_eth_macaddr_get(port, &eth->s_addr);

    arp->arp_hrd = rte_cpu_to_be_16(ARP_HRD_ETHER);
    arp->arp_pro = rte_cpu_to_be_16(ETHER_TYPE_IPv4);
    arp->arp_hln = ETHER_ADDR_LEN;
    arp->arp_pln = sizeof(uint32_t);
    arp->arp_op = rte_cpu_to_be_16(ARP_OP_REPLY);

    rte_memcpy(arp->arp_data.arp_sha.addr_bytes, &eth->s_addr, ETHER_ADDR_LEN);
    arp->arp_data.arp_sip = rte_cpu_to_be_32(sip);
    rte_memcpy(arp->arp_data.arp_tha.addr_bytes, mac, ETHER_ADDR_LEN);
    arp->arp_data.arp_tip = rte_cpu_to_be_32(dip);

    /*
     * Send the ARP reply
     */

    if (unlikely(!pkt_send_with_hash(port, mbuf, 0, true))) {

        RTE_LOG(DEBUG, USER2, "[%d:%s()] ERR: Failed tx ARP reply for IP %u.%u.%u.%u on port %d\n",
                rte_lcore_index(rte_lcore_id()),  __func__,
                (sip >> 24) & 0xff, (sip >> 16) & 0xff,
                (sip >>  8) & 0xff, (sip >>  0) & 0xff,
                port);

        INC_STATS(STATS_LOCAL(tpg_arp_statistics_t, port), as_sent_rep_failed);
        return false;
    }

    INC_STATS(STATS_LOCAL(tpg_arp_statistics_t, port), as_sent_rep);
    return true;
}

/*****************************************************************************
 * arp_send_arp_request()
 ****************************************************************************/
bool arp_send_arp_request(uint32_t port, uint32_t local_ip, uint32_t remote_ip,
                          uint16_t vlan_tci)
{
    struct rte_mbuf  *mbuf;
    struct arp_hdr   *arp;
    struct ether_hdr *eth;
    struct vlan_hdr  *tag_hdr;
    uint16_t          offset = 0;

    /*
     * Malloc MBUF and construct the ARP packet
     */

    mbuf = pkt_mbuf_alloc(mem_get_mbuf_local_pool());
    if (unlikely(mbuf == NULL)) {

        RTE_LOG(DEBUG, USER2, "[%d:%s()] ERR: Failed mbuf alloc for ARP request on port %d\n",
                rte_lcore_index(rte_lcore_id()),  __func__, port);

        return false;
    }

    eth = rte_pktmbuf_mtod(mbuf, struct ether_hdr *);
    offset += sizeof(struct ether_hdr);

    /*
     * Here we assume mbuf segment is big enough to hold ethetnet and arp header
     */
    if (VLAN_TAG_ID(vlan_tci) == 0) {
        mbuf->data_len = sizeof(struct ether_hdr) + sizeof(struct arp_hdr);
        eth->ether_type = rte_cpu_to_be_16(ETHER_TYPE_ARP);
    } else{
        tag_hdr =
            (struct vlan_hdr *)rte_pktmbuf_mtod_offset(mbuf, struct vlan_hdr *,
                                                       offset);
        tag_hdr->vlan_tci = rte_cpu_to_be_16(vlan_tci);
        tag_hdr->eth_proto = rte_cpu_to_be_16(ETHER_TYPE_ARP);
        mbuf->data_len = sizeof(struct ether_hdr) + sizeof(struct vlan_hdr)
                                                  + sizeof(struct arp_hdr);
        mbuf->l2_len = sizeof(struct ether_hdr) + sizeof(struct vlan_hdr);
        offset += sizeof(struct vlan_hdr);
        eth->ether_type = rte_cpu_to_be_16(ETHER_TYPE_VLAN);
    }

    mbuf->pkt_len = mbuf->data_len;
    memset(&eth->d_addr, 0xff, ETHER_ADDR_LEN);
    rte_eth_macaddr_get(port, &eth->s_addr);

    arp = rte_pktmbuf_mtod_offset(mbuf, struct arp_hdr *, offset);
    if (unlikely(!arp)) {
        pkt_mbuf_free(mbuf);
        return false;
    }
    arp->arp_hrd = rte_cpu_to_be_16(ARP_HRD_ETHER);
    arp->arp_pro = rte_cpu_to_be_16(ETHER_TYPE_IPv4);
    arp->arp_hln = ETHER_ADDR_LEN;
    arp->arp_pln = sizeof(uint32_t);
    arp->arp_op = rte_cpu_to_be_16(ARP_OP_REQUEST);

    rte_memcpy(arp->arp_data.arp_sha.addr_bytes, &eth->s_addr, ETHER_ADDR_LEN);
    arp->arp_data.arp_sip = rte_cpu_to_be_32(local_ip);
    bzero(arp->arp_data.arp_tha.addr_bytes, ETHER_ADDR_LEN);
    arp->arp_data.arp_tip = rte_cpu_to_be_32(remote_ip);

    /*
     * Send the ARP req
     */
    if (unlikely(!pkt_send_with_hash(port, mbuf, 0, true))) {

        RTE_LOG(DEBUG, USER2,
            "[%d:%s()] ERR: Failed tx ARP request for IP %u.%u.%u.%u on port %d\n",
            rte_lcore_index(rte_lcore_id()),  __func__,
            (remote_ip >> 24) & 0xff, (remote_ip >> 16) & 0xff,
            (remote_ip >>  8) & 0xff, (remote_ip >>  0) & 0xff,
            port);

        INC_STATS(STATS_LOCAL(tpg_arp_statistics_t, port), as_sent_req_failed);
        return false;
    }

    INC_STATS(STATS_LOCAL(tpg_arp_statistics_t, port), as_sent_req);
    return true;
}

/*****************************************************************************
 * arp_send_grat_arp_request()
 ****************************************************************************/
bool arp_send_grat_arp_request(uint32_t port, uint32_t ip, uint16_t vlan_tci)
{
    return arp_send_arp_request(port, ip, ip, vlan_tci);
}

/*****************************************************************************
 * arp_send_grat_arp_reply()
 ****************************************************************************/
bool arp_send_grat_arp_reply(uint32_t port, uint32_t ip, uint16_t vlan_tci)
{
    return arp_send_arp_reply(port, ip, ip, arp_bcast_addr, vlan_tci);
}

/*****************************************************************************
 * arp_lookup()
 ****************************************************************************/
static arp_entry_t *arp_lookup(uint32_t port, uint32_t ip, uint16_t vlan_id)
{
    int          i;
    arp_entry_t *port_entries = RTE_PER_LCORE(arp_local_per_port_tables)[port];

    for (i = 0; i < TPG_ARP_PORT_TABLE_SIZE; i++) {
        if (arp_is_entry_in_use(&port_entries[i]) &&
            port_entries[i].ae_ip_address == ip &&
            port_entries[i].ae_vlan_id == vlan_id) {

            return &port_entries[i];
        }
    }

    return NULL;
}

/*****************************************************************************
 * arp_lookup_mac()
 ****************************************************************************/
uint64_t arp_lookup_mac(uint32_t port, uint32_t ip, uint16_t vlan_id)
{
    arp_entry_t *arp;

    arp = arp_lookup(port, ip, vlan_id);
    if (!arp)
        return TPG_ARP_MAC_NOT_FOUND;

    return arp_get_mac_from_entry_as_uint64(arp);
}

/*****************************************************************************
 * arp_add_local()
 ****************************************************************************/
bool arp_add_local(uint32_t port, uint32_t ip, uint16_t vlan_id)
{
    struct ether_addr mac;

    rte_eth_macaddr_get(port, &mac);

    return arp_update_entry(port, ip, eth_mac_to_uint64(&mac.addr_bytes[0]),
                            true, vlan_id);
}

/*****************************************************************************
 * arp_delete_local()
 ****************************************************************************/
bool arp_delete_local(uint32_t port, uint32_t ip, uint16_t vlan_id)
{
    arp_entry_t *local_arp;

    local_arp = arp_lookup(port, ip, vlan_id);
    if (local_arp == NULL)
        return false;

    if (!ARP_IS_FLAG_SET(local_arp, TPG_ARP_FLAG_LOCAL))
        return false;

    local_arp->ae_mac_flags &= ~TPG_ARP_FLAG_LOCAL;

    return arp_delete_entry(port, ip);
}

/*****************************************************************************
 * arp_process_request()
 ****************************************************************************/
static void arp_process_request(packet_control_block_t *pcb, uint16_t vlan_id)
{
    struct arp_hdr *arp_hdr = pcb->pcb_arp;
    uint32_t        arp_req_ip;
    uint32_t        arp_req_sip;
    arp_entry_t    *local_arp;

    /*
     * ARP ip addresses are not 32 bit alligned, so just get it this way...
     */
    arp_req_ip = rte_be_to_cpu_32(arp_hdr->arp_data.arp_tip);

    /*
     * If the request is not for this port ignore it....
     */
    local_arp = arp_lookup(pcb->pcb_port, arp_req_ip, vlan_id);
    if (local_arp == NULL || !ARP_IS_FLAG_SET(local_arp, TPG_ARP_FLAG_LOCAL)) {
        INC_STATS(STATS_LOCAL(tpg_arp_statistics_t, pcb->pcb_port),
                  as_req_not_mine);
        return;
    }

    RTE_LOG(DEBUG, USER2, "[%d:%s()] DBG: ARP request for port %u's MAC\n",
            pcb->pcb_core_index, __func__, pcb->pcb_port);

    arp_req_sip = rte_be_to_cpu_32(arp_hdr->arp_data.arp_sip);

    arp_send_arp_reply(pcb->pcb_port, arp_req_ip, arp_req_sip,
                       arp_hdr->arp_data.arp_sha.addr_bytes, vlan_id);

    /*
     * TODO: a real stack would probably check for duplicate IP,
     * i.e. is the source IP any of mine
     */
    RTE_LOG(DEBUG, USER2, "[%d:%s()] DBG: ARP request received, update entry\n",
            pcb->pcb_core_index, __func__);

    arp_update_entry(pcb->pcb_port, arp_req_sip,
                     eth_mac_to_uint64(arp_hdr->arp_data.arp_sha.addr_bytes),
                     false, vlan_id);
}

/*****************************************************************************
 * arp_process_reply()
 ****************************************************************************/
static void arp_process_reply(packet_control_block_t *pcb, uint16_t vlan_id)
{
    struct arp_hdr *arp_hdr = pcb->pcb_arp;
    uint32_t        arp_req_ip;

    /*
     * ARP ip addresses are not 32 bit alligned, so just get it this way...
     */
    arp_req_ip = rte_be_to_cpu_32(arp_hdr->arp_data.arp_sip);

    /*
     * TODO: a real stack would probably try to detect a duplicate IP,
     *       is the source IP any of mine
     */

    RTE_LOG(DEBUG, USER2, "[%d:%s()] DBG: ARP reply from DUT, update entry\n",
            pcb->pcb_core_index, __func__);

    arp_update_entry(pcb->pcb_port, arp_req_ip,
                     eth_mac_to_uint64(arp_hdr->arp_data.arp_sha.addr_bytes),
                     false, vlan_id);
}

/*****************************************************************************
 * arp_receive_pkt()
 *
 * Return the mbuf only if it needs to be free'ed back to the pool, if it was
 * consumed, or needed later (ip refrag), return NULL.
 ****************************************************************************/
struct rte_mbuf *arp_receive_pkt(packet_control_block_t *pcb,
                                 struct rte_mbuf *mbuf,
                                 uint16_t vlan_tci)
{
    uint16_t        op;
    struct arp_hdr *arp_hdr;

    if (unlikely(rte_pktmbuf_data_len(mbuf) < sizeof(struct arp_hdr))) {
        RTE_LOG(DEBUG, USER2, "[%d:%s()] ERR: mbuf fragment to small for arp_hdr!\n",
                pcb->pcb_core_index, __func__);

        INC_STATS(STATS_LOCAL(tpg_arp_statistics_t, pcb->pcb_port),
                  as_to_small_fragment);
        return mbuf;
    }

    if (unlikely(PORT_CORE_DEFAULT(pcb->pcb_port) != rte_lcore_id())) {
        /*
         * We assume that the current DPDK returns all ARP (L2) traffic
         * to the first core assigned to the port. This allows us to
         * update/manipulate ARPs without having to add a write lock.
         */
        assert(false);
        return mbuf;
    }

    arp_hdr = rte_pktmbuf_mtod(mbuf, struct arp_hdr *);
    op = rte_be_to_cpu_16(arp_hdr->arp_op);

    PKT_TRACE(pcb, ARP, DEBUG, "hrd=%u, pro=0x%4.4X, hln=%u, pln=%u, op=%u",
              rte_be_to_cpu_16(arp_hdr->arp_hrd),
              rte_be_to_cpu_16(arp_hdr->arp_pro),
              arp_hdr->arp_hln, arp_hdr->arp_pln,
              op);

    PKT_TRACE(pcb, ARP, DEBUG, "sha=%02X:%02X:%02X:%02X:%02X:%02X spa=" TPG_IPV4_PRINT_FMT,
              arp_hdr->arp_data.arp_sha.addr_bytes[0],
              arp_hdr->arp_data.arp_sha.addr_bytes[1],
              arp_hdr->arp_data.arp_sha.addr_bytes[2],
              arp_hdr->arp_data.arp_sha.addr_bytes[3],
              arp_hdr->arp_data.arp_sha.addr_bytes[4],
              arp_hdr->arp_data.arp_sha.addr_bytes[5],
              TPG_IPV4_PRINT_ARGS(rte_be_to_cpu_32(arp_hdr->arp_data.arp_sip)));

    PKT_TRACE(pcb, ARP, DEBUG, "tha=%02X:%02X:%02X:%02X:%02X:%02X, tpa=" TPG_IPV4_PRINT_FMT,
              arp_hdr->arp_data.arp_tha.addr_bytes[0],
              arp_hdr->arp_data.arp_tha.addr_bytes[1],
              arp_hdr->arp_data.arp_tha.addr_bytes[2],
              arp_hdr->arp_data.arp_tha.addr_bytes[3],
              arp_hdr->arp_data.arp_tha.addr_bytes[4],
              arp_hdr->arp_data.arp_tha.addr_bytes[5],
              TPG_IPV4_PRINT_ARGS(rte_be_to_cpu_32(arp_hdr->arp_data.arp_tip)));

        switch (op) {
        case ARP_OP_REQUEST:
            INC_STATS(STATS_LOCAL(tpg_arp_statistics_t, pcb->pcb_port),
                      as_received_req);
            break;
        case ARP_OP_REPLY:
            INC_STATS(STATS_LOCAL(tpg_arp_statistics_t, pcb->pcb_port),
                      as_received_rep);
            break;

        default:
            /*
             * All other type we do not handle, so return
             */
            INC_STATS(STATS_LOCAL(tpg_arp_statistics_t, pcb->pcb_port),
                      as_received_other);
            return mbuf;
        }

        /*
         * Check handler before we process the ARP packet
         */
        if (unlikely(rte_be_to_cpu_16(arp_hdr->arp_hrd) != ARP_HRD_ETHER)) {
            RTE_LOG(DEBUG, USER2, "[%d:%s()] ERR: ARP hardware protocol is not ethernet!\n",
                    pcb->pcb_core_index, __func__);

            INC_STATS(STATS_LOCAL(tpg_arp_statistics_t, pcb->pcb_port),
                      as_invalid_hw_space);
            return mbuf;
        }

        if (unlikely(rte_be_to_cpu_16(arp_hdr->arp_pro) != ETHER_TYPE_IPv4)) {
            RTE_LOG(DEBUG, USER2, "[%d:%s()] ERR: Protocol is not IPv4!\n",
                    pcb->pcb_core_index, __func__);

            INC_STATS(STATS_LOCAL(tpg_arp_statistics_t, pcb->pcb_port),
                      as_invalid_proto_space);
            return mbuf;
        }

        if (unlikely(arp_hdr->arp_hln != ETHER_ADDR_LEN)) {
            RTE_LOG(DEBUG, USER2, "[%d:%s()] ERR: Hardware length is not 6!\n",
                    pcb->pcb_core_index, __func__);

            INC_STATS(STATS_LOCAL(tpg_arp_statistics_t, pcb->pcb_port),
                      as_invalid_hw_len);
            return mbuf;
        }

        if (unlikely(arp_hdr->arp_pln != sizeof(uint32_t))) {
            RTE_LOG(DEBUG, USER2, "[%d:%s()] ERR: Protocol length is not 4!\n",
                    pcb->pcb_core_index, __func__);

            INC_STATS(STATS_LOCAL(tpg_arp_statistics_t, pcb->pcb_port),
                      as_invalid_proto_len);
            return mbuf;
        }


        pcb->pcb_arp = arp_hdr;

        if (op == ARP_OP_REQUEST)
            arp_process_request(pcb, VLAN_TAG_ID(vlan_tci));
        else if (op == ARP_OP_REPLY)
            arp_process_reply(pcb, VLAN_TAG_ID(vlan_tci));

        return mbuf;
}
