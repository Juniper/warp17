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
 *     tpg_trace_cli.c
 *
 * Description:
 *     CLI for the tracing module.
 *
 * Author:
 *     Dumitru Ceara, Eelco Chaudron
 *
 * Initial Created:
 *     06/05/2015
 *
 * Notes:
 *
 */

/*****************************************************************************
 * Include files
 ***************************************************************************/
#include "tcp_generator.h"

/*****************************************************************************
 * Forward declarations
 ****************************************************************************/
static cmdline_parse_ctx_t cli_ctx[];

/*****************************************************************************
 * CLI
 ****************************************************************************/
#define TRACE_CMDLINE_CORE (1 << 0)
#define TRACE_CMDLINE_FILE (1 << 1)

#define IS_TRACE_COMPONENT_ALL_STR(name) \
    (!strncmp((name), "all", strlen("all") + 1))

#define IS_TRACE_ENADIS_ENABLE(value) \
    (!strncmp((value), "enable", strlen("enable") + 1))

#define IS_TRACE_ENADIS_DISABLE(value) \
    (!strncmp((value), "disable", strlen("disable") + 1))

/*****************************************************************************
 * show trace list
 ****************************************************************************/
struct cmd_trace_list_result {
    cmdline_fixed_string_t show;
    cmdline_fixed_string_t trace;
    cmdline_fixed_string_t list;
};

static cmdline_parse_token_string_t cmd_trace_list_T_show =
    TOKEN_STRING_INITIALIZER(struct cmd_trace_list_result,
        show, "show");
static cmdline_parse_token_string_t cmd_trace_list_T_trace =
    TOKEN_STRING_INITIALIZER(struct cmd_trace_list_result,
        trace, "trace");
static cmdline_parse_token_string_t cmd_trace_list_T_list =
    TOKEN_STRING_INITIALIZER(struct cmd_trace_list_result,
        list, "list");

static void cmd_trace_list_parsed(void *parsed_result __rte_unused,
                                  struct cmdline *cl,
                                  void *data __rte_unused)
{
    void trace_list_comp_cb(const trace_comp_t *tc,
                            void *data __rte_unused)
    {
        cmdline_printf(cl, "\t%s (ID: %u)\n",
            cfg_get_gtrace_name(tc->tc_comp_id),
            tc->tc_comp_id);
    }

    if (!CMD_CHECK_TRACE_SUPPORT(cl))
        return;

    cmdline_printf(cl, "Registered Trace Components:\n");
    trace_comp_iterate(trace_list_comp_cb, NULL);
    cmdline_printf(cl, "\n");
}

cmdline_parse_inst_t cmd_trace_list = {
    .f = cmd_trace_list_parsed,
    .data = NULL,
    .help_str = "show trace list",
    .tokens = {
        (void *)&cmd_trace_list_T_show,
        (void *)&cmd_trace_list_T_trace,
        (void *)&cmd_trace_list_T_list,
        NULL,
    },
};

/*****************************************************************************
 * set trace level <level> component <component>|all
 * set trace level <level> component <component>|all core-id <core-id>
 ****************************************************************************/
struct cmd_trace_level_result {
    cmdline_fixed_string_t set;
    cmdline_fixed_string_t trace;
    cmdline_fixed_string_t level_kw;
    cmdline_fixed_string_t level;
    cmdline_fixed_string_t component_kw;
    cmdline_fixed_string_t component;
    cmdline_fixed_string_t core_id_kw;
    uint8_t                core;
};

static cmdline_parse_token_string_t cmd_trace_level_T_set =
    TOKEN_STRING_INITIALIZER(struct cmd_trace_level_result,
        set, "set");
static cmdline_parse_token_string_t cmd_trace_level_T_trace =
    TOKEN_STRING_INITIALIZER(struct cmd_trace_level_result,
        trace, "trace");
static cmdline_parse_token_string_t cmd_trace_level_T_level_kw =
    TOKEN_STRING_INITIALIZER(struct cmd_trace_level_result,
        level_kw, "level");
static cmdline_parse_token_string_t cmd_trace_level_T_level =
    TOKEN_STRING_INITIALIZER(struct cmd_trace_level_result,
        level,
        "CRIT#ERR#INFO#LOG#DEBUG");
static cmdline_parse_token_string_t cmd_trace_level_T_component_kw =
    TOKEN_STRING_INITIALIZER(struct cmd_trace_level_result,
        component_kw, "component");
static cmdline_parse_token_string_t cmd_trace_level_T_component =
    TOKEN_STRING_INITIALIZER(struct cmd_trace_level_result,
        component, NULL);
static cmdline_parse_token_string_t cmd_trace_level_T_core_id_kw =
    TOKEN_STRING_INITIALIZER(struct cmd_trace_level_result,
        core_id_kw, "core-id");
