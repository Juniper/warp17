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
 *     tpg_trace_filter.h
 *
 * Description:
 *     Trace filters (apply to PCB and L4_CB too).
 *
 * Author:
 *     Dumitru Ceara, Eelco Chaudron
 *
 * Initial Created:
 *     07/28/2015
 *
 * Notes:
 *
 */

/*****************************************************************************
 * Multiple include protection
 ****************************************************************************/
#ifndef _H_TPG_TRACE_FILTER_
#define _H_TPG_TRACE_FILTER_

/*****************************************************************************
 * Trace filters
 ****************************************************************************/
#define FILTER_INTERFACE_ANY    0xFFFFFFFF

typedef struct trace_filter_s {

    uint16_t tf_tcb_state;

    uint32_t tf_interface;
    int      tf_domain;
    uint16_t tf_src_port;
    uint16_t tf_dst_port;

    tpg_ip_t tf_src_addr;
    tpg_ip_t tf_dst_addr;

    bool     tf_enabled;

} trace_filter_t;

/*****************************************************************************
 * TRACE FILTER module message type codes.
 ****************************************************************************/
enum trace_filter_control_msg_types {

    MSG_TYPE_DEF_START_MARKER(TRACE_FILTER),
    MSG_TRACE_FILTER,
    MSG_TYPE_DEF_END_MARKER(TRACE_FILTER),

};

MSG_TYPE_MAX_CHECK(TRACE_FILTER);

/*****************************************************************************
 * Trace Filter module message type definitions.
 ****************************************************************************/
typedef struct trace_filter_msg_s {

    trace_filter_t tfm_filter;

} __tpg_msg trace_filter_msg_t;

/*****************************************************************************
 * Externs
 ****************************************************************************/
RTE_DECLARE_PER_LCORE(trace_filter_t, trace_filter);

extern bool trace_filter_init(void);
extern void trace_filter_lcore_init(uint32_t lcore_id);


/*****************************************************************************
 * Packet trace MACRO
 ****************************************************************************/
#define PKT_TRACE(pcb, comp, lvl, fmt, ...) do { \
    if (PKT_TRACE_ENABLED(pcb)) {                \
        TRACE_FMT(comp, lvl, fmt, __VA_ARGS__);  \
    }                                            \
} while (0)

#define PKT_TRACE_ENABLED(pcb) \
    (unlikely(RTE_PER_LCORE(trace_filter).tf_enabled) ? (pcb)->pcb_trace : true)

/*****************************************************************************
 * TCB trace MACRO
 ****************************************************************************/
#define TCB_TRACE_ENABLED(tcb) \
    (unlikely(RTE_PER_LCORE(trace_filter).tf_enabled) ? (tcb)->tcb_trace : true)

#define TCB_TRACE(tcb, comp, lvl, fmt, ...) do { \
    if (TCB_TRACE_ENABLED(tcb)) {                \
        TRACE_FMT(comp, lvl, fmt, __VA_ARGS__);  \
    }                                            \
} while (0)

/*****************************************************************************
 * Externs for tpg_trace_filter.c
 ****************************************************************************/
extern bool trace_filter_match(trace_filter_t *filter, uint32_t interface,
                               int domain,
                               uint16_t src_port,
                               uint16_t dst_port,
                               tpg_ip_t src_ip,
                               tpg_ip_t dst_ip);

#endif /* _H_TPG_TRACE_FILTER_ */

