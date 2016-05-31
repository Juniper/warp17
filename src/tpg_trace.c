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
 *     tpg_trace.c
 *
 * Description:
 *     In memory tracing module
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
 * Include files
 ***************************************************************************/
#include "tcp_generator.h"

/*****************************************************************************
 * Internal type definitions.
 ****************************************************************************/

/*****************************************************************************
 * Trace entry definition:
 * ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
 * +            |                                          |          |
 * + ID (32bit) | Len(32 bit including ID & length fields) | Raw data |
 * +            |                                          |          |
 * ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
 ****************************************************************************/
typedef struct trace_entry_hdr_s {

    uint32_t      teh_id;
    uint32_t      teh_len;
    trace_level_t teh_lvl;

} trace_entry_hdr_t;

typedef struct trace_entry_s {

    trace_entry_hdr_t te_hdr;
    char              te_buf[0];

} trace_entry_t;

/*****************************************************************************
 * Global variables
 ****************************************************************************/
bool         trace_disabled = true;
trace_comp_t trace_components[TRACE_MAX];

/*****************************************************************************
 * trace_init_component()
 ****************************************************************************/
static int trace_init_component(uint32_t trace_id)
{
    trace_comp_t *tc;
    uint32_t      i;

    if (trace_id >= TRACE_MAX)
        return -EINVAL;

    tc = &trace_components[trace_id];

    tc->tc_comp_id = trace_id;
    /* To be set later if needed (through an API). */
    tc->tc_fmt = NULL;

    tc->tc_buffers = rte_zmalloc("trace_buffer",
                                 rte_lcore_count() * sizeof(*tc->tc_buffers),
                                 0);
    if (!tc->tc_buffers)
        return -ENOMEM;

    for (i = 0; i < rte_lcore_count(); i++)
        TRACE_BUF_SET_LEVEL(&tc->tc_buffers[i], TRACE_LVL_LOG);

    return 0;
}

/*****************************************************************************
 * trace_get_uniq_id()
 ****************************************************************************/
static uint32_t trace_get_uniq_id(void)
{
    static RTE_DEFINE_PER_LCORE(uint32_t, trace_id);

    /* Reserv 0 as invalid-id. */
    RTE_PER_LCORE(trace_id)++;
    if (unlikely(RTE_PER_LCORE(trace_id) == 0))
        RTE_PER_LCORE(trace_id)++;
    return RTE_PER_LCORE(trace_id);
}

/*****************************************************************************
 * trace_xchg_ptr()
 ****************************************************************************/
static void trace_xchg_ptr(trace_buffer_t *tb, char *newbuf, char **oldbuf)
{
    if (oldbuf)
        *oldbuf = tb->tb_buf;

    tb->tb_buf = newbuf;
}

/*****************************************************************************
 * trace_handle_enable()
 ****************************************************************************/
static int trace_handle_enable(uint16_t msgid, uint16_t lcore, void *msg)
{
    trace_buffer_t     *tb;
    trace_enable_msg_t *ena_msg;

    if (MSG_INVALID(msgid, msg, MSG_TRACE_ENABLE))
        return -EINVAL;

    ena_msg = (trace_enable_msg_t *)msg;

    tb = trace_get_pointer(ena_msg->tem_trace_buf_id, rte_lcore_index(lcore));
    if (unlikely(!tb))
        return -ENOENT;

    if (tb->tb_enabled)
        return 0;

    trace_xchg_ptr(tb, ena_msg->tem_newbuf, NULL);

    /* No need to check that the state has been reset.. */

    tb->tb_enabled = true;
    tb->tb_filled = false;
    return 0;
}

/*****************************************************************************
 * trace_handle_disable()
 ****************************************************************************/
static int trace_handle_disable(uint16_t msgid, uint16_t lcore, void *msg)
{
    trace_buffer_t      *tb;
    trace_disable_msg_t *dis_msg;

    if (MSG_INVALID(msgid, msg, MSG_TRACE_DISABLE))
        return -EINVAL;

    dis_msg = (trace_disable_msg_t *)msg;

    tb = trace_get_pointer(dis_msg->tdm_trace_buf_id, rte_lcore_index(lcore));
    if (unlikely(!tb))
        return -ENOENT;

    if (!tb->tb_enabled)
        return 0;

    trace_xchg_ptr(tb, NULL, dis_msg->tdm_oldbuf);

    /* No need to check that the state has been reset.. */

    tb->tb_enabled = false;
    tb->tb_filled = false;
    tb->tb_start = tb->tb_end = 0;
    return 0;
}

/*****************************************************************************
 * trace_entry_hdr_peek_buf()
 ****************************************************************************/
