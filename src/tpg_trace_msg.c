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
 *     tpg_trace_msg.c
 *
 * Description:
 *     In memory tracing module messaging interface.
 *
 * Author:
 *     Dumitru Ceara, Eelco Chaudron
 *
 * Initial Created:
 *     06/01/2015
 *
 * Notes:
 *
 */

/*****************************************************************************
 * Include files
 ***************************************************************************/
#include "tcp_generator.h"

/*****************************************************************************
 * trace_send_enable()
 ***************************************************************************/
int trace_send_enable(uint32_t trace_buf_id, unsigned lcore_dest,
                      uint32_t trace_buf_size)
{
    int           error;
    char         *newbuf;
    trace_comp_t *tc;
    msg_t        *msgp;
    MSG_LOCAL_DEFINE(trace_enable_msg_t, msg);

    newbuf = rte_malloc("trace_buffer", trace_buf_size, 0);
    if (!newbuf)
        return -ENOMEM;

    msgp = MSG_LOCAL(msg);
    msg_init(msgp, MSG_TRACE_ENABLE, lcore_dest, 0);

    MSG_INNER(trace_enable_msg_t, msgp)->tem_trace_buf_id = trace_buf_id;
    MSG_INNER(trace_enable_msg_t, msgp)->tem_newbuf = newbuf;

    /* This will block and wait for the message to be processed! */
    error = msg_send(msgp, 0);

    /* Mark tracing enabled globally too. */
    trace_disabled = false;
    tc = trace_get_comp_pointer(trace_buf_id);
    if (tc != NULL)
        tc->tc_disabled = false;

    return error;
}

/*****************************************************************************
 * trace_send_disable()
 ***************************************************************************/
int trace_send_disable(uint32_t trace_buf_id, unsigned lcore_dest)
{
    trace_comp_t *tc;
    char         *oldbuf;
    msg_t        *msgp;
    int           error;
    bool          any_trace_core_enabled = false;
    bool          any_trace_comp_enabled = false;
    uint32_t      core;
    MSG_LOCAL_DEFINE(trace_disable_msg_t, msg);

    void trace_check_enabled(const trace_comp_t *tc_it, void *data __rte_unused)
    {
        if (!tc_it->tc_disabled)
            any_trace_comp_enabled = true;
    }

    msgp = MSG_LOCAL(msg);
    msg_init(msgp, MSG_TRACE_DISABLE, lcore_dest, 0);

    MSG_INNER(trace_disable_msg_t, msgp)->tdm_trace_buf_id = trace_buf_id;
    MSG_INNER(trace_disable_msg_t, msgp)->tdm_oldbuf = &oldbuf;

    /* This blocks and waits until the message is processed! */
    error = msg_send(msgp, 0);
    if (error)
        return error;

    rte_free(oldbuf);

    /* Check to see if tracing is completely disabled for this component */
    RTE_LCORE_FOREACH_SLAVE(core) {
        trace_buffer_t *tb;

        if (cfg_is_cli_core(core))
            continue;

        tb = trace_get_pointer(trace_buf_id, rte_lcore_index(core));
        if (tb && tb->tb_enabled) {
            any_trace_core_enabled = true;
            break;
        }
    }

    /* If tracing enabled for this component at least on one core then don't
     * change the global tracing flag.
     */
    if (any_trace_core_enabled)
        return 0;

    /* Otherwise mark this component as disabled for tracing. */
    tc = trace_get_comp_pointer(trace_buf_id);
    if (tc)
        tc->tc_disabled = true;

    /* Iterate through all components and check if there's at least one that
     * is enabled. If not then disable tracing globally.
     */
    trace_comp_iterate(trace_check_enabled, NULL);
    if (any_trace_comp_enabled)
        return 0;

    trace_disabled = true;
    return 0;
}

/*****************************************************************************
 * trace_send_xchg_ptr()
 ***************************************************************************/
int trace_send_xchg_ptr(uint32_t trace_buf_id, unsigned lcore_dest,
                        char **oldbuf, uint32_t *bufsize,
                        uint32_t *start_id, uint32_t *start_pos,
                        uint32_t *end_pos, uint32_t trace_buf_size)
{
    char  *newbuf = NULL;
    msg_t *msgp;
    MSG_LOCAL_DEFINE(trace_xchg_ptr_msg_t, msg);

    if (trace_buf_size) {
        newbuf = rte_malloc("trace_buffer", trace_buf_size, 0);
        if (!newbuf)
            return -ENOMEM;
    }

    msgp = MSG_LOCAL(msg);
    msg_init(msgp, MSG_TRACE_XCHG_PTR, lcore_dest, 0);

    MSG_INNER(trace_xchg_ptr_msg_t, msgp)->txpm_trace_buf_id = trace_buf_id;
    MSG_INNER(trace_xchg_ptr_msg_t, msgp)->txpm_newbuf = newbuf;
    MSG_INNER(trace_xchg_ptr_msg_t, msgp)->txpm_oldbuf = oldbuf;
    MSG_INNER(trace_xchg_ptr_msg_t, msgp)->txpm_bufsize = bufsize;
    MSG_INNER(trace_xchg_ptr_msg_t, msgp)->txpm_start_id = start_id;
    MSG_INNER(trace_xchg_ptr_msg_t, msgp)->txpm_start_pos = start_pos;
    MSG_INNER(trace_xchg_ptr_msg_t, msgp)->txpm_end_pos = end_pos;

    /* This blocks and waits until the message is processed! */
    return msg_send(msgp, 0);
}

