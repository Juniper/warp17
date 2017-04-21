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
 *     tpg_route.h
 *
 * Description:
 *     Route management and lookup.
 *
 * Author:
 *     Dumitru Ceara, Eelco Chaudron
 *
 * Initial Created:
 *     08/24/2015
 *
 * Notes:
 *     For now only static default gw.
 */

/*****************************************************************************
 * Multiple include protection
 ****************************************************************************/
#ifndef _H_TPG_ROUTE_
#define _H_TPG_ROUTE_

/*****************************************************************************
 * ROUTE statistics
 ****************************************************************************/
typedef struct route_statistics_s {

    uint32_t rs_intf_add;
    uint32_t rs_intf_del;
    uint32_t rs_gw_add;
    uint32_t rs_gw_del;

    /* Error counters */
    uint32_t rs_tbl_full;
    uint32_t rs_intf_nomem;
    uint32_t rs_intf_notfound;
    uint32_t rs_gw_nointf;
    uint32_t rs_nh_not_found;
    uint32_t rs_route_not_found;

} route_statistics_t;

/*****************************************************************************
 * Route module message types.
 ****************************************************************************/
enum route_msg_types {

    MSG_TYPE_DEF_START_MARKER(ROUTE),
    MSG_ROUTE_INTF_ADD,
    MSG_ROUTE_INTF_DEL,
    MSG_ROUTE_GW_ADD,
    MSG_ROUTE_GW_DEL,
    MSG_TYPE_DEF_END_MARKER(ROUTE),

};

MSG_TYPE_MAX_CHECK(ROUTE);

/*****************************************************************************
 * Route module message definitions.
 ****************************************************************************/
typedef struct route_intf_msg_s {

    uint32_t rim_eth_port;
    tpg_ip_t rim_ip;
    tpg_ip_t rim_mask;

} __tpg_msg route_intf_msg_t;

typedef route_intf_msg_t route_intf_add_msg_t;
typedef route_intf_msg_t route_intf_del_msg_t;

typedef struct route_gw_msg_s {

    uint32_t rgm_eth_port;
    tpg_ip_t rgm_gw;

} __tpg_msg route_gw_msg_t;

typedef route_gw_msg_t route_gw_add_msg_t;
typedef route_gw_msg_t route_gw_del_msg_t;

/*****************************************************************************
 * Type definitions for tpg_route.
 ****************************************************************************/
typedef struct route_entry_s {

    tpg_ip_t re_net;
    tpg_ip_t re_mask;
    tpg_ip_t re_nh;

    uint32_t re_flags;
} route_entry_t;

#define ROUTE_FLAG_IN_USE 0x00000001
#define ROUTE_FLAG_LOCAL  0x00000002

#define ROUTE_IS_FLAG_SET(re, flag) (((re)->re_flags & (flag)) != 0)

#define ROUTE_V4(ip, msk, nhop, flags) \
    ((route_entry_t) {                 \
        .re_net = TPG_IPV4(ip),        \
        .re_mask = TPG_IPV4(msk),      \
        .re_nh = TPG_IPV4(nhop),       \
        .re_flags = (flags),           \
    })

/* TODO */
#define TPG_ROUTE_PORT_TABLE_SIZE (TPG_ARP_PORT_TABLE_SIZE / 2)

/*****************************************************************************
 * External's for tpg_route.c
 ****************************************************************************/
extern bool           route_init(void);
extern void           route_lcore_init(uint32_t lcore_id);

extern int            route_v4_intf_add(uint32_t port, tpg_ip_t ip,
                                        tpg_ip_t mask);
extern int            route_v4_intf_del(uint32_t port, tpg_ip_t ip,
                                        tpg_ip_t mask);
extern int            route_v4_gw_add(uint32_t port, tpg_ip_t gw);
extern int            route_v4_gw_del(uint32_t port, tpg_ip_t gw);
extern uint64_t       route_v4_nh_lookup(uint32_t port, uint32_t dest);
extern route_entry_t *route_v4_find_local(uint32_t port, uint32_t dest);
extern void           route_total_stats_clear(uint32_t port);

#endif /* _H_TPG_ROUTE_ */

