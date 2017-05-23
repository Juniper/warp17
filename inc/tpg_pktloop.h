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
 *     tpg_pktloop.h
 *
 * Description:
 *     Packet receive/send functions / loop.
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
#ifndef _H_TPG_PKTLOOP_
#define _H_TPG_PKTLOOP_

/*****************************************************************************
 * Definitions
 ****************************************************************************/
#define PKTLOOP_CMDLINE_OPTIONS() \
    CMDLINE_OPT_ARG("pkt-send-drop-rate", true)

#define PKTLOOP_CMDLINE_PARSER() \
    CMDLINE_ARG_PARSER(pkt_handle_cmdline_opt, NULL)

/*****************************************************************************
 * Pkt loop module message type codes.
 ****************************************************************************/
enum pktloop_msg_types {

    MSG_TYPE_DEF_START_MARKER(PKTLOOP),
    MSG_PKTLOOP_INIT_WAIT,
    MSG_PKTLOOP_UPDATE_PORT_DEV_INFO,
    MSG_PKTLOOP_START_PORT,
    MSG_PKTLOOP_STOP_PORT,
    MSG_TYPE_DEF_END_MARKER(PKTLOOP),

};

MSG_TYPE_MAX_CHECK(PKTLOOP);

/*****************************************************************************
 * Information about a port + queue that would be polled by the local core.
 ****************************************************************************/
typedef struct local_port_info_s {

    uint32_t     lpi_port_id;
    uint32_t     lpi_queue_id;
    port_info_t *lpi_port_info;

} local_port_info_t;

/*****************************************************************************
 * Message payload for MSG_PKTLOOP_START_PORT and MSG_PKTLOOP_STOP_PORT.
 ****************************************************************************/
typedef struct port_req_msg_s {

    uint32_t prm_port_id;

} __tpg_msg port_req_msg_t;

/*****************************************************************************
 * Global variables
 ****************************************************************************/
RTE_DECLARE_PER_LCORE(local_port_info_t *, pktloop_port_info);
RTE_DECLARE_PER_LCORE(uint32_t, pktloop_port_count);
RTE_DECLARE_PER_LCORE(port_info_t *, local_port_dev_info);

/*****************************************************************************
 * External's for tpg_pktloop.c
 ****************************************************************************/
extern int  pkt_send(uint32_t port, struct rte_mbuf *mbuf, bool trace);
extern void pkt_flush_tx_q(uint32_t port, tpg_port_statistics_t *stats);
extern int  pkt_receive_loop(void *arg __rte_unused);

extern bool pkt_handle_cmdline_opt(const char *opt_name, char *opt_arg);
extern bool pkt_loop_init(void);

/*****************************************************************************
 * Static inlines
 ****************************************************************************/
/*****************************************************************************
 * pkt_send_with_hash()
 ****************************************************************************/
static inline bool pkt_send_with_hash(uint32_t interface,
                                      struct rte_mbuf *pkt,
                                      uint32_t rss_hash,
                                      bool trace)
{
    /*
     * Slap the destination RSS hash if we're doing it ourselves.
     * Also mark that the RSS has been set.
     */
#if defined(TPG_EXPLICIT_RX_HASH)
    pkt->ol_flags |= PKT_RX_RSS_HASH;
    pkt->hash.rss = rss_hash;
#else /* defined(TPG_EXPLICIT_RX_HASH) */
    (void)rss_hash;
#endif /* defined(TPG_EXPLICIT_RX_HASH) */

    return pkt_send(interface, pkt, trace);
}


#endif /* _H_TPG_PKTLOOP_ */

