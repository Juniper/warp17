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
 *     tpg_test_stats.h
 *
 * Description:
 *     Helpers for pretty printing test information and for displaying the UI.
 *
 * Author:
 *     Dumitru Ceara, Eelco Chaudron
 *
 * Initial Created:
 *     09/08/2015
 *
 * Notes:
 *
 */

/*****************************************************************************
 * Multiple include protection
 ****************************************************************************/
#ifndef _H_TPG_TEST_STATS_
#define _H_TPG_TEST_STATS_

#define __tpg_display_func __attribute__((warn_unused_result))

/*****************************************************************************
 * UI Window definitions
 ****************************************************************************/
typedef struct ui_win_s {

    WINDOW *uw_win_border;
    WINDOW *uw_win;
    int     uw_lines;
    int     uw_cols;
    int     uw_y;
    int     uw_x;
    char    uw_border[9];

} ui_win_t;

#define UI_PRINT_WIN(win, line, col, ...) \
    mvwprintw((win), (line), (col), __VA_ARGS__)

#define UI_PRINTLN_WIN(win, line, col, ...)        \
    do {                                           \
        UI_PRINT_WIN(win, line, col, __VA_ARGS__); \
        (line)++;                                  \
    } while (0)

#define UI_HLINE_WIN(win, line, col, fmt, len) \
    mvwhline((win), (line)++, (col), (fmt), (len))

#define UI_HLINE_PRINT(win, fmt, len) \
    do {                              \
        int i;                        \
        for (i = 0; i < (len); i++)   \
            wprintw(win, "%s", fmt);  \
    } while (0)

/*****************************************************************************
 * Externals
 ****************************************************************************/
extern const char *test_entry_state(const tpg_test_case_state_t state);
extern const char *test_entry_type(const tpg_test_case_t *entry);
extern void        test_entry_criteria(const tpg_test_case_t *entry,
                                       char *buf,
                                       uint32_t len);
extern double      test_entry_run_time(const test_env_oper_state_t *state);
extern void        test_entry_quickstats(const tpg_test_case_t *entry,
                                         const test_env_oper_state_t *state,
                                         char *buf,
                                         uint32_t len);

extern void        test_config_show_port(uint32_t eth_port,
                                         printer_arg_t *printer_arg);
extern void        test_config_show_tc(const tpg_test_case_t *te,
                                       printer_arg_t *printer_arg);

extern void        test_state_show_tcs_hdr(uint32_t eth_port,
                                           printer_arg_t *printer_arg);
extern void        test_state_show_tcs(uint32_t eth_port,
                                       printer_arg_t *printer_arg);

extern void        test_state_show_stats(const tpg_test_case_t *te,
                                         printer_arg_t *printer_arg);

extern void        test_show_link_rate(uint32_t eth_port,
                                       printer_arg_t *printer_arg);

extern void        test_init_stats_screen(void);

#endif /* _H_TPG_TEST_STATS_ */

