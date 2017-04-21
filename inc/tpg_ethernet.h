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

/*****************************************************************************
 * Ethernet statistics
 ****************************************************************************/
typedef struct ethernet_statistics_s {

    uint64_t es_etype_arp;
    uint64_t es_etype_ipv4;
    uint64_t es_etype_ipv6;
    uint64_t es_etype_other;
    uint64_t es_etype_vlan;

    /* Unlikly uint16_t error counters */

    uint16_t es_to_small_fragment;

} ethernet_statistics_t;

/*****************************************************************************
 * External's for tpg_ethernet.c
 ****************************************************************************/
extern bool             eth_init(void);
extern void             eth_lcore_init(uint32_t lcore_id);
extern int              eth_build_eth_hdr(struct rte_mbuf *mbuf,
                                          uint64_t dst_mac, uint64_t src_mac,
                                          uint16_t ether_type);
extern struct rte_mbuf *eth_receive_pkt(packet_control_block_t *pcb,
                                        struct rte_mbuf *mbuf);
extern void             eth_total_stats_clear(uint32_t port);

#endif /* _H_TPG_ETHERNET_ */

