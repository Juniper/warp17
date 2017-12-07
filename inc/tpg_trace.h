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
 *     tpg_trace.h
 *
 * Description:
 *     In memory tracing module interface.
 *
 * Author:
 *     Dumitru Ceara, Eelco Chaudron
 *
 * Initial Created:
 *     05/19/2015
 *
 * Notes:
 *
 */

/*****************************************************************************
 * Multiple include protection
 ****************************************************************************/
#ifndef _H_TPG_TRACE_
#define _H_TPG_TRACE_

/*****************************************************************************
 * Trace buffer component definition
 ****************************************************************************/
typedef char * (*trace_printer_fmt_cb_t)(const char *data, uint32_t len);

/*****************************************************************************
 * Trace buffer definition (one circular buffer per component per core)
 ****************************************************************************/
typedef struct trace_buffer_s {

    /* Bit field for trace_buf_flags: */
    uint32_t  tb_enabled :1;
    uint32_t  tb_filled  :1;

    uint32_t  tb_size;
    uint32_t  tb_start;
    uint32_t  tb_end;

    uint32_t  tb_lvl;
    char     *tb_buf;

} __rte_cache_aligned trace_buffer_t;

typedef struct trace_comp_s {

    uint32_t                tc_comp_id;
    trace_buffer_t         *tc_buffers;
    trace_printer_fmt_cb_t  tc_fmt;

    uint32_t                tc_disabled :1;

} __rte_cache_aligned trace_comp_t;

typedef enum trace_level_s {
    TRACE_LVL_CRIT,
    TRACE_LVL_ERROR,
    TRACE_LVL_INFO,
    TRACE_LVL_LOG,
    TRACE_LVL_DEBUG,
    TRACE_LVL_MAX
} trace_level_t;

/*****************************************************************************
 * Trace helper macros.
 ****************************************************************************/
#define TRACE_BUF_GET_LEVEL(tb) ((tb)->tb_lvl)
#define TRACE_BUF_SET_LEVEL(tb, lvl)   \
    do {                               \
        assert((lvl) < TRACE_LVL_MAX); \
        (tb)->tb_lvl = (lvl);          \
    } while (0)

/*****************************************************************************
 * Trace external variables.
 ****************************************************************************/
extern bool         trace_disabled;
extern trace_comp_t trace_components[TRACE_MAX];

/*****************************************************************************
 * Trace inline functions.
 *****************************************************************************
 * trace_level_str()
 ****************************************************************************/
static inline __attribute__((always_inline))
const char *trace_level_str(trace_level_t lvl)
{
    switch (lvl) {
    case TRACE_LVL_CRIT:  return "CRI";
    case TRACE_LVL_ERROR: return "ERR";
    case TRACE_LVL_LOG:   return "LOG";
    case TRACE_LVL_INFO:  return "INF";
    case TRACE_LVL_DEBUG: return "DBG";
    default:
        return "Unknown";
    }
    return "Unknown";
}

/*****************************************************************************
 * trace_get_comp_pointer()
 ****************************************************************************/
static inline __attribute__((always_inline))
trace_comp_t *trace_get_comp_pointer(uint32_t id)
{
    if (unlikely(id >= TRACE_MAX))
        return NULL;

    return &trace_components[id];
}

/*****************************************************************************
 * trace_get_pointer_from_comp()
 ****************************************************************************/
static inline __attribute__((always_inline))
trace_buffer_t *trace_get_pointer_from_comp(const trace_comp_t *tc,
                                            int lcore_idx)
{
    if (unlikely(tc == NULL))
        return NULL;

    return &tc->tc_buffers[lcore_idx];
}

/*****************************************************************************
 * trace_get_pointer()
 ****************************************************************************/
static inline __attribute__((always_inline))
trace_buffer_t *trace_get_pointer(uint32_t id, int lcore_idx)
{
    return trace_get_pointer_from_comp(trace_get_comp_pointer(id), lcore_idx);
}

/*****************************************************************************
 * trace_allowed()
 ****************************************************************************/
