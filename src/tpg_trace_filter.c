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
 *     tpg_trace_filter.c
 *
 * Description:
 *     Trace filters (applying to PCB and L4_CB)
 *
 * Author:
 *     Dumitru Ceara, Eelco Chaudron
 *
 * Initial Created:
 *     03/19/2015
 *
 * Notes:
 *
 */

/*****************************************************************************
 * Include files
 ****************************************************************************/
#include "tcp_generator.h"

/*****************************************************************************
 * Globals
 ****************************************************************************/
RTE_DEFINE_PER_LCORE(trace_filter_t, trace_filter);

/*****************************************************************************
 * Forward declarations
 ****************************************************************************/
static cmdline_parse_ctx_t cli_ctx[];

/*****************************************************************************
 * filter_handler()
 ****************************************************************************/
static int filter_handler(uint16_t msgid, uint16_t lcore __rte_unused,
                          void *msg)
{
    trace_filter_msg_t *filter_msg;

    if (MSG_INVALID(msgid, msg, MSG_TRACE_FILTER))
        return -EINVAL;

    filter_msg = msg;

    rte_memcpy(&RTE_PER_LCORE(trace_filter), &filter_msg->tfm_filter,
               sizeof(RTE_PER_LCORE(trace_filter)));
    return 0;
}

/*****************************************************************************
 * trace_filter_reset()
 ****************************************************************************/
static void trace_filter_reset(trace_filter_t *filter)
{
    bzero(filter, sizeof(*filter));

    filter->tf_tcb_state = TS_CLOSED;
    filter->tf_interface = FILTER_INTERFACE_ANY;
}

/*****************************************************************************
 * trace_filter_match()
 ****************************************************************************/
bool trace_filter_match(trace_filter_t *filter, uint32_t interface,
                        int domain,
                        uint16_t src_port,
                        uint16_t dst_port,
                        tpg_ip_t src_addr,
                        tpg_ip_t dst_addr)
{
    if (filter->tf_interface != FILTER_INTERFACE_ANY &&
        filter->tf_interface != interface)
        return false;

    if (filter->tf_domain && filter->tf_domain != domain)
        return false;

    if (filter->tf_src_port && filter->tf_src_port != src_port)
        return false;

    if (filter->tf_dst_port && filter->tf_dst_port != dst_port)
        return false;

    if (filter->tf_src_addr.ip_v4 && filter->tf_src_addr.ip_v4 != src_addr.ip_v4)
        return false;

    if (filter->tf_dst_addr.ip_v4 && filter->tf_dst_addr.ip_v4 != dst_addr.ip_v4)
        return false;

    return true;
}

/*****************************************************************************
 * trace_filter_init()
 ****************************************************************************/
bool trace_filter_init(void)
{
    int error;

    /*
     * Register the handlers for our message types.
     */
    error = msg_register_handler(MSG_TRACE_FILTER, filter_handler);
    if (error) {
        RTE_LOG(ERR, USER1, "Failed to register trace filter msg handler: %s(%d)\n",
                rte_strerror(-error), -error);
        return false;
    }

    /*
     * Add port module CLI commands
     */
    if (!cli_add_main_ctx(cli_ctx)) {
        RTE_LOG(ERR, USER1, "ERROR: Can't add trace filter specific CLI commands!\n");
        return false;
    }

    return true;
}

/*****************************************************************************
 * trace_filter_lcore_init()
 ****************************************************************************/
void trace_filter_lcore_init(uint32_t lcore_id __rte_unused)
{
    trace_filter_reset(&RTE_PER_LCORE(trace_filter));
}


/*****************************************************************************
 * CLI commands
 *****************************************************************************
 * - "set trace filter core-id <core> interface <intf> src <src> dst <dst>
 *                     sport <sport> dport <dport> tcb-state <state> enable"
 * - "set trace filter core-id <core> enable"
 ****************************************************************************/
