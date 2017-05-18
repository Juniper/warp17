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
 *     tpg_timer.c
 *
 * Description:
 *     Custom timer wheel implementation.
 *
 * Author:
 *     Dumitru Ceara, Eelco Chaudron
 *
 * Initial Created:
 *     07/10/2015
 *
 * Notes:
 *
 */

/*****************************************************************************
  * Include files
  ***************************************************************************/
#include <rte_cycles.h>

#include "tcp_generator.h"

/*****************************************************************************
 * Global variables
 ****************************************************************************/
static RTE_DEFINE_PER_LCORE(tmr_wheel_t, tcp_slow_timer_wheel);
static RTE_DEFINE_PER_LCORE(tmr_wheel_t, tcp_rto_timer_wheel);
static RTE_DEFINE_PER_LCORE(tmr_wheel_t, l4cb_test_timer_wheel);

/* Define TIMER global statistics. Each thread has its own set of locally
 * allocated stats which are accessible through STATS_GLOBAL(type, core, port).
 */
STATS_DEFINE(timer_statistics_t);

/*****************************************************************************
 * Callbacks for processing the wheels in almost a generic way.
 * TODO: we should change the void * to a real timer structure but that would
 * require even more memory usage.
 ****************************************************************************/

typedef void *(*tmr_nxt_cb_t)(void *entry);

typedef void (*tmr_cb_t)(void *entry);

/*****************************************************************************
 * Forward declarations
 ****************************************************************************/
static cmdline_parse_ctx_t cli_ctx[];

/*****************************************************************************
 * timer_init_wheel()
 ****************************************************************************/
static bool timer_init_wheel(tmr_wheel_t *wheel, uint32_t size, uint32_t step)
{
    uint32_t i;

    wheel->tw_size = size;
    wheel->tw_step = step;
    wheel->tw_last_advance = 0;
    wheel->tw_current = ((rte_get_timer_cycles() / cycles_per_us) / step) % size;
    wheel->tw_wheel = rte_malloc("tpg_timer_wheel",
                                 wheel->tw_size * sizeof(*wheel->tw_wheel),
                                 0);
    if (wheel->tw_wheel == NULL)
        return false;

    for (i = 0; i < wheel->tw_size; i++)
        TMR_LIST_INIT(&wheel->tw_wheel[i]);

    return true;
}

/*****************************************************************************
 * timer_init()
 ****************************************************************************/
bool timer_init(void)
{
    /*
     * Add TPG timer module CLI commands
     */
    if (!cli_add_main_ctx(cli_ctx)) {
        RTE_LOG(ERR, USER1, "ERROR: Can't add timer specific CLI commands!\n");
        return false;
    }

    /*
     * Allocate memory for TIMER statistics, and clear all of them
     */
    if (STATS_GLOBAL_INIT(timer_statistics_t, "timer_stats") == NULL) {
        RTE_LOG(ERR, USER1,
                "ERROR: Failed allocating TIMER statistics memory!\n");
        return false;
    }

    return true;
}

/*****************************************************************************
 * timer_lcore_init()
 ****************************************************************************/