static cmdline_parse_token_num_t cmd_trace_level_T_core =
    TOKEN_NUM_INITIALIZER(struct cmd_trace_level_result,
        core, UINT8);

static void cmd_trace_level_parsed(void *parsed_result,
                                   struct cmdline *cl,
                                   void *data)
{
    struct cmd_trace_level_result *pr = parsed_result;
    int                            options = (intptr_t)data;
    bool                           coref = (options & TRACE_CMDLINE_CORE);

    void trace_set_level(const trace_comp_t *tc,
                         void *data __rte_unused)
    {
        if (coref) {
            if (pr->core == rte_lcore_id()) {
                cmdline_printf(cl, "WARNING: Ignoring console core\n");
                return;
            }

            trace_set_core_level(tc, pr->core, pr->level);
            cmdline_printf(cl, "[%s] Set trace level %s on lcore %u\n",
                cfg_get_gtrace_name(tc->tc_comp_id), pr->level, pr->core);
        } else {
            uint32_t i;

            RTE_LCORE_FOREACH_SLAVE(i) {

                trace_set_core_level(tc, i, pr->level);
                cmdline_printf(cl, "[%s] Set trace level %s on lcore %u\n",
                    cfg_get_gtrace_name(tc->tc_comp_id), pr->level, i);
            }
        }
    }

    if (!CMD_CHECK_TRACE_SUPPORT(cl))
        return;

    if (IS_TRACE_COMPONENT_ALL_STR(pr->component)) {
        trace_comp_iterate(trace_set_level, NULL);
    } else {
        trace_comp_t *tc = trace_get_comp_pointer_by_name(pr->component);

        if (!tc) {
            cmdline_printf(cl, "ERROR: Unknown component: %s\n", pr->component);
            return;
        }
        trace_set_level(tc, NULL);
    }
};

cmdline_parse_inst_t cmd_trace_level = {
    .f = cmd_trace_level_parsed,
    .data = NULL,
    .help_str = "set trace level <level> component <component>|all",
    .tokens = {
        (void *)&cmd_trace_level_T_set,
        (void *)&cmd_trace_level_T_trace,
        (void *)&cmd_trace_level_T_level_kw,
        (void *)&cmd_trace_level_T_level,
        (void *)&cmd_trace_level_T_component_kw,
        (void *)&cmd_trace_level_T_component,
        NULL,
    },
};

cmdline_parse_inst_t cmd_trace_level_core_id = {
    .f = cmd_trace_level_parsed,
    .data = (void *)(intptr_t)(TRACE_CMDLINE_CORE),
    .help_str = "set trace level <level> component <component>|all core_id <core_id>",
    .tokens = {
        (void *)&cmd_trace_level_T_set,
        (void *)&cmd_trace_level_T_trace,
        (void *)&cmd_trace_level_T_level_kw,
        (void *)&cmd_trace_level_T_level,
        (void *)&cmd_trace_level_T_component_kw,
        (void *)&cmd_trace_level_T_component,
        (void *)&cmd_trace_level_T_core_id_kw,
        (void *)&cmd_trace_level_T_core,
        NULL,
    },
};

/*****************************************************************************
 * set trace bufsize <bufsize> component <component>|all
 * set trace bufsize <bufsize> component <component>|all core_id <core_id>
 ****************************************************************************/
struct cmd_trace_bufsize_result {
    cmdline_fixed_string_t set;
    cmdline_fixed_string_t trace;
    cmdline_fixed_string_t bufsize_kw;
    uint16_t               bufsize;
    cmdline_fixed_string_t component_kw;
    cmdline_fixed_string_t component;
    cmdline_fixed_string_t core_id_kw;
    uint8_t                core;
};

static cmdline_parse_token_string_t cmd_trace_bufsize_T_set =
    TOKEN_STRING_INITIALIZER(struct cmd_trace_bufsize_result,
        set, "set");
static cmdline_parse_token_string_t cmd_trace_bufsize_T_trace =
    TOKEN_STRING_INITIALIZER(struct cmd_trace_bufsize_result,
        trace, "trace");
static cmdline_parse_token_string_t cmd_trace_bufsize_T_bufsize_kw =
    TOKEN_STRING_INITIALIZER(struct cmd_trace_bufsize_result,
        bufsize_kw, "bufsize");
static cmdline_parse_token_num_t cmd_trace_bufsize_T_bufsize =
    TOKEN_NUM_INITIALIZER(struct cmd_trace_bufsize_result,
        bufsize, UINT16);
static cmdline_parse_token_string_t cmd_trace_bufsize_T_component_kw =
    TOKEN_STRING_INITIALIZER(struct cmd_trace_bufsize_result,
        component_kw, "component");
