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
 * Globals
 ****************************************************************************/
static global_config_t global_config;
static bool            global_config_initialized;
cfg_cmdline_arg_parser_t cmdline_parsers[] = {
    MAIN_CMDLINE_PARSER(),
    PORT_CMDLINE_PARSER(),
    RING_IF_CMDLINE_PARSER(),
    KNI_IF_CMDLINE_PARSER(),
    MEM_CMDLINE_PARSER(),
    PKTLOOP_CMDLINE_PARSER(),
    CLI_CMDLINE_PARSER(),
    CMDLINE_ARG_PARSER(NULL, NULL, NULL),
};

struct option options[] = {
    MAIN_CMDLINE_OPTIONS(),
    PORT_CMDLINE_OPTIONS(),
    MEM_CMDLINE_OPTIONS(),
    PKTLOOP_CMDLINE_OPTIONS(),
    CLI_CMDLINE_OPTIONS(),
    RING_IF_CMDLINE_OPTIONS(),
    KNI_IF_CMDLINE_OPTIONS(),
    {.name = NULL},
};

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

/*****************************************************************************
 * cfg_init()
 ****************************************************************************/
bool cfg_init(void)
{
    bzero(&global_config, sizeof(global_config));

    global_config.gcfg_mbuf_cfg_poolsize = GCFG_MBUF_CFG_POOLSIZE_DEFAULT;

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

    global_config.gcfg_test_max_tc_runtime = TPG_DELAY_INF();

    global_config.gcfg_rate_no_lim_interval_size =
        GCFG_RATE_NO_LIM_INTERVAL_SIZE;

    global_config_initialized = true;
    return true;
}

/*****************************************************************************
 * cfg_handle_command_line()
 ****************************************************************************/
bool cfg_handle_command_line(int argc, char **argv)
{
    cfg_cmdline_arg_parser_t *cmdline_parser;
    cmdline_arg_parser_res_t  cmd_return;
    int                       opt;
    int                       optind;

    while ((opt = getopt_long(argc, argv, "", options, &optind)) != -1) {
        if (opt != 0) {
            cfg_print_usage(argv[0]);
            TPG_ERROR_EXIT(EXIT_FAILURE, "ERROR: invalid options supplied!\n");
            return false;
        }

        for (cmdline_parser = &cmdline_parsers[0];
             cmdline_parser->cap_arg_parser != NULL;
             cmdline_parser++) {
            cmd_return = cmdline_parser->cap_arg_parser(options[optind].name,
                                                        optarg);
            /* All times the argument was ignored, pass to the next parser.
             * If I find the correct parser and get a match or a bad value
             * then stop parsing.
             */
            if (cmd_return == CAPR_IGNORED)
                continue;
            break;
        }
        switch (cmd_return) {
        case CAPR_ERROR:
            cfg_print_usage(argv[0]);
            TPG_ERROR_EXIT(EXIT_FAILURE,
                           "ERROR: invalid option value supplied!\n");
            return false;
        case CAPR_IGNORED:
            cfg_print_usage(argv[0]);
            TPG_ERROR_EXIT(EXIT_FAILURE,
                           "ERROR: invalid options supplied!\n");
            return false;
        case CAPR_CONSUMED:
            continue;
        default:
            TPG_ERROR_EXIT(EXIT_FAILURE,
                           "ERROR: unknown parser return value!\n");
            return false;
        }
    }

    /* Announce all modules that the command line arg is done. */
    for (cmdline_parser = &cmdline_parsers[0];
         cmdline_parser->cap_arg_parser != NULL;
         cmdline_parser++) {
        if (cmdline_parser->cap_handler && !cmdline_parser->cap_handler())
            return false;
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
 * cfg_print_usage()
 ****************************************************************************/
void cfg_print_usage(const char *prgname)
{
    cfg_cmdline_arg_parser_t *cmdline_parser;

    printf("%s options:\n", prgname);

    for (cmdline_parser = &cmdline_parsers[0];
         cmdline_parser->cap_arg_parser != NULL;
         cmdline_parser++) {
        if (cmdline_parser->cap_usage != NULL)
            printf("%s", cmdline_parser->cap_usage);
    }
}
