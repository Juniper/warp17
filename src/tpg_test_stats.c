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
 *     tpg_test_stats.c
 *
 * Description:
 *     WARP17 UI implementation.
 *
 * Author:
 *     Dumitru Ceara, Eelco Chaudron
 *
 * Initial Created:
 *     09/07/2015
 *
 * Notes:
 *
 */

/*****************************************************************************
 * Include files
 ****************************************************************************/
#include <signal.h>

#include <rte_cycles.h>

#include "tcp_generator.h"

/*****************************************************************************
 * Local definitions.
 ****************************************************************************/
typedef void (*test_display_cb_t)(ui_win_t *win, void *arg);

#define WIN_SZ(sz, perc) ((sz) * (perc) / 100)

#define TEST_TMR_TO_DISP        100000
#define TEST_DISPLAY_TMR_TO     GCFG_TEST_STATS_TMR_TO
#define TEST_STATS_TMR_TO       (TEST_DISPLAY_TMR_TO + TEST_TMR_TO_DISP)
#define TEST_STATE_TMR_TO       (TEST_STATS_TMR_TO + TEST_TMR_TO_DISP)
#define TEST_TC_DETAIL_TMR_TO   (TEST_STATE_TMR_TO + TEST_TMR_TO_DISP)

/*****************************************************************************
 * Globals
 ****************************************************************************/
static struct rte_timer display_tmr;
static struct rte_timer stats_tmr;
static struct rte_timer state_tmr;
static struct rte_timer tc_detail_tmr;

static ui_win_t  main_win;
static ui_win_t  stats_cmd_win;
static ui_win_t  stats_test_state_win;
static ui_win_t  stats_test_detail_win;
static ui_win_t *stats_win;

#define STATE_WIN_XPERC  40
#define STATE_WIN_YPERC  40
#define DETAIL_WIN_YPERC 60
#define STATS_WIN_XPERC  60

static uint32_t stats_detail_port;
static uint32_t stats_detail_tcid;
static bool     stats_detail_changed;
static bool     stats_detail_set;
static bool     stats_display_config;

static uint32_t stats_old_rte_log_level;

static struct sigaction win_chg_old_sigact;
static bool             win_changed;

/*****************************************************************************
 * Forward references.
 ****************************************************************************/
static int cmd_test_show_stats_cli(char *buf, uint32_t size);

static void ui_printer(void *arg, const char *fmt, va_list ap);

/*****************************************************************************
 * String constants.
 ****************************************************************************/
static const char *test_entry_state_names[TEST_CASE_STATE__TC_STATE_MAX] = {

    [TEST_CASE_STATE__IDLE]    = "IDLE",
    [TEST_CASE_STATE__RUNNING] = "RUNNING",
    [TEST_CASE_STATE__PASSED]  = "PASSED",
    [TEST_CASE_STATE__FAILED]  = "FAILED",
    [TEST_CASE_STATE__STOPPED] = "STOPPED"

};

/*****************************************************************************
 * test_entry_state()
 ****************************************************************************/
const char *test_entry_state(tpg_test_case_state_t state)
{
    if (state >= TEST_CASE_STATE__TC_STATE_MAX)
        return "<UNKNOWN>";

    return test_entry_state_names[state];
}

/*****************************************************************************
 * test_entry_type()
 ****************************************************************************/
const char *test_entry_type(const tpg_test_case_t *entry)
{
    if (entry->tc_type == TEST_CASE_TYPE__SERVER &&
            entry->tc_server.srv_l4.l4s_proto == L4_PROTO__TCP) {
        return "TCP SRV";
    } else if (entry->tc_type == TEST_CASE_TYPE__SERVER &&
            entry->tc_server.srv_l4.l4s_proto == L4_PROTO__UDP) {
        return "UDP SRV";
    } else  if (entry->tc_type == TEST_CASE_TYPE__CLIENT &&
            entry->tc_client.cl_l4.l4c_proto == L4_PROTO__TCP) {
        return "TCP CL";
    } else if (entry->tc_type == TEST_CASE_TYPE__CLIENT &&
            entry->tc_client.cl_l4.l4c_proto == L4_PROTO__UDP) {
        return "UDP CL";
    }

    return "<UNKNOWN>";
}

/*****************************************************************************
 * test_entry_run_time()
 ****************************************************************************/
double test_entry_run_time(const test_env_oper_state_t *state)
{
    if (state->teos_test_case_state == TEST_CASE_STATE__IDLE)
        return 0;

    if (state->teos_test_case_state == TEST_CASE_STATE__RUNNING) {
        return (double)TPG_TIME_DIFF(rte_get_timer_cycles(),
                                     state->teos_start_time) /
                    rte_get_timer_hz();
    }

    return (double)TPG_TIME_DIFF(state->teos_stop_time,
                                 state->teos_start_time) /
                rte_get_timer_hz();
}

/*****************************************************************************
 * test_entry_criteria()
 ****************************************************************************/
void test_entry_criteria(const tpg_test_case_t *entry, char *buf, uint32_t len)
{
    switch (entry->tc_criteria.tc_crit_type) {
    case TEST_CRIT_TYPE__RUN_TIME:
        snprintf(buf, len, "%9s: %8"PRIu32"s", "Run Time",
                 entry->tc_criteria.tc_run_time_s);
        break;
    case TEST_CRIT_TYPE__SRV_UP:
        snprintf(buf, len, "%9s: %9"PRIu32, "Srv Up",
                 entry->tc_criteria.tc_srv_up);
        break;
    case TEST_CRIT_TYPE__CL_UP:
        snprintf(buf, len, "%9s: %9"PRIu32, "Cl Up",
                 entry->tc_criteria.tc_cl_up);
        break;
    case TEST_CRIT_TYPE__CL_ESTAB:
        snprintf(buf, len, "%9s: %9"PRIu32, "Cl Estab",
                 entry->tc_criteria.tc_cl_estab);
        break;
    case TEST_CRIT_TYPE__DATAMB_SENT:
        snprintf(buf, len, "%9s: %9"PRIu64, "Data MB Sent",
                 entry->tc_criteria.tc_data_mb_sent);
        break;
    default:
        snprintf(buf, len, "<UNKNOWN>");
    }
}

