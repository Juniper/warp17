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

/*
 * Helper to send out UDP related notifications.
 */
#define UDP_NOTIF(notif, ucb) \
    TEST_NOTIF((notif), &(ucb)->ucb_l4)

/*****************************************************************************
 * DATA definitions
 ****************************************************************************/
/* TODO: doesn't include any potential options (IP+TCP). */
#define UCB_MIN_HDRS_SZ                                   \
    (sizeof(struct ether_hdr) + sizeof(struct ipv4_hdr) + \
     sizeof(struct udp_hdr) + ETHER_CRC_LEN)

#define UDP_MTU(port_info, sockopt)                                         \
    ((port_info)->pi_mtu - ipv4_get_sockopt((sockopt))->ip4so_hdr_opt_len - \
     vlan_get_sockopt((sockopt))->vlanso_hdr_opt_len -                      \
     UCB_MIN_HDRS_SZ)

#define UDP_GLOBAL_MTU(port, sockopt) \
    UDP_MTU(&port_dev_info[(port)], (sockopt))

#define UCB_MTU(ucb)                                                           \
    UDP_MTU(&RTE_PER_LCORE(local_port_dev_info)[(ucb)->ucb_l4.l4cb_interface], \
            &(ucb)->ucb_l4.l4cb_sockopt)

/*****************************************************************************
 * Globals for tpg_udp.c
 ****************************************************************************/
STATS_GLOBAL_DECLARE(tpg_udp_statistics_t);

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

