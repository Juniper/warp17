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
 *     tpg_tcp_timer.h
 *
 * Description:
 *     Timer wheel infrastructure.
 *
 * Author:
 *     Dumitru Ceara, Eelco Chaudron
 *
 * Initial Created:
 *     07/10/2015
 *
 * Notes:
 *     This is currently specific to L4 CB timers. Can we make it generic
 *     without too much penalty on performance?
 */

/*****************************************************************************
 * Multiple include protection
 ****************************************************************************/
#ifndef _H_TPG_TIMER_
#define _H_TPG_TIMER_

/*****************************************************************************
 * Timer statistics
 ****************************************************************************/
typedef struct timer_statistics_s {

    uint64_t tts_rto_set;
    uint64_t tts_rto_cancelled;
    uint64_t tts_rto_fired;

    uint64_t tts_slow_set;
    uint64_t tts_slow_cancelled;
    uint64_t tts_slow_fired;

    uint64_t tts_test_set;
    uint64_t tts_test_cancelled;
    uint64_t tts_test_fired;

    /* Unlikely uint16_t error counters */
    uint16_t tts_rto_failed;
    uint16_t tts_slow_failed;
    uint16_t tts_test_failed;
    uint16_t tts_l4cb_null;
    uint16_t tts_l4cb_invalid_flags;
    uint16_t tts_timeout_overflow;

} __rte_cache_aligned timer_statistics_t;

STATS_GLOBAL_DECLARE(timer_statistics_t);

/*****************************************************************************
 * Timer definitions
 ****************************************************************************/
typedef struct tmr_list_head_s {
    void *tlh_first;
} tmr_list_head_t;

#define tmr_list_entry(type)                \
    struct {                                \
        __typeof__(struct type)  *tle_next; \
        __typeof__(struct type) **tle_prev; \
    }

#define TMR_LIST_INIT(head) \
    ((head)->tlh_first = NULL)

#define TMR_LIST_INSERT_HEAD(head, ctr_type, elm, field)           \
    do {                                                           \
        __typeof__(ctr_type) *ctr = (__typeof__(ctr_type) *)(elm); \
        __typeof__(ctr_type) *head_ctr =                           \
                (__typeof__(ctr_type) *)((head)->tlh_first);       \
        ctr->field.tle_next = (head)->tlh_first;                   \
        if (ctr->field.tle_next != NULL)                           \
            head_ctr->field.tle_prev = &ctr->field.tle_next;       \
        (head)->tlh_first = (elm);                                 \
        ctr->field.tle_prev =                                      \
            ((__typeof__(ctr_type) **)(&(head)->tlh_first));       \
} while (0)

#define TMR_LIST_REMOVE(ctr_type, elm, field)                      \
    do {                                                           \
        __typeof__(ctr_type) *ctr = (__typeof__(ctr_type) *)(elm); \
        if (ctr->field.tle_next != NULL) {                         \
            __typeof__(ctr_type) *nxt =                            \
                (__typeof__(ctr_type) *)ctr->field.tle_next;       \
            nxt->field.tle_prev = ctr->field.tle_prev;             \
        }                                                          \
        *ctr->field.tle_prev = ctr->field.tle_next;                \
} while (0)

typedef tmr_list_head_t tmr_wheel_bucket_t;

typedef struct tmr_wheel_s {

    uint32_t            tw_size;

    /* Step in useconds. */
    uint32_t            tw_step;
    uint32_t            tw_current;

    uint64_t            tw_last_advance;

    tmr_wheel_bucket_t *tw_wheel;

} tmr_wheel_t;

/*****************************************************************************
 * External's for tpg_tcp_timer.c
 ****************************************************************************/
extern bool timer_init(void);
extern void timer_lcore_init(uint32_t lcore_id);

extern void time_advance(void);

extern int  tcp_timer_rto_set(l4_control_block_t *l4_cb, uint32_t timeout_us);
extern int  tcp_timer_rto_cancel(l4_control_block_t *l4_cb);

extern int  tcp_timer_slow_set(l4_control_block_t *l4_cb, uint32_t timeout_us);
extern int  tcp_timer_slow_cancel(l4_control_block_t *l4_cb);

extern int  l4cb_timer_test_set(l4_control_block_t *l4_cb, uint32_t timeout_us);
extern int  l4cb_timer_test_cancel(l4_control_block_t *l4_cb);
extern void timer_total_stats_clear(uint32_t port);

#endif /* _H_TPG_TIMER_ */

