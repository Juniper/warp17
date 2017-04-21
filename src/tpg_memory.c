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
 *     tpg_memory.c
 *
 * Description:
 *     Common place where memory and buffer pools get allocated
 *
 * Author:
 *     Dumitru Ceara, Eelco Chaudron
 *
 * Initial Created:
 *     02/27/2015
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
static struct rte_mempool *mbuf_pool[RTE_MAX_LCORE]; /* Indexed by lcore id */
static struct rte_mempool *mbuf_pool_tx_hdr[RTE_MAX_LCORE]; /* Indexed by lcore id */
static struct rte_mempool *mbuf_pool_clone[RTE_MAX_LCORE]; /* Indexed by lcore id */
static struct rte_mempool *tcb_pool[RTE_MAX_LCORE]; /* Indexed by lcore id */
static struct rte_mempool *ucb_pool[RTE_MAX_LCORE]; /* Indexed by lcore id */

RTE_DEFINE_PER_LCORE(struct rte_mempool *, mbuf_pool);
RTE_DEFINE_PER_LCORE(struct rte_mempool *, mbuf_pool_tx_hdr);
RTE_DEFINE_PER_LCORE(struct rte_mempool *, mbuf_pool_clone);
RTE_DEFINE_PER_LCORE(struct rte_mempool *, tcb_pool);
RTE_DEFINE_PER_LCORE(struct rte_mempool *, ucb_pool);

static struct {

    uint32_t socket;
    uint32_t cores_per_socket;

}  mem_sockets[RTE_MAX_LCORE];

static uint32_t mem_socket_cnt;

/*****************************************************************************
 * Forward declarations
 ****************************************************************************/
static cmdline_parse_ctx_t cli_ctx[];

/*****************************************************************************
 * mem_init_sockets()
 *      Note: I don't see any rte* API for getting the number of available
 *      sockets
 ****************************************************************************/
static void mem_init_sockets(void)
{
    uint32_t core;
    uint32_t i;

    RTE_LCORE_FOREACH_SLAVE(core) {
        uint32_t socket = rte_lcore_to_socket_id(core);

        if (!cfg_is_pkt_core(core))
            continue;

        /* Check if we saw this socket before. */
        for (i = 0; i < mem_socket_cnt; i++) {
            if (mem_sockets[i].socket == socket) {
                mem_sockets[i].cores_per_socket++;
                break;
            }
        }

        /* Not seen before so increment the count. */
        if (i == mem_socket_cnt) {
            mem_sockets[mem_socket_cnt].cores_per_socket = 1;
            mem_sockets[mem_socket_cnt].socket = socket;
            mem_socket_cnt++;
        }
    }
}

/*****************************************************************************
 * mem_get_mbuf_pool()
 ****************************************************************************/
struct rte_mempool *mem_get_mbuf_pool(uint32_t port, uint32_t queue_id)
{
    uint32_t core;

    RTE_LCORE_FOREACH_SLAVE(core) {
        if (port_get_rx_queue_id(core, port) == (int)queue_id)
            return mbuf_pool[core];
    }

    /* Should never happen! */
    assert(false);
}

/*****************************************************************************
 * mem_get_tcb_pools()
 ****************************************************************************/
struct rte_mempool **mem_get_tcb_pools(void)
{
    return tcb_pool;
}

/*****************************************************************************
 * mem_get_ucb_pools()
 ****************************************************************************/
struct rte_mempool **mem_get_ucb_pools(void)
{
    return ucb_pool;
}

/*****************************************************************************
 * mem_create_local_pool()
 ****************************************************************************/
static struct rte_mempool *mem_create_local_pool(const char *name, uint32_t core,
                                                 uint32_t pool_size,
                                                 uint32_t obj_size,
                                                 uint32_t cache_size,
                                                 uint32_t private_size,
                                                 rte_mempool_ctor_t mp_ctor,
                                                 rte_mempool_obj_ctor_t obj_ctor,
                                                 uint32_t pool_flags)
{
    struct rte_mempool *pool;
    char                pool_name[strlen(name) + 64];

    sprintf(pool_name, "%s-%u", name, core);

    /* Cache size must be less than pool size. Force it! */
    if (cache_size >= pool_size)
        cache_size = pool_size / 2;

    RTE_LOG(INFO, USER1, "Creating mempool %s on core %"PRIu32
            "(size: %"PRIu32", obj_size: %"PRIu32", priv: %"PRIu32" cache: %"PRIu32")\n",
            pool_name,
            core,
            pool_size,
            obj_size,
            private_size,
            cache_size);

    pool = rte_mempool_create(pool_name, pool_size, obj_size, cache_size,
                              private_size,
                              mp_ctor, NULL,
                              obj_ctor, NULL,
                              rte_lcore_to_socket_id(core),
                              pool_flags);

    if (pool == NULL) {
        RTE_LOG(ERR, USER1, "Failed allocating %s pool!\n", pool_name);
        return NULL;
    }
    return pool;
}

