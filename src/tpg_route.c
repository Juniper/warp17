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
 *     tpg_route.c
 *
 * Description:
 *     Route lookup.
 *
 * Author:
 *     Dumitru Ceara, Eelco Chaudron
 *
 * Initial Created:
 *     08/24/2015
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
/* Define ROUTE global statistics. Each thread has its own set of locally
 * allocated stats which are accessible through STATS_GLOBAL(type, core, port).
 */
STATS_DEFINE(route_statistics_t);

static route_entry_t *route_per_port_table; /* local_intf[port][entries] */
static route_entry_t *default_gw_per_port;  /* default_gw[port] */

/*****************************************************************************
 * Forward declarations
 ****************************************************************************/
static cmdline_parse_ctx_t cli_ctx[];

/*****************************************************************************
 * route_update_entry()
 ****************************************************************************/
static bool route_update_entry(uint32_t port, tpg_ip_t *net, tpg_ip_t *mask,
                               tpg_ip_t *nh, uint32_t flags)
{
    int            i;
    route_entry_t *update_entry = NULL;
    route_entry_t *free_entry = NULL;
    route_entry_t *port_entries = route_per_port_table +
                                      (TPG_ROUTE_PORT_TABLE_SIZE * port);

    for (i = 0; i < TPG_ROUTE_PORT_TABLE_SIZE; i++) {
        if (!ROUTE_IS_FLAG_SET(&port_entries[i], ROUTE_FLAG_IN_USE)) {
            if (free_entry == NULL)
                free_entry = &port_entries[i];

            continue;
        }

        /* No ECMP support! */
        if (net->ip_v4 == port_entries[i].re_net.ip_v4 &&
                mask->ip_v4 == port_entries[i].re_mask.ip_v4) {
            update_entry = &port_entries[i];
            break;
        }
    }

    if (i >= TPG_ROUTE_PORT_TABLE_SIZE && free_entry == NULL) {
        INC_STATS(STATS_LOCAL(route_statistics_t, port), rs_tbl_full);
        return false;
    }

    if (free_entry != NULL) {
        /*
         * Use new entry, and add it...
         */
        update_entry = free_entry;
        update_entry->re_flags |= ROUTE_FLAG_IN_USE;
    }
    *update_entry = ROUTE_V4(net->ip_v4, mask->ip_v4, nh->ip_v4,
                             (update_entry->re_flags | flags));

    return true;
}

/*****************************************************************************
 * route_rem_entry()
 ****************************************************************************/
static bool route_rem_entry(uint32_t port, tpg_ip_t *net, tpg_ip_t *mask)
{
    int            i;
    route_entry_t *port_entries = route_per_port_table +
                                  (TPG_ROUTE_PORT_TABLE_SIZE * port);

    for (i = 0; i < TPG_ROUTE_PORT_TABLE_SIZE; i++) {
        if (ROUTE_IS_FLAG_SET(&port_entries[i], ROUTE_FLAG_IN_USE) &&
                port_entries[i].re_net.ip_v4 == net->ip_v4 &&
                port_entries[i].re_mask.ip_v4 == mask->ip_v4) {
            port_entries[i].re_flags &= ~ROUTE_FLAG_IN_USE;
            return true;
        }
    }

    return false;
}

/*****************************************************************************
 * route_intf_add_cb()
 ****************************************************************************/
static int route_intf_add_cb(uint16_t msgid, uint16_t lcore, void *msg)
{
    route_intf_add_msg_t *add_msg;
    tpg_ip_t              nh_zero;

    if (MSG_INVALID(msgid, msg, MSG_ROUTE_INTF_ADD))
        return -EINVAL;

    add_msg = msg;

    assert(PORT_CORE_DEFAULT(add_msg->rim_eth_port) == lcore);

    /*
     * Instead of adding a directly connected route, we will just abuse the
     * ARP table..
     */
    if (!arp_add_local(add_msg->rim_eth_port, add_msg->rim_ip.ip_v4)) {
        INC_STATS(STATS_LOCAL(route_statistics_t, add_msg->rim_eth_port),
                  rs_intf_nomem);
        return -ENOMEM;
    }

    nh_zero = TPG_IPV4(IPv4(0, 0, 0, 0));
    if (!route_update_entry(add_msg->rim_eth_port, &add_msg->rim_ip,
                            &add_msg->rim_mask,
                            &nh_zero,
                            ROUTE_FLAG_LOCAL)) {
        INC_STATS(STATS_LOCAL(route_statistics_t, add_msg->rim_eth_port),
                  rs_intf_nomem);
        return -ENOMEM;
    }


    INC_STATS(STATS_LOCAL(route_statistics_t, add_msg->rim_eth_port),
              rs_intf_add);

    arp_send_grat_arp_request(add_msg->rim_eth_port, add_msg->rim_ip.ip_v4);
    arp_send_grat_arp_reply(add_msg->rim_eth_port, add_msg->rim_ip.ip_v4);

    /*
     * If this is to be a full stack we shouldn't flush after
     * every packet. But it's not :) We expect ARPs to be installed only in
     * the beginning of the test.
     */
    /*
     * Flush the bulk tx queue to make sure the ARPs are sent.
     */
    pkt_flush_tx_q(add_msg->rim_eth_port,
                   STATS_LOCAL(port_statistics_t, add_msg->rim_eth_port));
    return 0;
}

