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
 *     tpg_port.h
 *
 * Description:
 *     Handle setting up the physical Ethernet ports and create the desired
 *     receive and transmit queue.
 *
 * Author:
 *     Dumitru Ceara, Eelco Chaudron
 *
 * Initial Created:
 *     06/29/2015
 *
 * Notes:
 *
 */

/*****************************************************************************
 * Multiple include protection
 ****************************************************************************/
#ifndef _H_TPG_PORT_
#define _H_TPG_PORT_

/*****************************************************************************
 * Definitions
 ****************************************************************************/
#define PORT_CMDLINE_OPTIONS()            \
    CMDLINE_OPT_ARG("qmap", true),        \
    CMDLINE_OPT_ARG("qmap-default", true)

#define PORT_CMDLINE_PARSER() \
    CMDLINE_ARG_PARSER(port_handle_cmdline_opt, port_handle_cmdline)

/*****************************************************************************
 * Port core mask - for each core in a mask we allocate an RX HW queue and a
 * TX HW queue.
 ****************************************************************************/
typedef struct port_port_cfg_s {

    uint32_t ppc_q_cnt;

    uint32_t ppc_core_default; /* For sending "non-hashed" traffic (e.g., ARP) */

    uint64_t ppc_core_mask;

} port_port_cfg_t;

#define PORT_COREID_IN_MASK(mask, core) \
    ((mask) & ((uint64_t) 1 << (core)))

#define PORT_ADD_CORE_TO_MASK(mask, core) \
    ((mask) |= ((uint64_t) 1 << (core)))

#define PORT_DEL_CORE_FROM_MASK(mask, core) \
    ((mask) &= (~((uint64_t) 1 << (core))))

#define PORT_QCNT(port) \
    (port_port_cfg[port].ppc_q_cnt)

#define PORT_CORE_DEFAULT(port) \
    (port_port_cfg[port].ppc_core_default)

typedef struct port_core_cfg_s {
    int32_t       *pcc_qport_map; /* Array of [port] holding the q associated to
                                   * the core for that port or -1 if none.
                                   */
} port_core_cfg_t;

#define CORE_PORT_QINVALID (-1)

#define FOREACH_PORT_IN_CORE_START(core, port)                 \
    for ((port) = 0; (port) < rte_eth_dev_count(); (port)++) { \
        if (port_core_cfg[core].pcc_qport_map[port] !=         \
                CORE_PORT_QINVALID) {

#define FOREACH_PORT_IN_CORE_END()                             \
        }                                                      \
    }

#define FOREACH_CORE_IN_PORT_START(core, port)   \
    RTE_LCORE_FOREACH_SLAVE(core) {              \
        if (cfg_is_pkt_core(core) &&             \
            (port_get_tx_queue_id(core, port) != \
                CORE_PORT_QINVALID ||            \
             port_get_rx_queue_id(core, port) != \
                CORE_PORT_QINVALID)) {

#define FOREACH_CORE_IN_PORT_END()               \
        }                                        \
    }

/*****************************************************************************
 * Port information struct (dev info and mtu for now).
 ****************************************************************************/
typedef struct port_info_s {

    struct rte_eth_dev_info pi_dev_info;
    uint16_t                pi_adjusted_reta_size;
    uint16_t                pi_mtu;
    uint16_t                pi_numa_node;

    /* True if the port is a ring interface. */
    uint16_t                pi_ring_if : 1;
    uint16_t                pi_kni_if  : 1;

} port_info_t;

/*****************************************************************************
 * Port options definitions.
 ****************************************************************************/
#define PORT_MIN_MTU   68
#define PORT_MAX_MTU 9198

/*****************************************************************************
 * rte_eth_link print macros
 ****************************************************************************/
#define LINK_STATE(ls) \
    ((ls)->link_status ? "UP" : "DOWN")

#define LINK_SPEED(ls)                 \
    ((ls)->link_status ?               \
        ((ls)->link_speed < 1000 ?     \
            (ls)->link_speed :         \
            ((ls)->link_speed/1000)) : \
        0)

#define LINK_SPEED_SZ(ls)          \
    ((ls)->link_status ?           \
        ((ls)->link_speed < 1000 ? \
            "Mbps" : "Gbps") :     \
        "Mbps")

#define LINK_DUPLEX(ls)                                       \
    ((ls)->link_status ?                                      \
        ((ls)->link_duplex == ETH_LINK_HALF_DUPLEX ? "half" : \
         (ls)->link_duplex == ETH_LINK_FULL_DUPLEX ? "full" : \
         "???") : "N/A"),                                     \
    ((ls)->link_autoneg == ETH_LINK_AUTONEG ? "(auto)" :      \
        "(manual)")

/*****************************************************************************
 * Port statistics
 ****************************************************************************/
typedef struct port_statistics_s {

    uint64_t ps_received_pkts;
    uint64_t ps_received_bytes;

    uint64_t ps_send_pkts;
    uint64_t ps_send_bytes;
    uint64_t ps_send_failure;
    uint64_t ps_rx_ring_if_failed;

    uint64_t ps_send_sim_failure;

} port_statistics_t;

/*****************************************************************************
 * Link rate statistics
 ****************************************************************************/
typedef struct link_rate_statistics_s {

    struct rte_eth_stats lrs_estats;
    uint64_t             lrs_timestamp;

} link_rate_statistics_t;

/*****************************************************************************
 * Globals
 ****************************************************************************/
extern port_port_cfg_t *port_port_cfg;
extern port_core_cfg_t *port_core_cfg;
extern port_info_t     *port_dev_info;

/* Port stats are actually updated in tpg_pktloop.c */
STATS_GLOBAL_DECLARE(port_statistics_t);
STATS_LOCAL_DECLARE(port_statistics_t);

/*****************************************************************************
 * External's for tpg_port.c
 ****************************************************************************/
extern bool     port_init(void);
extern void     port_lcore_init(uint32_t lcore_id);
extern uint32_t port_get_socket(int port, int queue);
extern void     port_link_info_get(uint32_t port,
                                   struct rte_eth_link *link_info);
extern void     port_link_stats_get(uint32_t port,
                                    struct rte_eth_stats *total_link_stats);
extern void     port_link_rate_stats_get(uint32_t port,
                                         struct rte_eth_stats *total_rate_stats);
extern void     port_total_stats_get(uint32_t port,
                                     port_statistics_t *total_port_stats);
extern int      port_get_global_rss_key(uint8_t ** const rss_key);
extern int      port_set_conn_options(uint32_t port,
                                      tpg_port_options_t *options);
extern void     port_get_conn_options(uint32_t port, tpg_port_options_t *out);
extern bool     port_handle_cmdline_opt(const char *opt_name,
                                        char *opt_arg);
extern bool     port_handle_cmdline(void);

/*****************************************************************************
 * Static inlines.
 ****************************************************************************/
static inline __attribute__((always_inline))
int32_t port_get_tx_queue_id(int core, int port)
{
    return port_core_cfg[core].pcc_qport_map[port];
}

static inline __attribute__((always_inline))
int32_t port_get_rx_queue_id(int core, int port)
{
    return port_core_cfg[core].pcc_qport_map[port];
}

static inline __attribute__((always_inline))
uint32_t port_get_core_count(uint32_t port)
{
    uint32_t core_cnt = 0;
    uint32_t core;

    FOREACH_CORE_IN_PORT_START(core, port) {
        core_cnt++;
    } FOREACH_CORE_IN_PORT_END()

    return core_cnt;
}

#endif /* _H_TPG_PORT_ */

