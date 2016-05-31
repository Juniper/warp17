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
 *     tpg_notif.h
 *
 * Description:
 *     WARP17 notifications definitions.
 *
 * Author:
 *     Dumitru Ceara, Eelco Chaudron
 *
 * Initial Created:
 *     12/14/2015
 *
 * Notes:
 *
 */

/*****************************************************************************
 * Multiple include protection
 ****************************************************************************/
#ifndef _H_TPG_NOTIF_
#define _H_TPG_NOTIF_

/*****************************************************************************
 * Notification module list. All modules that require notifications must
 * be declared here.
 ****************************************************************************/
enum notif_modules {

    NOTIF_MODULE_MIN = 1,
    NOTIF_TCP_MODULE = NOTIF_MODULE_MIN,
    NOTIF_UDP_MODULE,
    NOTIF_TEST_MODULE,
    NOTIF_MODULE_MAX

};

/*****************************************************************************
 * Notification ID macros
 ****************************************************************************/
#define NOTIF_PER_MODULE_NOTIFID_BITS (8)
#define NOTIF_PER_MODULE_MAX_NOTIFIDS (1 << NOTIF_PER_MODULE_NOTIFID_BITS)

/* Leave room for 2^8 notification types for each module. */
#define NOTIFID_INITIALIZER(module) \
    ((module) << NOTIF_PER_MODULE_NOTIFID_BITS)

/*****************************************************************************
 * TPG Notification Arguments
 ****************************************************************************/
typedef struct tcp_notif_arg_s {

    tcp_control_block_t *tnarg_tcb;

} tcp_notif_arg_t;

typedef struct udp_notif_arg_s {

    udp_control_block_t *unarg_ucb;

} udp_notif_arg_t;

typedef struct test_notif_arg_s {

    l4_control_block_t *tnarg_l4_cb;

} test_notif_arg_t;

typedef struct notif_arg_s {

    uint32_t             narg_tcid;
    uint32_t             narg_interface;

    union {
        tcp_notif_arg_t  narg_tcp;
        udp_notif_arg_t  narg_udp;
        test_notif_arg_t narg_test;
    };

} notif_arg_t;

#define TCP_NOTIF_ARG(tcb_arg, tcid, iface)                 \
    ((notif_arg_t) {.narg_tcid = (tcid),                    \
                       .narg_interface = (iface),           \
                       .narg_tcp = (tcp_notif_arg_t)        \
                                {.tnarg_tcb = (tcb_arg)} })

#define UDP_NOTIF_ARG(ucb_arg, tcid, iface)                 \
    ((notif_arg_t) {.narg_tcid = (tcid),                    \
                       .narg_interface = (iface),           \
                       .narg_udp = (udp_notif_arg_t)        \
                                {.unarg_ucb = (ucb_arg)} })

#define TEST_NOTIF_ARG(tcid, iface, cb)                  \
    ((notif_arg_t) {.narg_tcid = (tcid),                 \
                       .narg_interface = (iface),        \
                       .narg_test = (test_notif_arg_t)   \
                                {.tnarg_l4_cb = (cb)} })

/*****************************************************************************
 * Synchronous Notifications
 ****************************************************************************/
typedef void (*notif_cb_t)(uint32_t notification, notif_arg_t *notif_arg);


#endif /* _H_TPG_NOTIF_ */

