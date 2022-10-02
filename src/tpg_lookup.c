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
 *     tpg_lookup.c
 *
 * Description:
 *     Session lookup functions.
 *
 * Author:
 *     Dumitru Ceara, Eelco Chaudron
 *
 * Initial Created:
 *     10/22/2015
 *
 * Notes:
 *     For now we also assume the tcb can only be deleted/used once
 *     inserted in the hash table by the responsible lcore. If this is not
 *     the case in the future we might need to add an atomic ref count before
 *     freeing.
 *
 */

/*****************************************************************************
 * Include files
 ****************************************************************************/
#include "tcp_generator.h"

#include "toeplitz.h"
#include "toeplitz2.h"

/*****************************************************************************
 * tlkp_calc_connection_hash()
 ****************************************************************************/
uint32_t tlkp_calc_connection_hash(uint32_t dst_addr, uint32_t src_addr,
                                   uint16_t dst_port,
                                   uint16_t src_port)
{
    /*
     * This functions gets called by the client/server with all parameters in
     * host order. The destination address here is the destination
     * address of the connection to setup, however we would like to get
     * the hash matching the returning packets. Thats why the source, and
     * destination address are passed reversed to the hash function.
     */
    return toeplitz_cpuhash_addrport(dst_addr, src_addr, dst_port, src_port);
}

/*****************************************************************************
 * tlkp_calc_pkt_hash()
 ****************************************************************************/
uint32_t tlkp_calc_pkt_hash(uint32_t local_addr, uint32_t remote_addr,
                            uint16_t local_port, uint16_t remote_port)
{
    /*
     * This functions gets called by the server with all parameters in
     * host order to store the catch all entries, i.e. listen entries.
     */
    return toeplitz_rawhash_addrport(local_addr, remote_addr,
                                     local_port, remote_port);
}

/*****************************************************************************
 * tlkp_get_qindex_from_hash()
 ****************************************************************************/
inline uint32_t tlkp_get_qindex_from_hash(uint32_t hash, uint32_t phys_port)
{
    uint16_t reta_size = port_dev_info[phys_port].pi_adjusted_reta_size;

    /* TODO: we assume the indirection table is built in a round-robin fashion */
    return (hash % reta_size) % PORT_QCNT(phys_port);
}

/*****************************************************************************
 * tlkp_init()
 ****************************************************************************/
bool tlkp_init(void)
{
    int      rss_key_size;
    uint8_t *rss_key;

    /*
     * Initialize toeplitz() functions, and check to make sure they work
     */
    rss_key_size = port_get_global_rss_key(&rss_key);
    toeplitz_init(rss_key, rss_key_size);

    if (true) {
        /*
         * See https://msdn.microsoft.com/en-us/library/windows/hardware/ff571021(v=vs.85).aspx
         * for hash examples, we picket the first one.
         */
        uint32_t src_addr = IPv4(66, 9, 149, 187);
        uint16_t src_port = 2794;

        uint32_t dst_addr = IPv4(161, 142, 100, 80);
        uint16_t dst_port = 1766;

        uint32_t thash = tlkp_calc_connection_hash(src_addr, dst_addr,
                                                   src_port, dst_port);

        if (thash != 0x51ccc178)
            TPG_ERROR_ABORT("RSS Hash host order not working: tcp should be 0x%8.8X != 0x%8.8X\n",
                            0x51ccc178, thash);

        thash = tlkp_calc_pkt_hash(rte_cpu_to_be_32(src_addr),
                                   rte_cpu_to_be_32(dst_addr),
                                   rte_cpu_to_be_16(src_port),
                                   rte_cpu_to_be_16(dst_port));

        if (thash != 0x51ccc178)
            TPG_ERROR_ABORT("RSS Hash packet order not working: tcp should be 0x%8.8X != 0x%8.8X\n",
                            0x51ccc178, thash);
    }

    return true;
}

/*****************************************************************************
 * tlkp_find_v4_cb()
 ****************************************************************************/