void timer_lcore_init(uint32_t lcore_id)
{
    global_config_t *cfg;
    int              lcore_idx = rte_lcore_index(lcore_id);

    cfg = cfg_get_config();
    if (cfg == NULL)
        TPG_ERROR_ABORT("[%d] Cannot access config!\n", lcore_idx);

    if (timer_init_wheel(&RTE_PER_LCORE(tcp_slow_timer_wheel),
                         cfg->gcfg_slow_tmr_max / cfg->gcfg_slow_tmr_step,
                         cfg->gcfg_slow_tmr_step) == false) {
        TPG_ERROR_ABORT("[%d] Failed allocating tcp slow timer wheel, %s(%d)!\n",
                        lcore_idx,
                        rte_strerror(rte_errno), rte_errno);
    }

    if (timer_init_wheel(&RTE_PER_LCORE(tcp_rto_timer_wheel),
                         cfg->gcfg_rto_tmr_max / cfg->gcfg_rto_tmr_step,
                         cfg->gcfg_rto_tmr_step) == false) {
        TPG_ERROR_ABORT("[%d] Failed allocating tcp rto timer wheel, %s(%d)!\n",
                        lcore_idx,
                        rte_strerror(rte_errno), rte_errno);
    }

    if (timer_init_wheel(&RTE_PER_LCORE(l4cb_test_timer_wheel),
                         cfg->gcfg_test_tmr_max / cfg->gcfg_test_tmr_step,
                         cfg->gcfg_test_tmr_step) == false) {
        TPG_ERROR_ABORT("[%d] Failed allocating cb test timer wheel, %s(%d)!\n",
                        lcore_idx,
                        rte_strerror(rte_errno), rte_errno);
    }

    /* Init the local stats. */
    if (STATS_LOCAL_INIT(timer_statistics_t, "timer_stats", lcore_id) == NULL) {
        TPG_ERROR_ABORT("[%d:%s() Failed to allocate per lcore timer_stats!\n",
                        rte_lcore_index(lcore_id),
                        __func__);
    }
}

/*****************************************************************************
 * TIMER_SET()
 *   - get wheel where we need to insert timer.
 *   - add to the end of the bucket associated with the timeout.
 ****************************************************************************/
#define TIMER_SET(wheel, ctr_type, entry, tmr_field, timeout_us, status)  \
    do {                                                                  \
        int tmr_bucket = tcp_timer_get_wheel_bucket((wheel), timeout_us); \
        if (likely(tmr_bucket >= 0)) {                                    \
            TMR_LIST_INSERT_HEAD(&(wheel)->tw_wheel[tmr_bucket],          \
                                 ctr_type, (entry),                       \
                                 tmr_field);                              \
        } else {                                                          \
            (status) = -EINVAL;                                           \
        }                                                                 \
    } while (0)

/*****************************************************************************
 * TIMER_CANCEL()
 ****************************************************************************/
/* Just quickly remove from the list and pray for the best.. */
#define TIMER_CANCEL(ctr_type, entry, tmr_field)  \
    TMR_LIST_REMOVE(ctr_type, (entry), tmr_field)

/*****************************************************************************
 * TCP_TIMER_SET()
 *   - get wheel where we need to insert timer.
 *   - add to the end of the bucket associated with the timeout.
 ****************************************************************************/
#define TCP_TIMER_SET(wheel, tcb, tmr_field, timeout_us, status)  \
        TIMER_SET((wheel), tcp_control_block_t, (tcb), tmr_field, \
                  (timeout_us), (status))


/*****************************************************************************
 * TCP_TIMER_CANCEL()
 ****************************************************************************/
/* Just quickly remove from the list and pray for the best.. */
#define TCP_TIMER_CANCEL(tcb, tmr_field) \
        TIMER_CANCEL(tcp_control_block_t, (tcb), tmr_field)

/*****************************************************************************
 * L4CB_TIMER_SET()
 *   - get wheel where we need to insert timer.
 *   - add to the end of the bucket associated with the timeout.
 ****************************************************************************/
#define L4CB_TIMER_SET(wheel, l4_cb, tmr_field, timeout_us, status) \
        TIMER_SET((wheel), l4_control_block_t, (l4_cb), tmr_field,  \
                  (timeout_us), (status))

/*****************************************************************************
 * L4CB_TIMER_CANCEL()
 ****************************************************************************/
/* Just quickly remove from the list and pray for the best.. */
#define L4CB_TIMER_CANCEL(l4_cb, tmr_field) \
        TIMER_CANCEL(l4_control_block_t, (l4_cb), tmr_field)

/*****************************************************************************
 * tcp_tcb_slow_next()
 ****************************************************************************/
static inline void *tcp_tcb_slow_next(void *entry)
{
    tcp_control_block_t *tcb = entry;

    return tcb->tcb_slow_tmr_entry.tle_next;
}