/*****************************************************************************
 * test_entry_quickstats()
 ****************************************************************************/
void test_entry_quickstats(const tpg_test_case_t *entry,
                           const test_env_oper_state_t *state,
                           char *buf,
                           uint32_t len)
{
    switch (entry->tc_criteria.tc_crit_type) {
    case TEST_CRIT_TYPE__RUN_TIME:
        snprintf(buf, len, "%9s: %6"PRIu32"s", "Run Time",
                 (uint32_t)test_entry_run_time(state));
        break;
    case TEST_CRIT_TYPE__SRV_UP:
        snprintf(buf, len, "%9s: %7"PRIu32, "Srv Up",
                 state->teos_result.tc_srv_up);
        break;
    case TEST_CRIT_TYPE__CL_UP:
        snprintf(buf, len, "%9s: %7"PRIu32, "Cl Up",
                 state->teos_result.tc_cl_up);
        break;
    case TEST_CRIT_TYPE__CL_ESTAB:
        snprintf(buf, len, "%9s: %7"PRIu32, "Cl Estab",
                 state->teos_result.tc_cl_estab);
        break;
    case TEST_CRIT_TYPE__DATAMB_SENT:
        snprintf(buf, len, "%9s: %7"PRIu64, "Data MB Sent",
                 state->teos_result.tc_data_mb_sent);
        break;
    default:
        snprintf(buf, len, "<UNKNOWN>");
    }
}

/*****************************************************************************
 * test_config_show_port()
 ****************************************************************************/
void test_config_show_port(uint32_t eth_port, printer_arg_t *printer_arg)
{
    const tpg_port_cfg_t *pcfg;
    uint32_t              i;

    pcfg = test_mgmt_get_port_cfg(eth_port, printer_arg);
    if (!pcfg)
        return;

    tpg_printf(printer_arg, "Port %"PRIu32" config:\n", eth_port);

    for (i = 0; i < pcfg->pc_l3_intfs_count; i++) {
        tpg_printf(printer_arg,
                   "L3 Interface: " TPG_IPV4_PRINT_FMT "/" TPG_IPV4_PRINT_FMT
                   "\n",
                   TPG_IPV4_PRINT_ARGS(pcfg->pc_l3_intfs[i].l3i_ip.ip_v4),
                   TPG_IPV4_PRINT_ARGS(pcfg->pc_l3_intfs[i].l3i_mask.ip_v4));
    }
    tpg_printf(printer_arg, "GW: " TPG_IPV4_PRINT_FMT "\n",
               TPG_IPV4_PRINT_ARGS(pcfg->pc_def_gw.ip_v4));
    tpg_printf(printer_arg, "\n");
}

/*****************************************************************************
 * test_config_rate_show()
 ****************************************************************************/
static void test_config_rate_show(const tpg_rate_t *val,
                                  const char *name,
                                  const char *suffix,
                                  printer_arg_t *printer_arg)
{
    if (TPG_RATE_IS_INF(val))
        tpg_printf(printer_arg, "Rate %-11s : INFINITE\n", name);
    else
        tpg_printf(printer_arg, "Rate %-11s : %"PRIu32"%s\n", name,
                   TPG_RATE_VAL(val),
                   suffix);
}

/*****************************************************************************
 * test_config_duration_show()
 ****************************************************************************/
static void test_config_duration_show(const tpg_delay_t *val,
                                      const char *name,
                                      const char *suffix,
                                      printer_arg_t *printer_arg)
{
    if (TPG_DELAY_IS_INF(val))
        tpg_printf(printer_arg, "Delay %-10s : INFINITE\n", name);
    else
        tpg_printf(printer_arg, "Delay %-10s : %"PRIu32"%s\n", name,
                   TPG_DELAY_VAL(val),
                   suffix);
}

/*****************************************************************************
 * test_config_show_tc_app()
 ****************************************************************************/
static void test_config_show_tc_app(const tpg_test_case_t *te,
                                    printer_arg_t *printer_arg)
{
    if (te->tc_type == TEST_CASE_TYPE__SERVER) {
        APP_SRV_CALL(print_cfg,
                     te->tc_server.srv_app.as_app_proto)(te, printer_arg);
    } else if (te->tc_type == TEST_CASE_TYPE__CLIENT) {
        test_config_rate_show(&te->tc_client.cl_rates.rc_open_rate, "Open",
                              "s/s",
                              printer_arg);

        test_config_rate_show(&te->tc_client.cl_rates.rc_close_rate, "Close",
                              "s/s",
                              printer_arg);

        test_config_rate_show(&te->tc_client.cl_rates.rc_send_rate, "Send",
                              "s/s",
                              printer_arg);

        test_config_duration_show(&te->tc_client.cl_delays.dc_init_delay,
                                  "Init",
                                  "s",
                                  printer_arg);

        test_config_duration_show(&te->tc_client.cl_delays.dc_uptime, "Uptime",
                                  "s",
                                  printer_arg);

        test_config_duration_show(&te->tc_client.cl_delays.dc_downtime,
                                  "Downtime",
                                  "s",
                                  printer_arg);

        tpg_printf(printer_arg, "\n");
        APP_CL_CALL(print_cfg,
                    te->tc_client.cl_app.ac_app_proto)(te, printer_arg);
    } else {
        assert(false);
    }
}

