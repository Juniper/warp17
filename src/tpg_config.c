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
 *     tpg_config.c
 *
 * Description:
 *     Global configuration storage.
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
#include <getopt.h>

#include "tcp_generator.h"

/*****************************************************************************
 * Local definitions
 ****************************************************************************/
#define CFG_PMASK_STR_MAXLEN 512

/*****************************************************************************
 * Globals
 ****************************************************************************/
static global_config_t global_config;
static bool            global_config_initialized;

static const char * const gtrace_names[TRACE_MAX] = {
    [TRACE_PKT_TX] = "TX",
    [TRACE_PKT_RX] = "RX",
    [TRACE_ARP]    = "ARP",
    [TRACE_ETH]    = "ETH",
    [TRACE_IPV4]   = "IPV4",
    [TRACE_TCP]    = "TCP",
    [TRACE_UDP]    = "UDP",
    [TRACE_TLK]    = "TLK",
    [TRACE_TSM]    = "TSM",
    [TRACE_TST]    = "TST",
    [TRACE_TMR]    = "TMR",
    [TRACE_HTTP]   = "HTTP",
};

static char    *qmap_args[TPG_ETH_DEV_MAX + 1];
static uint32_t qmap_args_cnt;

/*****************************************************************************
 * cfg_init()
 ****************************************************************************/
bool cfg_init(void)
{
    bzero(&global_config, sizeof(global_config));

    global_config.gcfg_mbuf_poolsize = GCFG_MBUF_POOLSIZE_DEFAULT;
    global_config.gcfg_mbuf_size = GCFG_MBUF_SIZE;
    global_config.gcfg_mbuf_cache_size = GCFG_MBUF_CACHE_SIZE;

    global_config.gcfg_mbuf_hdr_poolsize = GCFG_MBUF_POOLSZ_HDR_DEF;
    global_config.gcfg_mbuf_hdr_size = GCFG_MBUF_HDR_SIZE;
    global_config.gcfg_mbuf_hdr_cache_size = GCFG_MBUF_HDR_CACHE_SIZE;

    global_config.gcfg_mbuf_clone_size = GCFG_MBUF_CLONE_SIZE;

    global_config.gcfg_tcb_pool_size = GCFG_TCB_POOL_SIZE;
    global_config.gcfg_tcb_pool_cache_size = GCFG_TCB_POOL_CACHE_SIZE;

    global_config.gcfg_ucb_pool_size = GCFG_UCB_POOL_SIZE;
    global_config.gcfg_ucb_pool_cache_size = GCFG_UCB_POOL_CACHE_SIZE;

    global_config.gcfg_msgq_size = GCFG_MSGQ_SIZE;

    global_config.gcfg_slow_tmr_max = GCFG_SLOW_TMR_MAX;
    global_config.gcfg_slow_tmr_step = GCFG_SLOW_TMR_STEP;
    global_config.gcfg_rto_tmr_max = GCFG_RTO_TMR_MAX;
    global_config.gcfg_rto_tmr_step = GCFG_RTO_TMR_STEP;
    global_config.gcfg_test_tmr_max = GCFG_TEST_TMR_MAX;
    global_config.gcfg_test_tmr_step = GCFG_TEST_TMR_STEP;

    global_config.gcfg_test_max_tc_runtime = GCFG_TEST_MAX_TC_RUNTIME;
    global_config.gcfg_rate_min_interval_size = GCFG_RATE_MIN_INTERVAL_SIZE;

    global_config_initialized = true;
    return true;
}

/*****************************************************************************
 * cfg_pkt_core_count()
 *      Note: we always reserve the first TPG_NR_OF_NON_PACKET_PROCESSING_CORES
 *            lcore indexes for CLI and non-packet stuff.
 ****************************************************************************/
static uint32_t cfg_pkt_core_count(void)
{
    static uint32_t pkt_core_count;
    uint32_t        core;

    if (pkt_core_count)
        return pkt_core_count;

    RTE_LCORE_FOREACH_SLAVE(core) {
        if (cfg_is_pkt_core(core))
            pkt_core_count++;
    }
    return pkt_core_count;
}

/*****************************************************************************
 * cfg_handle_qmap_default_max_q()
 *      Note: we create as many queues per port as available cores.
 ****************************************************************************/
static void
cfg_handle_qmap_default_max_q(uint64_t *pcore_mask)
{
    uint32_t port;
    uint32_t core;

    for (port = 0; port < rte_eth_dev_count(); port++) {
        RTE_LCORE_FOREACH_SLAVE(core) {
            if (cfg_is_pkt_core(core))
                PORT_ADD_CORE_TO_MASK(pcore_mask[port], core);
        }
    }
}

/*****************************************************************************
 * cfg_handle_qmap_default_max_c()
 *      Note: we try to have as many dedicated cores per port as possible.
 ****************************************************************************/