/*****************************************************************************
 * tcp_tcb_rto_next()
 ****************************************************************************/
static inline void *tcp_tcb_rto_next(void *entry)
{
    tcp_control_block_t *tcb = entry;

    return tcb->tcb_retrans_tmr_entry.tle_next;
}

/*****************************************************************************
 * l4cb_test_next()
 ****************************************************************************/
static inline void *l4cb_test_next(void *entry)
{
    l4_control_block_t *l4_cb = entry;

    return l4_cb->l4cb_test_tmr_entry.tle_next;
}

/*****************************************************************************
 * tcp_handle_slow_to()
 ****************************************************************************/
static void tcp_handle_slow_to(void *entry)
{
    tcp_control_block_t *tcb = entry;

    /*
     * Make sure we remove the tcb from the timer list first
     * (in case the event handler re-adds it). It's fine to call cancel here
     * because we know the timer just fired.
     */
    INC_STATS(STATS_LOCAL(timer_statistics_t, tcb->tcb_l4.l4cb_interface),
              tts_slow_fired);
    TCP_TIMER_CANCEL(tcb, tcb_slow_tmr_entry);
    tcb->tcb_on_slow_list = false;

    /*
     * Extrapolate the event from the state of the tcb and then call
     * tsm_dispatch_event.
     */
    switch (tcb->tcb_state) {
    case TS_INIT:
    case TS_LISTEN:
    case TS_SYN_SENT:
    case TS_SYN_RECV:
        break;
    case TS_ESTABLISHED:
        /* Add keep-alive here! */
        break;
    case TS_FIN_WAIT_I:
        tsm_dispatch_event(tcb, TE_ORPHAN_TIMEOUT, NULL);
        break;
    case TS_FIN_WAIT_II:
        tsm_dispatch_event(tcb, TE_FIN_TIMEOUT, NULL);
        break;
    case TS_LAST_ACK:
    case TS_CLOSING:
        break;
    case TS_TIME_WAIT:
        tsm_dispatch_event(tcb, TE_TIME_WAIT_TIMEOUT, NULL);
        break;
    case TS_CLOSE_WAIT:
    case TS_CLOSED:
        break;
    default:
        break;
    }
}

/*****************************************************************************
 * tcp_handle_retrans_to()
 ****************************************************************************/
static void tcp_handle_retrans_to(void *entry)
{
    tcp_control_block_t *tcb = entry;

    /*
     * Make sure we remove the tcb from the timer list first
     * (in case the event handler readds it). It's fine to call cancel here
     * because we know the timer just fired.
     */
    INC_STATS(STATS_LOCAL(timer_statistics_t, tcb->tcb_l4.l4cb_interface),
              tts_rto_fired);
    TCP_TIMER_CANCEL(tcb, tcb_retrans_tmr_entry);
    tcb->tcb_on_rto_list = false;

    tcb->tcb_retrans_cnt++;

    tsm_dispatch_event(tcb, TE_RETRANSMISSION_TIMEOUT, NULL);
}

/*****************************************************************************
 * l4cb_handle_test_to()
 ****************************************************************************/
static void l4cb_handle_test_to(void *entry)
{
    l4_control_block_t *l4_cb = entry;

    /*
     * Make sure we remove the cb from the timer list first
     * (in case the event handler readds it). It's fine to call cancel here
     * because we know the timer just fired.
     */
    INC_STATS(STATS_LOCAL(timer_statistics_t, l4_cb->l4cb_interface),
              tts_test_fired);
    L4CB_TIMER_CANCEL(l4_cb, l4cb_test_tmr_entry);
    l4_cb->l4cb_on_test_tmr_list = false;

    /*
     * Send notification to the test module.
     */
    TEST_NOTIF(TEST_NOTIF_TMR_FIRED, l4_cb, l4_cb->l4cb_test_case_id,
               l4_cb->l4cb_interface);
}

/*****************************************************************************
 * tcp_timer_get_wheel_bucket()
 *   - check that timeout doesn't overflow tmr_wheel_current
 *   - return correct bucket where timer would go
 ****************************************************************************/