/*****************************************************************************
 * mem_handle_cmdline_opt()
 * --tcb-pool-sz configuration - size in K (*1024) of the tcb mempool
 *      default: GCFG_TCB_POOL_SIZE
 * --ucb-pool-sz configuration - size in K (*1024) of the ucb mempool
 *      default: GCFG_UCB_POOL_SIZE
 * --mbuf-pool-sz configuration - size in K (*1024) of the mbuf mempool
 *      default: GCFG_MBUF_POOL_SIZE
 * --mbuf-sz configuration - size in bytes of the mbuf
 *      default: GCFG_MBUF_SIZE
 * --mbuf-hdr-pool-sz configuration - size in K (*1024) of the mbuf hdr mempool
 *      default: GCFG_MBUF_HDR_POOL_SIZE
 ****************************************************************************/
bool mem_handle_cmdline_opt(const char *opt_name, char *opt_arg)
{
    global_config_t *cfg = cfg_get_config();

    if (!cfg)
        TPG_ERROR_ABORT("ERROR: Unable to get config!\n");

    if (strcmp(opt_name, "tcb-pool-sz") == 0) {
        unsigned long var = strtoul(opt_arg, NULL, 10) * 1024ULL;

        if (var <= UINT32_MAX)
            cfg->gcfg_tcb_pool_size = var;
        else
            TPG_ERROR_EXIT(EXIT_FAILURE,
                           "ERROR: Invalid tcb-pool-sz value %s!\n"
                           "The value must be lower than %d\n",
                           opt_arg, UINT32_MAX);
        return true;
    }

    if (strcmp(opt_name, "ucb-pool-sz") == 0) {
        unsigned long var = strtoul(opt_arg, NULL, 10) * 1024ULL;

        if (var <= UINT32_MAX)
            cfg->gcfg_ucb_pool_size = var;
        else
            TPG_ERROR_EXIT(EXIT_FAILURE,
                           "ERROR: Invalid ucb-pool-sz value %s!\n"
                           "The value must be lower than %d\n",
                           opt_arg, UINT32_MAX);
        return true;
    }

    if (strcmp(opt_name, "mbuf-pool-sz") == 0) {
        unsigned long var = strtoul(opt_arg, NULL, 10) * 1024UL;

        if (var <= UINT32_MAX)
            cfg->gcfg_mbuf_poolsize = var;
        else
            TPG_ERROR_EXIT(EXIT_FAILURE,
                           "ERROR: Invalid mbuf-pool-sz value %s!\n"
                           "The value must be lower than %d\n",
                           opt_arg, UINT32_MAX);
        return true;
    }

    if (strcmp(opt_name, "mbuf-sz") == 0) {
        unsigned long mbuf_size = strtoul(opt_arg, NULL, 10);

        if (mbuf_size >= GCFG_MBUF_SIZE && mbuf_size <= PORT_MAX_MTU) {
            cfg->gcfg_mbuf_size = mbuf_size;
        } else {
            TPG_ERROR_EXIT(EXIT_FAILURE,
                           "ERROR: Invalid mbuf-sz value %s!\n"
                           "The value must be greater %d and lower than %d\n",
                           opt_arg, PORT_MAX_MTU,
                           GCFG_MBUF_SIZE);
        }
        return true;
    }

    if (strcmp(opt_name, "mbuf-hdr-pool-sz") == 0) {
        unsigned long var = strtoul(opt_arg, NULL, 10) * 1024UL;

        if (var <= UINT32_MAX)
            cfg->gcfg_mbuf_hdr_poolsize = var;
        else
            TPG_ERROR_EXIT(EXIT_FAILURE,
                           "ERROR: Invalid mbuf-hdr-pool-sz value %s!\n"
                           "The value must be lower than %d\n",
                           opt_arg, UINT32_MAX);
        return true;
    }

    return false;
}

