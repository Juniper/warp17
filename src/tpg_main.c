/*
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER
 *
 * Copyright (c) 2017, Juniper Networks, Inc. All rights reserved.
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
 *     tpg_main.c
 *
 * Description:
 *     High volume TCP generator.
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
#include <stdlib.h>

#include "tcp_generator.h"

/*****************************************************************************
 * Global variables
 ****************************************************************************/
bool      tpg_exit;
uint64_t  cycles_per_us;
char     *tpg_prgname;

/*****************************************************************************
 * start_cores()
 ****************************************************************************/
static void start_cores(void)
{
    uint32_t core;

    /*
     * Fire up the packet processing cores
     */
    RTE_LCORE_FOREACH_SLAVE(core) {
        int index = rte_lcore_index(core);

        switch (index) {
        case TPG_CORE_IDX_CLI:
            assert(false);
        break;
        case TPG_CORE_IDX_TEST_MGMT:
            rte_eal_remote_launch(test_mgmt_loop, NULL, core);
        break;
        default:
            assert(index >= TPG_NR_OF_NON_PACKET_PROCESSING_CORES);
            rte_eal_remote_launch(pkt_receive_loop, NULL, core);
        }
    }

    /*
     * Wait for packet cores to finish initialization.
     */
    RTE_LCORE_FOREACH_SLAVE(core) {
        int   error;
        msg_t msg;

        if (!cfg_is_pkt_core(core))
            continue;

        msg_init(&msg, MSG_PKTLOOP_INIT_WAIT, core, 0);
        /* BLOCK waiting for msg to be processed */
        error = msg_send(&msg, 0);
        if (error)
            TPG_ERROR_ABORT("ERROR: Failed to send pktloop init wait msg: %s(%d)!\n",
                            rte_strerror(-error), -error);
    }
}

/*****************************************************************************
 * main()
 ****************************************************************************/