static int tcp_timer_get_wheel_bucket(tmr_wheel_t *wheel, uint32_t timeout_us)
{
    uint64_t max_timeout_us;
    uint64_t now_us;

    max_timeout_us = (uint64_t)wheel->tw_size * wheel->tw_step;

    /*
     * First validate that timeout isn't too late.
     */
    if (unlikely(timeout_us > max_timeout_us)) {
        INC_STATS(STATS_LOCAL(timer_statistics_t, 0), tts_timeout_overflow);
        TRACE_FMT(TMR, ERROR, "[%s] Timeout exceeds limit %u > %"PRIu64,
                  __func__,
                  timeout_us,
                  max_timeout_us);
        return -EINVAL;
    }

    now_us = rte_get_timer_cycles() / cycles_per_us;

    return ((now_us + timeout_us) / wheel->tw_step) % wheel->tw_size;
}

/*****************************************************************************
 * tpg_time_wheel_advance()
 *  - Take the current time slot and process upto GCFG_TMR_MAX_RUN_CNT (??).
 *  - Advance time pointer if we finished processing everything for the current time.
 *  - Check if the current time is way ahead of the time pointer and log if so. (5seconds should be fine)
 ****************************************************************************/
static void tpg_time_wheel_advance(tmr_wheel_t *wheel,
                                   tmr_nxt_cb_t nxt_cb,
                                   tmr_cb_t cb,
                                   uint64_t now)
{
    uint32_t now_idx;
    uint32_t start_idx;
    uint64_t latest;
    uint32_t cnt = 0;
    uint64_t now_us = now / cycles_per_us;

    now_idx = (now_us / wheel->tw_step) % wheel->tw_size;
    start_idx = wheel->tw_current;

    for (;
            wheel->tw_current != now_idx;
            wheel->tw_current = (wheel->tw_current + 1) % wheel->tw_size) {
        void *current;

        /* NO LIST_FOREACH_SAFE available so we do it ourselves.. */
        for (current = wheel->tw_wheel[wheel->tw_current].tlh_first;
                current != NULL && cnt < GCFG_TMR_MAX_RUN_CNT;
                /* Advance in the loop */) {
            void *tmp;

            tmp = nxt_cb(current);

            /* The callback should remove the entry from the list. */
            cb(current);
            current = tmp;
            cnt++;
        }

        if (unlikely(cnt == GCFG_TMR_MAX_RUN_CNT))
            break;
    }

    wheel->tw_last_advance = now;

    latest = rte_get_timer_cycles();

#if !defined(TPG_DEBUG)
    if ((TPG_TIME_DIFF(latest, now) / cycles_per_us) > GCFG_TMR_MAX_RUN_US) {
        RTE_LOG(ERR, USER1,
                "[%d:%s] Timers hogging the CPU! cnt %"PRIu32" now_idx %"
                PRIu32" start_idx %"PRIu32"\n",
                rte_lcore_index(rte_lcore_id()),
                __func__,
                cnt,
                now_idx,
                start_idx);
    }
#else /* !defined(TPG_DEBUG) */
    (void)latest;
    (void)start_idx;
#endif /* !defined(TPG_DEBUG) */
}

/*****************************************************************************
 * tcp_time_should_advance()
 ****************************************************************************/
static inline __attribute__((always_inline))
bool tcp_time_should_advance(tmr_wheel_t *wheel, uint64_t now)
{
    uint64_t diff = TPG_TIME_DIFF(now, wheel->tw_last_advance);

    return (diff / cycles_per_us) > GCFG_TMR_STEP_ADVANCE;
}

/*****************************************************************************
 * time_advance()
 ****************************************************************************/