static cmdline_parse_token_string_t cmd_trace_bufsize_T_component =
    TOKEN_STRING_INITIALIZER(struct cmd_trace_bufsize_result,
        component, NULL);
static cmdline_parse_token_string_t cmd_trace_bufsize_T_core_id_kw =
    TOKEN_STRING_INITIALIZER(struct cmd_trace_bufsize_result,
        core_id_kw, "core-id");
static cmdline_parse_token_num_t cmd_trace_bufsize_T_core =
    TOKEN_NUM_INITIALIZER(struct cmd_trace_bufsize_result,
        core, UINT8);

static void cmd_trace_bufsize_parsed(void *parsed_result,
                                     struct cmdline *cl,
                                     void *data)
{
    struct cmd_trace_bufsize_result *pr = parsed_result;
    int                              options = (intptr_t)data;
    bool                             coref = (options & TRACE_CMDLINE_CORE);

    void trace_set_bufsize(const trace_comp_t *tc,
                           void *data __rte_unused)
    {
        uint32_t i;

        if (coref) {
            if (pr->core == rte_lcore_id()) {
                cmdline_printf(cl, "WARNING: Ignoring console core\n");
                return;
            }

            trace_set_core_bufsize(tc, pr->core, pr->bufsize);
            cmdline_printf(cl, "[%s] Set trace bufsize %u on lcore %u\n",
                cfg_get_gtrace_name(tc->tc_comp_id), pr->bufsize, pr->core);
        } else {
            RTE_LCORE_FOREACH_SLAVE(i) {

                trace_set_core_bufsize(tc, i, pr->bufsize);
                cmdline_printf(cl, "[%s] Set trace bufsize %u on lcore %u\n",
                    cfg_get_gtrace_name(tc->tc_comp_id), pr->bufsize, i);
            }
        }
    }

    if (!CMD_CHECK_TRACE_SUPPORT(cl))
        return;

    if (IS_TRACE_COMPONENT_ALL_STR(pr->component)) {
        trace_comp_iterate(trace_set_bufsize, NULL);
    } else {
        trace_comp_t *tc = trace_get_comp_pointer_by_name(pr->component);

        if (!tc) {
            cmdline_printf(cl, "ERROR: Unknown component: %s\n", pr->component);
            return;
        }
        trace_set_bufsize(tc, NULL);
    }
};

cmdline_parse_inst_t cmd_trace_bufsize = {
    .f = cmd_trace_bufsize_parsed,
    .data = NULL,
    .help_str = "set trace bufsize <bufsize> component <component>|all",
    .tokens = {
        (void *)&cmd_trace_bufsize_T_set,
        (void *)&cmd_trace_bufsize_T_trace,
        (void *)&cmd_trace_bufsize_T_bufsize_kw,
        (void *)&cmd_trace_bufsize_T_bufsize,
        (void *)&cmd_trace_bufsize_T_component_kw,
        (void *)&cmd_trace_bufsize_T_component,
        NULL,
    },
};

cmdline_parse_inst_t cmd_trace_bufsize_core_id = {
    .f = cmd_trace_bufsize_parsed,
    .data = (void *)(intptr_t)(TRACE_CMDLINE_CORE),
    .help_str = "set trace bufsize <bufsize> component <component>|all core_id <core_id>",
    .tokens = {
        (void *)&cmd_trace_bufsize_T_set,
        (void *)&cmd_trace_bufsize_T_trace,
        (void *)&cmd_trace_bufsize_T_bufsize_kw,
        (void *)&cmd_trace_bufsize_T_bufsize,
        (void *)&cmd_trace_bufsize_T_component_kw,
        (void *)&cmd_trace_bufsize_T_component,
        (void *)&cmd_trace_bufsize_T_core_id_kw,
        (void *)&cmd_trace_bufsize_T_core,
        NULL,
    },
};

/*****************************************************************************
 * set trace enable|disable component <component>|all
 * set trace enable|disable component <component>|all core-id <core-id>
 ****************************************************************************/
struct cmd_trace_enadis_result {
    cmdline_fixed_string_t set;
    cmdline_fixed_string_t trace;
    cmdline_fixed_string_t enadis;
    cmdline_fixed_string_t component_kw;
    cmdline_fixed_string_t component;
    cmdline_fixed_string_t core_id_kw;
    uint8_t                core;
};

static cmdline_parse_token_string_t cmd_trace_enadis_T_set =
    TOKEN_STRING_INITIALIZER(struct cmd_trace_enadis_result,
        set, "set");
static cmdline_parse_token_string_t cmd_trace_enadis_T_trace =
    TOKEN_STRING_INITIALIZER(struct cmd_trace_enadis_result,
        trace, "trace");
static cmdline_parse_token_string_t cmd_trace_enadis_T_enadis =
    TOKEN_STRING_INITIALIZER(struct cmd_trace_enadis_result,
        enadis, "enable#disable");