struct cmd_trace_filter_result {
    cmdline_fixed_string_t  set;
    cmdline_fixed_string_t  trace;
    cmdline_fixed_string_t  filter;
    cmdline_fixed_string_t  core_id_kw;
    uint8_t                 core;
    cmdline_fixed_string_t  interface_kw;
    uint32_t                interface;
    cmdline_fixed_string_t  src_kw;
    cmdline_ipaddr_t        src;
    cmdline_fixed_string_t  dst_kw;
    cmdline_ipaddr_t        dst;
    cmdline_fixed_string_t  sport_kw;
    uint16_t                sport;
    cmdline_fixed_string_t  dport_kw;
    uint16_t                dport;
    cmdline_fixed_string_t  tcb_state_kw;
    cmdline_fixed_string_t  tcb_state;
    cmdline_fixed_string_t  enable;
    cmdline_fixed_string_t  disable;
};

static cmdline_parse_token_string_t cmd_trace_filter_T_set =
    TOKEN_STRING_INITIALIZER(struct cmd_trace_filter_result,
        set, "set");
static cmdline_parse_token_string_t cmd_trace_filter_T_trace =
    TOKEN_STRING_INITIALIZER(struct cmd_trace_filter_result,
        trace, "trace");
static cmdline_parse_token_string_t cmd_trace_filter_T_filter =
    TOKEN_STRING_INITIALIZER(struct cmd_trace_filter_result,
        filter, "filter");

static cmdline_parse_token_string_t cmd_trace_filter_T_core_id =
    TOKEN_STRING_INITIALIZER(struct cmd_trace_filter_result,
        core_id_kw, "core-id");
static cmdline_parse_token_num_t cmd_trace_filter_T_core_id_val =
    TOKEN_NUM_INITIALIZER(struct cmd_trace_filter_result,
        core, UINT8);

static cmdline_parse_token_string_t cmd_trace_filter_T_interface =
    TOKEN_STRING_INITIALIZER(struct cmd_trace_filter_result,
        interface_kw, "interface");
static cmdline_parse_token_num_t cmd_trace_filter_T_interface_val =
    TOKEN_NUM_INITIALIZER(struct cmd_trace_filter_result,
        interface, UINT32);

static cmdline_parse_token_string_t cmd_trace_filter_T_src =
    TOKEN_STRING_INITIALIZER(struct cmd_trace_filter_result,
        src_kw, "src");
static cmdline_parse_token_ipaddr_t cmd_trace_filter_T_src_val =
    TOKEN_IPADDR_INITIALIZER(struct cmd_trace_filter_result, src);

static cmdline_parse_token_string_t cmd_trace_filter_T_dst =
    TOKEN_STRING_INITIALIZER(struct cmd_trace_filter_result,
        dst_kw, "dst");
static cmdline_parse_token_ipaddr_t cmd_trace_filter_T_dst_val =
    TOKEN_IPADDR_INITIALIZER(struct cmd_trace_filter_result, dst);

static cmdline_parse_token_string_t cmd_trace_filter_T_sport =
    TOKEN_STRING_INITIALIZER(struct cmd_trace_filter_result,
        sport_kw, "sport");
static cmdline_parse_token_num_t cmd_trace_filter_T_sport_val =
    TOKEN_NUM_INITIALIZER(struct cmd_trace_filter_result,
        sport, UINT16);

static cmdline_parse_token_string_t cmd_trace_filter_T_dport =
    TOKEN_STRING_INITIALIZER(struct cmd_trace_filter_result,
        dport_kw, "dport");
static cmdline_parse_token_num_t cmd_trace_filter_T_dport_val =
    TOKEN_NUM_INITIALIZER(struct cmd_trace_filter_result,
        dport, UINT16);

static cmdline_parse_token_string_t cmd_trace_filter_T_tcb_state_kw =
    TOKEN_STRING_INITIALIZER(struct cmd_trace_filter_result,
        tcb_state_kw, "tcb-state");
static cmdline_parse_token_string_t cmd_trace_filter_T_tcb_state =
    TOKEN_STRING_INITIALIZER(struct cmd_trace_filter_result,
        tcb_state, "ANY#INIT#LISTEN#SYN_SENT#SYN_RECV#ESTAB#FIN_WAIT_1#FIN_WAIT_2#LAST_ACK#CLOSING#TIME_WAIT#CLOSE_WAIT");

static cmdline_parse_token_string_t cmd_trace_filter_T_enable =
    TOKEN_STRING_INITIALIZER(struct cmd_trace_filter_result,
        enable, "enable");