static inline __attribute__((always_inline))
bool trace_allowed(int lcore_idx, uint32_t trace_buf_id, trace_level_t level)
{
    trace_comp_t   *tc;
    trace_buffer_t *tb;

    if (likely(trace_disabled))
        return false;

    tc = trace_get_comp_pointer(trace_buf_id);

    if (unlikely(!tc))
        return false;

    if (likely(tc->tc_disabled))
        return false;

    tb = trace_get_pointer_from_comp(tc, lcore_idx);
    if (likely(!tb->tb_enabled))
        return false;

    if (likely(level > TRACE_BUF_GET_LEVEL(tb)))
        return false;

    return true;
}

/*****************************************************************************
 * Tracing API
 ****************************************************************************/
extern bool          trace_init(void);
extern uint32_t      trace_get_component_cnt(void);
extern trace_comp_t *trace_get_comp_pointer_by_name(const char *name);

/* Configuration */
extern void          trace_set_core_level(const trace_comp_t *tc, int lcore,
                                          const char *lvl);
extern const char   *trace_get_core_level(const trace_comp_t *tc, int lcore);
extern void          trace_set_core_bufsize(const trace_comp_t *tc, int lcore,
                                            uint32_t bufsize);
extern uint32_t      trace_get_core_bufsize(const trace_comp_t *tc, int lcore);
extern bool          trace_get_core_enabled(const trace_comp_t *tc, int lcore);

/* Adding traces. */
extern int           trace_add_raw(int lcore_idx, uint32_t trace_buf_id,
                                   trace_level_t level,
                                   const void *data,
                                   uint32_t datalen);

extern int           trace_add_formatted(int lcore_idx, uint32_t trace_buf_id,
                                         trace_level_t level,
                                         const char *fmt,
                                         ...)
                     __attribute__ ((format (printf, 4, 5)));

typedef void (*trace_printer_cb_t)(void *arg, trace_level_t lvl,
                                   const char *comp_name,
                                   const char *data, uint32_t len);

/* Dumping traces from raw buffers. */
extern void trace_entry_dump(trace_printer_cb_t printer,
                             void *printer_arg,
                             trace_printer_fmt_cb_t fmt,
                             const char *comp_name,
                             const char *buffer,
                             uint32_t bufsize,
                             uint32_t *start_id,
                             uint32_t *start_pos,
                             uint32_t end_pos);

/* Trace component iterator stuff. */
typedef void (*trace_comp_it_cb_t)(const trace_comp_t *tc, void *data);

/* Iterates through trace components. Most definitely the callback shouldn't
 * try to change the trace_comp_t elements. Also it shouldn't be called in fast
 * path!
 */
extern void trace_comp_iterate(trace_comp_it_cb_t cb, void *data);

/* Actual tracing is done only if support is compiled in. */
#if defined(TPG_DBG_TRACE)

#define TRACE(comp, lvl, data, datalen)                          \
    (unlikely(trace_allowed(rte_lcore_index(-1), TRACE_ ## comp, \
                            TRACE_LVL_ ## lvl)) ?                \
        trace_add_raw(rte_lcore_index(-1), TRACE_ ## comp,       \
                      TRACE_LVL_ ## lvl, (data), (datalen)) :    \
        0)

#define TRACE_FMT(comp, lvl, fmt, ...)                               \
    (unlikely(trace_allowed(rte_lcore_index(-1), TRACE_ ## comp,     \
                            TRACE_LVL_ ## lvl)) ?                    \
        trace_add_formatted(rte_lcore_index(-1), TRACE_ ## comp,     \
                            TRACE_LVL_ ## lvl, (fmt), __VA_ARGS__) : \
        0)

#else /* defined(TPG_DBG_TRACE) */

#define TRACE(comp, lvl, data, datalen) \
    do {                                \
        RTE_SET_USED(data);             \
        RTE_SET_USED(datalen);          \
    } while (0)

#define TRACE_FMT(comp, lvl, fmt, ...) \
    RTE_SET_USED(fmt)

#endif /* defined(TPG_DBG_TRACE) */

#endif /* _H_TPG_TRACE_ */