/*****************************************************************************
 * test_config_show_tc()
 ****************************************************************************/
void test_config_show_tc(const tpg_test_case_t *te, printer_arg_t *printer_arg)
{
    tpg_printf(printer_arg, "%-16s : %s\n", "Test type", test_entry_type(te));
    tpg_printf(printer_arg, "%-16s : %s\n", "Async",
               (te->tc_async ? "true" : "false"));
    tpg_printf(printer_arg, "\n");

    if (te->tc_type == TEST_CASE_TYPE__SERVER) {
        tpg_printf(printer_arg,
                   "%-16s : [" TPG_IPV4_PRINT_FMT " -> "
                               TPG_IPV4_PRINT_FMT "]\n",
                   "Local IPs",
                   TPG_IPV4_PRINT_ARGS(te->tc_server.srv_ips.ipr_start.ip_v4),
                   TPG_IPV4_PRINT_ARGS(te->tc_server.srv_ips.ipr_end.ip_v4));
        tpg_printf(printer_arg,
                   "%-16s : [%"PRIu16" -> %"PRIu16"]\n",
                   "Local Ports",
                   te->tc_server.srv_l4.l4s_tcp_udp.tus_ports.l4pr_start,
                   te->tc_server.srv_l4.l4s_tcp_udp.tus_ports.l4pr_end);
    } else if (te->tc_type == TEST_CASE_TYPE__CLIENT) {
        tpg_printf(printer_arg,
                   "%-16s : [" TPG_IPV4_PRINT_FMT " -> "
                               TPG_IPV4_PRINT_FMT "]\n",
                   "Local IPs",
                   TPG_IPV4_PRINT_ARGS(te->tc_client.cl_src_ips.ipr_start.ip_v4),
                   TPG_IPV4_PRINT_ARGS(te->tc_client.cl_src_ips.ipr_end.ip_v4));
        tpg_printf(printer_arg,
                   "%-16s : [%"PRIu16" -> %"PRIu16"]\n",
                   "Local Ports",
                   te->tc_client.cl_l4.l4c_tcp_udp.tuc_sports.l4pr_start,
                   te->tc_client.cl_l4.l4c_tcp_udp.tuc_sports.l4pr_end);

        tpg_printf(printer_arg, "\n");

        tpg_printf(printer_arg,
                   "%-16s : [" TPG_IPV4_PRINT_FMT " -> "
                               TPG_IPV4_PRINT_FMT "]\n",
                   "Remote IPs",
                   TPG_IPV4_PRINT_ARGS(te->tc_client.cl_dst_ips.ipr_start.ip_v4),
                   TPG_IPV4_PRINT_ARGS(te->tc_client.cl_dst_ips.ipr_end.ip_v4));
        tpg_printf(printer_arg,
                   "%-16s : [%"PRIu16" -> %"PRIu16"]\n",
                   "Remote Ports",
                   te->tc_client.cl_l4.l4c_tcp_udp.tuc_dports.l4pr_start,
                   te->tc_client.cl_l4.l4c_tcp_udp.tuc_dports.l4pr_end);
    } else {
        assert(false);
    }

    tpg_printf(printer_arg, "\n");
    test_config_show_tc_app(te, printer_arg);
}


/*****************************************************************************
 * test_state_show_tcs_hdr()
 ****************************************************************************/
void test_state_show_tcs_hdr(uint32_t eth_port, printer_arg_t *printer_arg)
{
    /* Print the header */
    tpg_printf(printer_arg, "Port %"PRIu32 "\n", eth_port);
    tpg_printf(printer_arg, "%4s %11s %20s %10s %10s %20s\n",
               "TcId", "Type", "Criteria", "State", "Runtime",
               "Quick stats");
}

/*****************************************************************************
 * test_state_show_tcs()
 ****************************************************************************/
void test_state_show_tcs(uint32_t eth_port, printer_arg_t *printer_arg)
{
    uint32_t tcid;

    for (tcid = 0; tcid < TPG_TEST_MAX_ENTRIES; tcid++) {
        char                       crit_buf[256];
        char                       quick_stat_buf[256];
        tpg_test_case_t            entry;
        test_env_oper_state_t      state;

        if (test_mgmt_get_test_case_cfg(eth_port, tcid, &entry, NULL) != 0)
            continue;

        if (test_mgmt_get_test_case_state(eth_port, tcid, &state, NULL) != 0)
            continue;

        test_entry_criteria(&entry, crit_buf, sizeof(crit_buf));
        test_entry_quickstats(&entry, &state, quick_stat_buf,
                              sizeof(quick_stat_buf));

        tpg_printf(printer_arg, "%4"PRIu32" %11s %20s %10s %9.2lfs %20s\n",
                   tcid,
                   test_entry_type(&entry),
                   crit_buf,
                   test_entry_state(state.teos_test_case_state),
                   test_entry_run_time(&state),
                   quick_stat_buf);
    }
}

/*****************************************************************************
 * test_state_show_stats()
 ****************************************************************************/
