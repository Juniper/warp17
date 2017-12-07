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
 *     tpg_utils.h
 *
 * Description:
 *     Collection of common useful macros.
 *
 * Author:
 *     Dumitru Ceara, Eelco Chaudron
 *
 * Initial Created:
 *     08/12/2015
 *
 * Notes:
 *
 */

/*****************************************************************************
 * Multiple include protection
 ****************************************************************************/
#ifndef _H_TPG_UTILS_
#define _H_TPG_UTILS_

/*****************************************************************************
 * Printing common infrastructure
 ****************************************************************************/
typedef void (*printer_cb_t)(void *arg, const char *fmt, va_list ap);

typedef struct printer_arg_s {

    printer_cb_t  pa_printer;
    void         *pa_arg;

} printer_arg_t;

#define TPG_PRINTER_ARG(printer, arg) \
    ((printer_arg_t) {.pa_printer = (printer), .pa_arg = (arg)})

static inline void tpg_printf(printer_arg_t *pa, const char *fmt, ...)
              __attribute__ ((format (printf, 2, 3)));

/*****************************************************************************
 * tpg_printf()
 ****************************************************************************/
static inline void tpg_printf(printer_arg_t *pa, const char *fmt, ...)
{
    va_list ap;

    if (!pa || !pa->pa_printer)
        return;

    va_start(ap, fmt);
    pa->pa_printer(pa->pa_arg, fmt, ap);
    va_end(ap);
}

/*****************************************************************************
 * Macros to be used all over the place
 ****************************************************************************/
#define TPG_SEC_TO_USEC 1000000

#define TPG_ERROR_ABORT(fmt, ...)      \
    do {                               \
        cli_exit();                    \
        rpc_destroy();                 \
        rte_panic(fmt, ##__VA_ARGS__); \
    } while (0)

#define TPG_ERROR_EXIT(code, fmt, ...)        \
    do {                                      \
        cli_exit();                           \
        rpc_destroy();                        \
        rte_exit((code), fmt, ##__VA_ARGS__); \
    } while (0)

#define TPG_TIME_DIFF(now, prev) ((now) - (prev))

/*****************************************************************************
 * Simple divide and up macro
 ****************************************************************************/
#define TPG_DIV_UP(x, y) (((x) % (y)) ? (x) / (y) + 1 : (x) / (y))

/*****************************************************************************
 * TPG time delay definition
 ****************************************************************************/
#define TPG_DELAY(value) \
    ((tpg_delay_t) {.d_value = (value), .has_d_value = true})

#define TPG_DELAY_M(value, value_ms)                                \
    ((tpg_delay_t) {.d_value_ms = (value_ms), .has_d_value_ms = true, \
    .d_value = (value), .has_d_value = true})

#define TPG_DELAY_INF() ((tpg_delay_t) {.has_d_value = false})

#define TPG_DELAY_VAL(x) (((x)->has_d_value ? (x)->d_value : 0) * 1000 \
    + ((x)->has_d_value_ms ? (x)->d_value_ms : 0))

#define TPG_DELAY_IS_INF(x) (!(x)->has_d_value && !(x)->has_d_value_ms)

/*****************************************************************************
 * TPG rate value definition
 ****************************************************************************/
#define TPG_RATE(value) \
    ((tpg_rate_t) {.r_value = (value), .has_r_value = true})

#define TPG_RATE_INF() ((tpg_rate_t) {.has_r_value = false})

#define TPG_RATE_VAL(x) ((x)->r_value)

#define TPG_RATE_IS_INF(x) (!(x)->has_r_value)

#define TPG_RATE_LIM_INFINITE_VAL UINT32_MAX

#define TPG_RATE_VAL_DEFAULT(x) \
    (TPG_RATE_IS_INF(x) ? RATE_LIM_INFINITE_VAL : TPG_RATE_VAL(x))

/*****************************************************************************
 * TPG ip print utils
 ****************************************************************************/
#define TPG_IPV4_PRINT_FMT "%u.%u.%u.%u"

#define TPG_IPV4_PRINT_ARGS(ip) \
    (((ip) >> 24) & 0xff),      \
    (((ip) >> 16) & 0xff),      \
    (((ip) >>  8) & 0xff),      \
    (((ip) >>  0) & 0xff)

/*****************************************************************************
 * TPG ip initializer
 ****************************************************************************/
#define TPG_IPV4(val) \
    ((tpg_ip_t) {.ip_version = IP_V__IPV4, .ip_v4 = (val)})

/* TODO: IPv6 not supported yet. */
#define TPG_IPV6(val) \
    ((tpg_ip_t) {.ip_version = IP_V__IPV6})

#define TPG_IP_GT(i1, i2)                          \
    ((i1)->ip_version == (i2)->ip_version &&       \
     ((i1)->ip_version == IP_V__IPV4 &&            \
      (i1)->ip_v4 > (i2)->ip_v4) /* || v6-case */)

#define TPG_IP_EQ(i1, i2)                           \
    ((i1)->ip_version == (i2)->ip_version &&        \
     ((i1)->ip_version == IP_V__IPV4 &&             \
      (i1)->ip_v4 == (i2)->ip_v4) /* || v6-case */)

#define TPG_IP_GE(i1, i2) \
    (TPG_IP_GT((i1), (i2)) || TPG_IP_EQ((i1), (i2)))

#define TPG_IPV4_MCAST_PREFIX      0xE0000000
#define TPG_IPV4_MCAST_PREFIX_MASK 0xF0000000
#define TPG_IPV4_MCAST_MASK        0xEFFFFFFF
#define TPG_IPV4_BCAST_VAL         0xFFFFFFFF

#define TPG_IP_MCAST(ip)                                                  \
    ((ip)->ip_version == IP_V__IPV4 &&                                    \
     ((ip)->ip_v4 & TPG_IPV4_MCAST_PREFIX_MASK) == TPG_IPV4_MCAST_PREFIX)

/* TODO: IPv6 not supported yet. */
#define TPG_IP_MCAST_MIN(ipv) \
    ((ipv) ? TPG_IPV4(TPG_IPV4_MCAST_PREFIX) : TPG_IPV6(0))

/* TODO: IPv6 not supported yet. */
#define TPG_IP_MCAST_MAX(ipv) \
    ((ipv) ? TPG_IPV4(TPG_IPV4_MCAST_MASK) : TPG_IPV6(0))

#define TPG_IP_BCAST(ip) \
    ((ip)->ip_version == IP_V__IPV4 && ((ip)->ip_v4 == TPG_IPV4_BCAST_VAL))

/*****************************************************************************
 * TPG ip range initializer
 ****************************************************************************/
#define TPG_IPV4_RANGE(start, end)    \
    ((tpg_ip_range_t) {               \
        .ipr_start = TPG_IPV4(start), \
        .ipr_end = TPG_IPV4(end)      \
    })

/*****************************************************************************
 * TPG ip range macros
 ****************************************************************************/
#define TPG_IPV4_FOREACH(irange, ip)         \
    for ((ip) = (irange)->ipr_start.ip_v4;   \
            (ip) <= (irange)->ipr_end.ip_v4; \
            (ip)++)

#define TPG_IPV4_RANGE_SIZE(irange) \
    ((irange)->ipr_end.ip_v4 - (irange)->ipr_start.ip_v4 + 1)

/*****************************************************************************
 * TPG L4 port range initializer
 ****************************************************************************/
#define TPG_PORT_RANGE(start, end) \
    ((tpg_l4_port_range_t) {       \
        .l4pr_start = (start),     \
        .l4pr_end = (end)          \
    })

/*****************************************************************************
 * TPG L4 port range macros
 ****************************************************************************/
#define TPG_PORT_FOREACH(prange, port)    \
    for ((port) = (prange)->l4pr_start;   \
            (port) <= (prange)->l4pr_end; \
            (port)++)

#define TPG_PORT_RANGE_SIZE(prange) \
    ((prange)->l4pr_end - (prange)->l4pr_start + 1)

/*****************************************************************************
 * TPG utils macros
 ****************************************************************************/
#define TPG_FOREACH_CB_IN_RANGE(sips, dips, sports, dports, sip, dip, \
                                sport, dport)                         \
    TPG_IPV4_FOREACH((dips), (dip))                                   \
        TPG_PORT_FOREACH((dports), (dport))                           \
            TPG_IPV4_FOREACH((sips), (sip))                           \
                TPG_PORT_FOREACH((sports), (sport))

