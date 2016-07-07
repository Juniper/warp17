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
 *     tpg_tcp_lookup.c
 *
 * Description:
 *     TCP session lookup functions
 *
 * Author:
 *     Dumitru Ceara, Eelco Chaudron
 *
 * Initial Created:
 *     03/11/2015
 *
 * Notes:
 *
 */

/*****************************************************************************
 * Include files
 ****************************************************************************/
#include "tcp_generator.h"

/*****************************************************************************
 * Global variables
 ****************************************************************************/
/* Per core hashtable [port_cnt * TPG_TCP_HASH_BUCKET_SIZE] */
static RTE_DEFINE_PER_LCORE(tlkp_hash_bucket_t *, tlkp_tcb_hash_table);

rte_atomic16_t            *tlkp_tcb_mpool_alloc_in_use; /* array [cb_id] */
uint32_t                   tcb_l4cb_max_id;

/*****************************************************************************
 * tcb_trace_filter_match()
 ****************************************************************************/
static bool tcb_trace_filter_match(trace_filter_t *filter,
                                   tcp_control_block_t *tcb)
{
    if (filter->tf_tcb_state != TS_CLOSED &&
            filter->tf_tcb_state != tcb->tcb_state) {
        return false;
    }

    return trace_filter_match(filter, tcb->tcb_l4.l4cb_interface,
                              tcb->tcb_l4.l4cb_domain,
                              tcb->tcb_l4.l4cb_src_port,
                              tcb->tcb_l4.l4cb_dst_port,
                              tcb->tcb_l4.l4cb_src_addr,
                              tcb->tcb_l4.l4cb_dst_addr);
}

/*****************************************************************************
 * tlkp_tcp_init()
 ****************************************************************************/
bool tlkp_tcp_init(void)
{
    global_config_t *cfg;

    cfg = cfg_get_config();
    if (cfg == NULL)
        return false;

    L4_CB_MPOOL_INIT(mem_get_tcb_pools(), &tlkp_tcb_mpool_alloc_in_use,
                     &tcb_l4cb_max_id,
                     offsetof(tcp_control_block_t, tcb_l4));

    return true;
}

/*****************************************************************************
 * tlkp_tcp_lcore_init()
 ****************************************************************************/
void tlkp_tcp_lcore_init(uint32_t lcore_id)
{
    unsigned int i;

    RTE_PER_LCORE(tlkp_tcb_hash_table) =
        rte_zmalloc_socket("tcp_hash_table", rte_eth_dev_count() *
                           TPG_HASH_BUCKET_SIZE *
                           sizeof(tlkp_hash_bucket_t),
                           RTE_CACHE_LINE_SIZE,
                           rte_lcore_to_socket_id(lcore_id));
    if (RTE_PER_LCORE(tlkp_tcb_hash_table) == NULL) {
        TPG_ERROR_ABORT("[%d]: Failed to allocate per lcore tcp htable!\n",
                        rte_lcore_index(lcore_id));
    }

    for (i = 0; i < (rte_eth_dev_count() * TPG_HASH_BUCKET_SIZE); i++) {
        /*
         * Initialize all list headers.
         */
        LIST_INIT((&RTE_PER_LCORE(tlkp_tcb_hash_table)[i]));
    }
}

/*****************************************************************************
 * tlkp_alloc_tcb()
 ****************************************************************************/
tcp_control_block_t *tlkp_alloc_tcb(void)
{
    void *tcb;

    if (rte_mempool_sc_get(mem_get_tcb_local_pool(), &tcb) != 0)
        return NULL;

    L4_CB_ALLOC_INIT(&((tcp_control_block_t *)tcb)->tcb_l4,
                     tlkp_tcb_mpool_alloc_in_use,
                     tcb_l4cb_max_id);

    return tcb;
}

/*****************************************************************************
 * tlkp_init_tcb()
 ****************************************************************************/
void tlkp_init_tcb(tcp_control_block_t *tcb, uint32_t local_addr,
                   uint32_t remote_addr,
                   uint16_t local_port,
                   uint16_t remote_port,
                   uint32_t l4_hash,
                   uint32_t tcb_interface,
                   uint32_t test_case_id,
                   tpg_app_proto_t app_id,
                   uint32_t flags)
{
    tlkp_init_cb(&tcb->tcb_l4, local_addr, remote_addr, local_port, remote_port,
                 l4_hash,
                 tcb_interface,
                 test_case_id,
                 app_id,
                 flags);

    if ((flags & TCG_CB_CONSUME_ALL_DATA))
        tcb->tcb_consume_all_data = true;
    else
        tcb->tcb_consume_all_data = false;

    if ((flags & TCG_CB_NO_TIMEWAIT))
        tcb->tcb_no_timewait = true;
    else
        tcb->tcb_no_timewait = false;

    if ((flags & TCG_CB_MALLOCED))
        tcb->tcb_malloced = true;
    else
        tcb->tcb_malloced = false;
}

/*****************************************************************************
 * tlkp_init_tcb_client()
 ****************************************************************************/