void time_advance(void)
{
    uint64_t now;

    now = rte_get_timer_cycles();

    if (tcp_time_should_advance(&RTE_PER_LCORE(tcp_slow_timer_wheel), now)) {
        tpg_time_wheel_advance(&RTE_PER_LCORE(tcp_slow_timer_wheel),
                               tcp_tcb_slow_next,
                               tcp_handle_slow_to,
                               now);
    }

    if (tcp_time_should_advance(&RTE_PER_LCORE(tcp_rto_timer_wheel), now)) {
        tpg_time_wheel_advance(&RTE_PER_LCORE(tcp_rto_timer_wheel),
                               tcp_tcb_rto_next,
                               tcp_handle_retrans_to,
                               now);
    }

    if (tcp_time_should_advance(&RTE_PER_LCORE(l4cb_test_timer_wheel), now)) {
        tpg_time_wheel_advance(&RTE_PER_LCORE(l4cb_test_timer_wheel),
                               l4cb_test_next,
                               l4cb_handle_test_to,
                               now);
    }
}

/*****************************************************************************
 * tcp_timer_rto_set()
 ****************************************************************************/
int tcp_timer_rto_set(l4_control_block_t *l4_cb, uint32_t timeout_us)
{
    tcp_control_block_t *tcb;
    int                  status = 0;

    if (unlikely(l4_cb == NULL)) {
        INC_STATS(STATS_LOCAL(timer_statistics_t, 0), tts_l4cb_null);
        TRACE_FMT(TMR, ERROR, "[%s] tcb NULL", __func__);
        return -EINVAL;
    }

    L4_CB_CHECK(l4_cb);

    tcb = container_of(l4_cb, tcp_control_block_t, tcb_l4);

    if (unlikely(TCB_RTO_TMR_IS_SET(tcb))) {
        INC_STATS(STATS_LOCAL(timer_statistics_t, l4_cb->l4cb_interface),
                  tts_l4cb_invalid_flags);
        TRACE_FMT(TMR, ERROR, "[%s] tcb already on rto list.", __func__);
        return -EINVAL;
    }

    /* status is set inside! */
    TCP_TIMER_SET(&RTE_PER_LCORE(tcp_rto_timer_wheel), tcb,
                  tcb_retrans_tmr_entry,
                  timeout_us,
                  status);

    if (likely(status == 0)) {
        tcb->tcb_on_rto_list = true;
        INC_STATS(STATS_LOCAL(timer_statistics_t, l4_cb->l4cb_interface),
                  tts_rto_set);
    } else {
        INC_STATS(STATS_LOCAL(timer_statistics_t, l4_cb->l4cb_interface),
                  tts_rto_failed);
    }

    return status;
}

/*****************************************************************************
 * tcp_timer_rto_cancel()
 ****************************************************************************/
int tcp_timer_rto_cancel(l4_control_block_t *l4_cb)
{
    tcp_control_block_t *tcb;

    if (unlikely(l4_cb == NULL)) {
        INC_STATS(STATS_LOCAL(timer_statistics_t, 0), tts_l4cb_null);
        TRACE_FMT(TMR, ERROR, "[%s] tcb NULL", __func__);
        return -EINVAL;
    }

    L4_CB_CHECK(l4_cb);
    tcb = container_of(l4_cb, tcp_control_block_t, tcb_l4);

    if (unlikely(!TCB_RTO_TMR_IS_SET(tcb))) {
        INC_STATS(STATS_LOCAL(timer_statistics_t, l4_cb->l4cb_interface),
                  tts_l4cb_invalid_flags);
        TRACE_FMT(TMR, ERROR, "[%s] tcb not on rto list.", __func__);
        return -EINVAL;
    }

    TCP_TIMER_CANCEL(tcb, tcb_retrans_tmr_entry);
    tcb->tcb_on_rto_list = false;

    INC_STATS(STATS_LOCAL(timer_statistics_t, l4_cb->l4cb_interface),
              tts_rto_cancelled);

    return 0;
}

/*****************************************************************************
 * tcp_timer_slow_set()
 ****************************************************************************/
