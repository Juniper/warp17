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
 *     tpg_lookup.h
 *
 * Description:
 *     Session lookup functions
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
#ifndef _H_TPG_LOOKUP_
#define _H_TPG_LOOKUP_

/*****************************************************************************
 * Session hash buckets
 ****************************************************************************/
/*
 * Size should be a ^2 value, so modulo is easy to get hash bucket,
 * now we use 20 bits.
 */
#define TPG_HASH_BUCKET_BIT_SIZE 20
#define TPG_HASH_BUCKET_SIZE     (1 << TPG_HASH_BUCKET_BIT_SIZE)
#define TPG_HASH_BUCKET_MASK     (TPG_HASH_BUCKET_SIZE - 1)
#define TPG_HASH_MOD(hash)       ((((uint32_t)hash) >>  TPG_HASH_BUCKET_BIT_SIZE) ^ \
                                  (((uint32_t)hash) & TPG_HASH_BUCKET_MASK))

/*****************************************************************************
 * Lookup hash definitions
 ****************************************************************************/
typedef struct l4_control_block_s {

#if defined(TPG_L4_CB_DEBUG)
    uint32_t         l4cb_id;
#endif /* defined(TPG_L4_CB_DEBUG) */

    /*
     * Lookup hash linkage
     */
    LIST_ENTRY(l4_control_block_s)  l4cb_hash_bucket_entry;

    /*
     * TPG tests linkage.
     */
    TAILQ_ENTRY(l4_control_block_s) l4cb_test_list_entry;

    /*
     * Test state-machine information
     */
    test_sm_state_t  l4cb_test_state;

    /*
     * TPG test timer linkage.
     */
    tmr_list_entry(l4_control_block_s) l4cb_test_tmr_entry;

    /*
     * Address information
     */
    uint32_t         l4cb_rx_hash;
#if defined(TPG_L4_CB_TX_HASH)
    uint32_t         l4cb_tx_hash;
#endif /* defined(TPG_L4_CB_TX_HASH) */

    uint32_t         l4cb_interface;
    int              l4cb_domain;
    uint16_t         l4cb_src_port;
    uint16_t         l4cb_dst_port;

    tpg_ip_t         l4cb_src_addr;
    tpg_ip_t         l4cb_dst_addr;

    /*
     * Flags.
     */
    uint32_t         l4cb_test_case_id     :16;
    uint32_t         l4cb_on_test_tmr_list :1;

    uint32_t         l4cb_valid            :1; /* Only with TPG_L4_CB_DEBUG */

    /* uint32_t      l4cb_unused           :14; */

    /* Socket options. */
    sockopt_t        l4cb_sockopt;

    /* Application level state storage. */
    app_data_t       l4cb_app_data;

} l4_control_block_t;

typedef LIST_HEAD(tlkp_hash_bucket_s, l4_control_block_s) tlkp_hash_bucket_t;

/* Useful for extracting the l4cb_tx_hash from the control block. */
#if defined(TPG_L4_CB_TX_HASH)
#define L4CB_TX_HASH(l4_cb) ((l4_cb)->l4cb_tx_hash)
#else /* defined(TPG_L4_CB_TX_HASH) */
#define L4CB_TX_HASH(l4_cb) 0
#endif /* defined(TPG_L4_CB_TX_HASH) */

/*****************************************************************************
 * Externals for tpg_lookup.c
 ****************************************************************************/
extern uint32_t tlkp_calc_connection_hash(uint32_t dst_addr,
                                          uint32_t src_addr,
                                          uint16_t dst_port,
                                          uint16_t src_port);
extern uint32_t tlkp_calc_pkt_hash(uint32_t local_addr,
                                   uint32_t remote_addr,
                                   uint16_t local_port,
                                   uint16_t remote_port);
extern uint32_t tlkp_get_qindex_from_hash(uint32_t hash, uint32_t phys_port);

extern bool tlkp_init(void);

extern l4_control_block_t *tlkp_find_v4_cb(tlkp_hash_bucket_t *htable,
                                           uint32_t phys_port, uint32_t l4_hash,
                                           uint32_t local_addr, uint32_t remote_addr,
                                           uint16_t local_port, uint16_t remote_port);

extern int tlkp_add_cb(tlkp_hash_bucket_t *htable, l4_control_block_t *cb);

extern int tlkp_delete_cb(tlkp_hash_bucket_t *htable,
                          l4_control_block_t *cb);

extern void tlkp_init_cb(l4_control_block_t *l4_cb,
                         uint32_t local_addr, uint32_t remote_addr,
                         uint16_t local_port, uint16_t remote_port,
                         uint32_t l4_hash, uint32_t cb_interface,
                         uint32_t test_case_id,
                         tpg_app_proto_t app_id,
                         sockopt_t *sockopt,
                         uint32_t flags);

/*****************************************************************************
 * Inlines for l4_control_block_t
 ****************************************************************************/
#if defined(TPG_L4_CB_DEBUG)
/*****************************************************************************
 * l4_cb_valid()
 ****************************************************************************/
static inline __attribute__((__always_inline__))
bool l4_cb_valid(l4_control_block_t *cb)
{
    return cb->l4cb_valid;
}

