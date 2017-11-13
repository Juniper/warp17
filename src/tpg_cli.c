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
 *     tpg_cli.c
 *
 * Description:
 *     Command line interface for the WARP17 tool.
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
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>

#include "tcp_generator.h"

/*****************************************************************************
 * Global variables
 ****************************************************************************/
static struct cmdline        *cli_cmdline_instance;
static rte_atomic32_t         cli_cmdline_initialized;
static tpg_cli_override_cb_t  cli_override;

/*****************************************************************************
 * Wrapper on top of the DPDK string parser which also strips begin/end
 * quotes.
 ****************************************************************************/
struct cmdline_token_ops cli_token_quoted_string_ops = {
	.parse = cli_parse_quoted_string,
	.complete_get_nb = cmdline_complete_get_nb_string,
	.complete_get_elt = cmdline_complete_get_elt_string,
	.get_help = cmdline_get_help_string,
};

/*****************************************************************************
 * cli_printer()
 ****************************************************************************/
void cli_printer(void *printer_arg, const char *fmt, va_list ap)
{
    struct cmdline *cl = printer_arg;
    va_list         temp_ap;
    int             datalen;

    va_copy(temp_ap, ap);

    /* Trick the library to compute the length (excluding the null byte
     * terminator, therefore the +1) for us..
     */
    datalen = vsnprintf(NULL, 0, fmt, temp_ap) + 1;

    va_end(temp_ap);
    if (datalen >= 0) {
        char data[datalen];

        vsnprintf(data, datalen, fmt, ap);
        cmdline_printf(cl, "%s", data);
    }
}

/*****************************************************************************
 * Main menu global commands
 *****************************************************************************
 * - "help"
 ****************************************************************************/

struct cmd_help_result {
    cmdline_fixed_string_t help;
};

static void cmd_help_parsed(void *parsed_result __rte_unused,
                            struct cmdline *cl,
                            void *data __rte_unused)
{
    cmdline_printf(cl,
                   "TODO: warp17 help, this should show all possible\n"
                   "      commands, and allow additional parameters to\n"
                   "      show help related to the specific commands\n");
}

static cmdline_parse_token_string_t cmd_help_help =
    TOKEN_STRING_INITIALIZER(struct cmd_help_result, help, "help");

cmdline_parse_inst_t cmd_help = {
    .f = cmd_help_parsed,
    .data = NULL,
    .help_str = "show help",
    .tokens = {
        (void *)&cmd_help_help,
        NULL,
    },
};

/*****************************************************************************
 * - "show version"
 ****************************************************************************/

struct cmd_show_version_result {
    cmdline_fixed_string_t show;
    cmdline_fixed_string_t version;
};

static void cmd_show_version_parsed(void *parsed_result __rte_unused,
                                    struct cmdline *cl,
                                    void *data __rte_unused)
{
    cmdline_printf(cl, TPG_VERSION_PRINTF_STR "\n",
                   TPG_VERSION_PRINTF_ARGS);
}

static cmdline_parse_token_string_t cmd_show_version_show =
    TOKEN_STRING_INITIALIZER(struct cmd_show_version_result, show, "show");
static cmdline_parse_token_string_t cmd_show_version_version =
    TOKEN_STRING_INITIALIZER(struct cmd_show_version_result, version, "version");

cmdline_parse_inst_t cmd_show_version = {
    .f = cmd_show_version_parsed,
    .data = NULL,
    .help_str = "show version",
    .tokens = {
        (void *)&cmd_show_version_show,
        (void *)&cmd_show_version_version,
        NULL,
    },
};

/*****************************************************************************
 * - "file input <filename>"
 ****************************************************************************/

struct cmd_file_input_result {
    cmdline_fixed_string_t file;
    cmdline_fixed_string_t input;
    cmdline_fixed_string_t filename;
};

static void cmd_file_input_parsed(void *parsed_result, struct cmdline *cl,
                                  void *data __rte_unused)
{
    struct cmd_file_input_result *pr = parsed_result;
    const char *fname = pr->filename;

    if (cli_run_input_file(fname) == false)
        cmdline_printf(cl, "ERROR: Failed to execute file %s!\n", fname);
}