int main(int argc, char **argv)
{
    global_config_t *cfg;
    int              ret;

    tpg_prgname = argv[0];

    /*
     * Initialize DPDK infrastructure before we do anything else
     */
    rte_set_application_usage_hook(cfg_print_usage);
    ret = rte_eal_init(argc, argv);
    if (ret < 0)
        rte_panic("Cannot init EAL\n");

    /*
     * Initialize RTE timer library
     */
    rte_timer_subsystem_init();

    /*
     * Precalculate the number of cycles per us so we don't do it everytime.
     */
    cycles_per_us = (rte_get_timer_hz() / 1000000);

    /*
     * Return value above to be used to scan app specific options
     */
    argc -= ret;
    argv += ret;

    /*
     * General checks
     */
    if (rte_lcore_count() < 3) {
        TPG_ERROR_EXIT(EXIT_FAILURE, "ERROR: %s\n",
                       "WARP17 needs at least three cores!");
    }
    /* We only support at most 64 cores right now (to make parsing easier). */
    if (rte_lcore_count() > (sizeof(uint64_t) * 8)) {
        TPG_ERROR_EXIT(EXIT_FAILURE,
                       "ERROR: WARP17 supports at most %"PRIu32" cores!\n",
                       (uint32_t)sizeof(uint64_t) * 8);
    }
    if (rte_eth_dev_count() > TPG_ETH_DEV_MAX) {
        TPG_ERROR_EXIT(EXIT_FAILURE,
                       "ERROR: WARP17 works with at most %u ports!\n",
                       TPG_ETH_DEV_MAX);
    }

    /*
     * Initialize various submodules
     */

    if (!cli_init()) {
        TPG_ERROR_EXIT(EXIT_FAILURE, "ERROR: %s!\n",
                       "Failed initializing the command line interface");
    }

    if (!rpc_init()) {
        TPG_ERROR_EXIT(EXIT_FAILURE, "ERROR: %s!\n",
                       "Failed initializing the RPC server");
    }

    if (!cfg_init()) {
        TPG_ERROR_EXIT(EXIT_FAILURE, "ERROR: %s\n",
                       "Failed initializing default configuration!\n");
    }

    if (!cfg_handle_command_line(argc, argv))
        exit(EXIT_FAILURE); /* Error reporting is handled by the function itself */

    if (!trace_init()) {
        TPG_ERROR_EXIT(EXIT_FAILURE, "ERROR: %s!\n",
                       "Failed initializing the tracing module");
    }

    if (!trace_filter_init()) {
        TPG_ERROR_EXIT(EXIT_FAILURE, "ERROR: %s!\n",
                       "Failed initializing the trace filter module");
    }

    if (!mem_init()) {
        TPG_ERROR_EXIT(EXIT_FAILURE, "ERROR: %s!\n",
                       "Failed allocating required mbufs");
    }

    /* WARNING: Careful when adding code above this point. Up until ports are
     * initialized DPDK can't know that there might be ring interfaces that
     * still need to be created. Therefore any call to rte_eth_dev_count()
     * doesn't include them.
     */
    if (!port_init()) {
        TPG_ERROR_EXIT(EXIT_FAILURE, "ERROR: %s!\n",
                       "Failed initializing the Ethernets ports");
    }

    if (!msg_sys_init()) {
        TPG_ERROR_EXIT(EXIT_FAILURE, "ERROR: %s!\n",
                       "Failed initializing the message queues");
    }

    if (!test_mgmt_init()) {
        TPG_ERROR_EXIT(EXIT_FAILURE, "ERROR: %s!\n",
                       "Failed initializing test mgmt");
    }

    if (!test_init()) {
        TPG_ERROR_EXIT(EXIT_FAILURE, "ERROR: %s!\n",
                       "Failed initializing tests");
    }

    if (!eth_init()) {
        TPG_ERROR_EXIT(EXIT_FAILURE, "ERROR: %s!\n",
                       "Failed initializing the Ethernets pkt handler");
    }

    if (!arp_init()) {
        TPG_ERROR_EXIT(EXIT_FAILURE, "ERROR: %s!\n",
                       "Failed initializing the ARP pkt handler");
    }

    if (!route_init()) {
        TPG_ERROR_EXIT(EXIT_FAILURE, "ERROR: %s!\n",
                       "Failed initializing the ROUTE module");
    }

    if (!ipv4_init()) {
        TPG_ERROR_EXIT(EXIT_FAILURE, "ERROR: %s!\n",
                       "Failed initializing the IPv4 pkt handler");
    }

    if (!tcp_init()) {
        TPG_ERROR_EXIT(EXIT_FAILURE, "ERROR: %s!\n",
                       "Failed initializing the TCP pkt handler");
    }

    if (!udp_init()) {
        TPG_ERROR_EXIT(EXIT_FAILURE, "ERROR: %s!\n",
                       "Failed initializing the UDP pkt handler");
    }

    if (!tlkp_init()) {
        TPG_ERROR_EXIT(EXIT_FAILURE, "ERROR: %s!\n",
                       "Failed initializing the Session lookup engine");
    }

    if (!tlkp_tcp_init()) {
        TPG_ERROR_EXIT(EXIT_FAILURE, "ERROR: %s!\n",
                       "Failed initializing the TCP lookup engine");
    }

    if (!tlkp_udp_init()) {
        TPG_ERROR_EXIT(EXIT_FAILURE, "ERROR: %s!\n",
                       "Failed initializing the UDP lookup engine");
    }

    if (!tsm_init()) {
        TPG_ERROR_EXIT(EXIT_FAILURE, "ERROR: %s!\n",
                       "Failed initializing the TSM module");
    }

    if (!timer_init()) {
        TPG_ERROR_EXIT(EXIT_FAILURE, "ERROR: %s!\n",
                       "Failed initializing the TCP timers module");
    }

    if (!pkt_loop_init()) {
        TPG_ERROR_EXIT(EXIT_FAILURE, "ERROR: %s!\n",
                       "Failed initializing the pkt loop");
    }

    if (!raw_init()) {
        TPG_ERROR_EXIT(EXIT_FAILURE, "ERROR: %s!\n",
                       "Failed initializing the RAW Application module");
    }

    if (!http_init()) {
        TPG_ERROR_EXIT(EXIT_FAILURE, "ERROR: %s!\n",
                       "Failed initializing the RAW Application module");
    }

    start_cores();

    /*
     * Process startup command file, if any.
     */
    cfg = cfg_get_config();
    if (cfg != NULL && cfg->gcfg_cmd_file) {
        if (!cli_run_input_file(cfg->gcfg_cmd_file)) {
            TPG_ERROR_EXIT(EXIT_FAILURE, "Failed to run command file: %s!\n",
                           cfg->gcfg_cmd_file);
        }
    }

    /*
     * Process CLI commands, and other house keeping tasks...
     */
    cli_interact();
    tpg_exit = true;

    /*
     * Exit!!!
     */
    rte_eal_mp_wait_lcore();

    /*
     * Destroy the CLI.
     */
    cli_exit();

    /*
     * Destroy the mgmt RPC server.
     */
    rpc_destroy();
    return 0;
}

/*****************************************************************************
 * main_handle_cmdline_opt()
 * --version - Returns version and exit
 ****************************************************************************/
cmdline_arg_parser_res_t main_handle_cmdline_opt(const char *opt_name,
                                                 char *opt_arg __rte_unused)
{
    if (strncmp(opt_name, "version", strlen("version") + 1) == 0) {
        printf(TPG_VERSION_PRINTF_STR"\n", TPG_VERSION_PRINTF_ARGS);
        cli_exit();
        rpc_destroy();
        exit(0);
        return CAPR_CONSUMED;
    }
    if (strncmp(opt_name, "help", strlen("help") + 1) == 0) {
        cfg_print_usage(tpg_prgname);
        cli_exit();
        rpc_destroy();
        exit(0);
        return CAPR_CONSUMED;
    }

    return CAPR_IGNORED;
}
