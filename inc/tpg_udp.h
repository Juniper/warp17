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
 *     tpg_tcp.h
 *
 * Description:
 *     General UDP processing, hopefully it will work for v4 and v6.
 *
 * Author:
 *     Dumitru Ceara, Eelco Chaudron
 *
 * Initial Created:
 *     10/22/2015
 *
 * Notes:
 *
 */

/*****************************************************************************
 * Multiple include protection
 ****************************************************************************/
#ifndef _H_TPG_UDP_
#define _H_TPG_UDP_

/*****************************************************************************
 * UDP statistics
 ****************************************************************************/
typedef struct udp_statistics_s {

    uint64_t us_received_pkts;
    uint64_t us_received_bytes;

    uint64_t us_sent_pkts;
    uint64_t us_sent_bytes;

    uint64_t us_ucb_malloced;
    uint64_t us_ucb_freed;
    uint64_t us_ucb_not_found;
    uint64_t us_ucb_alloc_err;


    /* Unlikely uint16_t error counters */

    uint16_t us_to_small_fragment;
    uint16_t us_invalid_checksum;
    uint16_t us_failed_pkts;

} __rte_cache_aligned udp_statistics_t;

STATS_GLOBAL_DECLARE(udp_statistics_t);

typedef enum udpState {

    US_INIT,
    US_LISTEN,
    US_OPEN,
    US_CLOSED,

} udpState_t;

/*****************************************************************************
 * UDP Control Block
 ****************************************************************************/
typedef struct udp_control_block_s {
    /*
     * Generic L4 control block.
     */
    l4_control_block_t ucb_l4;

    /*
     * UCB flags
     */
    uint32_t           ucb_active   :1;
    uint32_t           ucb_malloced :1;
    uint32_t           ucb_trace    :1;

    /* uint32_t        ucb_unused   :29; */

    udpState_t         ucb_state;

} udp_control_block_t;

/*****************************************************************************
 * UDP stack notifications
 ****************************************************************************/
enum {
    UCB_NOTIF_STATE_CHANGE = NOTIFID_INITIALIZER(NOTIF_UDP_MODULE),
    UDP_NOTIF_SEG_SENT,
    UDP_NOTIF_SEG_DELIVERED
};

#define UDP_NOTIF(notif, ucb, tcid, iface)                       \
    do {                                                         \
        notif_arg_t arg = UDP_NOTIF_ARG((ucb), (tcid), (iface)); \
        udp_notif_cb((notif), &arg);                             \
    } while (0)

#define UDP_NOTIF_UCB(notif, ucb)                              \
    UDP_NOTIF((notif), (ucb), (ucb)->ucb_l4.l4cb_test_case_id, \
              (ucb)->ucb_l4.l4cb_interface)

/* Callback to be executed whenever an interesting event happens. */
extern notif_cb_t udp_notif_cb;

/*****************************************************************************
 * DATA definitions
 ****************************************************************************/
/* TODO: doesn't include any potential options (IP+TCP). */
#define UCB_MIN_HDRS_SZ                                   \
    (sizeof(struct ether_hdr) + sizeof(struct ipv4_hdr) + \
     sizeof(struct udp_hdr) + ETHER_CRC_LEN)

#define UCB_MTU(ucb) \
    (RTE_PER_LCORE(local_port_dev_info)[(ucb)->ucb_l4.l4cb_interface].pi_mtu - UCB_MIN_HDRS_SZ)

/*****************************************************************************
 * Externals for tpg_udp.c
 ****************************************************************************/
extern bool             udp_init(void);
extern void             udp_lcore_init(uint32_t lcore_id);

extern struct rte_mbuf *udp_receive_pkt(packet_control_block_t *pcb,
                                        struct rte_mbuf *mbuf);

extern int              udp_open_v4_connection(udp_control_block_t **ucb, uint32_t eth_port,
                                               uint32_t src_ip_addr, uint16_t src_port,
                                               uint32_t dst_ip_addr, uint16_t dst_port,
                                               uint32_t test_case_id, tpg_app_proto_t app_id,
                                               sockopt_t *sockopt, uint32_t flags);

extern int              udp_listen_v4(udp_control_block_t **ucb, uint32_t eth_port,
                                      uint32_t local_ip_addr, uint16_t local_port,
                                      uint32_t test_case_id, tpg_app_proto_t app_id,
                                      sockopt_t *sockopt, uint32_t flags);

extern void             udp_connection_cleanup(udp_control_block_t *ucb);

extern int              udp_close_v4(udp_control_block_t *ucb);

extern int              udp_send_v4(udp_control_block_t *ucb,
                                    struct rte_mbuf *data_mbuf,
                                    uint32_t *data_sent);

#endif /* _H_TPG_UDP_ */