static cmdline_parse_token_string_t cmd_trace_enadis_T_component_kw =
    TOKEN_STRING_INITIALIZER(struct cmd_trace_enadis_result,
        component_kw, "component");
static cmdline_parse_token_string_t cmd_trace_enadis_T_component =
    TOKEN_STRING_INITIALIZER(struct cmd_trace_enadis_result,
        component, NULL);
static cmdline_parse_token_string_t cmd_trace_enadis_T_core_id_kw =
    TOKEN_STRING_INITIALIZER(struct cmd_trace_bufsize_result,
        core_id_kw, "core-id");
static cmdline_parse_token_num_t cmd_trace_enadis_T_core =
    TOKEN_NUM_INITIALIZER(struct cmd_trace_enadis_result,
        core, UINT8);

static void cmd_trace_enadis_parsed(void *parsed_result,
                                    struct cmdline *cl,
                                    void *data)
{
    struct cmd_trace_enadis_result *pr = parsed_result;
    int                             options = (intptr_t)data;
    bool                            coref = (options & TRACE_CMDLINE_CORE);
    trace_comp_it_cb_t              enadis_handler = NULL;

    void trace_enable(const trace_comp_t *tc, void *data __rte_unused)
    {
        uint32_t i;
        int      error;

        if (coref) {
            if (pr->core == rte_lcore_id()) {
                cmdline_printf(cl, "WARNING: Ignoring console core\n");
                return;
            }

            error = trace_send_enable(tc->tc_comp_id, pr->core,
                                      trace_get_core_bufsize(tc, pr->core));
            if (error) {
                RTE_LOG(ERR, USER1,
                    "Failed to send trace enable msg: %s(%d)\n",
                    rte_strerror(-error), -error);
                return;
            }
            cmdline_printf(cl, "[%s] Trace enabled on lcore %u\n",
                cfg_get_gtrace_name(tc->tc_comp_id), pr->core);
        } else {
            RTE_LCORE_FOREACH_SLAVE(i) {

                error = trace_send_enable(tc->tc_comp_id, i,
                                          trace_get_core_bufsize(tc, i));
                if (error) {
                    RTE_LOG(ERR, USER1,
                        "Failed to send trace enable msg: %s(%d)\n",
                        rte_strerror(-error), -error);
                    return;
                }
                cmdline_printf(cl, "[%s] Trace enabled on lcore %u\n",
                    cfg_get_gtrace_name(tc->tc_comp_id), i);
            }
        }
    }

    /* TODO: in case there are traces in the trace buffer we need to dump them
     * too.. For now the user should pay attention and always dump traces before
     * disabling them.
     */
    void trace_disable(const trace_comp_t *tc, void *data __rte_unused)
    {
        uint32_t i;
        int      error;

        if (coref) {
            if (pr->core == rte_lcore_id()) {
                cmdline_printf(cl, "WARNING: Ignoring console core\n");
                return;
            }

            error = trace_send_disable(tc->tc_comp_id, pr->core);
            if (error) {
                RTE_LOG(ERR, USER1,
                    "Failed to send trace disable msg: %s(%d)\n",
                    rte_strerror(-error), -error);
                return;
            }
            cmdline_printf(cl, "[%s] Trace disabled on lcore %u\n",
                cfg_get_gtrace_name(tc->tc_comp_id), pr->core);
        } else {
            RTE_LCORE_FOREACH_SLAVE(i) {

                error = trace_send_disable(tc->tc_comp_id, i);
                if (error) {
                    RTE_LOG(ERR, USER1,
                        "Failed to send trace disable msg: %s(%d)\n",
                        rte_strerror(-error), -error);
                    return;
                }
                cmdline_printf(cl, "[%s] Trace disabled on lcore %u\n",
                    cfg_get_gtrace_name(tc->tc_comp_id), i);
            }
        }
    }

    if (!CMD_CHECK_TRACE_SUPPORT(cl))
        return;

    if (IS_TRACE_ENADIS_ENABLE(pr->enadis)) {
        enadis_handler = trace_enable;
    } else if (IS_TRACE_ENADIS_DISABLE(pr->enadis)) {
        enadis_handler = trace_disable;
    } else {
        cmdline_printf(cl, "ERROR: Unknown value: %s\n", pr->enadis);
        return;
    }

    if (IS_TRACE_COMPONENT_ALL_STR(pr->component)) {
        trace_comp_iterate(enadis_handler, NULL);
    } else {
        trace_comp_t *tc = trace_get_comp_pointer_by_name(pr->component);

        if (!tc) {
            cmdline_printf(cl, "ERROR: Unknown component: %s\n", pr->component);
            return;
        }
        enadis_handler(tc, NULL);
    }
};