/*****************************************************************************
 * route_intf_del_cb()
 ****************************************************************************/
static int route_intf_del_cb(uint16_t msgid, uint16_t lcore __rte_unused,
                                void *msg)
{
    route_intf_del_msg_t *del_msg;

    if (MSG_INVALID(msgid, msg, MSG_ROUTE_INTF_DEL))
        return -EINVAL;

    del_msg = msg;

    assert(PORT_CORE_DEFAULT(del_msg->rim_eth_port) == lcore);

    /*
     * Just abuse the ARP table..
     */
    if (!arp_delete_local(del_msg->rim_eth_port, del_msg->rim_ip.ip_v4)) {
        INC_STATS(STATS_LOCAL(route_statistics_t, del_msg->rim_eth_port),
                  rs_intf_notfound);
        return -ENOENT;
    }

    if (!route_rem_entry(del_msg->rim_eth_port, &del_msg->rim_ip,
                         &del_msg->rim_mask)) {
        INC_STATS(STATS_LOCAL(route_statistics_t, del_msg->rim_eth_port),
                  rs_intf_notfound);
        return -ENOENT;
    }

    INC_STATS(STATS_LOCAL(route_statistics_t, del_msg->rim_eth_port),
              rs_intf_del);

    return 0;
}

/*****************************************************************************
 * route_gw_add_cb()
 ****************************************************************************/
static int route_gw_add_cb(uint16_t msgid, uint16_t lcore, void *msg)
{
    route_gw_add_msg_t *add_msg;
    uint32_t            port;
    route_entry_t      *local_intf;

    if (MSG_INVALID(msgid, msg, MSG_ROUTE_GW_ADD))
        return -EINVAL;

    add_msg = msg;
    port = add_msg->rgm_eth_port;

    assert(PORT_CORE_DEFAULT(port) == lcore);

    default_gw_per_port[port] = ROUTE_V4(IPv4(0, 0, 0, 0), IPv4(0, 0, 0, 0),
                                         add_msg->rgm_gw.ip_v4,
                                         ROUTE_FLAG_IN_USE);

    INC_STATS(STATS_LOCAL(route_statistics_t, port), rs_gw_add);

    /* Find local network matching default gw. */
    local_intf = route_v4_find_local(port,
                                     default_gw_per_port[port].re_nh.ip_v4);
    if (!local_intf) {
        INC_STATS(STATS_LOCAL(route_statistics_t, port), rs_gw_nointf);
        return -EINVAL;
    }

    arp_send_arp_request(port, local_intf->re_net.ip_v4,
                         default_gw_per_port[port].re_nh.ip_v4);

    /*
     * If this is to be a full stack we shouldn't flush after
     * every packet. But it's not :) We expect ARPs to be installed only in
     * the beginning of the test.
     */
    /*
     * Flush the bulk tx queue to make sure the ARPs are sent.
     */
    pkt_flush_tx_q(port, STATS_LOCAL(port_statistics_t, port));
    return 0;
}

/*****************************************************************************
 * route_gw_del_cb()
 ****************************************************************************/
static int route_gw_del_cb(uint16_t msgid, uint16_t lcore __rte_unused,
                           void *msg)
{
    route_gw_del_msg_t *gwmsg;
    uint32_t            port;

    if (MSG_INVALID(msgid, msg, MSG_ROUTE_GW_DEL))
        return -EINVAL;

    gwmsg = msg;
    port = gwmsg->rgm_eth_port;

    assert(PORT_CORE_DEFAULT(port) == lcore);

    INC_STATS(STATS_LOCAL(route_statistics_t, port), rs_gw_del);

    default_gw_per_port[port].re_flags &= ~ROUTE_FLAG_IN_USE;

    return 0;
}