int tcp_timer_slow_set(l4_control_block_t *l4_cb, uint32_t timeout_us)
{
    tcp_control_block_t *tcb;
    int                  status = 0;

    if (unlikely(l4_cb == NULL)) {
        INC_STATS(STATS_LOCAL(timer_statistics_t, 0), tts_l4cb_null);
        TRACE_FMT(TMR, ERROR, "[%s] tcb NULL", __func__);
        return -EINVAL;
    }

    L4_CB_CHECK(l4_cb);

    tcb = container_of(l4_cb, tcp_control_block_t, tcb_l4);

    if (unlikely(TCB_SLOW_TMR_IS_SET(tcb))) {
        INC_STATS(STATS_LOCAL(timer_statistics_t, l4_cb->l4cb_interface),
                  tts_l4cb_invalid_flags);
        TRACE_FMT(TMR, ERROR, "[%s] tcb already on slow list.",
                  __func__);
        return -EINVAL;
    }

    /* status is set inside! */
    TCP_TIMER_SET(&RTE_PER_LCORE(tcp_slow_timer_wheel), tcb,
                  tcb_slow_tmr_entry,
                  timeout_us,
                  status);

    if (likely(status == 0)) {
        tcb->tcb_on_slow_list = true;
        INC_STATS(STATS_LOCAL(timer_statistics_t, l4_cb->l4cb_interface),
                  tts_slow_set);
    } else {
        INC_STATS(STATS_LOCAL(timer_statistics_t, l4_cb->l4cb_interface),
                  tts_slow_failed);
    }

    return status;
}

/*****************************************************************************
 * tcp_timer_slow_cancel()
 ****************************************************************************/
int tcp_timer_slow_cancel(l4_control_block_t *l4_cb)
{
    tcp_control_block_t *tcb;

    if (unlikely(l4_cb == NULL)) {
        INC_STATS(STATS_LOCAL(timer_statistics_t, 0), tts_l4cb_null);
        TRACE_FMT(TMR, ERROR, "[%s] tcb NULL", __func__);
        return -EINVAL;
    }

    L4_CB_CHECK(l4_cb);

    tcb = container_of(l4_cb, tcp_control_block_t, tcb_l4);

    if (unlikely(!TCB_SLOW_TMR_IS_SET(tcb))) {
        INC_STATS(STATS_LOCAL(timer_statistics_t, l4_cb->l4cb_interface),
                  tts_l4cb_invalid_flags);
        TRACE_FMT(TMR, ERROR, "[%s] tcb not on slow list.", __func__);
        return -EINVAL;
    }


    TCP_TIMER_CANCEL(tcb, tcb_slow_tmr_entry);
    tcb->tcb_on_slow_list = false;

    INC_STATS(STATS_LOCAL(timer_statistics_t, l4_cb->l4cb_interface),
              tts_slow_cancelled);

    return 0;
}

/*****************************************************************************
 * l4cb_timer_test_set()
 ****************************************************************************/
int l4cb_timer_test_set(l4_control_block_t *l4_cb, uint32_t timeout_us)
{
    int status = 0;

    L4_CB_CHECK(l4_cb);

    if (unlikely(l4_cb == NULL)) {
        INC_STATS(STATS_LOCAL(timer_statistics_t, 0), tts_l4cb_null);
        TRACE_FMT(TMR, ERROR, "[%s] tcb NULL", __func__);
        return -EINVAL;
    }

    if (unlikely(L4CB_TEST_TMR_IS_SET(l4_cb))) {
        INC_STATS(STATS_LOCAL(timer_statistics_t, l4_cb->l4cb_interface),
                  tts_l4cb_invalid_flags);
        TRACE_FMT(TMR, ERROR, "[%s] l4cb already on rto list.", __func__);
        return -EINVAL;
    }

    /* status is set inside! */
    L4CB_TIMER_SET(&RTE_PER_LCORE(l4cb_test_timer_wheel), l4_cb,
                   l4cb_test_tmr_entry,
                   timeout_us,
                   status);

    if (likely(status == 0)) {
        l4_cb->l4cb_on_test_tmr_list = true;
        INC_STATS(STATS_LOCAL(timer_statistics_t, l4_cb->l4cb_interface),
                  tts_test_set);
    } else {
        INC_STATS(STATS_LOCAL(timer_statistics_t, l4_cb->l4cb_interface),
                  tts_test_failed);
    }

    return status;
}

