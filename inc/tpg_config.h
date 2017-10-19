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
 *     tpg_config.h
 *
 * Description:
 *     Global configuration storage.
 *
 * Author:
 *     Dumitru Ceara, Eelco Chaudron
 *
 * Initial Created:
 *     06/24/2015
 *
 * Notes:
 *
 */

/*****************************************************************************
 * Multiple include protection
 ****************************************************************************/
#ifndef _H_TPG_CONFIG_
#define _H_TPG_CONFIG_

/*****************************************************************************
 * Command line helpers
 ****************************************************************************/
#define CMDLINE_OPT_ARG(knob_name, knob_has_arg) \
    ((struct option) {.name = (knob_name), .has_arg = (knob_has_arg)})
/*
 * Return type for cfg_handle_cmdline_arg_cb_t,
 * helps to figure out what is going wrong
 */
typedef enum cmdline_arg_parser_res_s {
    /*
     * CAPR_CONSUMED: means that the given option and the value has been parsed
     *                in a proper way.
     * CAPR_ERROR: means that the given option wasn't been recognized by the
     *             current parser
     * CAPR_IGNORED: means that the given option was recognized but the give
     *               wasn't fit the option range/accepted values
     */
    CAPR_CONSUMED,
    CAPR_ERROR,
    CAPR_IGNORED
} cmdline_arg_parser_res_t;

/* To be called whenever when a command line arg is found. */
typedef cmdline_arg_parser_res_t (*cfg_handle_cmdline_arg_cb_t)
                                 (const char *arg_name, char *opt_arg);

/* To be called whenever the parsing of the command line is done. */
typedef bool (*cfg_handle_cmdline_cb_t)(void);

typedef struct cmdline_arg_parser_s {

    cfg_handle_cmdline_arg_cb_t  cap_arg_parser;
    cfg_handle_cmdline_cb_t      cap_handler;
    const char                  *cap_usage;

} cfg_cmdline_arg_parser_t;

#define CMDLINE_ARG_PARSER(arg_parser, handler, usage)           \
    ((cfg_cmdline_arg_parser_t) {.cap_arg_parser = (arg_parser), \
                                 .cap_handler    = (handler),    \
                                 .cap_usage      = (usage)})
/*****************************************************************************
 * Global configuration, and defaults
 ****************************************************************************/
/*
 * One core is used for the CMD line interfaces, one for test management,
 * rest are packet processing cores for now.
 */
enum {

    TPG_CORE_IDX_CLI,
    TPG_CORE_IDX_TEST_MGMT,
    TPG_NR_OF_NON_PACKET_PROCESSING_CORES,

};

#define TPG_ETH_DEV_RX_QUEUE_SIZE      1024
#define TPG_ETH_DEV_TX_QUEUE_SIZE      1024

/*
 * The maximum number of ports we can handle.
 */
#define TPG_ETH_DEV_MAX                64

/*
 * Number of MBUFS we take out of the ring in one go, used by the
 * calls to rte_eth_rx_burst()
 */
#define TPG_RX_BURST_SIZE              128

/*
 * Number of MBUFS we put on the ring in one go, used by the
 * calls to rte_eth_tx_burst()
 */
#define TPG_TX_BURST_SIZE              64

/*
 * MBUF relates definitions, for now we put one packet in one MBUF.
 *
 * TODO: Implement fragmented mbufs, so the TCP header will fit,
 *       and the rest can be in remaining buffers. This way we can
 *       have more outstanding packets as the TCP setup packets
 *       are small.
 *       Need to make sure that the max number of segments is <= than
 *       tx_rs_thresh.
 */
#define GCFG_MBUF_POOL_NAME            "global_pkt_mbuf"
#define GCFG_MBUF_POOLSIZE_DEFAULT     (768 * 1024)
#define GCFG_MBUF_CACHE_SIZE           (512)
#define GCFG_MBUF_PACKET_FRAGMENT_SIZE (2048)
#define GCFG_MBUF_SIZE                 (GCFG_MBUF_PACKET_FRAGMENT_SIZE + \
                                        RTE_PKTMBUF_HEADROOM)

#define GCFG_MBUF_POOL_HDR_NAME        "global_pkt_mbuf_tx_hdr"
#define GCFG_MBUF_POOLSZ_HDR_DEF       (512 * 1024)
#define GCFG_MBUF_HDR_CACHE_SIZE       (512)
/* TODO: No IPv6 supported. No IP Options supported. No TCP options supported. */
#define GCFG_MBUF_HDR_FRAG_SIZE        (sizeof(struct ether_hdr) + \
                                        sizeof(struct ipv4_hdr) +  \
                                        sizeof(struct tcp_hdr))
