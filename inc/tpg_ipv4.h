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

STATS_GLOBAL_DECLARE(ipv4_statistics_t);

/*****************************************************************************
 * General IPv4 L4 checksum functions for scattered mbufs.
 *
 * - mbuf       Pointer to mbuf chain
 * - hdr        IPv4 header needed to calculate pseudo header, uses src/dst
 *              IP, and protocol fields
 * - l4_offset  Offset in first mbuf where l4 data starts
 * - l4_length  Total l4 data length to calculate checksum for
 *
 ****************************************************************************/
static inline uint16_t ipv4_general_l4_cksum(struct rte_mbuf *mbuf,
                                             struct ipv4_hdr *hdr,
                                             uint16_t l4_offset,
                                             uint16_t l4_length)
{
    struct ipv4_psd_header {
            uint32_t src_addr;
            uint32_t dst_addr;
            uint8_t  zero;
            uint8_t  proto;
            uint16_t len;
    } __attribute__((__packed__)) ip_psd_hdr;

    uint32_t         cksum = 0;
    struct rte_mbuf *mbuf_frag;
    unsigned int     bytes_left = l4_length;
    unsigned int     mbuf_data_len;

    /*
     * Checksum pseudo header, and data in first fragment
     */
    ip_psd_hdr.src_addr = hdr->src_addr;
    ip_psd_hdr.dst_addr = hdr->dst_addr;
    ip_psd_hdr.zero = 0;
    ip_psd_hdr.proto = hdr->next_proto_id;
    ip_psd_hdr.len = rte_cpu_to_be_16(bytes_left);
    cksum = rte_raw_cksum(&ip_psd_hdr, sizeof(ip_psd_hdr));

    mbuf_data_len = rte_pktmbuf_data_len(mbuf) - l4_offset;
    if (bytes_left < mbuf_data_len)
        mbuf_data_len = bytes_left;

    cksum += rte_raw_cksum(rte_pktmbuf_mtod(mbuf, uint8_t *) + l4_offset,
                           mbuf_data_len);
    bytes_left -= mbuf_data_len;

    /*
     * Checksum additional fragments
     */
    mbuf_frag = mbuf->next;
    while (mbuf_frag != NULL && bytes_left > 0) {

        mbuf_data_len = rte_pktmbuf_data_len(mbuf_frag);
        if (bytes_left < mbuf_data_len)
            mbuf_data_len = bytes_left;

        cksum += rte_raw_cksum(rte_pktmbuf_mtod(mbuf_frag, uint8_t *),
                               mbuf_data_len);
        bytes_left -= mbuf_data_len;

        mbuf_frag = mbuf_frag->next;
    }

    /*
     * Wrap back to 16 bits
     */

    cksum = ((cksum >> 16) & 0xffff) + (cksum & 0xffff);
    cksum = (~cksum) & 0xffff;
    if (cksum == 0)
        cksum = 0xffff;

    return cksum;
}

/*****************************************************************************
 * General IPv4 L4 checksum functions for scattered mbufs.
 *
 * - cksum_in   Current checksum returned by ipv4_general_l4_cksum()
 * - mbuf       Pointer to data mbuf chain
 *
 ****************************************************************************/
static inline uint16_t ipv4_update_general_l4_cksum(uint16_t cksum_in,
                                                    struct rte_mbuf *mbuf)
{
    uint32_t         cksum;
    unsigned int     mbuf_data_len;
    unsigned int     bytes_left = mbuf->pkt_len;

    cksum = (~cksum_in) & 0xffff;
    /* Add missing length for data segment in pseudo header */
    cksum += rte_cpu_to_be_16(bytes_left);

    while (mbuf != NULL && bytes_left > 0) {

        mbuf_data_len = rte_pktmbuf_data_len(mbuf);
        if (bytes_left < mbuf_data_len)
            mbuf_data_len = bytes_left;

        cksum += rte_raw_cksum(rte_pktmbuf_mtod(mbuf, uint8_t *),
                               mbuf_data_len);

        bytes_left -= mbuf_data_len;

        mbuf = mbuf->next;
    }

    cksum = ((cksum >> 16) & 0xffff) + (cksum & 0xffff);
    cksum = (~cksum) & 0xffff;
    if (cksum == 0)
        cksum = 0xffff;

    return cksum;
}

/*****************************************************************************
 * External's for tpg_ipv4.c
 ****************************************************************************/
extern bool             ipv4_init(void);
extern void             ipv4_lcore_init(uint32_t lcore_id);
extern int              ipv4_build_ipv4_hdr(sockopt_t *sockopt,
                                            struct rte_mbuf *mbuf,
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