cmdline_parse_inst_t cmd_trace_enadis = {
    .f = cmd_trace_enadis_parsed,
    .data = NULL,
    .help_str = "set trace enable|disable component <component>|all",
    .tokens = {
        (void *)&cmd_trace_enadis_T_set,
        (void *)&cmd_trace_enadis_T_trace,
        (void *)&cmd_trace_enadis_T_enadis,
        (void *)&cmd_trace_enadis_T_component_kw,
        (void *)&cmd_trace_enadis_T_component,
        NULL,
    },
};

cmdline_parse_inst_t cmd_trace_enadis_core_id = {
    .f = cmd_trace_enadis_parsed,
    .data = (void *)(intptr_t)(TRACE_CMDLINE_CORE),
    .help_str = "set trace enable|disable component <component>|all core_id <core_id>",
    .tokens = {
        (void *)&cmd_trace_enadis_T_set,
        (void *)&cmd_trace_enadis_T_trace,
        (void *)&cmd_trace_enadis_T_enadis,
        (void *)&cmd_trace_enadis_T_component_kw,
        (void *)&cmd_trace_enadis_T_component,
        (void *)&cmd_trace_enadis_T_core_id_kw,
        (void *)&cmd_trace_enadis_T_core,
        NULL,
    },
};

/*****************************************************************************
 * show trace component <component>|all
 * show trace component <component>|all core-id <core-id>
 ****************************************************************************/
struct cmd_trace_show_result {
    cmdline_fixed_string_t show;
    cmdline_fixed_string_t trace;
    cmdline_fixed_string_t component_kw;
    cmdline_fixed_string_t component;
    cmdline_fixed_string_t core_id_kw;
    uint8_t                core;
};

static cmdline_parse_token_string_t cmd_trace_show_result_T_show =
    TOKEN_STRING_INITIALIZER(struct cmd_trace_show_result,
        show, "show");
static cmdline_parse_token_string_t cmd_trace_show_result_T_trace =
    TOKEN_STRING_INITIALIZER(struct cmd_trace_show_result,
        trace, "trace");
static cmdline_parse_token_string_t cmd_trace_show_result_T_component_kw =
    TOKEN_STRING_INITIALIZER(struct cmd_trace_show_result,
        component_kw, "component");
static cmdline_parse_token_string_t cmd_trace_show_result_T_component =
    TOKEN_STRING_INITIALIZER(struct cmd_trace_show_result,
        component, NULL);
static cmdline_parse_token_string_t cmd_trace_show_result_T_core_id_kw =
    TOKEN_STRING_INITIALIZER(struct cmd_trace_show_result,
        core_id_kw, "core-id");
static cmdline_parse_token_num_t cmd_trace_show_result_T_core =
    TOKEN_NUM_INITIALIZER(struct cmd_trace_show_result,
        core, UINT8);

static void cmd_trace_show_parsed(void *parsed_result, struct cmdline *cl,
                                  void *data)
{
    struct cmd_trace_show_result *pr = parsed_result;
    int                           options = (intptr_t)data;
    bool                          coref = (options & TRACE_CMDLINE_CORE);

    void trace_show(const trace_comp_t *tc, void *data __rte_unused)
    {
        uint32_t i;

        if (coref) {
            if (pr->core == rte_lcore_id()) {
                cmdline_printf(cl, "WARNING: Ignoring console core\n");
                return;
            }

            cmdline_printf(cl, "[%s][Core %u]: (Level: %s, Bufsize: %u, %s)\n",
                cfg_get_gtrace_name(tc->tc_comp_id),
                pr->core,
                trace_get_core_level(tc, pr->core),
                trace_get_core_bufsize(tc, pr->core),
                trace_get_core_enabled(tc, pr->core) ? "Enabled" : "Disabled");
        } else {
            cmdline_printf(cl, "[%s]:\n", cfg_get_gtrace_name(tc->tc_comp_id));
            RTE_LCORE_FOREACH_SLAVE(i) {

                cmdline_printf(cl, "\t[Core %u]: (Level: %s, Bufsize: %u, %s)\n",
                    i,
                    trace_get_core_level(tc, i),
                    trace_get_core_bufsize(tc, i),
                    trace_get_core_enabled(tc, i) ? "Enabled" : "Disabled");
            }
        }
    }

    if (!CMD_CHECK_TRACE_SUPPORT(cl))
        return;

    if (IS_TRACE_COMPONENT_ALL_STR(pr->component)) {
        trace_comp_iterate(trace_show, NULL);
    } else {
        trace_comp_t *tc = trace_get_comp_pointer_by_name(pr->component);

        if (!tc) {
            cmdline_printf(cl, "ERROR: Unknown component: %s\n", pr->component);
            return;
        }
        trace_show(tc, NULL);
    }

}

