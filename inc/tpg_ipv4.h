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
 *     tpg_ipv4.h
 *
 * Description:
 *     IPv4 processing
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
#ifndef _H_TPG_IPV4_
#define _H_TPG_IPV4_

/*****************************************************************************
 * IPv4 statistics
 ****************************************************************************/
typedef struct ipv4_statistics_s {

    uint64_t ips_received_pkts;
    uint64_t ips_received_bytes;
    uint64_t ips_protocol_icmp;
    uint64_t ips_protocol_tcp;
    uint64_t ips_protocol_udp;
    uint64_t ips_protocol_other;

    /* Unlikely uint16_t error counters */

    uint16_t ips_to_small_fragment;
    uint16_t ips_hdr_to_small;
    uint16_t ips_invalid_checksum;
    uint16_t ips_total_length_invalid;
    uint16_t ips_received_frags;

#ifndef _SPEEDY_PKT_PARSE_
    uint16_t ips_not_v4;
    uint16_t ips_reserved_bit_set;
#endif

} ipv4_statistics_t;

/*****************************************************************************
 * External's for tpg_ipv4.c
 ****************************************************************************/
extern bool             ipv4_init(void);
extern void             ipv4_lcore_init(uint32_t lcore_id);
extern int              ipv4_build_ipv4_hdr(struct rte_mbuf *mbuf,
                                            uint32_t src_addr,
                                            uint32_t dst_addr,
                                            uint8_t protocol,
                                            uint16_t l4_len,
                                            struct ipv4_hdr *hdr);
extern struct rte_mbuf *ipv4_receive_pkt(packet_control_block_t *pcb,
                                         struct rte_mbuf *mbuf);
extern void             ipv4_total_stats_get(uint32_t port,
                                             ipv4_statistics_t *total_ipv4_stats);

#endif /* _H_TPG_IPV4_ */