/*****************************************************************************
 * l4_cb_set_valid()
 ****************************************************************************/
static inline __attribute__((__always_inline__))
void l4_cb_set_valid(l4_control_block_t *cb, bool valid)
{
    cb->l4cb_valid = valid;
}

/*****************************************************************************
 * l4_cb_alloc_init()
 *      Sets the atomic corresponding to the cb id to 1 and checks that it
 *      wasn't allocated before.
 ****************************************************************************/
static inline __attribute__((__always_inline__))
void l4_cb_alloc_init(l4_control_block_t *cb, rte_atomic16_t *mask,
                      uint32_t max_id)
{
    if (cb->l4cb_valid)
        TPG_ERROR_ABORT("Corruption: %s!\n",
                        "Allocating an L4 control block that was already allocated");

    /* Validate id. */
    if (cb->l4cb_id >= max_id)
        TPG_ERROR_ABORT("Corruption: %s!\n", "Corrupted L4 control block id");

    /* Mark as allocated. */
    if (rte_atomic16_cmpset((volatile uint16_t *)&mask[cb->l4cb_id].cnt,
                            0, 1) == 0)
        TPG_ERROR_ABORT("Corruption: %s!\n",
                        "L4 control block already allocated");

    l4_cb_set_valid(cb, true);
}

/*****************************************************************************
 * l4_cb_free_deinit()
 *      Sets the atomic corresponding to the cb id to 0 and checks that it
 *      wasn't freed before.
 ****************************************************************************/
static inline __attribute__((__always_inline__))
void l4_cb_free_deinit(l4_control_block_t *cb, rte_atomic16_t *mask,
                      uint32_t max_id)
{
    if (!cb->l4cb_valid) {
        TPG_ERROR_ABORT("Corruption: %s!\n",
                        "Freeing an L4 control block that was already freed");
    }

    /* Validate id. */
    if (cb->l4cb_id >= max_id)
        TPG_ERROR_ABORT("Corruption: %s!\n", "Corrupted L4 control block id");

    /* Mark as freed. */
    if (rte_atomic16_cmpset((volatile uint16_t *)&mask[cb->l4cb_id].cnt,
                            1, 0) == 0)
        TPG_ERROR_ABORT("Corruption: %s!\n", "L4 control block already freed");

    l4_cb_set_valid(cb, false);
}

/* If we start tracking more than 32M TCBs we might have a problem with the
 * fact that we use atomic16_t per tcb!
 */
#define TPG_MAX_CB_DEBUG (0x2000000)

static_assert(GCFG_TCB_POOL_SIZE < TPG_MAX_CB_DEBUG,
              "Please check max TCB pool size");

/*****************************************************************************
 * l4_cb_mempool_init()
 *      Initializes the valid bit and id in all the cbs in the mempool.
 ****************************************************************************/
static inline __attribute__((__always_inline__))
void l4_cb_mempool_init(struct rte_mempool **mempools, rte_atomic16_t **mask,
                        uint32_t *max_id,
                        size_t l4_cb_offset)
{
    void                *entry;
    tlkp_test_cb_list_t  tq;
    l4_control_block_t  *l4_cb;
    uint32_t             id = 0;
    uint32_t             core;

    RTE_LCORE_FOREACH_SLAVE(core) {
        if (!cfg_is_pkt_core(core))
            continue;

        TAILQ_INIT(&tq);

        while (rte_mempool_generic_get(mempools[core], &entry, 1, NULL,
                                       MEMPOOL_F_SC_GET) == 0) {
            l4_cb = (l4_control_block_t *)(((char *)entry) + l4_cb_offset);

            l4_cb->l4cb_id = id++;
            l4_cb->l4cb_valid = false;
            TAILQ_INSERT_TAIL(&tq, l4_cb, l4cb_test_list_entry);
        }

        while (!TAILQ_EMPTY(&tq)) {
            l4_cb = (l4_control_block_t *)TAILQ_FIRST(&tq);
            entry = (((char *)l4_cb) - l4_cb_offset);

            TAILQ_REMOVE(&tq, l4_cb, l4cb_test_list_entry);
            rte_mempool_generic_put(mempools[core], &entry, 1, NULL,
                                    MEMPOOL_F_SP_PUT);
        }
    }

    *mask = rte_zmalloc("cb_mpool_mask", id * sizeof(**mask), 0);
    if (*mask == NULL)
        TPG_ERROR_ABORT("ERROR: %s!\n", "Failed to allocate cb_mpool_mask");

    *max_id = id;
}

/*****************************************************************************
 * l4_cb_check()
 *      Checks that the cb is still valid.
 ****************************************************************************/
static inline __attribute__((__always_inline__))
void l4_cb_check(l4_control_block_t *cb)
{
    if (!cb->l4cb_valid)
        TPG_ERROR_ABORT("Corruption: %s!\n",
                        "Trying to use invalid L4 Control Block");
}

#define L4_CB_ID(cb) \
    ((cb)->l4cb_id)
#define L4_CB_ID_SET(cb, val) \
    ((cb)->l4cb_id = (val))
#define L4_CB_VALID(cb) \
    l4_cb_valid((cb))