/*****************************************************************************
 * route_init()
 ****************************************************************************/
bool route_init(void)
{
    int error;

    /*
     * Add ROUTE module CLI commands
     */
    if (!cli_add_main_ctx(cli_ctx)) {
        RTE_LOG(ERR, USER1, "ERROR: Can't add ROUTE specific CLI commands!\n");
        return false;
    }

    /*
     * Allocate per port routing table.
     */
    route_per_port_table = rte_zmalloc("route_tables",
                                       rte_eth_dev_count() *
                                       TPG_ROUTE_PORT_TABLE_SIZE *
                                       sizeof(*route_per_port_table),
                                       0);
    if (route_per_port_table == NULL) {
        RTE_LOG(ERR, USER1, "ERROR: Failed allocating ROUTE table memory!\n");
        return false;
    }

    default_gw_per_port = rte_zmalloc("default_gw_per_port",
                                      rte_eth_dev_count() *
                                      sizeof(*default_gw_per_port),
                                      0);
    if (default_gw_per_port == NULL) {
        RTE_LOG(ERR, USER1, "ERROR: Failed allocating Default GW memory!\n");
        return false;
    }

    /*
     * Allocate memory for ROUTE statistics, and clear all of them
     */
    if (STATS_GLOBAL_INIT(route_statistics_t, "route_stats") == NULL) {
        RTE_LOG(ERR, USER1,
                "ERROR: Failed allocating ROUTE statistics memory!\n");
        return false;
    }

    /*
     * Register TPG MSG handlers.
     */
    while (true) {

        error = msg_register_handler(MSG_ROUTE_INTF_ADD, route_intf_add_cb);
        if (error)
            break;
        error = msg_register_handler(MSG_ROUTE_INTF_DEL, route_intf_del_cb);
        if (error)
            break;
        error = msg_register_handler(MSG_ROUTE_GW_ADD, route_gw_add_cb);
        if (error)
            break;
        error = msg_register_handler(MSG_ROUTE_GW_DEL, route_gw_del_cb);
        if (error)
            break;

        return true;
    }

    RTE_LOG(ERR, USER1, "Failed to register route msg handler: %s(%d)\n",
            rte_strerror(-error), -error);

    return false;
}

/*****************************************************************************
 * route_lcore_init()
 ****************************************************************************/
void route_lcore_init(uint32_t lcore_id)
{
    /* Init the local stats. */
    if (STATS_LOCAL_INIT(route_statistics_t, "route_stats", lcore_id) == NULL) {
        TPG_ERROR_ABORT("[%d:%s() Failed to allocate per lcore route_stats!\n",
                        rte_lcore_index(lcore_id),
                        __func__);
    }
}

/*****************************************************************************
 * route_v4_intf_add()
 ****************************************************************************/
int route_v4_intf_add(uint32_t port, tpg_ip_t ip, tpg_ip_t mask)
{
    MSG_LOCAL_DEFINE(route_intf_add_msg_t, msg);
    route_intf_add_msg_t *add_msg;
    msg_t                *msgp;
    int                   error;

    msgp = MSG_LOCAL(msg);

    msg_init(msgp, MSG_ROUTE_INTF_ADD, PORT_CORE_DEFAULT(port), 0);

    add_msg = MSG_INNER(route_intf_add_msg_t, msgp);
    add_msg->rim_eth_port = port;
    add_msg->rim_ip = ip;
    add_msg->rim_mask = mask;

    /* BLOCK waiting for msg to be processed */
    error = msg_send(msgp, 0);
    if (error)
        TPG_ERROR_ABORT("ERROR: Failed to send intf add msg: %s(%d)!\n",
                        rte_strerror(-error), -error);

    return 0;
}

/*****************************************************************************
 * route_v4_intf_del()
 ****************************************************************************/
int route_v4_intf_del(uint32_t port, tpg_ip_t ip, tpg_ip_t mask)
{
    MSG_LOCAL_DEFINE(route_intf_del_msg_t, msg);
    route_intf_del_msg_t *del_msg;
    msg_t                *msgp;
    int                   error;

    msgp = MSG_LOCAL(msg);

    msg_init(msgp, MSG_ROUTE_INTF_DEL, PORT_CORE_DEFAULT(port), 0);

    del_msg = MSG_INNER(route_intf_del_msg_t, msgp);
    del_msg->rim_eth_port = port;
    del_msg->rim_ip = ip;
    del_msg->rim_mask = mask;

    /* BLOCK waiting for msg to be processed */
    error = msg_send(msgp, 0);
    if (error)
        TPG_ERROR_ABORT("ERROR: Failed to send intf del msg: %s(%d)!\n",
                        rte_strerror(-error), -error);

    return 0;
}