/*****************************************************************************
 * l4cb_timer_test_cancel()
 ****************************************************************************/
int l4cb_timer_test_cancel(l4_control_block_t *l4_cb)
{
    L4_CB_CHECK(l4_cb);

    if (unlikely(l4_cb == NULL)) {
        INC_STATS(STATS_LOCAL(timer_statistics_t, 0), tts_l4cb_null);
        TRACE_FMT(TMR, ERROR, "[%s] l4_cb NULL", __func__);
        return -EINVAL;
    }

    if (unlikely(!L4CB_TEST_TMR_IS_SET(l4_cb))) {
        INC_STATS(STATS_LOCAL(timer_statistics_t, l4_cb->l4cb_interface),
                  tts_l4cb_invalid_flags);
        TRACE_FMT(TMR, ERROR, "[%s] l4_cb not on test list.", __func__);
        return -EINVAL;
    }

    L4CB_TIMER_CANCEL(l4_cb, l4cb_test_tmr_entry);
    l4_cb->l4cb_on_test_tmr_list = false;

    INC_STATS(STATS_LOCAL(timer_statistics_t, l4_cb->l4cb_interface),
              tts_test_cancelled);

    return 0;
}

/*****************************************************************************
 * CLI commands
 *****************************************************************************
 * - "show timer statistics {details}"
 ****************************************************************************/
struct cmd_show_timer_statistics_result {
    cmdline_fixed_string_t show;
    cmdline_fixed_string_t timer;
    cmdline_fixed_string_t statistics;
    cmdline_fixed_string_t details;
};

static cmdline_parse_token_string_t cmd_show_timer_statistics_t_show =
    TOKEN_STRING_INITIALIZER(struct cmd_show_timer_statistics_result, show, "show");
static cmdline_parse_token_string_t cmd_show_timer_statistics_t_timer =
    TOKEN_STRING_INITIALIZER(struct cmd_show_timer_statistics_result, timer, "timer");
static cmdline_parse_token_string_t cmd_show_timer_statistics_t_statistics =
    TOKEN_STRING_INITIALIZER(struct cmd_show_timer_statistics_result, statistics, "statistics");
static cmdline_parse_token_string_t cmd_show_timer_statistics_t_details =
    TOKEN_STRING_INITIALIZER(struct cmd_show_timer_statistics_result, details, "details");

static void cmd_show_timer_statistics_parsed(void *parsed_result __rte_unused,
                                             struct cmdline *cl, void *data)
{
    int port;
    int core;
    int option = (intptr_t) data;

