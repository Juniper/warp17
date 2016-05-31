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
 *     tpg_udp_lookup.c
 *
 * Description:
 *     UDP session lookup functions.
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
 * Include files
 ****************************************************************************/
#include "tcp_generator.h"

/*****************************************************************************
 * TCP lookup hash definitions
 ****************************************************************************/
/*****************************************************************************
 * Global variables
 ****************************************************************************/
/* Per core hashtable [port_cnt * TPG_TCP_HASH_BUCKET_SIZE] */
static RTE_DEFINE_PER_LCORE(tlkp_hash_bucket_t *, tlkp_ucb_hash_table);

rte_atomic16_t            *tlkp_ucb_mpool_alloc_in_use; /* array[cb_id] */
uint32_t                   ucb_l4cb_max_id;

/*****************************************************************************
 * ucb_trace_filter_match()
 ****************************************************************************/
static bool ucb_trace_filter_match(trace_filter_t *filter,
                                   udp_control_block_t *ucb)
{
    return trace_filter_match(filter, ucb->ucb_l4.l4cb_interface,
                              ucb->ucb_l4.l4cb_domain,
                              ucb->ucb_l4.l4cb_src_port,
                              ucb->ucb_l4.l4cb_dst_port,
                              ucb->ucb_l4.l4cb_src_addr,
                              ucb->ucb_l4.l4cb_dst_addr);
}

/*****************************************************************************
 * tlkp_udp_init()
 ****************************************************************************/
bool tlkp_udp_init(void)
{
    global_config_t *cfg;

    cfg = cfg_get_config();
    if (cfg == NULL)
        return false;

    L4_CB_MPOOL_INIT(mem_get_ucb_pools(), &tlkp_ucb_mpool_alloc_in_use,
                     &ucb_l4cb_max_id);

    return true;
}

/*****************************************************************************
 * tlkp_udp_lcore_init()
 ****************************************************************************/
void tlkp_udp_lcore_init(uint32_t lcore_id)
{
    unsigned int i;

    RTE_PER_LCORE(tlkp_ucb_hash_table) =
        rte_zmalloc_socket("udp_hash_table", rte_eth_dev_count() *
                           TPG_HASH_BUCKET_SIZE *
                           sizeof(tlkp_hash_bucket_t),
                           RTE_CACHE_LINE_SIZE,
                           rte_lcore_to_socket_id(lcore_id));
    if (RTE_PER_LCORE(tlkp_ucb_hash_table) == NULL) {
        TPG_ERROR_ABORT("[%d]: Failed to allocate per lcore udp htable!\n",
                        rte_lcore_index(lcore_id));
    }

    for (i = 0; i < (rte_eth_dev_count() * TPG_HASH_BUCKET_SIZE); i++) {
        /*
         * Initialize all list headers.
         */
        LIST_INIT((&RTE_PER_LCORE(tlkp_ucb_hash_table)[i]));
    }
}

/*****************************************************************************
 * tlkp_alloc_ucb()
 ****************************************************************************/
udp_control_block_t *tlkp_alloc_ucb(void)
{
    void *ucb;

    if (rte_mempool_sc_get(mem_get_ucb_local_pool(), &ucb) != 0)
        return NULL;

    L4_CB_ALLOC_INIT(&((udp_control_block_t *)ucb)->ucb_l4,
                     tlkp_ucb_mpool_alloc_in_use,
                     ucb_l4cb_max_id);

    return ucb;
}

/*****************************************************************************
 * tlkp_init_ucb()
 ****************************************************************************/
void tlkp_init_ucb(udp_control_block_t *ucb, uint32_t local_addr,
                   uint32_t remote_addr,
                   uint16_t local_port,
                   uint16_t remote_port,
                   uint32_t l4_hash,
                   uint32_t interface,
                   uint32_t test_case_id,
                   tpg_app_proto_t app_id,
                   uint32_t flags)
{
    tlkp_init_cb(&ucb->ucb_l4, local_addr, remote_addr, local_port, remote_port,
                 l4_hash,
                 interface,
                 test_case_id,
                 app_id,
                 flags);

    if ((flags & TCG_CB_MALLOCED))
        ucb->ucb_malloced = true;
    else
        ucb->ucb_malloced = false;
}

/*****************************************************************************
 * tlkp_init_ucb_client()
 ****************************************************************************/