static cmdline_parse_token_string_t cmd_file_input_file =
    TOKEN_STRING_INITIALIZER(struct cmd_file_input_result, file, "file");
static cmdline_parse_token_string_t cmd_file_input_input =
    TOKEN_STRING_INITIALIZER(struct cmd_file_input_result, input, "input");
static cmdline_parse_token_string_t cmd_file_input_filename =
    TOKEN_STRING_INITIALIZER(struct cmd_file_input_result, filename, NULL);


cmdline_parse_inst_t cmd_file_input = {
    .f = cmd_file_input_parsed,
    .data = NULL,
    .help_str = "file input <filename>",
    .tokens = {
        (void *)&cmd_file_input_file,
        (void *)&cmd_file_input_input,
        (void *)&cmd_file_input_filename,
        NULL,
    },
};

/*****************************************************************************
 * Main menu context
 ****************************************************************************/

static cmdline_parse_ctx_t main_ctx[] = {
    (void *)&cmd_help,
    (void *)&cmd_show_version,
    (void *)&cmd_file_input,
    NULL,
};

/*****************************************************************************
 * cli_init()
 ****************************************************************************/
bool cli_init(void)
{
    if (!rte_atomic32_cmpset((volatile uint32_t *)&cli_cmdline_initialized.cnt,
                             false,
                             true)) {
        /* Someone already initialized the CLI! */
        return false;
    }

    cli_cmdline_instance = cmdline_stdin_new(main_ctx, "warp17> ");
    if (cli_cmdline_instance == NULL) {
        RTE_LOG(ERR, USER1, "ERROR: Failed initializing command line instance!\n");
        return false;
    }

    /*
     * To allow main CLI additions we hack around in the command line data
     * structures, rather than expending the existing APIs.
     */
    cli_cmdline_instance->ctx = malloc(sizeof(main_ctx));
    if (cli_cmdline_instance->ctx == NULL) {
        RTE_LOG(ERR, USER1, "ERROR: Failed initializing command line context!\n");
        return false;
    }
    memcpy(cli_cmdline_instance->ctx, main_ctx, sizeof(main_ctx));

    return true;
}

/*****************************************************************************
 * cli_exit()
 ****************************************************************************/
void cli_exit(void)
{
    if (!rte_atomic32_cmpset((volatile uint32_t *)&cli_cmdline_initialized.cnt,
                             true,
                             false)) {
        /* Someone already exited the CLI! */
        return;
    }

    if (cli_cmdline_instance == NULL)
        return;

    cmdline_stdin_exit(cli_cmdline_instance);
}

/*****************************************************************************
 * cli_add_main_ctx()
 ****************************************************************************/
bool cli_add_main_ctx(cmdline_parse_ctx_t *ctx)
{
    int   main_ctx_size;
    int   ctx_size;
    void *new_ctx_mem;

    if (!cli_cmdline_instance || !ctx || !cli_cmdline_instance->ctx)
        return false;

    for (main_ctx_size = 0;
         cli_cmdline_instance->ctx[main_ctx_size];
         main_ctx_size++) {
        /*
         * Do nothing just count the entries in the table
         */
    }

    for (ctx_size = 0; ctx[ctx_size] != NULL; ctx_size++) {
        /*
         * Do nothing just count the entries in the table
         */
    }

    new_ctx_mem = realloc(cli_cmdline_instance->ctx, (main_ctx_size + ctx_size + 1) * sizeof(void *));
    if (new_ctx_mem == NULL) {
        RTE_LOG(ERR, USER1, "Failed allocating additional space for main menu context!\n");
        return false;
    }

    /*
     * Copy new additional CLI commands at end of main context, including NULL termination.
     */
    memcpy((char *)new_ctx_mem + (main_ctx_size * sizeof(void *)), ctx,
           (ctx_size + 1) * sizeof(void *));

    cli_cmdline_instance->ctx = new_ctx_mem;

    return true;
}

/*****************************************************************************
 * cli_set_override()
 ****************************************************************************/
bool cli_set_override(tpg_cli_override_cb_t override)
{
    if (cli_override)
        return false;

    cli_override = override;
    return true;
}

