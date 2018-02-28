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
 *     tcp_generator.h
 *
 * Description:
 *     High volume TCP stream generator's main include file.
 *
 * Author:
 *     Dumitru Ceara, Eelco Chaudron
 *
 * Initial Created:
 *     02/26/2015
 *
 * Notes:
 *
 */

/*****************************************************************************
 * Multiple include protection
 ****************************************************************************/
#ifndef _H_TCP_GENERATOR_
#define _H_TCP_GENERATOR_

/*****************************************************************************
 * General includes needed to include tcp_generator.h without any additional
 * includes (does this make sense ;)
 ****************************************************************************/
#include <stdbool.h>
#include <stdint.h>

#include <rte_ethdev.h>
#include <rte_timer.h>
#include <rte_ip.h>
#include <rte_tcp.h>
#include <rte_udp.h>
#include <rte_errno.h>
#include <rte_kni.h>
#include <rte_cycles.h>

#include <termios.h>
#include <cmdline_parse.h>
#include <cmdline_parse_ipaddr.h>
#include <cmdline_parse_num.h>
#include <cmdline_parse_string.h>
#include <cmdline_parse_portlist.h>
#include <cmdline_rdline.h>
#include <cmdline_socket.h>
#include <cmdline.h>

#include <ncurses.h>

/*****************************************************************************
 * DPDK compiler does not define bool to _Bool
 ****************************************************************************/
#ifndef bool
#define bool _Bool
#endif

/*****************************************************************************
 * Static assert
 ****************************************************************************/
#ifndef static_assert
#define static_assert _Static_assert
#endif

/*****************************************************************************
 * container_of()
 ****************************************************************************/
#define container_of(ptr, type, member) \
    ((type *)((char *)(ptr) - offsetof(type, member)))

/*****************************************************************************
 * Remove debug message is requested
 ****************************************************************************/
#ifdef TPG_NO_DEBUG_LOGS

    #undef RTE_LOG_LEVEL
    #define RTE_LOG_LEVEL RTE_LOG_INFO

#endif

/*****************************************************************************
 * Version information
 ****************************************************************************/
#define TPG_BUILD_DATE __DATE__
#define TPG_BUILD_TIME __TIME__

#define TPG_VERSION_PRINTF_STR  \
    "warp17 v%s @%s, %s, revision: %s"
#define TPG_VERSION_PRINTF_ARGS \
    TPG_VERSION, TPG_BUILD_TIME, TPG_BUILD_DATE, TPG_BUILD_HASH

/*****************************************************************************
 * Global variables
 ****************************************************************************/
extern bool     tpg_exit;
extern uint64_t cycles_per_us;

/*****************************************************************************
 * Container for all WARP17 includes.
 ****************************************************************************/
#include "warp17-common.proto.xlate.h"
#include "warp17-l3.proto.xlate.h"
#include "warp17-app-raw.proto.xlate.h"
#include "warp17-app-http.proto.xlate.h"
#include "warp17-app.proto.xlate.h"
#include "warp17-client.proto.xlate.h"
#include "warp17-server.proto.xlate.h"
#include "warp17-test-case.proto.xlate.h"
#include "warp17-sockopt.proto.xlate.h"
#include "warp17-stats.proto.xlate.h"
#include "warp17-service.proto.xlate.h"

/* Forward declarations to avoid include complaints. Maybe we should move them
 * somewhere else?
 */
typedef struct l4_control_block_s l4_control_block_t;
typedef struct tcp_control_block_s tcp_control_block_t;
typedef struct udp_control_block_s udp_control_block_t;
typedef union  app_storage_u app_storage_t;
typedef struct app_data_s app_data_t;
typedef struct test_case_info_s test_case_info_t;
typedef TAILQ_HEAD(tcp_test_cb_list_s, l4_control_block_s) tlkp_test_cb_list_t;

#include "tpg_utils.h"
#include "tpg_rpc.h"
#include "tpg_stats.h"

#include "tpg_config.h"
#include "tpg_main.h"
#include "tpg_msg.h"

#include "tpg_trace.h"
#include "tpg_trace_filter.h"
#include "tpg_trace_cli.h"
#include "tpg_trace_msg.h"
#include "tpg_sockopts.h"

#include "tpg_pcb.h"
#include "tpg_cli.h"
#include "tpg_timer.h"

#include "tpg_rate.h"

#include "tpg_sockopts.h"

#include "tpg_test_app_data.h"
#include "tpg_test_raw_app.h"
#include "tpg_test_http_1_1_app.h"
#include "tpg_test_generic_app.h"
#include "tpg_test_imix_app.h"
#include "tpg_test_app.h"

#include "tpg_tests_sm_states.h"
#include "tpg_lookup.h"
#include "tpg_tests.h"
#include "tpg_tests_sm.h"

#include "tpg_ethernet.h"
#include "tpg_arp.h"
#include "tpg_ipv4.h"

#include "tpg_tcp_sm.h"
#include "tpg_tcp.h"
#include "tpg_memory.h"
#include "tpg_mbuf.h"
#include "tpg_timestamp.h"
#include "tpg_data.h"
#include "tpg_tcp_data.h"
#include "tpg_tcp_lookup.h"
#include "tpg_udp.h"
#include "tpg_udp_lookup.h"

#include "tpg_kni_if.h"
#include "tpg_ring_if.h"
#include "tpg_port.h"
#include "tpg_route.h"
#include "tpg_pktloop.h"

#include "tpg_test_mgmt.h"
#include "tpg_test_mgmt_api.h"
#include "tpg_test_mgmt_cli.h"
#include "tpg_test_stats.h"

/*****************************************************************************
 * End of include file
 ****************************************************************************/
#endif /* _H_TCP_GENERATOR_ */