cmdline_parse_inst_t cmd_trace_show_component = {
    .f = cmd_trace_show_parsed,
    .data = NULL,
    .help_str = "show trace component <component>|all",
    .tokens = {
        (void *)&cmd_trace_show_result_T_show,
        (void *)&cmd_trace_show_result_T_trace,
        (void *)&cmd_trace_show_result_T_component_kw,
        (void *)&cmd_trace_show_result_T_component,
        NULL,
    },
};

cmdline_parse_inst_t cmd_trace_show_component_core = {
    .f = cmd_trace_show_parsed,
    .data = (void *)(intptr_t)(TRACE_CMDLINE_CORE),
    .help_str = "show trace component <component>|all core-id <core-id>",
    .tokens = {
        (void *)&cmd_trace_show_result_T_show,
        (void *)&cmd_trace_show_result_T_trace,
        (void *)&cmd_trace_show_result_T_component_kw,
        (void *)&cmd_trace_show_result_T_component,
        (void *)&cmd_trace_show_result_T_core_id_kw,
        (void *)&cmd_trace_show_result_T_core,
        NULL,
    },
};

/*****************************************************************************
 * show trace buffer component <component>|all
 * show trace buffer component <component>|all file <file>
 * show trace buffer component <component>|all core-id <core-id>
 * show trace buffer component <component>|all core-id <core-id> file <file>
 ****************************************************************************/
#define MAX_LOGFILE_NAMELEN 1024

struct cmd_trace_buffer_result {
    cmdline_fixed_string_t show;
    cmdline_fixed_string_t trace;
    cmdline_fixed_string_t buffer;
    cmdline_fixed_string_t component_kw;
    cmdline_fixed_string_t component;
    cmdline_fixed_string_t core_id_kw;
    uint8_t                core;
    cmdline_fixed_string_t file_kw;
    cmdline_fixed_string_t file;
};

static cmdline_parse_token_string_t cmd_trace_buffer_result_T_show =
    TOKEN_STRING_INITIALIZER(struct cmd_trace_buffer_result,
        show, "show");
static cmdline_parse_token_string_t cmd_trace_buffer_result_T_trace =
    TOKEN_STRING_INITIALIZER(struct cmd_trace_buffer_result,
        trace, "trace");
static cmdline_parse_token_string_t cmd_trace_buffer_result_T_buffer =
    TOKEN_STRING_INITIALIZER(struct cmd_trace_buffer_result,
        buffer, "buffer");
static cmdline_parse_token_string_t cmd_trace_buffer_result_T_component_kw =
    TOKEN_STRING_INITIALIZER(struct cmd_trace_buffer_result,
        component_kw, "component");
static cmdline_parse_token_string_t cmd_trace_buffer_result_T_component =
    TOKEN_STRING_INITIALIZER(struct cmd_trace_buffer_result,
        component, NULL);
static cmdline_parse_token_string_t cmd_trace_buffer_result_T_core_kw =
    TOKEN_STRING_INITIALIZER(struct cmd_trace_buffer_result,
        core_id_kw, "core-id");
static cmdline_parse_token_num_t cmd_trace_buffer_result_T_core =
    TOKEN_NUM_INITIALIZER(struct cmd_trace_buffer_result,
        core, UINT8);
static cmdline_parse_token_string_t cmd_trace_buffer_result_T_file_kw =
    TOKEN_STRING_INITIALIZER(struct cmd_trace_buffer_result,
        file_kw, "file");
static cmdline_parse_token_string_t cmd_trace_buffer_result_T_file =
    TOKEN_STRING_INITIALIZER(struct cmd_trace_buffer_result,
        file, NULL);

static void trace_printer(void *arg, trace_level_t lvl,
                          const char *comp_name,
                          const char *data,
                          uint32_t len __rte_unused)
{
    struct cmdline *cl = arg;

    cmdline_printf(cl, "[%3s] [%5s]: %s\n", trace_level_str(lvl), comp_name,
        data);
}

static void trace_file_printer(void *arg, trace_level_t lvl,
                               const char *comp_name,
                               const char *data,
                               uint32_t len __rte_unused)
{
    FILE *fp = arg;

    fprintf(fp, "[%3s] [%5s]: %s\n", trace_level_str(lvl), comp_name, data);
}