void test_state_show_stats(const tpg_test_case_t *te,
                           printer_arg_t *printer_arg)
{
    tpg_test_case_rate_stats_t rate_stats;
    tpg_test_case_app_stats_t  app_stats;
    tpg_app_proto_t            app_id;

    if (test_mgmt_get_test_case_rate_stats(te->tc_eth_port, te->tc_id,
                                           &rate_stats,
                                           printer_arg) != 0)
        return;

    tpg_printf(printer_arg, "%13s %13s %13s\n", "Estab/s", "Closed/s",
               "Data Send/s");
    tpg_printf(printer_arg, "%13"PRIu32 " %13"PRIu32 " %13"PRIu32"\n",
               rate_stats.tcrs_estab_per_s,
               rate_stats.tcrs_closed_per_s,
               rate_stats.tcrs_data_per_s);

    if (test_mgmt_get_test_case_app_stats(te->tc_eth_port, te->tc_id,
                                          &app_stats,
                                          printer_arg) != 0)
        return;

    if (te->tc_type == TEST_CASE_TYPE__SERVER) {
        app_id = te->tc_server.srv_app.as_app_proto;
        APP_SRV_CALL(stats_print, app_id)(&app_stats, printer_arg);
    } else if (te->tc_type == TEST_CASE_TYPE__CLIENT) {
        app_id = te->tc_client.cl_app.ac_app_proto;
        APP_CL_CALL(stats_print, app_id)(&app_stats, printer_arg);
    }

    tpg_printf(printer_arg, "\n");
}

/*****************************************************************************
 * test_show_link_rate()
 ****************************************************************************/
void test_show_link_rate(uint32_t eth_port, printer_arg_t *printer_arg)
{
    uint64_t             link_speed_bytes;
    double               usage_tx;
    double               usage_rx;
    struct rte_eth_link  link_info;
    struct rte_eth_stats link_rate_stats;

    port_link_info_get(eth_port, &link_info);
    port_link_rate_stats_get(eth_port, &link_rate_stats);

    link_speed_bytes = (uint64_t)link_info.link_speed * 1000 * 1000 / 8;

    if (link_info.link_status) {
        usage_tx = (double)link_rate_stats.obytes * 100 / link_speed_bytes;
        usage_rx = (double)link_rate_stats.ibytes * 100 / link_speed_bytes;
    } else {
        usage_tx = 0;
        usage_rx = 0;
    }

    tpg_printf(printer_arg,
               "Port %"PRIu32": link %s, speed %d%s, duplex %s%s, TX: %.2lf%% RX: %.2lf%%\n",
               eth_port,
               LINK_STATE(&link_info),
               LINK_SPEED(&link_info),
               LINK_SPEED_SZ(&link_info),
               LINK_DUPLEX(&link_info),
               usage_tx,
               usage_rx);
}

/*****************************************************************************
 * test_sig_winch()
 ****************************************************************************/
static void test_sig_winch(int signo, siginfo_t *siginfo __rte_unused,
                           void *context __rte_unused)
{
    if (signo != SIGWINCH)
        return;

    win_changed = true;
}

/*****************************************************************************
 * test_display_stats_cmds()
 ****************************************************************************/
static void test_display_stats_cmds(ui_win_t *ui_win,
                                    void *arg __rte_unused)
{
    int line = 0;

    UI_PRINTLN_WIN(ui_win->uw_win, line, 0,
                   "Quit=q, Next TestCase=n, Prev TestCase=p, Config View=c, Stats View=s");
}

/*****************************************************************************
 * test_display_stats_test_state()
 ****************************************************************************/
static void test_display_stats_test_state(ui_win_t *ui_win,
                                          void *arg __rte_unused)
{
    uint32_t      port;
    printer_arg_t parg;

    parg = TPG_PRINTER_ARG(ui_printer, ui_win->uw_win);

    for (port = 0; port < rte_eth_dev_count(); port++) {
        test_state_show_tcs_hdr(port, &parg);
        UI_HLINE_PRINT(ui_win->uw_win, "=", ui_win->uw_cols - 2);

        test_state_show_tcs(port, &parg);
    }
}

/*****************************************************************************
 * test_display_stats_test_detail()
 ****************************************************************************/
static void test_display_stats_test_detail(ui_win_t *ui_win,
                                           void *arg __rte_unused)
{
    WINDOW          *win = ui_win->uw_win;
    printer_arg_t    parg = TPG_PRINTER_ARG(ui_printer, win);
    tpg_test_case_t  tc;

    if (!stats_detail_set) {
        wprintw(win, "No tests running!");
        return;
    }

    wprintw(win, "Port %"PRIu32", Test Case %"PRIu32"\n\n",
            stats_detail_port,
            stats_detail_tcid);

    if (test_mgmt_get_test_case_cfg(stats_detail_port, stats_detail_tcid, &tc,
                                    &parg))
        return;

    if (stats_display_config) {
        test_config_show_port(stats_detail_port, &parg);

        test_config_show_tc(&tc, &parg);
    } else {
        wprintw(win, "Statistics:\n");
        test_state_show_stats(&tc, &parg);
    }
}
/*****************************************************************************
 * test_display_stats_hdr()
 ****************************************************************************/
static int __tpg_display_func
test_display_stats_hdr(ui_win_t *ui_win, int line, uint32_t port)
{
    WINDOW        *win = ui_win->uw_win;
    printer_arg_t  parg = TPG_PRINTER_ARG(ui_printer, win);

    test_show_link_rate(port, &parg);

    /* Take into account the link rate display (one line). */
    line++;
    UI_HLINE_WIN(win, line, 0, '=', ui_win->uw_cols - 2);

    return line;
}

/*****************************************************************************
 * test_display_stats_link()
 ****************************************************************************/
