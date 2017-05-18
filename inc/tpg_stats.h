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
 *     tpg_stats.h
 *
 * Description:
 *     Generic stats implementation. Allows defining generic stats that can be
 *     efficiently updated by multiple threads.
 *
 * Author:
 *     Dumitru Ceara, Eelco Chaudron
 *
 * Initial Created:
 *     07/30/2015
 *
 * Notes:
 *
 */

/*****************************************************************************
 * Multiple include protection
 ****************************************************************************/
#ifndef _H_TPG_STATS_
#define _H_TPG_STATS_

/* Each module that needs statistics should use the STATS_DEFINE macro to
 * register its own stats type.
 *
 * STATS_DEFINE(module_statistics_t);
 *
 * This will define a global array indexed by core of pointers to per port
 * statistics. It also defines a pointer to the core-local statistics.
 *
 * The global array should be initialized in the main function of the module:
 *
 * bool module_init()
 * {
 *     [...]
 *     if (STATS_GLOBAL_INIT(module_statistics_t, "module_stats") == NULL) {
 *         return false;
 *     }
 *     [...]
 * }
 *
 * The core local statistics should be initialized in the module_lcore_init
 * function:
 *
 * void module_lcore_init(uint32_t lcore_id)
 * {
 *     [...]
 *     if (STATS_LOCAL_INIT(module_statistics_t, "module_stats",
 *                          lcore_id) == NULL) {
 *         TPG_ERROR_ABORT([...]);
 *     }
 *     [...]
 * }
 *
 * Updating counters can be done through the INC/DEC_STATS macros:
 *
 * INC_STATS(STATS_LOCAL(module_statistics_t, eth_port), stat_field_name);
 *
 * The global counter array can be walked for a given port by using:
 *
 * uint32_t             core;
 * module_statistics_t *module_stats;
 *
 * STATS_FOREACH_CORE(module_statistics_t, port, core, module_stats) {
 *     [...]
 * }
 *
 * Stats can be dumped using the SHOW_*_STATS macros.
 * The SHOW_*_STATS_ALL_CORES macros also iterate through the stats of the
 * non-packet cores.
 */

#define STATS_GLOBAL_NAME(type) type ## _global
#define STATS_LOCAL_NAME(type)  per_lcore_ ## type ## _local

#define STATS_GLOBAL_DEFINE(type) \
    __typeof__(type) **STATS_GLOBAL_NAME(type)

#define STATS_GLOBAL_DECLARE(type) \
    extern STATS_GLOBAL_DEFINE(type)

#define STATS_GLOBAL_BY_CORE(type, lcore_id) \
    (STATS_GLOBAL_NAME(type))[rte_lcore_index(lcore_id)]

#define STATS_GLOBAL(type, lcore_id, eth_port)               \
    (STATS_GLOBAL_BY_CORE(type, (lcore_id)) ?                \
     (STATS_GLOBAL_BY_CORE(type, (lcore_id)) + (eth_port)) : \
     NULL)

#define STATS_GLOBAL_INIT(type, tag)                                   \
    (STATS_GLOBAL_NAME(type) = rte_zmalloc(tag "_global",              \
                                           rte_lcore_count() *         \
                                           sizeof(__typeof__(type) *), \
                               0))

#define STATS_LOCAL_DEFINE(type) \
    __thread __typeof__(type) *STATS_LOCAL_NAME(type)

#define STATS_LOCAL_DECLARE(type) \
    extern STATS_LOCAL_DEFINE(type)

#define STATS_CLEAR(type, eth_port)                 \
do {                                                \
    __typeof__(type) *var;                          \
    uint32_t          core;                         \
                                                    \
    STATS_FOREACH_CORE(type, eth_port, core, var) { \
                                                    \
        bzero(var, sizeof(*var));                   \
    }                                               \
} while (0)

#define STATS_LOCAL(type, eth_port) \
    (STATS_LOCAL_NAME(type) + (eth_port))


#define STATS_LOCAL_INIT(type, tag, lcore_id)                         \
    (STATS_GLOBAL_BY_CORE(type, (lcore_id)) =                         \
        rte_zmalloc_socket(tag "_local", rte_eth_dev_count() *        \
                           sizeof(__typeof__(type)),                  \
                           RTE_CACHE_LINE_SIZE,                       \
                           rte_lcore_to_socket_id((lcore_id))),       \
     STATS_LOCAL_NAME(type) = STATS_GLOBAL_BY_CORE(type, (lcore_id)))

#define STATS_FOREACH_CORE(type, port, core, stats)    \
    RTE_LCORE_FOREACH_SLAVE(core)                      \
        for ((stats) = STATS_GLOBAL(type, core, port); \
             (stats);                                  \
             (stats) = NULL)

#define STATS_DEFINE(type)     \
    STATS_GLOBAL_DEFINE(type); \
    STATS_LOCAL_DEFINE(type)