/*****************************************************************************
 * route_v4_gw_add()
 ****************************************************************************/
int route_v4_gw_add(uint32_t port, tpg_ip_t gw)
{
    MSG_LOCAL_DEFINE(route_gw_add_msg_t, msg);
    route_gw_add_msg_t *add_msg;
    msg_t              *msgp;
    int                 error;

    msgp = MSG_LOCAL(msg);

    msg_init(msgp, MSG_ROUTE_GW_ADD, PORT_CORE_DEFAULT(port), 0);

    add_msg = MSG_INNER(route_gw_add_msg_t, msgp);
    add_msg->rgm_eth_port = port;
    add_msg->rgm_gw = gw;

    /* BLOCK waiting for msg to be processed */
    error = msg_send(msgp, 0);
    if (error)
        TPG_ERROR_ABORT("ERROR: Failed to send gw add msg: %s(%d)!\n",
                        rte_strerror(-error), -error);

    return 0;
}

/*****************************************************************************
 * route_v4_gw_del()
 ****************************************************************************/
int route_v4_gw_del(uint32_t port, tpg_ip_t gw)
{
    MSG_LOCAL_DEFINE(route_gw_del_msg_t, msg);
    route_gw_del_msg_t *del_msg;
    msg_t              *msgp;
    int                 error;

    msgp = MSG_LOCAL(msg);

    msg_init(msgp, MSG_ROUTE_GW_DEL, PORT_CORE_DEFAULT(port), 0);

    del_msg = MSG_INNER(route_gw_del_msg_t, msgp);
    del_msg->rgm_eth_port = port;
    del_msg->rgm_gw = gw;

    /* BLOCK waiting for msg to be processed */
    error = msg_send(msgp, 0);
    if (error)
        TPG_ERROR_ABORT("ERROR: Failed to send gw del msg: %s(%d)!\n",
                        rte_strerror(-error), -error);

    return 0;
}

/*****************************************************************************
 * route_v4_nh_lookup()
 *  NOTES: the function directly returns the MAC address of the nexthop.
 ****************************************************************************/
uint64_t route_v4_nh_lookup(uint32_t port, uint32_t dest)
{
    uint64_t nh_mac;

    /* TODO: lookup directly connected networks.
     * For now we use a hack and look for the ARP in case the destination
     * is directly connected.
     */
    nh_mac = arp_lookup_mac(port, dest);
    if (nh_mac != TPG_ARP_MAC_NOT_FOUND)
        return nh_mac;

    nh_mac = arp_lookup_mac(port, default_gw_per_port[port].re_nh.ip_v4);
    if (nh_mac == TPG_ARP_MAC_NOT_FOUND) {
        INC_STATS(STATS_LOCAL(route_statistics_t, port), rs_nh_not_found);
    }

    return nh_mac;
}

/*****************************************************************************
 * route_v4_find_local()
 ****************************************************************************/
route_entry_t *route_v4_find_local(uint32_t port, uint32_t dest)
{
    int            i;
    route_entry_t *port_entries = route_per_port_table +
                                  (TPG_ROUTE_PORT_TABLE_SIZE * port);

    for (i = 0; i < TPG_ROUTE_PORT_TABLE_SIZE; i++) {
        if (ROUTE_IS_FLAG_SET(&port_entries[i], ROUTE_FLAG_IN_USE) &&
                ROUTE_IS_FLAG_SET(&port_entries[i], ROUTE_FLAG_LOCAL) &&
                ((port_entries[i].re_net.ip_v4 & port_entries[i].re_mask.ip_v4) ==
                    (dest & port_entries[i].re_mask.ip_v4))) {

            return &port_entries[i];
        }
    }

    INC_STATS(STATS_LOCAL(route_statistics_t, port), rs_route_not_found);

    return NULL;
}

/*****************************************************************************
 * - "show route statistics {details}"
 ****************************************************************************/
struct cmd_show_route_statistics_result {
    cmdline_fixed_string_t show;
    cmdline_fixed_string_t route;
    cmdline_fixed_string_t statistics;
    cmdline_fixed_string_t details;
};