/*****************************************************************************
 * mem_init()
 ****************************************************************************/
bool mem_init(void)
{
    global_config_t *cfg;
    uint32_t         core;
    uint32_t         core_divider;

    /*
     * Add Memory module CLI commands
     */
    if (!cli_add_main_ctx(cli_ctx)) {
        RTE_LOG(ERR, USER1, "ERROR: Can't add mem module specific CLI commands!\n");
        return false;
    }

    cfg = cfg_get_config();
    if (cfg == NULL)
        return false;

    mem_init_sockets();

    core_divider = (rte_lcore_count() - TPG_NR_OF_NON_PACKET_PROCESSING_CORES);

    RTE_LCORE_FOREACH_SLAVE(core) {
        if (!cfg_is_pkt_core(core))
            continue;

        mbuf_pool[core] =
            mem_create_local_pool(GCFG_MBUF_POOL_NAME, core,
                                  cfg->gcfg_mbuf_poolsize / core_divider,
                                  cfg->gcfg_mbuf_size + sizeof(struct rte_mbuf),
                                  cfg->gcfg_mbuf_cache_size,
                                  sizeof(struct rte_pktmbuf_pool_private),
                                  rte_pktmbuf_pool_init,
                                  rte_pktmbuf_init,
                                  MEM_MBUF_POOL_FLAGS);

        if (mbuf_pool[core] == NULL)
            return false;

        mbuf_pool_tx_hdr[core] =
            mem_create_local_pool(GCFG_MBUF_POOL_HDR_NAME, core,
                                  cfg->gcfg_mbuf_hdr_poolsize / core_divider,
                                  cfg->gcfg_mbuf_hdr_size,
                                  cfg->gcfg_mbuf_hdr_cache_size,
                                  sizeof(struct rte_pktmbuf_pool_private),
                                  rte_pktmbuf_pool_init,
                                  rte_pktmbuf_init,
                                  MEM_MBUF_POOL_FLAGS);

        if (mbuf_pool_tx_hdr[core] == NULL)
            return false;

        mbuf_pool_clone[core] =
            mem_create_local_pool(GCFG_MBUF_POOL_CLONE_NAME, core,
                                  cfg->gcfg_mbuf_poolsize / core_divider,
                                  cfg->gcfg_mbuf_clone_size,
                                  cfg->gcfg_mbuf_cache_size,
                                  sizeof(struct rte_pktmbuf_pool_private),
                                  rte_pktmbuf_pool_init,
                                  rte_pktmbuf_init,
                                  MEM_MBUF_POOL_FLAGS);

        if (mbuf_pool_clone[core] == NULL)
            return false;

        tcb_pool[core] =
            mem_create_local_pool(GCFG_TCB_POOL_NAME, core,
                                  cfg->gcfg_tcb_pool_size / core_divider,
                                  sizeof(tcp_control_block_t),
                                  0,
                                  0,
                                  NULL,
                                  NULL,
                                  MEM_TCB_POOL_FLAGS);

        if (tcb_pool[core] == NULL)
            return false;

        ucb_pool[core] =
            mem_create_local_pool(GCFG_UCB_POOL_NAME, core,
                                  cfg->gcfg_ucb_pool_size / core_divider,
                                  sizeof(udp_control_block_t),
                                  0,
                                  0,
                                  NULL,
                                  NULL,
                                  MEM_UCB_POOL_FLAGS);

        if (ucb_pool[core] == NULL)
            return false;
    }

    return true;
}

/*****************************************************************************
 * mem_lcore_init()
 ****************************************************************************/
void mem_lcore_init(uint32_t lcore_id)
{
    RTE_PER_LCORE(mbuf_pool) = mbuf_pool[lcore_id];
    RTE_PER_LCORE(mbuf_pool_tx_hdr) = mbuf_pool_tx_hdr[lcore_id];
    RTE_PER_LCORE(mbuf_pool_clone) = mbuf_pool_clone[lcore_id];
    RTE_PER_LCORE(tcb_pool) = tcb_pool[lcore_id];
    RTE_PER_LCORE(ucb_pool) = ucb_pool[lcore_id];
}