/*****************************************************************************
 * cli_unset_override()
 ****************************************************************************/
bool cli_unset_override(void)
{
    if (!cli_override)
        return false;

    cli_override = NULL;
    return true;
}

/*****************************************************************************
 * cli_redisplay_prompt()
 ****************************************************************************/
void cli_redisplay_prompt(void)
{
    cmdline_in(cli_cmdline_instance, "\n", 1);
}

/*****************************************************************************
 * cli_run_input_file()
 ****************************************************************************/
bool cli_run_input_file(const char *filename)
{
    struct cmdline *file_cl;
    int             fd;

    if (!filename)
        return true;

    fd = open(filename, O_RDONLY, 0);
    if (fd < 0)
        return false;

    file_cl = cmdline_new(cli_cmdline_instance->ctx, "", fd, STDOUT_FILENO);
    cmdline_interact(file_cl);
    close(fd);
    cli_redisplay_prompt();
    return true;
}

/*****************************************************************************
 * cli_interact()
 ****************************************************************************/
void cli_interact(void)
{
    struct pollfd fds;
    char          c;
    int           ret;

    fds.fd      = cli_cmdline_instance->s_in;
    fds.events  = POLLIN;
    fds.revents = 0;

    /* Use our own version of cmdline_interact.. Sometimes we don't want
     * to pass all characters we read from stdin to cmdline. We might have
     * modules interested in processing user input too.
     */
    for (;;) {
        rte_timer_manage();

        /* Check for RPC events too. */
        rpc_dispatch();

        ret = poll(&fds, 1, 1);

        if (ret == 0)
            continue;

        if (ret < 0 && errno != EINTR)
            TPG_ERROR_ABORT("ERROR: %s!\n", "poll syscall failed");

        if ((fds.revents & (POLLERR | POLLNVAL | POLLHUP)))
            break;

        if (!(fds.revents & POLLIN))
            continue;

        if (read(cli_cmdline_instance->s_in, &c, 1) < 0)
            break;

        if (!cli_override) {
            if (cmdline_in(cli_cmdline_instance, &c, 1) < 0)
                break;
        } else {
            if (cli_override(&c, 1) < 0)
                break;
        }
    }
}

/*****************************************************************************
 * cli_handle_cmdline_opt()
 * --cmd-file - file containing startup commands
 ****************************************************************************/
cmdline_arg_parser_res_t cli_handle_cmdline_opt(const char *opt_name,
                                                char *opt_arg)
{
    global_config_t *cfg = cfg_get_config();

    if (!cfg)
        TPG_ERROR_ABORT("ERROR: Unable to get config!\n");

    if (strncmp(opt_name, "cmd-file", strlen("cmd-file") + 1) == 0) {
        cfg->gcfg_cmd_file = strdup(opt_arg);
        return CAPR_CONSUMED;
    }

    return CAPR_IGNORED;
}

/*****************************************************************************
 * cli_parse_quoted_string()
 *  NOTES:
 *      Strips begin and end quotes if present. Just a wrapper on top of
 *      the DPDK string parser.
 ****************************************************************************/
int cli_parse_quoted_string(cmdline_parse_token_hdr_t *tk, const char *buf,
                            void *res, unsigned ressize)
{
    int   token_len = cmdline_parse_string(tk, buf, res, ressize);
    char *result = res;

    if (token_len < 1)
        return token_len;

    if (result) {
        /* Don't allow one lonely single or double quote. */
        if (token_len == 1 && (result[0] == '"' || result[0] == '\''))
            return -1;

        /* Strip quotes if in pairs. */
        if ((result[0] == '"' && result[token_len - 1] == '"') ||
                (result[0] == '\'' && result[token_len - 1] == '\'')) {
            memmove(&result[0], &result[1], token_len - 2);
            result[token_len - 2] = 0;
            return token_len;
        }

        /* Don't allow qoutes that don't close. */
        if (result[0] == '"' || result[0] == '\'')
            return -1;
        if (result[token_len - 1] == '"' || result[token_len - 1] == '\'')
            return -1;
    }

    return token_len;
}

