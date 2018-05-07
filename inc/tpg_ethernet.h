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
 *     tpg_ethernet.h
 *
 * Description:
 *     Ethernet processing.
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
#ifndef _H_TPG_ETHERNET_
#define _H_TPG_ETHERNET_

/* Allowed vlan range are from 1-4094 (0 and 4095 are reserved values) */
#define VLAN_MIN        1
#define VLAN_MAX        4094
#define VLAN_VID_MASK   0x0fff /* VLAN Identifier */
#define VLAN_PRIO_SHIFT 13
#define VLAN_NO_TAG     0

#define VLAN_TAG_ID(vlan_tci) ((vlan_tci) & VLAN_VID_MASK)


STATS_GLOBAL_DECLARE(tpg_eth_statistics_t);

/*****************************************************************************
 * Static inlines for tpg_ethernet.c
 ****************************************************************************/
static inline uint64_t eth_mac_to_uint64(uint8_t *mac)
{
    uint64_t uint64_mac;

    uint64_mac = (uint64_t) mac[0] << 40 |
                 (uint64_t) mac[1] << 32 |
                 (uint64_t) mac[2] << 24 |
                 (uint64_t) mac[3] << 16 |
                 (uint64_t) mac[4] << 8 |
                 (uint64_t) mac[5];

    return uint64_mac;
}

static inline void eth_uint64_to_mac(uint64_t uint64_mac, uint8_t *mac)
{
    mac[0] = (uint64_mac >> 40) & 0xff;
    mac[1] = (uint64_mac >> 32) & 0xff;
    mac[2] = (uint64_mac >> 24) & 0xff;
    mac[3] = (uint64_mac >> 16) & 0xff;
    mac[4] = (uint64_mac >> 8) & 0xff;
    mac[5] = uint64_mac & 0xff;
}

/*****************************************************************************
 * Externals for tpg_ethernet.c
 ****************************************************************************/
extern bool             eth_init(void);
extern void             eth_lcore_init(uint32_t lcore_id);
extern struct rte_mbuf *eth_receive_pkt(packet_control_block_t *pcb,
                                        struct rte_mbuf *mbuf);
extern struct rte_mbuf *eth_build_hdr_mbuf(l4_control_block_t *l4_cb,
                                           uint64_t dst_mac,
                                           uint64_t src_mac,
                                           uint16_t ether_type);
extern void             vlan_store_sockopt(vlan_sockopt_t *dest,
                                          const tpg_vlan_sockopt_t *options);
extern void             vlan_load_sockopt(tpg_vlan_sockopt_t *dest,
                                          const vlan_sockopt_t *options);
extern const char *vlan_option_name(const tpg_vlan_sockopt_t *options);
#endif /* _H_TPG_ETHERNET_ */