void tlkp_init_tcb_client(tcp_control_block_t *tcb, uint32_t local_addr,
                          uint32_t remote_addr,
                          uint16_t local_port,
                          uint16_t remote_port,
                          uint32_t l4_hash,
                          uint32_t tcb_interface,
                          uint32_t test_case_id,
                          tpg_app_proto_t app_id,
                          uint32_t flags)
{
    tlkp_init_tcb(tcb, local_addr, remote_addr, local_port, remote_port,
                  l4_hash,
                  tcb_interface,
                  test_case_id,
                  app_id,
                  flags);

    /* At least the minimal initialization of the state has to be already
     * done here. The clients can be preallocated so we want to avoid using
     * uninitialized data.
     */
    tsm_initialize_minimal_statemachine(tcb, true);
}

/*****************************************************************************
 * tlkp_free_tcb()
 ****************************************************************************/
void tlkp_free_tcb(tcp_control_block_t *tcb)
{
    L4_CB_FREE_DEINIT(&tcb->tcb_l4,
                      tlkp_tcb_mpool_alloc_in_use,
                      tcb_l4cb_max_id);
    rte_mempool_sp_put(mem_get_tcb_local_pool(), tcb);
}


/*****************************************************************************
 * tlkp_total_tcbs_allocated()
 ****************************************************************************/
unsigned int tlkp_total_tcbs_allocated(void)
{
    return rte_mempool_free_count(mem_get_tcb_local_pool());
}

/*****************************************************************************
 * tlkp_find_v4_tcb()
 ****************************************************************************/
tcp_control_block_t *tlkp_find_v4_tcb(uint32_t phys_port, uint32_t l4_hash,
                                      uint32_t local_addr, uint32_t remote_addr,
                                      uint16_t local_port, uint16_t remote_port)
{
    l4_control_block_t  *l4_cb;
    tcp_control_block_t *tcb;

    TRACE_FMT(TLK, DEBUG,
              "[%s()]: phys_port %u l4_hash %08X ladd/radd %08X/%08X lp/rp %u/%u",
              __func__,
              phys_port,
              l4_hash,
              local_addr,
              remote_addr,
              local_port,
              remote_port);

    l4_cb = tlkp_find_v4_cb(RTE_PER_LCORE(tlkp_tcb_hash_table),
                                          phys_port,
                                          l4_hash,
                                          local_addr,
                                          remote_addr,
                                          local_port,
                                          remote_port);

    if (unlikely(l4_cb == NULL))
        return NULL;

    tcb = container_of(l4_cb, tcp_control_block_t, tcb_l4);

    /*
     * If we found a TCB and we have TCB trace filters enabled then
     * check if we should update the tcb_trace flag.
     */
    if (unlikely(RTE_PER_LCORE(trace_filter).tf_enabled)) {
        if (tcb_trace_filter_match(&RTE_PER_LCORE(trace_filter), tcb))
            tcb->tcb_trace = true;
        else
            tcb->tcb_trace = false;

    } else if (unlikely(tcb->tcb_trace == true))
        tcb->tcb_trace = false;

    return tcb;
}


/*****************************************************************************
 * tlkp_add_tcb()
 ****************************************************************************/
int tlkp_add_tcb(tcp_control_block_t *tcb)
{
    int error;

    if (tcb == NULL)
        return -EINVAL;

    TRACE_FMT(TLK, DEBUG,
              "[%s()]: phys_port %u l4_hash %08X ladd/radd %08X/%08X lp/rp %u/%u",
              __func__,
              tcb->tcb_l4.l4cb_interface,
              tcb->tcb_l4.l4cb_rx_hash,
              tcb->tcb_l4.l4cb_dst_addr.ip_v4,
              tcb->tcb_l4.l4cb_src_addr.ip_v4,
              tcb->tcb_l4.l4cb_dst_port,
              tcb->tcb_l4.l4cb_src_port);

    error = tlkp_add_cb(RTE_PER_LCORE(tlkp_tcb_hash_table), &tcb->tcb_l4);
    if (error)
        return error;

    /*
     * If we have TCB trace filters enabled and the new TCB matches then
     * update the tcb_trace flag.
     */
    if (unlikely(RTE_PER_LCORE(trace_filter).tf_enabled)) {
        if (tcb_trace_filter_match(&RTE_PER_LCORE(trace_filter), tcb))
            tcb->tcb_trace = true;
    }

    return 0;
}

/*****************************************************************************
 * tlkp_delete_tcb()
 ****************************************************************************/
int tlkp_delete_tcb(tcp_control_block_t *tcb)
{
    return tlkp_delete_cb(RTE_PER_LCORE(tlkp_tcb_hash_table), &tcb->tcb_l4);
}

/*****************************************************************************
 * tlkp_walk_tcb()
 ****************************************************************************/
void tlkp_walk_tcb(uint32_t phys_port, tlkp_walk_v4_cb_t callback, void *arg)
{
    tlkp_walk_v4(RTE_PER_LCORE(tlkp_tcb_hash_table), phys_port, callback, arg);
}