/*****************************************************************************
 * CLI commands
 *****************************************************************************
 * - "show memory statistics {details}"
 ****************************************************************************/
struct cmd_show_memory_statistics_result {
    cmdline_fixed_string_t show;
    cmdline_fixed_string_t memory;
    cmdline_fixed_string_t statistics;
    cmdline_fixed_string_t details;
};

static cmdline_parse_token_string_t cmd_show_memory_statistics_T_show =
    TOKEN_STRING_INITIALIZER(struct cmd_show_memory_statistics_result, show, "show");
static cmdline_parse_token_string_t cmd_show_memory_statistics_T_memory =
    TOKEN_STRING_INITIALIZER(struct cmd_show_memory_statistics_result, memory, "memory");
static cmdline_parse_token_string_t cmd_show_memory_statistics_T_statistics =
    TOKEN_STRING_INITIALIZER(struct cmd_show_memory_statistics_result, statistics, "statistics");
static cmdline_parse_token_string_t cmd_show_memory_statistics_T_details =
    TOKEN_STRING_INITIALIZER(struct cmd_show_memory_statistics_result, details, "details");

static void cmd_show_memory_statistics_mempool(struct cmdline *cl,
                                               const char *pool_name,
                                               struct rte_mempool **mempools,
                                               int option)
{
    uint64_t alloc_cnt = 0;
    uint64_t free_cnt = 0;
    uint32_t core;

    cmdline_printf(cl, "Mempool %s:\n", pool_name);

    RTE_LCORE_FOREACH_SLAVE(core) {
        struct rte_mempool *mempool;

        if (!cfg_is_pkt_core(core))
            continue;

        mempool = mempools[core];

        alloc_cnt += rte_mempool_in_use_count(mempool);
        free_cnt += rte_mempool_avail_count(mempool);

        if (option == 'd') {
            cmdline_printf(cl, "  Core %"PRIu32":\n", core);
            cmdline_printf(cl, "    Allocated: %"PRIu32"\n",
                           rte_mempool_in_use_count(mempool));
            cmdline_printf(cl, "    Free     : %"PRIu32"\n",
                           rte_mempool_avail_count(mempool));
            cmdline_printf(cl, "\n");
        }
    }
    cmdline_printf(cl, "  Total Allocated: %"PRIu64"\n", alloc_cnt);
    cmdline_printf(cl, "  Total Free     : %"PRIu64"\n", free_cnt);
    cmdline_printf(cl, "\n");
}

static void cmd_show_memory_statistics_parsed(void *parsed_result __rte_unused,
                                              struct cmdline *cl,
                                              void *data)
{
    int option = (intptr_t) data;

    cmd_show_memory_statistics_mempool(cl, "MBUF RX", mbuf_pool, option);
    cmd_show_memory_statistics_mempool(cl, "MBUF TX HDR", mbuf_pool_tx_hdr,
                                       option);
    cmd_show_memory_statistics_mempool(cl, "MBUF CLONE", mbuf_pool_clone,
                                       option);
    cmd_show_memory_statistics_mempool(cl, "TCB", tcb_pool, option);
    cmd_show_memory_statistics_mempool(cl, "UCB", ucb_pool, option);
}

cmdline_parse_inst_t cmd_show_memory_statistics = {
    .f = cmd_show_memory_statistics_parsed,
    .data = NULL,
    .help_str = "show memory statistics",
    .tokens = {
        (void *)&cmd_show_memory_statistics_T_show,
        (void *)&cmd_show_memory_statistics_T_memory,
        (void *)&cmd_show_memory_statistics_T_statistics,
        NULL,
    },
};

cmdline_parse_inst_t cmd_show_memory_statistics_details = {
    .f = cmd_show_memory_statistics_parsed,
    .data = (void *) (intptr_t) 'd',
    .help_str = "show memory statistics details",
    .tokens = {
        (void *)&cmd_show_memory_statistics_T_show,
        (void *)&cmd_show_memory_statistics_T_memory,
        (void *)&cmd_show_memory_statistics_T_statistics,
        (void *)&cmd_show_memory_statistics_T_details,
        NULL,
    },
};

/*****************************************************************************
 * Main menu context
 ****************************************************************************/
static cmdline_parse_ctx_t cli_ctx[] = {
    &cmd_show_memory_statistics,
    &cmd_show_memory_statistics_details,
    NULL,
};