static void cmd_trace_dump_parsed(void *parsed_result,
                                  struct cmdline *cl,
                                  void *data)
{
    struct cmd_trace_buffer_result *pr = parsed_result;
    int                             options = (intptr_t)data;
    bool                            coref = (options & TRACE_CMDLINE_CORE);
    bool                            filef = (options & TRACE_CMDLINE_FILE);
    uint32_t                        lcore;

    struct comp_buffer_s {

        TAILQ_ENTRY(comp_buffer_s)  entries;
        char                       *buffer;
        uint32_t                    bufsize;
        uint32_t                    start_id;
        uint32_t                    start_pos;
        uint32_t                    end_pos;
        const trace_comp_t         *comp;

    } comp_buffers[TRACE_MAX];

    TAILQ_HEAD(listhead, comp_buffer_s) head = TAILQ_HEAD_INITIALIZER(head);
    uint32_t comp_cnt;

    void trace_dump_init_list(const trace_comp_t *tc, void *data)
    {
        struct comp_buffer_s *elem;
        int                   lcore = *(int *)data;
        int                   error;

        error = trace_send_xchg_ptr(tc->tc_comp_id, lcore,
                                    &comp_buffers[comp_cnt].buffer,
                                    &comp_buffers[comp_cnt].bufsize,
                                    &comp_buffers[comp_cnt].start_id,
                                    &comp_buffers[comp_cnt].start_pos,
                                    &comp_buffers[comp_cnt].end_pos,
                                    trace_get_core_bufsize(tc, lcore));
        if (error) {
            RTE_LOG(ERR, USER1, "Failed to send trace dump msg: %s(%d)\n",
                rte_strerror(-error), -error);
            return;
        }

        if (!comp_buffers[comp_cnt].buffer)
            return;

        comp_buffers[comp_cnt].comp = tc;

        /* If no entry available just bail! */
        if (comp_buffers[comp_cnt].start_id == 0)
            return;

        /* Walk the list and insert in the correct position based on the
         * first id of the trace buffer.
         */
        TAILQ_FOREACH(elem, &head, entries) {
            if (comp_buffers[comp_cnt].start_id < elem->start_id)
                break;
        }

        if (!elem)
            TAILQ_INSERT_TAIL(&head, &comp_buffers[comp_cnt], entries);
        else
            TAILQ_INSERT_BEFORE(elem, &comp_buffers[comp_cnt], entries);

        comp_cnt++;
    }

    void trace_dump_core(int lcore)
    {
        FILE               *filep = NULL;
        uint32_t            trace_cnt = 0;
        trace_printer_cb_t  printer;
        void               *printer_arg;

        TAILQ_INIT(&head);
        memset(comp_buffers, 0, sizeof(comp_buffers));
        comp_cnt = 0;

        if (IS_TRACE_COMPONENT_ALL_STR(pr->component)) {
            trace_comp_iterate(trace_dump_init_list, &lcore);
        } else {
            trace_comp_t *tc = trace_get_comp_pointer_by_name(pr->component);

            if (!tc) {
                cmdline_printf(cl, "ERROR: Unknown component: %s\n", pr->component);
                return;
            }
            trace_dump_init_list(tc, &lcore);
        }

        if (filef) {
            char logfile_per_core_name[MAX_LOGFILE_NAMELEN];

            snprintf(logfile_per_core_name, MAX_LOGFILE_NAMELEN,
                "%s-%u", pr->file, lcore);
            filep = fopen(logfile_per_core_name, "w+");
            if (filep == NULL) {
                cmdline_printf(cl, "ERROR: Failed to open file %s. Error: %s\n",
                    logfile_per_core_name, strerror(errno));
                return;
            }
            cmdline_printf(cl, "Traces from Core %d dumped to %s.\n", lcore,
                logfile_per_core_name);
            printer = trace_file_printer;
            printer_arg = filep;
        } else {
            cmdline_printf(cl, "Traces on Core %d:\n", lcore);
            printer = trace_printer;
            printer_arg = cl;
        }

        /* Walk the list of buffers.. */
        while (!TAILQ_EMPTY(&head)) {
            struct comp_buffer_s *comp_buf = TAILQ_FIRST(&head);
            struct comp_buffer_s *next_comp_buf = TAILQ_NEXT(comp_buf, entries);

            do {
                trace_entry_dump(printer, printer_arg, comp_buf->comp->tc_fmt,
                    cfg_get_gtrace_name(comp_buf->comp->tc_comp_id),
                    comp_buf->buffer, comp_buf->bufsize, &comp_buf->start_id,
                    &comp_buf->start_pos, comp_buf->end_pos);
                trace_cnt++;
            } while (comp_buf->start_pos != comp_buf->end_pos &&
                (next_comp_buf == NULL ||
                    comp_buf->start_id < next_comp_buf->start_id));

            TAILQ_REMOVE(&head, comp_buf, entries);

            /* If we still have traces in the buffer we need to reenqueue
             * it in the right position based on the start_id.
             */
            if (comp_buf->start_pos != comp_buf->end_pos) {
                struct comp_buffer_s *elem;

                TAILQ_FOREACH(elem, &head, entries) {
                    if (comp_buf->start_id < elem->start_id)
                        break;
                }
                if (elem)
                    TAILQ_INSERT_BEFORE(elem, comp_buf, entries);
                else
                    TAILQ_INSERT_TAIL(&head, comp_buf, entries);
            } else
                rte_free(comp_buf->buffer);
        }

        if (filef) {
            fclose(filep);
            cmdline_printf(cl, "Dumped %u entries.\n", trace_cnt);
        } else {
            cmdline_printf(cl, "\n");
        }
    }

    if (coref) {
        int lcore_idx;

        if (pr->core == rte_lcore_id()) {
            cmdline_printf(cl, "WARNING: Ignoring console core\n");
            return;
        }

        lcore_idx = rte_lcore_index(pr->core);
        if (lcore_idx == -1 || lcore_idx >= (int)rte_lcore_count()) {
            cmdline_printf(cl, "ERROR: Invalid core-id: %u\n", pr->core);
            return;
        }

        trace_dump_core(pr->core);
    } else {
        RTE_LCORE_FOREACH_SLAVE(lcore) {
            if (cfg_is_cli_core(lcore))
                continue;

            trace_dump_core(lcore);
        }
    }
}