static cmdline_parse_token_string_t cmd_show_route_statistics_T_show =
    TOKEN_STRING_INITIALIZER(struct cmd_show_route_statistics_result, show, "show");
static cmdline_parse_token_string_t cmd_show_route_statistics_T_route =
    TOKEN_STRING_INITIALIZER(struct cmd_show_route_statistics_result, route, "route");
static cmdline_parse_token_string_t cmd_show_route_statistics_T_statistics =
    TOKEN_STRING_INITIALIZER(struct cmd_show_route_statistics_result, statistics, "statistics");
static cmdline_parse_token_string_t cmd_show_route_statistics_T_details =
    TOKEN_STRING_INITIALIZER(struct cmd_show_route_statistics_result, details, "details");

static void cmd_show_route_statistics_parsed(void *parsed_result __rte_unused,
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
        route_statistics_t  total_stats;
        route_statistics_t *route_stats;

        bzero(&total_stats, sizeof(total_stats));

        STATS_FOREACH_CORE(route_statistics_t, port, core, route_stats) {
            total_stats.rs_intf_add += route_stats->rs_intf_add;
            total_stats.rs_intf_del += route_stats->rs_intf_del;
            total_stats.rs_gw_add += route_stats->rs_gw_add;
            total_stats.rs_gw_del += route_stats->rs_gw_del;

            total_stats.rs_tbl_full += route_stats->rs_tbl_full;
            total_stats.rs_intf_nomem += route_stats->rs_intf_nomem;
            total_stats.rs_intf_notfound += route_stats->rs_intf_notfound;
            total_stats.rs_gw_nointf += route_stats->rs_gw_nointf;
            total_stats.rs_nh_not_found += route_stats->rs_nh_not_found;
            total_stats.rs_route_not_found += route_stats->rs_route_not_found;
        }

        /*
         * Display individual counters
         */
        cmdline_printf(cl, "Port %d Route statistics:\n", port);

        SHOW_32BIT_STATS("Intf Add", route_statistics_t, rs_intf_add,
                         port,
                         option);

        SHOW_32BIT_STATS("Intf Del", route_statistics_t, rs_intf_del,
                         port,
                         option);

        SHOW_32BIT_STATS("Gw Add", route_statistics_t, rs_gw_add,
                         port,
                         option);

        SHOW_32BIT_STATS("Gw Del", route_statistics_t, rs_gw_del,
                         port,
                         option);

        cmdline_printf(cl, "\n");

        SHOW_32BIT_STATS("Route Tbl Full", route_statistics_t, rs_tbl_full,
                         port,
                         option);

        SHOW_32BIT_STATS("Intf No Mem", route_statistics_t, rs_intf_nomem,
                         port,
                         option);

        SHOW_32BIT_STATS("Intf Not Found", route_statistics_t, rs_intf_notfound,
                         port,
                         option);

        SHOW_32BIT_STATS("Gw Intf Not Found", route_statistics_t, rs_gw_nointf,
                         port,
                         option);

        SHOW_32BIT_STATS("NH not found", route_statistics_t, rs_nh_not_found,
                         port,
                         option);

        SHOW_32BIT_STATS("Route not found", route_statistics_t,
                         rs_route_not_found,
                         port,
                         option);

        cmdline_printf(cl, "\n");
    }

}

cmdline_parse_inst_t cmd_show_route_statistics = {
    .f = cmd_show_route_statistics_parsed,
    .data = NULL,
    .help_str = "show route statistics",
    .tokens = {
        (void *)&cmd_show_route_statistics_T_show,
        (void *)&cmd_show_route_statistics_T_route,
        (void *)&cmd_show_route_statistics_T_statistics,
        NULL,
    },
};

cmdline_parse_inst_t cmd_show_route_statistics_details = {
    .f = cmd_show_route_statistics_parsed,
    .data = (void *) (intptr_t) 'd',
    .help_str = "show route statistics details",
    .tokens = {
        (void *)&cmd_show_route_statistics_T_show,
        (void *)&cmd_show_route_statistics_T_route,
        (void *)&cmd_show_route_statistics_T_statistics,
        (void *)&cmd_show_route_statistics_T_details,
        NULL,
    },
};

/*****************************************************************************
 * Main menu context
 ****************************************************************************/
static cmdline_parse_ctx_t cli_ctx[] = {
    &cmd_show_route_statistics,
    &cmd_show_route_statistics_details,
    NULL,
};