static void trace_entry_hdr_peek_buf(const char *buffer, uint32_t bufsize,
                                     uint32_t start_pos, uint32_t *id,
                                     trace_level_t *lvl, uint32_t *len)
{
    trace_entry_hdr_t        local_teh;
    const trace_entry_hdr_t *teh;

    if (unlikely(bufsize - start_pos < sizeof(*teh))) {
        rte_memcpy(&local_teh, &buffer[start_pos], bufsize - start_pos);
        rte_memcpy(&((char *)&local_teh)[bufsize - start_pos], buffer,
            sizeof(*teh) - (bufsize - start_pos));
        teh = &local_teh;
    } else {
        teh = (const trace_entry_hdr_t *)&buffer[start_pos];
    }
    if (id)
        *id = teh->teh_id;
    if (lvl)
        *lvl = teh->teh_lvl;
    if (len)
        *len = teh->teh_len;
}

/*****************************************************************************
 * trace_handle_xchg_ptr()
 ****************************************************************************/
static int trace_handle_xchg_ptr(uint16_t msgid, uint16_t lcore, void *msg)
{
    trace_xchg_ptr_msg_t *xchg_msg;
    trace_buffer_t       *tb;

    if (MSG_INVALID(msgid, msg, MSG_TRACE_XCHG_PTR))
        return -EINVAL;

    xchg_msg = (trace_xchg_ptr_msg_t *)msg;

    tb = trace_get_pointer(xchg_msg->txpm_trace_buf_id, rte_lcore_index(lcore));
    if (unlikely(!tb))
        return -ENOENT;

    if (!tb->tb_enabled)
        return 0;

    *xchg_msg->txpm_start_pos = tb->tb_start;
    *xchg_msg->txpm_end_pos = tb->tb_end;

    if (!tb->tb_filled && tb->tb_start == tb->tb_end)
        *xchg_msg->txpm_start_id = 0;
    else
        trace_entry_hdr_peek_buf(tb->tb_buf, tb->tb_size, tb->tb_start,
                                 xchg_msg->txpm_start_id, NULL, NULL);

    trace_xchg_ptr(tb, xchg_msg->txpm_newbuf, xchg_msg->txpm_oldbuf);

    *xchg_msg->txpm_bufsize = tb->tb_size;

    tb->tb_filled = false;
    tb->tb_start = tb->tb_end = 0;
    return 0;
}

/*****************************************************************************
 * trace_init()
 ****************************************************************************/
bool trace_init(void)
{
    int      error;
    uint32_t i;

    /*
     * Add tracing module CLI commands
     */
    if (!trace_cli_init()) {
        RTE_LOG(ERR, USER1, "ERROR: Can't add tracing specific CLI commands!\n");
        return false;
    }

    /*
     * Register the handlers for our message types.
     */
    error = msg_register_handler(MSG_TRACE_ENABLE, trace_handle_enable);
    if (error) {
        RTE_LOG(ERR, USER1, "Failed to register Trace msg handler: %s(%d)\n",
                rte_strerror(-error), -error);
        return false;
    }

    error = msg_register_handler(MSG_TRACE_DISABLE,
                                 trace_handle_disable);
    if (error) {
        RTE_LOG(ERR, USER1, "Failed to register Trace msg handler: %s(%d)\n",
                rte_strerror(-error), -error);
        return false;
    }

    error = msg_register_handler(MSG_TRACE_XCHG_PTR,
                                 trace_handle_xchg_ptr);
    if (error) {
        RTE_LOG(ERR, USER1, "Failed to register Trace msg handler: %s(%d)\n",
                rte_strerror(-error), -error);
        return false;
    }

    /* Initialize the defined trace components. */
    for (i = 0; i < TRACE_MAX; i++) {
        error = trace_init_component(i);
        if (error) {
            RTE_LOG(ERR, USER1, "Failed to init Trace component: %s(%d)\n",
                    rte_strerror(-error), -error);
            return false;
        }
    }

    return true;
}

/*****************************************************************************
 * trace_get_comp_pointer_by_name()
 ****************************************************************************/
trace_comp_t *trace_get_comp_pointer_by_name(const char *name)
{
    uint32_t i;

    for (i = 0; i < TRACE_MAX; i++) {
        const char *cfg_name = cfg_get_gtrace_name(i);

        if (!strncmp(name, cfg_name, strlen(cfg_name) + 1))
            return &trace_components[i];
    }
    return NULL;
}

/*****************************************************************************
 * trace_set_core_level()
 ****************************************************************************/
