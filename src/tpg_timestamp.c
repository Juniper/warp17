/*
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER
 *
 * Copyright (c) 2017, Juniper Networks, Inc. All rights reserved.
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
 *     tpg_timestamp.c
 *
 * Description:
 *     Timestamp management module.
 *
 * Author:
 *     Matteo Triggiani
 *
 * Initial Created:
 *     13/09/2017
 *
 * Notes:
 *
 */

/*****************************************************************************
  * Include files
  ***************************************************************************/

#include "tcp_generator.h"

/*****************************************************************************
 * Global variables
 ****************************************************************************/

/* Here we define a local variable in order to keep how many transmission and
 * receiving timestamp queue do we have and their callbacks
 */
RTE_DEFINE_PER_LCORE(tstamp_info_t, tstamp_tc_per_port_t)[TPG_ETH_DEV_MAX];

/*****************************************************************************
 * tstamp_start()
 ****************************************************************************/
void tstamp_start_tx(uint32_t port, uint32_t rss_queue __rte_unused,
                     tstamp_tx_post_cb_t cb)
{
    /* TODO: we’ll need rss_queue later if we decide to do timestamping on
     * single queues instead of the whole port
     *
     * we would overwrite the callback if we actually had multiple calls to
     * tstamp_start_tx with a non-null callback
     */
    RTE_PER_LCORE(tstamp_tc_per_port_t)[port].tstamp_tx_per_port++;
    RTE_PER_LCORE(tstamp_tc_per_port_t)[port].tstamp_cb = cb;
}

/*****************************************************************************
 * tstamp_stop()
 ****************************************************************************/
void tstamp_stop_tx(uint32_t port, uint32_t rss_queue __rte_unused)
{
    assert(RTE_PER_LCORE(tstamp_tc_per_port_t)[port].tstamp_tx_per_port > 0);
    RTE_PER_LCORE(tstamp_tc_per_port_t)[port].tstamp_tx_per_port--;
}

/*****************************************************************************
 * tstamp_start()
 ****************************************************************************/
void tstamp_start_rx(uint32_t port, uint32_t rss_queue __rte_unused)
{
    /* TODO: we’ll need rss_queue later if we decide to do timestamping on
     * single queues instead of the whole port
     *
     * we would overwrite the callback if we actually had multiple calls to
     * tstamp_start_rx with a non-null callback
     */
    RTE_PER_LCORE(tstamp_tc_per_port_t)[port].tstamp_rx_per_port++;
}

/*****************************************************************************
 * tstamp_stop()
 ****************************************************************************/
void tstamp_stop_rx(uint32_t port, uint32_t rss_queue __rte_unused)
{
    assert(RTE_PER_LCORE(tstamp_tc_per_port_t)[port].tstamp_rx_per_port > 0);
    RTE_PER_LCORE(tstamp_tc_per_port_t)[port].tstamp_rx_per_port--;
}

/*****************************************************************************
 * tstamp_is_running()
 ****************************************************************************/
bool tstamp_tx_is_running(uint32_t port, uint32_t rss_queue __rte_unused)
{
    return RTE_PER_LCORE(tstamp_tc_per_port_t)[port].tstamp_tx_per_port > 0;
}

/*****************************************************************************
 * tstamp_is_running()
 ****************************************************************************/
bool tstamp_rx_is_running(uint32_t port, uint32_t rss_queue __rte_unused)
{
    return RTE_PER_LCORE(tstamp_tc_per_port_t)[port].tstamp_rx_per_port > 0;
}

/*****************************************************************************
 * tstamp_tx_pkt()
 ****************************************************************************/
void tstamp_tx_pkt(struct rte_mbuf *mbuf, uint32_t offset, uint32_t size)
{
    DATA_SET_TSTAMP_OFFSET(mbuf, offset);
    DATA_SET_TSTAMP_SIZE(mbuf, size);
    DATA_SET_TSTAMP(mbuf);
}

/*****************************************************************************
 * tstamp_pktloop_rx_pkt_burst()
 ****************************************************************************/
void tstamp_pktloop_rx_pkt_burst(uint32_t eth_port __rte_unused,
                                 int32_t queue_id __rte_unused,
                                 struct rte_mbuf **rx_mbufs __rte_unused,
                                 packet_control_block_t *pcbs, uint32_t count)
{
    uint64_t i;
    uint64_t now;

    now = rte_get_timer_cycles() / cycles_per_us;
    for (i = 0; i < count; i++)
        pcbs[i].pcb_tstamp = now;

    /*TODO: support timestamp module for rx/tx on different machines*/

}

/*****************************************************************************
 * tstamp_pktloop_tx_pkt()
 *      If needed, timestamp an mbuf segment inside a packet.
 ****************************************************************************/
static void tstamp_pktloop_tx_pkt(struct rte_mbuf *hdr,
                                  struct rte_mbuf *seg)
{
    uint64_t             timestamp;
    uint32_t            *tstamp_ptr;
    uint32_t             offset;
    uint32_t             size;
    tstamp_tx_post_cb_t  cb;

    offset = (uint32_t) DATA_GET_TSTAMP_OFFSET(seg);
    size   = (uint32_t) DATA_GET_TSTAMP_SIZE(seg);

    tstamp_ptr = (uint32_t *)data_mbuf_mtod_offset(seg, offset);
    timestamp  = rte_get_timer_cycles() / cycles_per_us;

    tstamp_ptr[0] = rte_cpu_to_be_32(TSTAMP_SPLIT_LOW(timestamp));
    tstamp_ptr[1] = rte_cpu_to_be_32(TSTAMP_SPLIT_HIGH(timestamp));

    cb = RTE_PER_LCORE(tstamp_tc_per_port_t)[hdr->port].tstamp_cb;
    if (unlikely(cb != NULL))
        cb(hdr, seg, offset, size);
}

/*****************************************************************************
 * tstamp_pktloop_tx_pkt_burst()
 *      If needed, timestamp a burst of outgoing packets.
 ****************************************************************************/
void tstamp_pktloop_tx_pkt_burst(uint32_t eth_port __rte_unused,
                                 int32_t queue_id __rte_unused,
                                 struct rte_mbuf **tx_mbufs, uint32_t count)
{
    uint32_t i;

    for (i = 0; i < count; i++) {
        struct rte_mbuf *mbuf = tx_mbufs[i];

        if (likely(!DATA_IS_TSTAMP(mbuf) && !DATA_IS_TSTAMP_MULTI(mbuf)))
            continue;

        if (unlikely(DATA_IS_TSTAMP_MULTI(mbuf))) {
            /* Check each particle if it needs timestamping. */
            for (; mbuf; mbuf = mbuf->next) {
                if (DATA_IS_TSTAMP(mbuf))
                    tstamp_pktloop_tx_pkt(tx_mbufs[i], mbuf);
            }
        } else {
            tstamp_pktloop_tx_pkt(mbuf, mbuf);
        }
    }
}
