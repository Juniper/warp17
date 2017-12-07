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
 *     tpg_tcp_lookup.h
 *
 * Description:
 *     TCP session lookup functions
 *
 * Author:
 *     Dumitru Ceara, Eelco Chaudron
 *
 * Initial Created:
 *     06/29/2015
 *
 * Notes:
 *
 */

/*****************************************************************************
 * Multiple include protection
 ****************************************************************************/
#ifndef _H_TPG_TCP_LOOKUP_
#define _H_TPG_TCP_LOOKUP_

/*****************************************************************************
 * Defines for tcp lookup.
 ****************************************************************************/
#define TCB_CHECK(tcb) (L4_CB_CHECK(&(tcb)->tcb_l4))

/*****************************************************************************
 * External's for tpg_tcp_lookup.c
 ****************************************************************************/
extern int                  tlkp_add_tcb(tcp_control_block_t *tcb);
extern int                  tlkp_delete_tcb(tcp_control_block_t *tcb);
extern tcp_control_block_t *tlkp_find_v4_tcb(uint32_t phys_port, uint32_t l4_hash,
                                             uint32_t src_addr, uint32_t dst_addr,
                                             uint16_t src_port, uint16_t dst_port);
extern void                 tlkp_walk_tcb(uint32_t phys_port,
                                          tlkp_walk_v4_cb_t callback,
                                          void *arg);

extern bool                 tlkp_tcp_init(void);
extern void                 tlkp_tcp_lcore_init(uint32_t lcore_id);
extern void                 tlkp_preinit_tcb(struct rte_mempool *mp,
                                             void *opaque_arg,
                                             void *ptr,
                                             unsigned index);
extern tcp_control_block_t *tlkp_alloc_tcb(void);
extern void                 tlkp_init_tcb(tcp_control_block_t *tcb,
                                          uint32_t local_addr, uint32_t remote_addr,
                                          uint16_t local_port, uint16_t remote_port,
                                          uint32_t l4_hash, uint32_t tcb_interface,
                                          uint32_t test_case_id, tpg_app_proto_t app_id,
                                          sockopt_t *sockopt,
                                          uint32_t flags);
extern void                 tlkp_init_tcb_client(tcp_control_block_t *tcb,
                                                 uint32_t local_addr,
                                                 uint32_t remote_addr,
                                                 uint16_t local_port,
                                                 uint16_t remote_port,
                                                 uint32_t l4_hash,
                                                 uint32_t tcb_interface,
                                                 uint32_t test_case_id,
                                                 tpg_app_proto_t app_id,
                                                 sockopt_t *sockopt,
                                                 uint32_t flags);
extern void                 tlkp_free_tcb(tcp_control_block_t *tcb);
extern unsigned int         tlkp_total_tcbs_allocated(void);

#endif /* _H_TPG_TCP_LOOKUP_ */