void trace_set_core_level(const trace_comp_t *tc, int lcore, const char *lvl)
{
    trace_buffer_t *tb;
    trace_level_t   tlevel;

    if (!tc)
        return;

    tb = trace_get_pointer(tc->tc_comp_id, rte_lcore_index(lcore));
    if (!tb)
        return;

    if (!strncmp(lvl, "CRIT", strlen("CRIT") + 1)) {
        tlevel = TRACE_LVL_CRIT;
    } else if (!strncmp(lvl, "ERR", strlen("ERR") + 1)) {
        tlevel = TRACE_LVL_ERROR;
    } else if (!strncmp(lvl, "INFO", strlen("INFO") + 1)) {
        tlevel = TRACE_LVL_INFO;
    } else if (!strncmp(lvl, "LOG", strlen("LOG") + 1)) {
        tlevel = TRACE_LVL_LOG;
    } else if (!strncmp(lvl, "DEBUG", strlen("DEBUG") + 1)) {
        tlevel = TRACE_LVL_DEBUG;
    } else {
        assert(0);
        return;
    }

    TRACE_BUF_SET_LEVEL(tb, tlevel);
}

/*****************************************************************************
 * trace_get_core_level()
 ****************************************************************************/
const char *trace_get_core_level(const trace_comp_t *tc, int lcore)
{
    trace_buffer_t *tb;

    if (!tc)
        return "Unknown";

    tb = trace_get_pointer(tc->tc_comp_id, rte_lcore_index(lcore));
    if (!tb)
        return "Unknown";

    return trace_level_str(tb->tb_lvl);
}

/*****************************************************************************
 * trace_set_core_bufsize()
 ****************************************************************************/
void trace_set_core_bufsize(const trace_comp_t *tc, int lcore,
                            uint32_t bufsize)
{
    trace_buffer_t *tb;

    if (!tc)
        return;

    tb = trace_get_pointer(tc->tc_comp_id, rte_lcore_index(lcore));
    if (!tb)
        return;

    tb->tb_size = bufsize;
}

/*****************************************************************************
 * trace_get_core_bufsize()
 ****************************************************************************/
uint32_t trace_get_core_bufsize(const trace_comp_t *tc, int lcore)
{
    trace_buffer_t *tb;

    if (!tc)
        return 0;

    tb = trace_get_pointer(tc->tc_comp_id, rte_lcore_index(lcore));
    if (!tb)
        return 0;

    return tb->tb_size;
}

/*****************************************************************************
 * trace_get_core_enabled()
 ****************************************************************************/
bool trace_get_core_enabled(const trace_comp_t *tc, int lcore)
{
    trace_buffer_t *tb;

    if (!tc)
        return 0;

    tb = trace_get_pointer(tc->tc_comp_id, rte_lcore_index(lcore));
    if (!tb)
        return 0;

    return tb->tb_enabled;
}

/*****************************************************************************
 * trace_add_raw()
 ****************************************************************************/
int trace_add_raw(int lcore_idx, uint32_t trace_buf_id,
                  trace_level_t level,
                  const void *data,
                  uint32_t datalen)
{
    trace_buffer_t *tb;
    trace_entry_t  *new_trace;
    uint32_t        new_trace_size;
    uint32_t        new_start;
    uint32_t        available;

    tb = trace_get_pointer(trace_buf_id, lcore_idx);
    if (unlikely(!tb))
        return -EINVAL;

    new_trace_size = datalen + sizeof(trace_entry_hdr_t);

    if (unlikely(new_trace_size > tb->tb_size))
        return -ENOSPC;

    if (unlikely(tb->tb_filled)) {
        available = 0;
    } else {
        if (tb->tb_start <= tb->tb_end) {
            available = tb->tb_size -
                (tb->tb_end - tb->tb_start);
        } else {
            available = tb->tb_start - tb->tb_end;
        }
    }

    new_start = (tb->tb_end + new_trace_size) % tb->tb_size;

    /* If overwriting the first trace (which will most likely happen
     * once the buffer is full) we need to advance the start of the
     * buffer to the next acceptable value.
     */
    if (likely(available < new_trace_size)) {
        /* Advance start */
        do {
            uint32_t start_tel;

            trace_entry_hdr_peek_buf(tb->tb_buf, tb->tb_size,
                tb->tb_start, NULL, NULL, &start_tel);
            tb->tb_start =
                (tb->tb_start + start_tel) %
                    tb->tb_size;
            available += start_tel;
        } while (available < new_trace_size);

        if (likely(tb->tb_start != tb->tb_end))
            tb->tb_filled = false;
    }

    available = tb->tb_size - tb->tb_end;

    if (likely(new_trace_size < available)) {
        /* TODO: WARNING! Misaligned access! We need to make sure that
         * this works on all platforms so we might need to add padding.
         */
        new_trace = (trace_entry_t *)&tb->tb_buf[tb->tb_end];
        new_trace->te_hdr.teh_id = trace_get_uniq_id();
        new_trace->te_hdr.teh_len = new_trace_size;
        new_trace->te_hdr.teh_lvl = level;
        rte_memcpy(&new_trace->te_buf[0], data, datalen);
    } else {
        /* Slowpath: split the copy in two. */
        char new_trace_buf[sizeof(trace_entry_hdr_t) + datalen];

        new_trace = (trace_entry_t *)&new_trace_buf[0];
        new_trace->te_hdr.teh_id = trace_get_uniq_id();
        new_trace->te_hdr.teh_len = new_trace_size;
        new_trace->te_hdr.teh_lvl = level;
        rte_memcpy(&new_trace->te_buf[0], data, datalen);

        /* Finally copy in the trace buffer. */
        rte_memcpy(&tb->tb_buf[tb->tb_end], &new_trace_buf[0], available);
        rte_memcpy(&tb->tb_buf[0], &new_trace_buf[available],
                   new_trace_size - available);
    }

    tb->tb_end = new_start;

    return 0;
}