#define GCFG_MBUF_HDR_SIZE             (GCFG_MBUF_HDR_FRAG_SIZE +  \
                                        sizeof(struct rte_mbuf) +  \
                                        RTE_PKTMBUF_HEADROOM)

#define GCFG_MBUF_POOL_CLONE_NAME      "global_pkt_mbuf_clone"
#define GCFG_MBUF_CLONE_SIZE           (sizeof(struct rte_mbuf))

/*
 * To support 1M sessions a second, we need 2* the number of TCBs,
 * plus the ones we keep active... Another second to close them??
 *
 * For mempools the optimum size is; n = (2^q - 1)
 */
#define GCFG_TCB_POOL_NAME             "tcb_pool"
/* 8M flows #define GCFG_TCB_POOL_SIZE             ((0x800000) - 1) */
/* 2M flows #define GCFG_TCB_POOL_SIZE             ((0x200000) - 1) */
#define GCFG_TCB_POOL_SIZE             ((0xa00000) - 1)
#define GCFG_TCB_POOL_CACHE_SIZE       (512)

#define GCFG_UCB_POOL_NAME             "ucb_pool"
/* 8M flows #define GCFG_UCB_POOL_SIZE             ((0x800000) - 1) */
/* 2M flows #define GCFG_UCB_POOL_SIZE             ((0x200000) - 1) */
#define GCFG_UCB_POOL_SIZE             ((0xa00000) - 1)
#define GCFG_UCB_POOL_CACHE_SIZE       (512)


/*
 * The size of the inter-module/thread message queue has to be a power of 2.
 * Since for now it's used only for control messages we don't need to many
 * elements. The size can be increased later if needed.
 */
#define GCFG_MSGQ_NAME                 "tpg_mqueue_lcore"
#define GCFG_MSGQ_SIZE                 (4096)

/*
 * The default values for the TCP timer wheels (in useconds).
 */
#define GCFG_SLOW_TMR_MAX              (60 * 1000000) /* 1 min */
#define GCFG_SLOW_TMR_STEP             100000   /* 100ms */

#define GCFG_RTO_TMR_MAX               (30 * 1000000) /* 30s */
#define GCFG_RTO_TMR_STEP              50        /* 50us */

#define GCFG_TEST_TMR_MAX              (30 * 60 * 1000000) /* 30 min */
#define GCFG_TEST_TMR_STEP             100       /* 100us */

#define GCFG_TMR_MAX_RUN_US            10000     /* 10ms */
#define GCFG_TMR_MAX_RUN_CNT           10000     /* max 10K tcb timers in one shot */
#define GCFG_TMR_STEP_ADVANCE          25        /* us */

/*
 * The default values for TCP Data processing.
 */
#define GCFG_TCP_SEGS_PER_SEND         4

/*
 * Test management defaults.
 */
#define GCFG_TCP_CLIENT_BURST_MAX      1
#define GCFG_UDP_CLIENT_BURST_MAX      16

#define GCFG_TEST_MGMT_TMR_TO          500000    /* 500ms */
#define GCFG_TEST_MGMT_RATES_TMR_TO    1000000   /* 1s */
#define GCFG_TEST_MAX_TC_RUNTIME       600000000 /* 10min */

/*
 * Test defaults.
 */
#define GCFG_RATE_MIN_INTERVAL_SIZE    100      /* 100us */
#define GCFG_RATE_NO_LIM_INTERVAL_SIZE 10000    /* 10ms  */