static int __tpg_display_func
test_display_stats_link(ui_win_t *ui_win, int line,
                        struct rte_eth_stats *link_stats,
                        struct rte_eth_stats *link_rate_stats,
                        port_statistics_t *ptotal_stats)
{
    WINDOW *win = ui_win->uw_win;

    UI_HLINE_WIN(win, line, 0, '=', ui_win->uw_cols - 2);
    UI_PRINTLN_WIN(win, line, 0, "Link Stats");
    UI_HLINE_WIN(win, line, 0, '=', ui_win->uw_cols - 2);

    UI_PRINTLN_WIN(win, line, 0, "%10s %15s %10s %15s", "", "Link", "Rate",
                   "SW");
    UI_PRINTLN_WIN(win, line, 0, "%10s %15"PRIu64" %10"PRIu64" %15"PRIu64,
                   "Rx Pkts", link_stats->ipackets, link_rate_stats->ipackets,
                   ptotal_stats->ps_received_pkts);
    UI_PRINTLN_WIN(win, line, 0, "%10s %15"PRIu64" %10"PRIu64" %15"PRIu64,
                   "Rx Bytes", link_stats->ibytes, link_rate_stats->ibytes,
                   ptotal_stats->ps_received_bytes);
    UI_PRINTLN_WIN(win, line, 0, "%10s %15"PRIu64" %10"PRIu64" %15"PRIu64,
                   "Tx Pkts", link_stats->opackets, link_rate_stats->opackets,
                   ptotal_stats->ps_send_pkts);
    UI_PRINTLN_WIN(win, line, 0, "%10s %15"PRIu64" %10"PRIu64" %15"PRIu64,
                   "Tx Bytes", link_stats->obytes, link_rate_stats->obytes,
                   ptotal_stats->ps_send_bytes);
    UI_PRINTLN_WIN(win, line, 0, "%10s %15"PRIu64" %10"PRIu64" %15s",
                   "Rx Err", link_stats->ierrors, link_rate_stats->ierrors,
                   "N/A");
    UI_PRINTLN_WIN(win, line, 0, "%10s %15"PRIu64" %10"PRIu64" %15"PRIu32,
                   "Tx Err", link_stats->oerrors, link_rate_stats->oerrors,
                   ptotal_stats->ps_send_failure);
    UI_PRINTLN_WIN(win, line, 0, "%10s %15"PRIu64" %10"PRIu64" %15s",
                   "Rx No Mbuf", link_stats->rx_nombuf,
                   link_rate_stats->rx_nombuf,
                   "N/A");

    UI_PRINTLN_WIN(win, line, 0, "");

    return line;
}

/*****************************************************************************
 * test_display_stats_ip()
 ****************************************************************************/
static int __tpg_display_func
test_display_stats_ip(ui_win_t *ui_win, int line,
                      ipv4_statistics_t *ipv4_stats)
{
    WINDOW *win = ui_win->uw_win;

    UI_HLINE_WIN(win, line, 0, '=', ui_win->uw_cols - 2);
    UI_PRINTLN_WIN(win, line, 0, "IP Stats");
    UI_HLINE_WIN(win, line, 0, '=', ui_win->uw_cols - 2);

    UI_PRINTLN_WIN(win, line, 0, "%-11s : %16"PRIu64 "  %-15s : %6"PRIu16,
                   "Rx Pkts",
                   ipv4_stats->ips_received_pkts,
                   "Invalid Cksum",
                   ipv4_stats->ips_invalid_checksum);
    UI_PRINTLN_WIN(win, line, 0, "%-11s : %16"PRIu64 "  %-15s : %6"PRIu16,
                   "Rx Bytes",
                   ipv4_stats->ips_received_bytes,
                   "Small Mbuf",
                   ipv4_stats->ips_to_small_fragment);
    UI_PRINTLN_WIN(win, line, 0, "%-11s : %16"PRIu64 "  %-15s : %6"PRIu16,
                   "Rx ICMP",
                   ipv4_stats->ips_protocol_icmp,
                   "Small Hdr",
                   ipv4_stats->ips_hdr_to_small);
    UI_PRINTLN_WIN(win, line, 0, "%-11s : %16"PRIu64 "  %-15s : %6"PRIu16,
                   "Rx TCP",
                   ipv4_stats->ips_protocol_tcp,
                   "Invalid Len",
                   ipv4_stats->ips_total_length_invalid);
    UI_PRINTLN_WIN(win, line, 0, "%-11s : %16"PRIu64 "  %-15s : %6"PRIu16,
                   "Rx UDP",
                   ipv4_stats->ips_protocol_udp,
                   "Rx Frags",
                   ipv4_stats->ips_received_frags);
    UI_PRINTLN_WIN(win, line, 0, "%-11s : %16"PRIu64,
                   "Rx Other",
                   ipv4_stats->ips_protocol_other);
#ifndef _SPEEDY_PKT_PARSE_
    UI_PRINTLN_WIN(win, line, 0, "%-11s : %16"PRIu16 "  %-15s : %6"PRIu16,
                   "Invalid Ver",
                   ipv4_stats->ips_not_v4,
                   "Res Bit",
                   ipv4_stats->ips_reserved_bit_set);
#endif /* _SPEEDY_PKT_PARSE_ */
    UI_PRINTLN_WIN(win, line, 0, "");

    return line;
}

/*****************************************************************************
 * test_display_stats_tsm()
 ****************************************************************************/
static int __tpg_display_func
test_display_stats_tsm(ui_win_t *ui_win, int line, tsm_statistics_t *tsm_stats)
{
    tcpState_t  tcp_state;
    int         tcp_col = 0;
    WINDOW     *win = ui_win->uw_win;

    UI_HLINE_WIN(win, line, 0, '=', ui_win->uw_cols - 2);
    UI_PRINTLN_WIN(win, line, 0, "TCP SM Stats");
    UI_HLINE_WIN(win, line, 0, '=', ui_win->uw_cols - 2);

    for (tcp_state = TS_INIT; tcp_state < TS_MAX_STATE; tcp_state++) {
        UI_PRINT_WIN(win, line, tcp_col, "%-10s : %16"PRIu64,
                     tsm_get_state_str(tcp_state),
                     tsm_stats->tsms_tcb_states[tcp_state]);
        if (tcp_state % 2 == 0) {
            tcp_col = 35;
        } else {
            tcp_col = 0;
            UI_PRINTLN_WIN(win, line, tcp_col, "");
        }
    }
    UI_PRINTLN_WIN(win, line, 0, "");

    return line;
}