    for (port = 0; port < rte_eth_dev_count(); port++) {

        /*
         * Calculate totals first
         */
        timer_statistics_t  total_stats;
        timer_statistics_t *timer_stats;

        bzero(&total_stats, sizeof(total_stats));
        STATS_FOREACH_CORE(timer_statistics_t, port, core, timer_stats) {
            total_stats.tts_rto_set += timer_stats->tts_rto_set;
            total_stats.tts_rto_cancelled += timer_stats->tts_rto_cancelled;
            total_stats.tts_rto_fired += timer_stats->tts_rto_fired;

            total_stats.tts_slow_set += timer_stats->tts_slow_set;
            total_stats.tts_slow_cancelled += timer_stats->tts_slow_cancelled;
            total_stats.tts_slow_fired += timer_stats->tts_slow_fired;

            total_stats.tts_test_set += timer_stats->tts_test_set;
            total_stats.tts_test_cancelled += timer_stats->tts_test_cancelled;
            total_stats.tts_test_fired += timer_stats->tts_test_fired;

            total_stats.tts_rto_failed += timer_stats->tts_rto_failed;
            total_stats.tts_slow_failed += timer_stats->tts_slow_failed;
            total_stats.tts_l4cb_null += timer_stats->tts_l4cb_null;
            total_stats.tts_l4cb_invalid_flags +=
                timer_stats->tts_l4cb_invalid_flags;
            total_stats.tts_timeout_overflow +=
                timer_stats->tts_timeout_overflow;
        }

        /*
         * Display individual counters
         */
        cmdline_printf(cl, "Port %d TCP Timer statistics:\n", port);

        SHOW_64BIT_STATS("RTO Timer Set", timer_statistics_t, tts_rto_set,
                         port,
                         option);

        SHOW_64BIT_STATS("RTO Timer Cancelled", timer_statistics_t,
                         tts_rto_cancelled,
                         port,
                         option);

        SHOW_64BIT_STATS("RTO Timer Fired", timer_statistics_t, tts_rto_fired,
                         port,
                         option);

        cmdline_printf(cl, "\n");

        SHOW_64BIT_STATS("Slow Timer Set", timer_statistics_t, tts_slow_set,
                         port,
                         option);

        SHOW_64BIT_STATS("Slow Timer Cancelled", timer_statistics_t,
                         tts_slow_cancelled,
                         port,
                         option);

        SHOW_64BIT_STATS("Slow Timer Fired", timer_statistics_t, tts_slow_fired,
                         port,
                         option);

        cmdline_printf(cl, "\n");

        SHOW_64BIT_STATS("Test Timer Set", timer_statistics_t, tts_test_set,
                         port,
                         option);

        SHOW_64BIT_STATS("Test Timer Cancelled", timer_statistics_t,
                         tts_test_cancelled,
                         port,
                         option);

        SHOW_64BIT_STATS("Test Timer Fired", timer_statistics_t, tts_test_fired,
                         port,
                         option);

        cmdline_printf(cl, "\n");

        SHOW_16BIT_STATS("RTO Timer Failed", timer_statistics_t, tts_rto_failed,
                         port,
                         option);

        SHOW_16BIT_STATS("Slow Timer Failed", timer_statistics_t,
                         tts_slow_failed,
                         port,
                         option);

        SHOW_16BIT_STATS("Test Timer Failed", timer_statistics_t,
                         tts_test_failed,
                         port,
                         option);

        SHOW_16BIT_STATS("TCB NULL", timer_statistics_t, tts_l4cb_null,
                         port,
                         option);

        SHOW_16BIT_STATS("TCB Invalid Flags", timer_statistics_t,
                         tts_l4cb_invalid_flags,
                         port,
                         option);

        SHOW_16BIT_STATS("Timeout Overflow", timer_statistics_t,
                         tts_timeout_overflow,
                         port,
                         option);

        cmdline_printf(cl, "\n");
    }
}

cmdline_parse_inst_t cmd_show_timer_statistics = {
    .f = cmd_show_timer_statistics_parsed,
    .data = NULL,
    .help_str = "show tcp timer statistics",
    .tokens = {
        (void *)&cmd_show_timer_statistics_t_show,
        (void *)&cmd_show_timer_statistics_t_timer,
        (void *)&cmd_show_timer_statistics_t_statistics,
        NULL,
    },
};

cmdline_parse_inst_t cmd_show_timer_statistics_details = {
    .f = cmd_show_timer_statistics_parsed,
    .data = (void *) (intptr_t) 'd',
    .help_str = "show tcp timer statistics details",
    .tokens = {
        (void *)&cmd_show_timer_statistics_t_show,
        (void *)&cmd_show_timer_statistics_t_timer,
        (void *)&cmd_show_timer_statistics_t_statistics,
        (void *)&cmd_show_timer_statistics_t_details,
        NULL,
    },
};

/*****************************************************************************
 * Main menu context
 ****************************************************************************/
static cmdline_parse_ctx_t cli_ctx[] = {
    &cmd_show_timer_statistics,
    &cmd_show_timer_statistics_details,
    NULL,
};