void tlkp_init_ucb_client(udp_control_block_t *ucb, uint32_t local_addr,
                          uint32_t remote_addr,
                          uint16_t local_port,
                          uint16_t remote_port,
                          uint32_t l4_hash,
                          uint32_t tcb_interface,
                          uint32_t test_case_id,
                          tpg_app_proto_t app_id,
                          uint32_t flags)
{
    tlkp_init_ucb(ucb, local_addr, remote_addr, local_port, remote_port,
                  l4_hash,
                  tcb_interface,
                  test_case_id,
                  app_id,
                  flags);
    ucb->ucb_state = US_INIT;
    ucb->ucb_active = true;
}

/*****************************************************************************
 * tlkp_free_ucb()
 ****************************************************************************/
void tlkp_free_ucb(udp_control_block_t *ucb)
{
    L4_CB_FREE_DEINIT(&ucb->ucb_l4, tlkp_ucb_mpool_alloc_in_use,
                      ucb_l4cb_max_id);
    rte_mempool_sp_put(mem_get_ucb_local_pool(), ucb);
}

/*****************************************************************************
 * tlkp_total_ucbs_allocated()
 ****************************************************************************/
unsigned int tlkp_total_ucbs_allocated(void)
{
    return rte_mempool_free_count(mem_get_ucb_local_pool());
}

/*****************************************************************************
 * tlkp_find_v4_ucb()
 ****************************************************************************/
udp_control_block_t *tlkp_find_v4_ucb(uint32_t phys_port, uint32_t l4_hash,
                                      uint32_t local_addr, uint32_t remote_addr,
                                      uint16_t local_port, uint16_t remote_port)
{
    udp_control_block_t *ucb;

    TRACE_FMT(TLK, DEBUG,
              "[%s()]: phys_port %u l4_hash %08X ladd/radd %08X/%08X lp/rp %u/%u",
              __func__,
              phys_port,
              l4_hash,
              local_addr,
              remote_addr,
              local_port,
              remote_port);

    ucb = container_of(tlkp_find_v4_cb(RTE_PER_LCORE(tlkp_ucb_hash_table),
                                       phys_port,
                                       l4_hash,
                                       local_addr,
                                       remote_addr,
                                       local_port,
                                       remote_port),
                                       udp_control_block_t, ucb_l4);

    if (unlikely(ucb == NULL))
        return NULL;

    /*
     * If we found a UCB and we have UCB trace filters enabled then
     * check if we should update the ucb_trace flag.
     */
    if (unlikely(RTE_PER_LCORE(trace_filter).tf_enabled)) {
        if (ucb_trace_filter_match(&RTE_PER_LCORE(trace_filter), ucb))
            ucb->ucb_trace = true;
        else
            ucb->ucb_trace = false;
    } else if (unlikely(ucb->ucb_trace == true))
        ucb->ucb_trace = false;

    return ucb;
}

/*****************************************************************************
 * tlkp_add_ucb()
 ****************************************************************************/
int tlkp_add_ucb(udp_control_block_t *ucb)
{
    int error;

    if (ucb == NULL)
        return -EINVAL;

    TRACE_FMT(TLK, DEBUG,
              "[%s()]: phys_port %u l4_hash %08X ladd/radd %08X/%08X lp/rp %u/%u",
              __func__,
              ucb->ucb_l4.l4cb_interface,
              ucb->ucb_l4.l4cb_hash,
              ucb->ucb_l4.l4cb_dst_addr.ip_v4,
              ucb->ucb_l4.l4cb_src_addr.ip_v4,
              ucb->ucb_l4.l4cb_dst_port,
              ucb->ucb_l4.l4cb_src_port);

    error = tlkp_add_cb(RTE_PER_LCORE(tlkp_ucb_hash_table), &ucb->ucb_l4);
    if (error)
        return error;

    /*
     * If we have UCB trace filters enabled and the new UCB matches then
     * update the ucb_trace flag.
     */
    if (unlikely(RTE_PER_LCORE(trace_filter).tf_enabled)) {
        if (ucb_trace_filter_match(&RTE_PER_LCORE(trace_filter), ucb))
            ucb->ucb_trace = true;
    }

    return 0;
}

/*****************************************************************************
 * tlkp_delete_ucb()
 ****************************************************************************/
int tlkp_delete_ucb(udp_control_block_t *ucb)
{
    return tlkp_delete_cb(RTE_PER_LCORE(tlkp_ucb_hash_table), &ucb->ucb_l4);
}

/*****************************************************************************
 * tlkp_walk_ucb()
 ****************************************************************************/
void tlkp_walk_ucb(uint32_t phys_port, tlkp_walk_v4_cb_t callback, void *arg)
{
    tlkp_walk_v4(RTE_PER_LCORE(tlkp_ucb_hash_table), phys_port, callback, arg);
}