/*****************************************************************************
 * test_aggregate_rate_stats()
 ****************************************************************************/
static void test_aggregate_rate_stats(uint32_t port,
                                      tpg_test_case_rate_stats_t *stats)
{
    uint32_t tcid;

    bzero(stats, sizeof(*stats));

    for (tcid = 0; tcid < TPG_TEST_MAX_ENTRIES; tcid++) {

        tpg_test_case_t             te;
        tpg_test_case_rate_stats_t  rate_stats;

        if (test_mgmt_get_test_case_cfg(port, tcid, &te, NULL) != 0)
            continue;

        if (test_mgmt_get_test_case_rate_stats(te.tc_eth_port,
                                               te.tc_id,
                                               &rate_stats,
                                               NULL) != 0)
            continue;

        stats->tcrs_estab_per_s += rate_stats.tcrs_estab_per_s;
        stats->tcrs_closed_per_s += rate_stats.tcrs_closed_per_s;
        stats->tcrs_data_per_s += rate_stats.tcrs_data_per_s;
    }
}

/*****************************************************************************
 * test_display_stats()
 ****************************************************************************/
static void test_display_stats(ui_win_t *ui_win, void *arg)
{
    uint32_t                    port = (uintptr_t)arg;
    tpg_test_case_rate_stats_t  rate_stats;
    WINDOW                     *win = ui_win->uw_win;
    int                         line = 0;

    struct rte_eth_stats        link_stats;
    struct rte_eth_stats        link_rate_stats;

    port_statistics_t           ptotal_stats;
    ipv4_statistics_t           ipv4_stats;
    tsm_statistics_t            tsm_stats;

    test_aggregate_rate_stats(port, &rate_stats);
    port_link_stats_get(port, &link_stats);
    port_link_rate_stats_get(port, &link_rate_stats);
    port_total_stats_get(port, &ptotal_stats);
    ipv4_total_stats_get(port, &ipv4_stats);
    tsm_total_stats_get(port, &tsm_stats);

    /* Print the header. */
    line = test_display_stats_hdr(ui_win, line, port);

    UI_PRINTLN_WIN(win, line, 0, "%8s %12s %12s %12s", "CL s/s",
                   "Established", "Closed", "Data");
    UI_PRINTLN_WIN(win, line, 0, "%8s %12"PRIu32" %12"PRIu32" %12"PRIu32,
                   "TCP/UDP",
                   rate_stats.tcrs_estab_per_s,
                   rate_stats.tcrs_closed_per_s,
                   rate_stats.tcrs_data_per_s);

    UI_PRINTLN_WIN(win, line, 0, "");

    line = test_display_stats_tsm(ui_win, line, &tsm_stats);

    line = test_display_stats_ip(ui_win, line, &ipv4_stats);

    line = test_display_stats_link(ui_win, line, &link_stats,
                                   &link_rate_stats,
                                   &ptotal_stats);
}

/*****************************************************************************
 * test_display_win()
 ****************************************************************************/
static void test_display_win(ui_win_t *ui_win, bool clear,
                             test_display_cb_t cb,
                             void *arg)
{
    char *b = ui_win->uw_border;

    if (clear) {
        wclear(ui_win->uw_win_border);
        wborder(ui_win->uw_win_border,
                b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7]);
        wrefresh(ui_win->uw_win_border);
        wclear(ui_win->uw_win);
    }

    mvwprintw(ui_win->uw_win, 0, 0, "");
    cb(ui_win, arg);
    wrefresh(ui_win->uw_win);
}

/*****************************************************************************
 * test_display()
 ****************************************************************************/
static void test_display(void)
{
    uint32_t port;

    /* Always clear the test state win as the content size might change. */
    test_display_win(&stats_test_state_win, true,
                      test_display_stats_test_state, NULL);
    /* Always clear the test detail win as the content size might change. */
    test_display_win(&stats_test_detail_win, true,
                      test_display_stats_test_detail, NULL);

    for (port = 0; port < rte_eth_dev_count(); port++) {
        test_display_win(&stats_win[port], true, test_display_stats,
                         (void *)(uintptr_t)port);
    }

    test_display_win(&stats_cmd_win, true, test_display_stats_cmds,
                     NULL);
}

/*****************************************************************************
 * test_stats_ui_win_init()
 ****************************************************************************/
static int test_stats_ui_win_init(ui_win_t *ui_win, int ysz, int xsz,
                                  int y, int x,
                                  const char *border)
{
    bool has_border = (border != NULL);

    if (has_border) {
        /* Check that we have room for the border. */
        if (ysz < 3 || xsz < 3)
            return -1;

        ui_win->uw_win_border = newwin(ysz, xsz, y, x);
        if (ui_win->uw_win_border == NULL)
            return -1;

        wclear(ui_win->uw_win_border);

        ui_win->uw_win = newwin(ysz - 2, xsz - 2, y + 1, x + 1);
        if (ui_win->uw_win == NULL)
            return -1;

        ui_win->uw_lines = ysz;
        ui_win->uw_cols = xsz;
        rte_memcpy(&ui_win->uw_border[0], &border[0], sizeof(ui_win->uw_border));
    } else {
        ui_win->uw_win_border = NULL;
        ui_win->uw_win = newwin(ysz, xsz, y, x);
        if (ui_win->uw_win == NULL)
            return -1;

        ui_win->uw_lines = LINES;
        ui_win->uw_cols = COLS;
        bzero(&ui_win->uw_border[0], sizeof(ui_win->uw_border));
    }

    ui_win->uw_y = y;
    ui_win->uw_x = x;

    wclear(ui_win->uw_win);
    return 0;
}

