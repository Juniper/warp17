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
 *     tpg_trace_msg.h
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
 * Multiple include protection
 ****************************************************************************/
#ifndef _H_TPG_TRACE_MSG_
#define _H_TPG_TRACE_MSG_

/*****************************************************************************
 * Trace module message type codes.
 ****************************************************************************/
enum trace_control_msg_types {

    MSG_TYPE_DEF_START_MARKER(TRACE),
    MSG_TRACE_ENABLE,
    MSG_TRACE_DISABLE,
    MSG_TRACE_XCHG_PTR,
    MSG_TRACE_MAX_MSG,
    MSG_TYPE_DEF_END_MARKER(TRACE),

};

MSG_TYPE_MAX_CHECK(TRACE);

/*****************************************************************************
 * Trace module message type definitions.
 ****************************************************************************/
typedef struct trace_enable_msg_s {

    uint32_t  tem_trace_buf_id;
    char     *tem_newbuf;

} __tpg_msg trace_enable_msg_t;

typedef struct trace_disable_msg_s {

    uint32_t   tdm_trace_buf_id;
    char     **tdm_oldbuf;

} __tpg_msg trace_disable_msg_t;

typedef struct trace_xchg_ptr_msg_s {

    uint32_t    txpm_trace_buf_id;
    char       *txpm_newbuf;
    char      **txpm_oldbuf;
    uint32_t   *txpm_bufsize;
    uint32_t   *txpm_start_id;
    uint32_t   *txpm_start_pos;
    uint32_t   *txpm_end_pos;

} __tpg_msg trace_xchg_ptr_msg_t;

/*****************************************************************************
 * Externals for tpg_trace_msg.c.
 ****************************************************************************/
extern int trace_send_enable(uint32_t trace_buf_id,
                             unsigned lcore,
                             uint32_t trace_buf_size);
extern int trace_send_disable(uint32_t trace_buf_id, unsigned lcore);
extern int trace_send_xchg_ptr(uint32_t trace_buf_id,
                               unsigned lcore,
                               char **oldbuf,
                               uint32_t *bufsize,
                               uint32_t *start_id,
                               uint32_t *start_pos,
                               uint32_t *end_pos,
                               uint32_t trace_buf_size);

#endif /*_H_TPG_TRACE_MSG_ */

