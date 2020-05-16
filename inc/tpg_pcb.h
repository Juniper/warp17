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
 *     tpg_pcb.h
 *
 * Description:
 *     Packet control block.
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
#ifndef _H_TPG_PCB_
#define _H_TPG_PCB_

/*****************************************************************************
 * Packet control block
 ****************************************************************************/
typedef struct packet_control_block_s {
    uint32_t             pcb_core_index;
    uint32_t             pcb_port;
    uint32_t             pcb_hash;

    uint32_t             pcb_trace:1;
    uint32_t             pcb_hash_valid:1;

    /* true if we stored if fore more processing (e.g, tcp) */
    uint32_t             pcb_mbuf_stored:1;

    struct rte_mbuf     *pcb_mbuf;

    union {
        void                *pcb_l3;
        struct rte_ipv4_hdr *pcb_ipv4;
        struct rte_arp_hdr  *pcb_arp;
    };

    union {
        void                *pcb_l4;
        struct rte_tcp_hdr  *pcb_tcp;
        struct rte_udp_hdr  *pcb_udp;
    };

    uint16_t             pcb_l4_len;
    uint16_t             pcb_l5_len;

    uint64_t             pcb_tstamp;
    sockopt_t           *pcb_sockopt;

} packet_control_block_t;

static inline __attribute__((always_inline))
void pcb_minimal_init(packet_control_block_t *pcb, uint32_t core_index,
                      uint32_t port,
                      struct rte_mbuf *mbuf)
{
    pcb->pcb_core_index = core_index;
    pcb->pcb_port = port;

    pcb->pcb_mbuf = mbuf;

    pcb->pcb_trace = false;
    pcb->pcb_hash_valid = false;

    pcb->pcb_mbuf_stored = false;
    pcb->pcb_sockopt = NULL;
}

#endif /* _H_TPG_PCB_ */