/*****************************************************************************
 * trace_add_formatted()
 ****************************************************************************/
int trace_add_formatted(int lcore_idx, uint32_t trace_buf_id,
                        trace_level_t level,
                        const char *fmt, ...)
{
    va_list ap;
    va_list temp_ap;
    int     datalen;
    int     ret;

    va_start(ap, fmt);
    va_copy(temp_ap, ap);

    /* Trick the library to compute the length (excluding the null byte
     * terminator, therefore the +1) for us..
     */
    datalen = vsnprintf(NULL, 0, fmt, temp_ap) + 1;

    va_end(temp_ap);
    if (datalen >= 0) {
        char data[datalen];

        vsnprintf(data, datalen, fmt, ap);
        ret = trace_add_raw(lcore_idx, trace_buf_id, level, data, datalen);
    } else {
        ret = -EPERM;
    }

    va_end(ap);
    return ret;
}

/*****************************************************************************
 * trace_entry_dump()
 ****************************************************************************/
void trace_entry_dump(trace_printer_cb_t printer, void *printer_arg,
                      trace_printer_fmt_cb_t fmt, const char *comp_name,
                      const char *buffer, uint32_t bufsize, uint32_t *start_id,
                      uint32_t *start_pos, uint32_t end_pos)
{
    uint32_t      old_start = *start_pos;
    uint32_t      start_raw;
    trace_level_t te_lvl;
    uint32_t      te_len;
    uint32_t      len_to_copy;

    trace_entry_hdr_peek_buf(buffer, bufsize, old_start, start_id,
                             &te_lvl, &te_len);
    len_to_copy = te_len - offsetof(trace_entry_t, te_buf);
    start_raw = (old_start + offsetof(trace_entry_t, te_buf)) % bufsize;

    if (start_raw + len_to_copy > bufsize) {
        /* LOCAL copy! */
        char raw_entry[len_to_copy];

        rte_memcpy(raw_entry, &buffer[start_raw], bufsize - start_raw);
        rte_memcpy(&raw_entry[bufsize - start_raw], buffer,
                   len_to_copy - (bufsize - start_raw));
        if (fmt) {
            char *fmt_data = fmt(raw_entry, te_len);

            printer(printer_arg, te_lvl, comp_name, fmt_data,
                    strlen(fmt_data));
        } else
            printer(printer_arg, te_lvl, comp_name, raw_entry, te_len);

        *start_pos = len_to_copy - (bufsize - start_raw);
    } else {
        if (fmt) {
            char *fmt_data = fmt(&buffer[start_raw], te_len);

            printer(printer_arg, te_lvl, comp_name, fmt_data,
                    strlen(fmt_data));
        } else
            printer(printer_arg, te_lvl, comp_name,
                    &buffer[start_raw], te_len);

        *start_pos = (old_start + te_len) % bufsize;
    }

    /* Look at the next entry too in order to return the new id. */
    if (*start_pos != end_pos)
        trace_entry_hdr_peek_buf(buffer, bufsize, *start_pos, start_id,
                                 NULL,
                                 NULL);
}

/*****************************************************************************
 * trace_comp_iterate()
 ****************************************************************************/
void trace_comp_iterate(trace_comp_it_cb_t cb, void *data)
{
    uint32_t i;

    if (!cb)
        return;

    for (i = 0; i < TRACE_MAX; i++)
        cb(&trace_components[i], data);
}

