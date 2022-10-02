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
 *     tpg_timestamp.h
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
 * Multiple include protection
 ****************************************************************************/
#ifndef _H_TPG_TIMESTAMP_
#define _H_TPG_TIMESTAMP_

/*
 * Those functions are useful for splitting in two different 32 unsigned int
 * a 64 unsigned int
 */

/* Mask 32 bits for high. */
#define TSTAMP_UINT64_TO_UINT32_LOW_MASK  0x00000000FFFFFFFF
/* Mask 32 bits for low. */
#define TSTAMP_UINT64_TO_UINT32_HIGH_MASK 0xFFFFFFFF00000000

#define TSTAMP_SPLIT_LOW(source) \
    ((source) & TSTAMP_UINT64_TO_UINT32_LOW_MASK)

#define TSTAMP_SPLIT_HIGH(source) \
    (((source) & TSTAMP_UINT64_TO_UINT32_HIGH_MASK) >> 32)

#define TSTAMP_JOIN(high, low)                       \
    ((((uint64_t)(high)) << 32) | ((uint64_t)(low) & \
        TSTAMP_UINT64_TO_UINT32_LOW_MASK))

/*****************************************************************************
 * tstamp_tx_post_cb_t
 * @param mbuf_hdr
 *        pointer to the beginning of the packet where one (or multiple)
 *        timestamps were stored
 * @param mbuf_seg
 *        pointer to the segment inside the packet where the current timestamp
 *        was stored
 * @param offset
 *        offset inside the header where the tx tstamp is stored
 *        (i.e., offset-of-timestamp-field-in-ip-option + sizeof(ipv4_hdr))
 * @param size
 *        size of the stored timestamp (in bytes)
 ****************************************************************************/
typedef void (*tstamp_tx_post_cb_t)(struct rte_mbuf *mbuf_hdr,
                                    struct rte_mbuf *mbuf_seg,
                                    uint32_t offset,
                                    uint32_t size);

/*****************************************************************************
 * tstamp_info_t
 * @param tstamp_tx_per_port
 *        how many timestamp transmission queue per given port
 * @param tstamp_tx_per_port
 *        how many timestamp receiving queue per given port
 * @param tstamp_cb
 *        callback has to be call at the end of tstamp_tx_pkt
 ****************************************************************************/
typedef struct tstamp_info_s {
    uint8_t             tstamp_tx_per_port;
    uint8_t             tstamp_rx_per_port;
    tstamp_tx_post_cb_t tstamp_cb;
} tstamp_info_t;

/*****************************************************************************
 * Functions declarations
 ****************************************************************************/

extern void tstamp_start_rx(uint32_t port, uint32_t rss_queue);
extern void tstamp_stop_rx(uint32_t port, uint32_t rss_queue);
extern bool tstamp_rx_is_running(uint32_t port, uint32_t rss_queue);

extern void tstamp_start_tx(uint32_t port, uint32_t rss_queue,
                            tstamp_tx_post_cb_t cb);
extern void tstamp_stop_tx(uint32_t port, uint32_t rss_queue);
extern bool tstamp_tx_is_running(uint32_t port, uint32_t rss_queue);

extern void tstamp_tx_pkt(struct rte_mbuf *mbuf, uint32_t offset,
                          uint32_t size);

extern void tstamp_pktloop_rx_pkt_burst(uint32_t eth_port, int32_t queue_id,
                                        struct rte_mbuf **rx_mbufs,
                                        packet_control_block_t *pcbs,
                                        uint32_t count);
extern void tstamp_pktloop_tx_pkt_burst(uint32_t eth_port, int32_t queue_id,
                                        struct rte_mbuf **tx_mbufs,
                                        uint32_t count);

/*****************************************************************************
 * Static inlines
 ****************************************************************************/

/*****************************************************************************
 * tstamp_data_append()
 *      Propagate any timestamping information from a payload mbuf (`data`) to
 *      its header mbuf (`hdr`).
 ****************************************************************************/
static inline void tstamp_data_append(struct rte_mbuf *hdr,
                                      struct rte_mbuf *data)
{
    if (unlikely(DATA_IS_TSTAMP(data))) {
        tstamp_tx_pkt(hdr, DATA_GET_TSTAMP_OFFSET(data) + hdr->pkt_len,
                      DATA_GET_TSTAMP_SIZE(data));
    } else if (unlikely(DATA_IS_TSTAMP_MULTI(data))) {
        DATA_SET_TSTAMP_MULTI(hdr);
    }
}

#endif /* _H_TPG_TIMESTAMP_ */