#define L4_CB_ALLOC_INIT(cb, mask, max) \
    l4_cb_alloc_init((cb), (mask), (max))
#define L4_CB_FREE_DEINIT(cb, mask, max) \
    l4_cb_free_deinit((cb), (mask), (max))
#define L4_CB_MPOOL_INIT(mpool, mask, max, l4_cb_offset) \
    l4_cb_mempool_init((mpool), (mask), (max), (l4_cb_offset))
#define L4_CB_CHECK(cb) \
    l4_cb_check((cb))

#else /* defined(TPG_L4_CB_DEBUG) */

#define L4_CB_ID(cb) 0
#define L4_CB_ID_SET(cb, val) ((void)(cb), (void)(val))
#define L4_CB_VALID(cb) true
#define L4_CB_ALLOC_INIT(cb, mask, max)
#define L4_CB_FREE_DEINIT(cb, mask, max)
#define L4_CB_MPOOL_INIT(mpool, mask, max, l4_cb_offset)
#define L4_CB_CHECK(cb) (void)0

#endif /* defined(TPG_L4_CB_DEBUG) */

#define L4_CB_ADD_LIST(list, cb)                               \
    do {                                                       \
        L4_CB_CHECK((cb));                                     \
        TAILQ_INSERT_TAIL((list), (cb), l4cb_test_list_entry); \
    } while (0)

#define L4_CB_REM_LIST(list, cb)                          \
    do {                                                  \
        L4_CB_CHECK((cb));                                \
        TAILQ_REMOVE((list), (cb), l4cb_test_list_entry); \
    } while (0)

#define L4CB_TEST_TMR_IS_SET(cb)  \
    (L4_CB_CHECK(cb),             \
     (cb)->l4cb_on_test_tmr_list)

#define L4CB_TEST_TMR_SET(cb, val)        \
    do {                                  \
        L4_CB_CHECK((cb));                \
        l4cb_timer_test_set((cb), (val)); \
    } while (0)

#define L4CB_TEST_TMR_CANCEL(cb)      \
    do {                              \
        L4_CB_CHECK((cb));            \
        l4cb_timer_test_cancel((cb)); \
    } while (0)

/*****************************************************************************
 * Inlines for tpg_lookup.c
 ****************************************************************************/
/*****************************************************************************
 * tlkp_get_hash_bucket()
 ****************************************************************************/
static inline tlkp_hash_bucket_t *tlkp_get_hash_bucket(tlkp_hash_bucket_t *htable,
                                                       uint32_t phys_port,
                                                       uint32_t hash)
{
    return htable + (phys_port * TPG_HASH_BUCKET_SIZE) + TPG_HASH_MOD(hash);
}

/*****************************************************************************
 * tlkp_walk_v4()
 ****************************************************************************/
typedef bool (*tlkp_walk_v4_cb_t)(l4_control_block_t *cb, void *arg);

static inline void tlkp_walk_v4(tlkp_hash_bucket_t *htable,
                                uint32_t phys_port,
                                tlkp_walk_v4_cb_t callback,
                                void *arg)
{
    tlkp_hash_bucket_t *bucket;
    tlkp_hash_bucket_t *end;

    bucket = htable + (phys_port * TPG_HASH_BUCKET_SIZE);
    end = bucket + TPG_HASH_BUCKET_SIZE;

    for (; bucket != end; bucket++) {
        l4_control_block_t *cb = bucket->lh_first;

        /* LIST_FOREACH is not safe for deletion. */
        while (cb) {
            l4_control_block_t *nxt = cb->l4cb_hash_bucket_entry.le_next;

            if (!callback(cb, arg))
                return;
            cb = nxt;
        }
    }
}

/*****************************************************************************
 * l4_cb_calc_connection_hash()
 ****************************************************************************/
static inline void l4_cb_calc_connection_hash(l4_control_block_t *cb)
{
    cb->l4cb_rx_hash = tlkp_calc_connection_hash(cb->l4cb_dst_addr.ip_v4,
                                                 cb->l4cb_src_addr.ip_v4,
                                                 cb->l4cb_dst_port,
                                                 cb->l4cb_src_port);
#if defined(TPG_L4_CB_TX_HASH)
    cb->l4cb_tx_hash = tlkp_calc_connection_hash(cb->l4cb_src_addr.ip_v4,
                                                 cb->l4cb_dst_addr.ip_v4,
                                                 cb->l4cb_src_port,
                                                 cb->l4cb_dst_port);
#endif /* defined(TPG_L4_CB_TX_HASH) */
}

/*****************************************************************************
 * Flags for tlkp_init_*cb()
 ****************************************************************************/
#define TPG_CB_INIT_NO_FLAG             0x00000000
/* Use the supplied L4 Hash value */
#define TPG_CB_USE_L4_HASH_FLAG         0x00000001
/* Use existing flag values. */
#define TCG_CB_REUSE_CB                 0x00000002
/* Flags. */
#define TCG_CB_CONSUME_ALL_DATA         0x00000004
#define TCG_CB_MALLOCED                 0x00000008

#endif /* _H_TPG_LOOKUP_ */

