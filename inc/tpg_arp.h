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
 *     tpg_arp.h
 *
 * Description:
 *     ARP processing.
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
#ifndef _H_TPG_ARP_
#define _H_TPG_ARP_

/*****************************************************************************
 * ARP statistics
 ****************************************************************************/
typedef struct arp_statistics_s {

    uint64_t as_received_req;
    uint64_t as_received_rep;
    uint64_t as_received_other;

    uint64_t as_req_not_mine;

    uint64_t as_sent_req;
    uint64_t as_sent_req_failed;
    uint64_t as_sent_rep;
    uint64_t as_sent_rep_failed;

    /* Unlikly uint16_t error counters */

    uint16_t as_to_small_fragment;
    uint16_t as_invalid_hw_space;
    uint16_t as_invalid_proto_space;
    uint16_t as_invalid_hw_len;
    uint16_t as_invalid_proto_len;

} arp_statistics_t;

STATS_GLOBAL_DECLARE(arp_statistics_t);

/*****************************************************************************
 * ARP table handling definitions
 ****************************************************************************/
/*
 * Currently ARP table size is small, as we only need to store the default
 * gw's ARP entry and our own ARPs.
 */
#define TPG_ARP_PORT_TABLE_SIZE    24

#define TPG_ARP_UINT64_MAC_MASK    0x0000FFFFFFFFFFFF
#define TPG_ARP_UINT64_FLAGS_MASK  (~TPG_ARP_UINT64_MAC_MASK)

#define TPG_ARP_FLAG_IN_USE        0x0001000000000000
#define TPG_ARP_FLAG_INCOMPLETE    0x0002000000000000
#define TPG_ARP_FLAG_LOCAL         0x0004000000000000

#define ARP_IS_FLAG_SET(arp, flag) ((((arp)->ae_mac_flags &      \
                                    TPG_ARP_UINT64_FLAGS_MASK) & \
                                     (flag)) != 0)
typedef struct arp_entry_s {

    uint64_t ae_mac_flags;
    uint32_t ae_ip_address;

} arp_entry_t;


static inline void arp_set_entry_in_use(arp_entry_t *arp)
{
    rte_smp_wmb();
    arp->ae_mac_flags |= TPG_ARP_FLAG_IN_USE;
}

static inline void arp_clear_entry_in_use(arp_entry_t *arp)
{
    arp->ae_mac_flags &= ~TPG_ARP_FLAG_IN_USE;
}

static inline bool arp_is_entry_in_use(arp_entry_t *arp)
{
    if (!ARP_IS_FLAG_SET(arp, TPG_ARP_FLAG_IN_USE))
        return false;

    rte_smp_rmb();
    return true;
}

static inline uint64_t arp_mac_to_uint64(uint8_t *mac)
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

static inline void arp_uint64_to_mac(uint64_t uint64_mac, uint8_t *mac)
{
    mac[0] = (uint64_mac >> 40) & 0xff;
    mac[1] = (uint64_mac >> 32) & 0xff;
    mac[2] = (uint64_mac >> 24) & 0xff;
    mac[3] = (uint64_mac >> 16) & 0xff;
    mac[4] = (uint64_mac >> 8) & 0xff;
    mac[5] = uint64_mac & 0xff;
}

static inline void arp_set_mac_in_entry_as_uint64(arp_entry_t *arp,
                                                  uint64_t mac)
{
    arp->ae_mac_flags = (mac & TPG_ARP_UINT64_MAC_MASK) |
                        (arp->ae_mac_flags & TPG_ARP_UINT64_FLAGS_MASK);
}

static inline void arp_set_mac_in_entry_as_uint8(arp_entry_t *arp,
                                                 uint8_t *mac)
{
    arp_set_mac_in_entry_as_uint64(arp,
                                   arp_mac_to_uint64(mac));
}

static inline uint64_t arp_get_mac_from_entry_as_uint64(arp_entry_t *arp)
{
    return arp->ae_mac_flags & TPG_ARP_UINT64_MAC_MASK;
}

static inline void arp_get_mac_from_entry_as_uint8(arp_entry_t *arp,
                                                   uint8_t *mac)
{
    arp_uint64_to_mac(arp_get_mac_from_entry_as_uint64(arp),
                      mac);
}

static inline uint64_t arp_get_entry_flags(arp_entry_t *arp)
{
    return arp->ae_mac_flags & TPG_ARP_UINT64_FLAGS_MASK;
}

#define TPG_USE_PORT_MAC      0xffff000000000001

#define TPG_ARP_MAC_NOT_FOUND TPG_ARP_FLAG_INCOMPLETE

/*****************************************************************************
 * External's for tpg_arp.c
 ****************************************************************************/
extern bool             arp_init(void);
extern void             arp_lcore_init(uint32_t lcore_id);
extern struct rte_mbuf *arp_receive_pkt(packet_control_block_t *pcb,
                                        struct rte_mbuf *mbuf);
extern uint64_t         arp_lookup_mac(uint32_t port, uint32_t ip);
extern bool             arp_add_local(uint32_t port, uint32_t ip);
extern bool             arp_delete_local(uint32_t port, uint32_t ip);
extern bool             arp_send_arp_request(uint32_t port, uint32_t local_ip,
                                             uint32_t remote_ip);
extern bool             arp_send_grat_arp_request(uint32_t port, uint32_t ip);
extern bool             arp_send_grat_arp_reply(uint32_t port, uint32_t ip);
extern void             arp_total_stats_clear(uint32_t port);

#endif /* _H_TPG_ARP_ */