/*****************************************************************************
 * test_stats_init_windows()
 ****************************************************************************/
static void test_stats_init_windows(void)
{
    uint32_t port;
    int      lines;
    int      cols;
    int      stats_x;
    int      stats_xsz;

    initscr();
    refresh();

    if (stdscr == NULL)
        TPG_ERROR_ABORT("ERROR: %s!\n", "Failed to initialize stats screen");

    cbreak();
    nonl();
    noecho();

    /*
     * Create a full screen window
     */
    test_stats_ui_win_init(&main_win, 0, 0, 0, 0, NULL);

    lines = main_win.uw_lines;
    cols = main_win.uw_cols;

    /*
     * Create the "command" window (i.e., the last line at the bottom of the
     * screen).
     */
    test_stats_ui_win_init(&stats_cmd_win, 1, cols, lines - 1, 0, NULL);

    /* Save some lines for the command window. */
    lines -= 1;

    /*
     * Create the test state window - the left part of the screen.
     */
    test_stats_ui_win_init(&stats_test_state_win,
                           WIN_SZ(lines, STATE_WIN_YPERC),
                           WIN_SZ(cols, STATE_WIN_XPERC),
                           0, 0,
                           "| - +-| ");

    /*
     * Create the test detail window - the left part of the screen.
     */
    test_stats_ui_win_init(&stats_test_detail_win,
                           lines - stats_test_state_win.uw_lines,
                           WIN_SZ(cols, STATE_WIN_XPERC),
                           stats_test_state_win.uw_lines, 0,
                           "| --|-+-");

    /*
     * Create the stats windows - the right part of the screen.
     */
    stats_x = stats_test_state_win.uw_cols;
    stats_xsz = WIN_SZ(cols, STATS_WIN_XPERC) / rte_eth_dev_count();

    for (port = 0; port < rte_eth_dev_count(); port++) {
        test_stats_ui_win_init(&stats_win[port],
                               lines, stats_xsz,
                               0, stats_x,
                               "| ----+-");
        stats_x += stats_xsz;
    }
}

/*****************************************************************************
 * test_stats_destroy_windows()
 ****************************************************************************/
static void test_stats_destroy_windows(void)
{
    endwin();
}

/*****************************************************************************
 * test_state_tmr_cb()
 ****************************************************************************/
static void test_state_tmr_cb(struct rte_timer *tmr __rte_unused,
                              void *arg __rte_unused)
{
    test_display_win(&stats_test_state_win, false,
                     test_display_stats_test_state, NULL);
}

/*****************************************************************************
 * test_stats_tmr_cb()
 ****************************************************************************/
static void test_stats_tmr_cb(struct rte_timer *tmr __rte_unused,
                              void *arg __rte_unused)
{
    uint32_t port;

    for (port = 0; port < rte_eth_dev_count(); port++)
        test_display_win(&stats_win[port], false, test_display_stats,
                         (void *)(uintptr_t)port);
}

/*****************************************************************************
 * test_tc_detail_tmr_cb()
 ****************************************************************************/
static void test_tc_detail_tmr_cb(struct rte_timer *tmr __rte_unused,
                                  void *arg __rte_unused)
{
    bool refresh = false;

    if (stats_detail_changed) {
        stats_detail_changed = false;
        refresh = true;
    }

    test_display_win(&stats_test_detail_win, refresh,
                     test_display_stats_test_detail, NULL);
}

/*****************************************************************************
 * test_display_tmr_cb()
 ****************************************************************************/
static void test_display_tmr_cb(struct rte_timer *tmr __rte_unused,
                                void *arg __rte_unused)
{
    if (win_changed) {
        win_changed = false;
        test_stats_destroy_windows();
        test_stats_init_windows();
        test_display();
    } else
        test_display_win(&stats_cmd_win, false, test_display_stats_cmds, NULL);
}

/*****************************************************************************
 * test_stats_init_detail_tc()
 ****************************************************************************/
static void test_stats_init_detail_tc(void)
{
    uint32_t port;

    for (port = 0; port < rte_eth_dev_count(); port++) {
        if (test_mgmt_get_test_case_count(port)) {
            tpg_test_case_t entry;

            stats_detail_port = port;
            stats_detail_tcid = 0;
            while (test_mgmt_get_test_case_cfg(stats_detail_port,
                                               stats_detail_tcid,
                                               &entry,
                                               NULL) != 0) {
                stats_detail_tcid++;
            }
            assert(stats_detail_tcid < TPG_TEST_MAX_ENTRIES);
            stats_detail_set = true;
            return;
        }
    }
    stats_detail_set = false;
}

/*****************************************************************************
 * test_stats_forward_detail_tc()
 ****************************************************************************/
static void test_stats_forward_detail_tc(void)
{
    tpg_test_case_t entry;
    int             err;

    if (!stats_detail_set)
        return;

    stats_detail_changed = true;

    do {
        stats_detail_tcid++;
        err = test_mgmt_get_test_case_cfg(stats_detail_port,
                                          stats_detail_tcid,
                                          &entry,
                                          NULL);
    } while (stats_detail_tcid != TPG_TEST_MAX_ENTRIES && err == -ENOENT);

    if (err == 0)
        return;

    do {
        stats_detail_port++;
        stats_detail_port %= rte_eth_dev_count();
    } while (test_mgmt_get_test_case_count(stats_detail_port) == 0);

    for (stats_detail_tcid = 0;
            stats_detail_tcid < TPG_TEST_MAX_ENTRIES;
            stats_detail_tcid++) {
        if (!test_mgmt_get_test_case_cfg(stats_detail_port,
                                         stats_detail_tcid,
                                         &entry,
                                         NULL))
            return;
    }
    /* Should never be reached. */
    assert(false);
}