/*****************************************************************************
 * TPG min/max defs
 ****************************************************************************/
#define TPG_MIN(a, b) (((a) < (b)) ? (a) : (b))

#define TPG_MAX(a, b) (((a) > (b)) ? (a) : (b))

/*****************************************************************************
 * Protobuf related helpers
 ****************************************************************************/

/*****************************************************************************
 * Macro for setting optional fields values.
 ****************************************************************************/
#define TPG_XLATE_OPTIONAL_SET_FIELD(msg, field, value) \
    do {                                                \
        (msg)->field = (value);                         \
        (msg)->has_##field = true;                      \
    } while (0)

/*****************************************************************************
 * Macro for copying optional fields values.
 ****************************************************************************/
#define TPG_XLATE_OPTIONAL_COPY_FIELD(dest, src, field)                \
    do {                                                               \
        if ((src)->has_##field)                                        \
            TPG_XLATE_OPTIONAL_SET_FIELD((dest), field, (src)->field); \
        else                                                           \
            (dest)->has_##field = false;                               \
    } while (0)

/*****************************************************************************
 * Macro for translating active union fields.
 ****************************************************************************/
#define TPG_XLATE_UNION_SET_FIELD(out, in, field) \
    TPG_XLATE_OPTIONAL_SET_FIELD(out, field, (in)->field)

/*****************************************************************************
 * Macro for getting the value of an optional boolean field.
 ****************************************************************************/
#define TPG_XLATE_OPT_BOOL(obj, field) \
    ((obj)->has_##field && (obj)->field)

#endif /* _H_TPG_UTILS_ */