l4_control_block_t *tlkp_find_v4_cb(tlkp_hash_bucket_t *htable,
                                    uint32_t phys_port, uint32_t l4_hash,
                                    uint32_t local_addr, uint32_t remote_addr,
                                    uint16_t local_port, uint16_t remote_port)
{
    l4_control_block_t *cb = NULL;
    tlkp_hash_bucket_t *bucket;

    bucket = tlkp_get_hash_bucket(htable, phys_port, l4_hash);

    LIST_FOREACH(cb, bucket, l4cb_hash_bucket_entry) {
        if (l4_hash == cb->l4cb_rx_hash &&
            local_port == cb->l4cb_src_port &&
            remote_port == cb->l4cb_dst_port &&
            local_addr == cb->l4cb_src_addr.ip_v4 &&
            remote_addr == cb->l4cb_dst_addr.ip_v4) {
            /*
             * cb holds cb to return...
             */
            break;
        }
    }

    return cb;
}

/*****************************************************************************
 * tlkp_add_cb()
 ****************************************************************************/
int tlkp_add_cb(tlkp_hash_bucket_t *htable, l4_control_block_t *cb)
{
    tlkp_hash_bucket_t *bucket;

    if (cb == NULL)
        return -EINVAL;

    TRACE_FMT(TLK, DEBUG, "[%s()]: phys_port %"PRIu32" l4_hash %"PRIX32,
              __func__,
              cb->l4cb_interface,
              cb->l4cb_rx_hash);

    bucket = tlkp_get_hash_bucket(htable, cb->l4cb_interface, cb->l4cb_rx_hash);

    /* TODO: Add duplicate check */
    LIST_INSERT_HEAD(bucket, cb, l4cb_hash_bucket_entry);

    return 0;
}

/*****************************************************************************
 * tlkp_delete_cb()
 ****************************************************************************/
int tlkp_delete_cb(tlkp_hash_bucket_t *htable __rte_unused,
                   l4_control_block_t *cb)
{
    if (cb == NULL)
        return -EINVAL;

    /*
     * TODO: Do we need to add some form of protection, i.e. make sure the
     *       cb is realy part of the hash bucket?
     */
    LIST_REMOVE(cb, l4cb_hash_bucket_entry);

    return 0;
}

/*****************************************************************************
 * tlkp_init_cb()
 ****************************************************************************/
void tlkp_init_cb(l4_control_block_t *l4_cb, uint32_t local_addr,
                  uint32_t remote_addr,
                  uint16_t local_port,
                  uint16_t remote_port,
                  uint32_t l4_hash,
                  uint32_t cb_interface,
                  uint32_t test_case_id,
                  tpg_app_proto_t app_id,
                  sockopt_t *sockopt,
                  uint32_t flags)
{
    l4_cb->l4cb_src_addr = TPG_IPV4(local_addr);
    l4_cb->l4cb_dst_addr = TPG_IPV4(remote_addr);
    l4_cb->l4cb_src_port = local_port;
    l4_cb->l4cb_dst_port = remote_port;

    l4_cb->l4cb_domain = AF_INET;

    l4_cb->l4cb_interface = cb_interface;
    l4_cb->l4cb_test_case_id = test_case_id;

    l4_cb->l4cb_app_data.ad_type = app_id;

    /* Struct copy! */
    l4_cb->l4cb_sockopt = *sockopt;

    if (flags & TPG_CB_USE_L4_HASH_FLAG)
        l4_cb->l4cb_rx_hash = l4_hash;
    else
        l4_cb->l4cb_rx_hash =
            tlkp_calc_connection_hash(l4_cb->l4cb_dst_addr.ip_v4,
                                      l4_cb->l4cb_src_addr.ip_v4,
                                      l4_cb->l4cb_dst_port,
                                      l4_cb->l4cb_src_port);

#if defined(TPG_L4_CB_TX_HASH)
    l4_cb->l4cb_tx_hash =
        tlkp_calc_connection_hash(l4_cb->l4cb_src_addr.ip_v4,
                                  l4_cb->l4cb_dst_addr.ip_v4,
                                  l4_cb->l4cb_src_port,
                                  l4_cb->l4cb_dst_port);

#endif /* defined(TPG_L4_CB_TX_HASH) */
}