/*****************************************************************************
 * test_stats_back_detail_tc()
 ****************************************************************************/
static void test_stats_back_detail_tc(void)
{
    tpg_test_case_t entry;
    int             new_tcid;
    int             err = -ENOENT;

    if (!stats_detail_set)
        return;

    stats_detail_changed = true;

    if (stats_detail_tcid) {
        do {
            stats_detail_tcid--;
            err = test_mgmt_get_test_case_cfg(stats_detail_port,
                                                stats_detail_tcid,
                                                &entry,
                                                NULL);
        } while (stats_detail_tcid > 0 && err == -ENOENT);
    }

    if (err == 0)
        return;

    do {
        if (stats_detail_port == 0)
            stats_detail_port = rte_eth_dev_count() - 1;
        else
            stats_detail_port--;
    } while (test_mgmt_get_test_case_count(stats_detail_port) == 0);

    for (new_tcid = TPG_TEST_MAX_ENTRIES - 1; new_tcid >= 0; new_tcid--) {
        if (!test_mgmt_get_test_case_cfg(stats_detail_port, new_tcid, &entry,
                                         NULL)) {
            stats_detail_tcid = new_tcid;
            return;
        }
    }

    /* Should never be reached. */
    assert(false);
}

/*****************************************************************************
 * test_stats_set_display_config()
 ****************************************************************************/
static void test_stats_set_display_config(void)
{
    stats_display_config = true;
    stats_detail_changed = true;
}

/*****************************************************************************
 * test_stats_set_display_stats()
 ****************************************************************************/
static void test_stats_set_display_stats(void)
{
    stats_display_config = false;
    stats_detail_changed = true;
}

/*****************************************************************************
 * test_stats_quit_screen()
 ****************************************************************************/
static void test_stats_quit_screen(void)
{
    cli_unset_override();
    sigaction(SIGWINCH, &win_chg_old_sigact, NULL);
    rte_timer_stop(&display_tmr);
    rte_timer_stop(&stats_tmr);
    rte_timer_stop(&state_tmr);
    rte_timer_stop(&tc_detail_tmr);
    test_stats_destroy_windows();
    rte_free(stats_win);
    win_changed = false;
    rte_set_log_level(stats_old_rte_log_level);
}

/*****************************************************************************
 * test_init_stats_screen()
 ****************************************************************************/
void test_init_stats_screen(void)
{
    struct sigaction sigact = {
        .sa_sigaction   = test_sig_winch,
        .sa_flags       = SA_SIGINFO,
    };

    stats_win = rte_zmalloc_socket("stats_win",
                                   rte_eth_dev_count() * sizeof(*stats_win),
                                   0,
                                   rte_lcore_to_socket_id(rte_lcore_id()));

    if (stats_win == NULL) {
        TPG_ERROR_ABORT("ERROR: %s!\n", "Failed to allocate stats windows");
    }

    sigaction(SIGWINCH, &sigact, &win_chg_old_sigact);
    win_changed = true;
    stats_old_rte_log_level = rte_get_log_level();
    rte_set_log_level(0);
    test_stats_init_detail_tc();

    rte_timer_init(&display_tmr);
    rte_timer_reset(&display_tmr, TEST_DISPLAY_TMR_TO * cycles_per_us,
                    PERIODICAL,
                    rte_lcore_id(),
                    test_display_tmr_cb,
                    NULL);

    rte_timer_init(&stats_tmr);
    rte_timer_reset(&stats_tmr, TEST_STATS_TMR_TO * cycles_per_us,
                    PERIODICAL,
                    rte_lcore_id(),
                    test_stats_tmr_cb,
                    NULL);

    rte_timer_init(&state_tmr);
    rte_timer_reset(&state_tmr, TEST_STATE_TMR_TO * cycles_per_us,
                    PERIODICAL,
                    rte_lcore_id(),
                    test_state_tmr_cb,
                    NULL);

    rte_timer_init(&tc_detail_tmr);
    rte_timer_reset(&tc_detail_tmr, TEST_TC_DETAIL_TMR_TO * cycles_per_us,
                    PERIODICAL,
                    rte_lcore_id(),
                    test_tc_detail_tmr_cb,
                    NULL);

    if (cli_set_override(cmd_test_show_stats_cli) == false) {
        RTE_LOG(ERR, USER1, "Failed to set the stats CLI override\n");
        test_stats_quit_screen();
        return;
    }
}

/*****************************************************************************
 * cmd_test_show_stats_cli()
 ****************************************************************************/
static int cmd_test_show_stats_cli(char *buf, uint32_t size)
{
    for (; size; buf++, size--) {
        switch (*buf) {
        case 'q':
        case 'Q':
            test_stats_quit_screen();
            cli_redisplay_prompt();
            break;
        case 'n':
        case 'N':
            test_stats_forward_detail_tc();
            break;
        case 'p':
        case 'P':
            test_stats_back_detail_tc();
            break;
        case 'c':
        case 'C':
            test_stats_set_display_config();
            break;
        case 's':
        case 'S':
            test_stats_set_display_stats();
            break;
        }
    }
    return 0;
}

/*****************************************************************************
 * ui_printer()
 ****************************************************************************/
static void ui_printer(void *arg, const char *fmt, va_list ap)
{
    WINDOW *win = arg;

    vw_printw(win, fmt, ap);
}