static cmdline_parse_token_string_t cmd_trace_filter_T_disable =
    TOKEN_STRING_INITIALIZER(struct cmd_trace_filter_result,
        disable, "disable");

static void cmd_trace_filter_parsed(void *parsed, struct cmdline *cl, void *data)
{
    MSG_LOCAL_DEFINE(trace_filter_msg_t, msg);
    msg_t                          *msgp;
    struct cmd_trace_filter_result *pr = parsed;
    trace_filter_t                 *filter;

    int enable = (intptr_t)data;
    int lcore_index = rte_lcore_index(pr->core);

    if (!CMD_CHECK_TRACE_SUPPORT(cl))
        return;

    if (pr->core == rte_lcore_id()) {
        cmdline_printf(cl, "WARNING: Ignoring console core!\n");
        return;
    }

    if (lcore_index == -1 || lcore_index < TPG_NR_OF_NON_PACKET_PROCESSING_CORES)
        cmdline_printf(cl, "WARNING: Ignoring non-packet core!\n");

    msgp = MSG_LOCAL(msg);
    msg_init(msgp, MSG_TRACE_FILTER, pr->core, 0);
    filter = &MSG_INNER(trace_filter_msg_t, msgp)->tfm_filter;
    trace_filter_reset(filter);

    if (!enable) {
        filter->tf_enabled = false;
    } else {
        filter->tf_enabled = true;
        filter->tf_interface = pr->interface;
        filter->tf_src_addr.ip_v4 =
            rte_be_to_cpu_32(pr->src.addr.ipv4.s_addr);
        filter->tf_dst_addr.ip_v4 =
            rte_be_to_cpu_32(pr->dst.addr.ipv4.s_addr);
        filter->tf_src_port = pr->sport;
        filter->tf_dst_port = pr->dport;

        if (strcmp(pr->tcb_state, "ANY") != 0)
            filter->tf_tcb_state = tsm_str_to_state(pr->tcb_state);
    }

    /* This will block and wait for the message to be processed! */
    msg_send(msgp, 0);
}

cmdline_parse_inst_t cmd_trace_filter_enable = {
    .f =  cmd_trace_filter_parsed,
    .data = (void *) (intptr_t)true,
    .help_str = "set trace filter core-id <core> interface <intf> src <src> dst <dst> sport <sport> dport <dport> tcb-state <state> enable",
    .tokens = {
        (void *)&cmd_trace_filter_T_set,
        (void *)&cmd_trace_filter_T_trace,
        (void *)&cmd_trace_filter_T_filter,
        (void *)&cmd_trace_filter_T_core_id,
        (void *)&cmd_trace_filter_T_core_id_val,
        (void *)&cmd_trace_filter_T_interface,
        (void *)&cmd_trace_filter_T_interface_val,
        (void *)&cmd_trace_filter_T_src,
        (void *)&cmd_trace_filter_T_src_val,
        (void *)&cmd_trace_filter_T_dst,
        (void *)&cmd_trace_filter_T_dst_val,
        (void *)&cmd_trace_filter_T_sport,
        (void *)&cmd_trace_filter_T_sport_val,
        (void *)&cmd_trace_filter_T_dport,
        (void *)&cmd_trace_filter_T_dport_val,
        (void *)&cmd_trace_filter_T_tcb_state_kw,
        (void *)&cmd_trace_filter_T_tcb_state,
        (void *)&cmd_trace_filter_T_enable,
        NULL,
    },
};

cmdline_parse_inst_t cmd_trace_filter_disable = {
    .f =  cmd_trace_filter_parsed,
    .data = (void *) (intptr_t)false,
    .help_str = "set trace filter core-id <core> disable",
    .tokens = {
        (void *)&cmd_trace_filter_T_set,
        (void *)&cmd_trace_filter_T_trace,
        (void *)&cmd_trace_filter_T_filter,
        (void *)&cmd_trace_filter_T_core_id,
        (void *)&cmd_trace_filter_T_core_id_val,
        (void *)&cmd_trace_filter_T_disable,
        NULL,
    },
};

/*****************************************************************************
 * Main menu context
 ****************************************************************************/
static cmdline_parse_ctx_t cli_ctx[] = {
    &cmd_trace_filter_enable,
    &cmd_trace_filter_disable,
    NULL,
};