typedef struct global_config_s {

    uint32_t gcfg_mbuf_poolsize;
    uint32_t gcfg_mbuf_size;
    uint32_t gcfg_mbuf_cache_size;

    uint32_t gcfg_mbuf_hdr_poolsize;
    uint32_t gcfg_mbuf_hdr_size;
    uint32_t gcfg_mbuf_hdr_cache_size;

    uint32_t gcfg_mbuf_clone_size;

    uint32_t gcfg_tcb_pool_size;
    uint32_t gcfg_tcb_pool_cache_size;

    uint32_t gcfg_ucb_pool_size;
    uint32_t gcfg_ucb_pool_cache_size;

    bool     gcfg_mpool_any_sock;

    uint32_t gcfg_msgq_size;

    uint32_t gcfg_slow_tmr_max;
    uint32_t gcfg_slow_tmr_step;

    uint32_t gcfg_test_tmr_max;
    uint32_t gcfg_test_tmr_step;

    uint32_t gcfg_rto_tmr_max;
    uint32_t gcfg_rto_tmr_step;

    /* Drop 1 packet every 'gcfg_pkt_send_drop_rate' sends per core. */
    uint32_t gcfg_pkt_send_drop_rate;

    uint32_t gcfg_test_max_tc_runtime;

    uint32_t gcfg_rate_min_interval_size;
    uint32_t gcfg_rate_no_lim_interval_size;

    const char *gcfg_cmd_file;

} global_config_t;

/*
 * All the trace components MUST be listed here. Also, the mapping ID-Name
 * should be defined in tpg_config.c.
 */
typedef enum gtrace_id_s {

    TRACE_PKT_TX,
    TRACE_PKT_RX,
    TRACE_ARP,
    TRACE_ETH,
    TRACE_IPV4,
    TRACE_TCP,
    TRACE_UDP,
    TRACE_TLK,
    TRACE_TSM,
    TRACE_TST,
    TRACE_TMR,
    TRACE_HTTP,

    /* Should always be last! */
    TRACE_MAX,

} gtrace_id_t;

/*****************************************************************************
 * cfg_is_pkt_core()
 *      Note: we always reserve the first TPG_NR_OF_NON_PACKET_PROCESSING_CORES
 *            lcore indexes for CLI and non-packet stuff.
 ****************************************************************************/
static inline __attribute__((always_inline))
bool cfg_is_pkt_core(uint32_t core)
{
    return rte_lcore_is_enabled(core) &&
           rte_lcore_index(core) >= TPG_NR_OF_NON_PACKET_PROCESSING_CORES;
}

/*****************************************************************************
 * cfg_is_cli_core()
 *      Note: we always reserve the first TPG_NR_OF_NON_PACKET_PROCESSING_CORES
 *            lcore indexes for CLI and non-packet stuff.
 ****************************************************************************/
static inline __attribute__((always_inline))
bool cfg_is_cli_core(uint32_t core)
{
    return rte_lcore_is_enabled(core) &&
           rte_lcore_index(core) == TPG_CORE_IDX_CLI;
}

/*****************************************************************************
 * cfg_is_test_core()
 *      Note: we always reserve the first TPG_NR_OF_NON_PACKET_PROCESSING_CORES
 *            lcore indexes for CLI and non-packet stuff.
 ****************************************************************************/
static inline __attribute__((always_inline))
bool cfg_is_test_core(uint32_t core)
{
    return rte_lcore_is_enabled(core) &&
           rte_lcore_index(core) == TPG_CORE_IDX_TEST_MGMT;
}


/*****************************************************************************
 * cfg_get_test_mgmt_core()
 ****************************************************************************/
static inline __attribute__((always_inline))
int cfg_get_test_mgmt_core(void)
{
    int core;

    RTE_LCORE_FOREACH_SLAVE(core) {
        if (rte_lcore_index(core) == TPG_CORE_IDX_TEST_MGMT)
            return core;
    }

    /* Should never ever happen!! */
    assert(false);
    return -1;
}

/*****************************************************************************
 * cfg_pkt_core_count()
 *      Note: we always reserve the first TPG_NR_OF_NON_PACKET_PROCESSING_CORES
 *            lcore indexes for CLI and non-packet stuff.
 ****************************************************************************/
static inline __attribute__((always_inline))
uint32_t cfg_pkt_core_count(void)
{
    static uint32_t pkt_core_count;
    uint32_t        core;

    if (pkt_core_count)
        return pkt_core_count;

    RTE_LCORE_FOREACH_SLAVE(core) {
        if (cfg_is_pkt_core(core))
            pkt_core_count++;
    }
    return pkt_core_count;
}

/*****************************************************************************
 * External's for tpg_config.c
 ****************************************************************************/
extern bool             cfg_init(void);
extern bool             cfg_handle_command_line(int argc, char **argv);
extern global_config_t *cfg_get_config(void);
extern const char      *cfg_get_gtrace_name(gtrace_id_t id);
extern void             cfg_print_usage(const char *prgname);

#endif /* _H_TPG_CONFIG_ */