#define SHOW_PER_CORE_STATS(type, counter, port, opt, int_fmt)         \
do {                                                                   \
    if ((opt) == 'd') {                                                \
        int _core;                                                     \
        RTE_LCORE_FOREACH(_core) {                                     \
            int _idx = rte_lcore_index(_core);                         \
            cmdline_printf(cl,                                         \
                           "    - core idx %3.3u    : %20"int_fmt"\n", \
                           _idx,                                       \
                           STATS_GLOBAL(type, _core, port)->counter);  \
        }                                                              \
    }                                                                  \
} while (0)

#define SHOW_PER_PACKET_CORE_STATS(type, counter, port, opt, int_fmt)      \
do {                                                                       \
    if ((opt) == 'd') {                                                    \
        int _core;                                                         \
        RTE_LCORE_FOREACH(_core) {                                         \
            int _idx = rte_lcore_index(_core);                             \
            if (cfg_is_pkt_core(_core)) {                                  \
                cmdline_printf(cl,                                         \
                               "    - core idx %3.3u    : %20"int_fmt"\n", \
                               _idx,                                       \
                               STATS_GLOBAL(type, _core, port)->counter);  \
            }                                                              \
        }                                                                  \
    }                                                                      \
} while (0)

#define SHOW_PER_PACKET_CORE_64BIT_STATS(type, counter, port, opt) \
    SHOW_PER_PACKET_CORE_STATS(type, counter, port, opt, PRIu64)

#define SHOW_PER_PACKET_CORE_32BIT_STATS(type, counter, port, opt) \
    SHOW_PER_PACKET_CORE_STATS(type, counter, port, opt, PRIu32)

#define SHOW_PER_PACKET_CORE_16BIT_STATS(type, counter, port, opt) \
    SHOW_PER_PACKET_CORE_STATS(type, counter, port, opt, PRIu16)

#define SHOW_64BIT_STATS(stat_str, type, counter, port, opt)    \
do {                                                            \
    cmdline_printf(cl, "  %-20s: %20"PRIu64"\n", stat_str,      \
        total_stats.counter);                                   \
    SHOW_PER_PACKET_CORE_64BIT_STATS(type, counter, port, opt); \
} while (0)

#define SHOW_32BIT_STATS(stat_str, type, counter, port, opt)    \
do {                                                            \
    cmdline_printf(cl, "  %-20s: %20"PRIu32"\n", stat_str,      \
        total_stats.counter);                                   \
    SHOW_PER_PACKET_CORE_32BIT_STATS(type, counter, port, opt); \
} while (0)

#define SHOW_16BIT_STATS(stat_str, type, counter, port, opt)    \
do {                                                            \
    cmdline_printf(cl, "  %-20s: %20"PRIu16"\n", stat_str,      \
        total_stats.counter);                                   \
    SHOW_PER_PACKET_CORE_16BIT_STATS(type, counter, port, opt); \
} while (0)


#define SHOW_PER_CORE_64BIT_STATS(type, counter, port, opt) \
    SHOW_PER_CORE_STATS(type, counter, port, opt, PRIu64)

#define SHOW_PER_CORE_32BIT_STATS(type, counter, port, opt) \
    SHOW_PER_CORE_STATS(type, counter, port, opt, PRIu32)

#define SHOW_PER_CORE_16BIT_STATS(type, counter, port, opt) \
    SHOW_PER_CORE_STATS(type, counter, port, opt, PRIu16)

#define SHOW_64BIT_STATS_ALL_CORES(stat_str, type, counter, port, opt) \
do {                                                                   \
    cmdline_printf(cl, "  %-20s: %20"PRIu64"\n", stat_str,             \
        total_stats.counter);                                          \
    SHOW_PER_CORE_64BIT_STATS(type, counter, port, opt);               \
} while (0)

#define SHOW_32BIT_STATS_ALL_CORES(stat_str, type, counter, port, opt) \
do {                                                                   \
    cmdline_printf(cl, "  %-20s: %20"PRIu32"\n", stat_str,             \
        total_stats.counter);                                          \
    SHOW_PER_CORE_32BIT_STATS(type, counter, port, opt);               \
} while (0)

#define SHOW_16BIT_STATS_ALL_CORES(stat_str, type, counter, port, opt) \
do {                                                                   \
    cmdline_printf(cl, "  %-20s: %20"PRIu16"\n", stat_str,             \
        total_stats.counter);                                          \
    SHOW_PER_CORE_16BIT_STATS(type, counter, port, opt);               \
} while (0)


#if defined(TPG_HAVE_STATS)

#define INC_STATS_VAL(stat_ptr, counter, val) \
    ((stat_ptr)->counter += (val))

#else /* defined(TPG_HAVE_STATS) */

#define INC_STATS_VAL(stat_ptr, counter, val) \
    ((void)(stat_ptr))

#endif /* defined(TPG_HAVE_STATS) */

#define INC_STATS(stat_ptr, counter) INC_STATS_VAL(stat_ptr, counter, 1)

#define DEC_STATS_VAL(stat_ptr, counter, val) \
    INC_STATS_VAL(stat_ptr, counter, -(val))

#define DEC_STATS(stat_ptr, counter) DEC_STATS_VAL(stat_ptr, counter, 1)

#endif /* _H_TPG_STATS_ */