static void cfg_handle_qmap_default_max_c(uint64_t *pcore_mask)
{
    uint32_t port;
    uint32_t core;
    uint32_t port_count     = rte_eth_dev_count();
    uint32_t pkt_core_count = cfg_pkt_core_count();
    uint32_t max            = ((port_count >= pkt_core_count) ?
                               port_count : pkt_core_count);
    uint32_t i;

    port = 0;
    core = 0;

    /* Start with the first packet core. */
    while (!cfg_is_pkt_core(core))
        core = rte_get_next_lcore(core, true, true);

    for (i = 0; i < max; i++) {
        PORT_ADD_CORE_TO_MASK(pcore_mask[port], core);

        /* Advance port and core ("modulo" port count and pkt core count) */
        port++; port %= port_count;
        do {
            core = rte_get_next_lcore(core, true, true);
        } while (!cfg_is_pkt_core(core));
    }
}

/*****************************************************************************
 * cfg_handle_command_line()
 *
 * qmap configuration - for example (for 2 ports and 3 cores):
 *
 *      +-----------------------+
 *      |    P0   |   P1        |
 *      +----+----+---+----+----+
 *      | Q0 | Q1 | Q0| Q1 | Q2 |
 *      +----+----+---+----+----+
 *      | C1 | C2 | C1| C2 | C3 |
 *      +----+----+---+----+----+
 * => "--qmap 0.0x6" & "--qmap 1.0xE"
 *
 * tcb-pool-sz configuration - size in M (*1024*1024) of the tcb mempool
 *      default: GCFG_TCB_POOL_SIZE
 *
 * pkt-send-drop-rate - if set then one packet every 'pkt-send-drop-rate' will
 *      be dropped at TX. (per lcore)
 * cmd-file           - file containing startup commands
 ****************************************************************************/
bool cfg_handle_command_line(int argc, char **argv)
{
    char *qmap_default = NULL;
    int   opt;
    int   optind;

    static struct option options[] = {
        {.name = "qmap", .has_arg = true, },
        {.name = "qmap-default", .has_arg = true},
        {.name = "tcb-pool-sz", .has_arg = true},
        {.name = "ucb-pool-sz", .has_arg = true},
        {.name = "pkt-send-drop-rate", .has_arg = true},
        {.name = "cmd-file", .has_arg = true},
        {.name = NULL},
    };

    while ((opt = getopt_long(argc, argv, "", options, &optind)) != -1) {
        if (opt != 0)
            TPG_ERROR_EXIT(EXIT_FAILURE, "ERROR: %s\n",
                           "invalid options supplied!");

        if (strcmp(options[optind].name, "qmap") == 0) {
            qmap_args[qmap_args_cnt++] = optarg;
        } else if (strcmp(options[optind].name, "qmap-default") == 0) {
            if (strcmp(optarg, "max-q") == 0 ||
                    strcmp(optarg, "max-c") == 0) {
                qmap_default = optarg;
            } else {
                TPG_ERROR_EXIT(EXIT_FAILURE,
                               "ERROR: invalid qmap-default value %s!\n",
                               optarg);
            }
        } else if (strcmp(options[optind].name, "tcb-pool-sz") == 0) {
            global_config.gcfg_tcb_pool_size = atoi(optarg) *
                                                    1024ULL * 1024ULL;
        } else if (strcmp(options[optind].name, "ucb-pool-sz") == 0) {
            global_config.gcfg_ucb_pool_size = atoi(optarg) *
                                                    1024ULL * 1024ULL;
        } else if (strcmp(options[optind].name, "pkt-send-drop-rate") == 0) {
            global_config.gcfg_pkt_send_drop_rate = atoi(optarg);
        } else if (strcmp(options[optind].name, "cmd-file") == 0) {
            global_config.gcfg_cmd_file = strdup(optarg);
        } else {
            TPG_ERROR_EXIT(EXIT_FAILURE, "ERROR: %s\n",
                           "invalid options supplied!");
        }
    }

    /* Initialize to default values here. E.g.: use the default qmap */
    if (qmap_default != NULL) {
        uint64_t pcore_mask[rte_eth_dev_count()];
        uint32_t port;

        if (qmap_args_cnt)
            TPG_ERROR_EXIT(EXIT_FAILURE,
                           "ERROR: %s\n",
                           "cannot supply both qmap and qmap-default at the same time!");

        bzero(&pcore_mask[0], sizeof(pcore_mask[0]) * rte_eth_dev_count());

        if (strcmp(qmap_default, "max-q") == 0)
            cfg_handle_qmap_default_max_q(pcore_mask);
        else if (strcmp(qmap_default, "max-c") == 0)
            cfg_handle_qmap_default_max_c(pcore_mask);

        for (port = 0; port < rte_eth_dev_count(); port++) {
            char pcore_mask_str[CFG_PMASK_STR_MAXLEN];

            sprintf(pcore_mask_str, "%u.0x%" PRIX64, port, pcore_mask[port]);

            /* We never free this memory */
            qmap_args[qmap_args_cnt++] = strdup(pcore_mask_str);
        }
    }

    return true;
}

/*****************************************************************************
 * cfg_get_config()
 ****************************************************************************/
global_config_t *cfg_get_config(void)
{
    if (!global_config_initialized)
        return NULL;

    return &global_config;
}

/*****************************************************************************
 * cfg_get_gtrace_name(id)
 ****************************************************************************/
const char *cfg_get_gtrace_name(gtrace_id_t id)
{
    assert(id < TRACE_MAX);
    return gtrace_names[id];
}

/*****************************************************************************
 * cfg_get_qmap()
 ****************************************************************************/
char **cfg_get_qmap(void)
{
    return qmap_args;
}