cmdline_parse_inst_t cmd_trace_dump_component_allcore = {
    .f = cmd_trace_dump_parsed,
    .data = NULL,
    .help_str = "show trace buffer component <component>|all",
    .tokens = {
        (void *)&cmd_trace_buffer_result_T_show,
        (void *)&cmd_trace_buffer_result_T_trace,
        (void *)&cmd_trace_buffer_result_T_buffer,
        (void *)&cmd_trace_buffer_result_T_component_kw,
        (void *)&cmd_trace_buffer_result_T_component,
        NULL,
    },
};

cmdline_parse_inst_t cmd_trace_dump_component_allcore_file = {
    .f = cmd_trace_dump_parsed,
    .data = (void *)(intptr_t)(TRACE_CMDLINE_FILE),
    .help_str = "show trace buffer component <component>|all file <file>",
    .tokens = {
        (void *)&cmd_trace_buffer_result_T_show,
        (void *)&cmd_trace_buffer_result_T_trace,
        (void *)&cmd_trace_buffer_result_T_buffer,
        (void *)&cmd_trace_buffer_result_T_component_kw,
        (void *)&cmd_trace_buffer_result_T_component,
        (void *)&cmd_trace_buffer_result_T_file_kw,
        (void *)&cmd_trace_buffer_result_T_file,
        NULL,
    },
};

cmdline_parse_inst_t cmd_trace_dump_component_core = {
    .f = cmd_trace_dump_parsed,
    .data = (void *)(intptr_t)(TRACE_CMDLINE_CORE),
    .help_str = "show trace buffer component <component>|all core-id <core-id>",
    .tokens = {
        (void *)&cmd_trace_buffer_result_T_show,
        (void *)&cmd_trace_buffer_result_T_trace,
        (void *)&cmd_trace_buffer_result_T_buffer,
        (void *)&cmd_trace_buffer_result_T_component_kw,
        (void *)&cmd_trace_buffer_result_T_component,
        (void *)&cmd_trace_buffer_result_T_core_kw,
        (void *)&cmd_trace_buffer_result_T_core,
        NULL,
    },
};

cmdline_parse_inst_t cmd_trace_dump_component_core_file = {
    .f = cmd_trace_dump_parsed,
    .data = (void *)(intptr_t)(TRACE_CMDLINE_CORE | TRACE_CMDLINE_FILE),
    .help_str = "show trace buffer component <component>|all core-id <core-id> file <file>",
    .tokens = {
        (void *)&cmd_trace_buffer_result_T_show,
        (void *)&cmd_trace_buffer_result_T_trace,
        (void *)&cmd_trace_buffer_result_T_buffer,
        (void *)&cmd_trace_buffer_result_T_component_kw,
        (void *)&cmd_trace_buffer_result_T_component,
        (void *)&cmd_trace_buffer_result_T_core_kw,
        (void *)&cmd_trace_buffer_result_T_core,
        (void *)&cmd_trace_buffer_result_T_file_kw,
        (void *)&cmd_trace_buffer_result_T_file,
        NULL,
    },
};

static cmdline_parse_ctx_t cli_ctx[] = {
    &cmd_trace_list,

    &cmd_trace_level,
    &cmd_trace_level_core_id,

    &cmd_trace_bufsize,
    &cmd_trace_bufsize_core_id,
    &cmd_trace_enadis,
    &cmd_trace_enadis_core_id,

    &cmd_trace_show_component,
    &cmd_trace_show_component_core,

    &cmd_trace_dump_component_allcore,
    &cmd_trace_dump_component_allcore_file,
    &cmd_trace_dump_component_core,
    &cmd_trace_dump_component_core_file,

    NULL,
};

/*****************************************************************************
 * trace_cli_init()
 ****************************************************************************/
bool trace_cli_init(void)
{
    return cli_add_main_ctx(cli_ctx);
}

