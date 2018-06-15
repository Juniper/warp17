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
 *     tpg_mbuf.h
 *
 * Description:
 *     Mbuf specific definitions (user data and alloc/free wrappers).
 *
 * Author:
 *     Matteo Triggiani
 *
 * Initial Created:
 *     26/09/2017.
 *
 * Notes:
 *
 */

/*****************************************************************************
 * Multiple include protection
 ****************************************************************************/
#ifndef _H_TPG_MBUF_
#define _H_TPG_MBUF_

/*****************************************************************************
 * Flags to be used in the mbuf user data when passing data from an
 * application to the WARP17 TCP/IP stack.
 ****************************************************************************/
/* At most 16 bits for app dependent flags. */
#define TPG_APP_UINT64_MBUF_FLAG_STATIC       0x0001000000000000
#define TPG_APP_UINT64_MBUF_FLAG_TSTAMP       0x0002000000000000
#define TPG_APP_UINT64_MBUF_FLAG_TSTAMP_MULTI 0x0004000000000000
#define TPG_APP_UINT64_MBUF_FLAG_TX_TRACE     0x0008000000000000
#define TPG_APP_UINT64_MBUF_FLAG_MAX          0xFFFF000000000000

/* Reserve 16 bits for timestamp offset. */
#define TPG_APP_UINT64_MBUF_TSTAMP_OFFSET_MASK_LEN 16
/* Reserve 16 bits for timestamp size. */
#define TPG_APP_UINT64_MBUF_TSTAMP_SIZE_MASK_LEN   16
/* Reserve 16 bits for timestamp offset. */
#define TPG_APP_UINT64_MBUF_CKSUM_OFFSET_MASK_LEN 16

#define TPG_APP_UINT64_MBUF_TSTAMP_MASK_LEN        \
    (TPG_APP_UINT64_MBUF_TSTAMP_OFFSET_MASK_LEN +  \
        TPG_APP_UINT64_MBUF_TSTAMP_SIZE_MASK_LEN + \
        TPG_APP_UINT64_MBUF_CKSUM_OFFSET_MASK_LEN)

static_assert(TPG_APP_UINT64_MBUF_FLAG_MAX >
              (((uint64_t) 1 << TPG_APP_UINT64_MBUF_TSTAMP_MASK_LEN) - 1),
              "Check usage of the mbuf 64 private bits!");

#define TPG_APP_UINT64_MBUF_TSTAMP_OFFSET_MASK \
    (uint64_t)((1 << TPG_APP_UINT64_MBUF_TSTAMP_OFFSET_MASK_LEN) - 1)

#define TPG_APP_UINT64_MBUF_TSTAMP_SIZE_MASK \
    (uint64_t)((1 << TPG_APP_UINT64_MBUF_TSTAMP_SIZE_MASK_LEN) - 1)

#define TPG_APP_UINT64_MBUF_CKSUM_OFFSET_MASK \
    (uint64_t)((1 << TPG_APP_UINT64_MBUF_CKSUM_OFFSET_MASK_LEN) - 1)

/* Use the lowest 16 bits for timestamp offset and the next 16 bits for
 * timestamp size (length).
 */
#define TPG_APP_UINT64_MBUF_TSTAMP_OFFSET_POS 0

#define TPG_APP_UINT64_MBUF_TSTAMP_SIZE_POS \
            TPG_APP_UINT64_MBUF_TSTAMP_OFFSET_MASK_LEN

#define TPG_APP_UINT64_MBUF_CKSUM_OFFSET_POS          \
            (TPG_APP_UINT64_MBUF_TSTAMP_SIZE_POS +    \
             TPG_APP_UINT64_MBUF_TSTAMP_SIZE_MASK_LEN)

#define DATA_SET_TSTAMP_OFFSET(mbuf, offset)                   \
    ((mbuf)->udata64 |= (                                      \
        ((offset) & TPG_APP_UINT64_MBUF_TSTAMP_OFFSET_MASK) << \
         TPG_APP_UINT64_MBUF_TSTAMP_OFFSET_POS))

#define DATA_SET_TSTAMP_SIZE(mbuf, size)                                    \
    ((mbuf)->udata64 |= (((size) & TPG_APP_UINT64_MBUF_TSTAMP_SIZE_MASK) << \
        TPG_APP_UINT64_MBUF_TSTAMP_SIZE_POS))

#define DATA_SET_CKSUM_OFFSET(mbuf, offset)                   \
    ((mbuf)->udata64 |= (                                      \
        ((offset) & TPG_APP_UINT64_MBUF_CKSUM_OFFSET_MASK) << \
         TPG_APP_UINT64_MBUF_CKSUM_OFFSET_POS))

#define DATA_GET_TSTAMP_OFFSET(mbuf)                              \
    (((mbuf)->udata64 >> TPG_APP_UINT64_MBUF_TSTAMP_OFFSET_POS) & \
        TPG_APP_UINT64_MBUF_TSTAMP_OFFSET_MASK)

#define DATA_GET_TSTAMP_SIZE(mbuf)                              \
    (((mbuf)->udata64 >> TPG_APP_UINT64_MBUF_TSTAMP_SIZE_POS) & \
        TPG_APP_UINT64_MBUF_TSTAMP_SIZE_MASK)

#define DATA_GET_CKSUM_OFFSET(mbuf)                              \
    (((mbuf)->udata64 >> TPG_APP_UINT64_MBUF_CKSUM_OFFSET_POS) & \
        TPG_APP_UINT64_MBUF_CKSUM_OFFSET_MASK)

/*****************************************************************************
 * DATA_SET_TSTAMP()
 *      Uses the userdata field in mbuf to mark the fact that just before
 *      sending the packet warp17 has to fill the timestamp field
 ****************************************************************************/
#define DATA_SET_TSTAMP(mbuf) \
    ((mbuf)->udata64 |= TPG_APP_UINT64_MBUF_FLAG_TSTAMP)

/*****************************************************************************
 * DATA_CLEAR_TSTAMP()
 *      Clears the TSTAMP flag in the mbuf user data field.
 ****************************************************************************/
#define DATA_CLEAR_TSTAMP(mbuf) \
    ((mbuf)->udata64 &= ~TPG_APP_UINT64_MBUF_FLAG_TSTAMP)

/*****************************************************************************
 * DATA_IS_TSTAMP()
 *     Checks the userdata field in the mbuf in order to see if warp17 had to
 *     fill the timestamp field.
 ****************************************************************************/
#define DATA_IS_TSTAMP(mbuf) \
    ((mbuf)->udata64 & TPG_APP_UINT64_MBUF_FLAG_TSTAMP)

/*****************************************************************************
 * DATA_SET_TSTAMP_MULTI()
 *      Uses the userdata field in mbuf to mark the fact that just before
 *      sending the packet warp17 has to fill the timestamp field on multiple
 *      segments of the packet mbuf.
 ****************************************************************************/
#define DATA_SET_TSTAMP_MULTI(mbuf) \
    ((mbuf)->udata64 |= TPG_APP_UINT64_MBUF_FLAG_TSTAMP_MULTI)

/*****************************************************************************
 * DATA_CLEAR_TSTAMP_MULTI()
 *      Clears the TSTAMP_MULTI flag in the mbuf user data field.
 ****************************************************************************/
#define DATA_CLEAR_TSTAMP_MULTI(mbuf) \
    ((mbuf)->udata64 &= ~TPG_APP_UINT64_MBUF_FLAG_TSTAMP_MULTI)

/*****************************************************************************
 * DATA_IS_TSTAMP_MULTI()
 *     Checks the userdata field in the mbuf in order to see if warp17 had to
 *     fill the timestamp field for multiple segments of the packet mbuf.
 ****************************************************************************/
#define DATA_IS_TSTAMP_MULTI(mbuf) \
    ((mbuf)->udata64 & TPG_APP_UINT64_MBUF_FLAG_TSTAMP_MULTI)

/*****************************************************************************
 * DATA_SET_STATIC()
 *     Uses the userdata field in the mbuf to mark the fact that the data it
 *     points to is static data.
 ****************************************************************************/
#define DATA_SET_STATIC(mbuf) \
    ((mbuf)->udata64 |= TPG_APP_UINT64_MBUF_FLAG_STATIC)

/*****************************************************************************
 * DATA_CLEAR_STATIC()
 *     Clears the STATIC flag in the mbuf user data field.
 ****************************************************************************/
#define DATA_CLEAR_STATIC(mbuf) \
    ((mbuf)->udata64 &= ~TPG_APP_UINT64_MBUF_FLAG_STATIC)

/*****************************************************************************
 * DATA_IS_STATIC()
 *     Checks the userdata field in the mbuf for the static flag which
 *     indicates the fact that the data it points to is static data.
 ****************************************************************************/
#define DATA_IS_STATIC(mbuf) \
    ((mbuf)->udata64 & TPG_APP_UINT64_MBUF_FLAG_STATIC)

/*****************************************************************************
 * DATA_SET_TX_TRACE()
 *     Uses the userdata field in the mbuf to mark the fact that the data it
 *     points to needs to be traced on TX.
 ****************************************************************************/
#define DATA_SET_TX_TRACE(mbuf) \
    ((mbuf)->udata64 |= TPG_APP_UINT64_MBUF_FLAG_TX_TRACE)

/*****************************************************************************
 * DATA_CLEAR_TX_TRACE()
 *     Clears the TX_TRACE flag in the mbuf user data field.
 ****************************************************************************/
#define DATA_CLEAR_TX_TRACE(mbuf) \
    ((mbuf)->udata64 &= ~TPG_APP_UINT64_MBUF_FLAG_TX_TRACE)

/*****************************************************************************
 * DATA_IS_TX_TRACE()
 *     Checks the userdata field in the mbuf for the TX_TRACE flag which
 *     indicates the fact that the data it points to needs to be traced on TX.
 ****************************************************************************/
#define DATA_IS_TX_TRACE(mbuf) \
    ((mbuf)->udata64 & TPG_APP_UINT64_MBUF_FLAG_TX_TRACE)

/*****************************************************************************
 * Static inlines
 ****************************************************************************/

/*****************************************************************************
 * pkt_mbuf_alloc()
 *      Wrapper on top of rte_pktmbuf_alloc which also resets the udata64
 *      field.
 ****************************************************************************/
static inline struct rte_mbuf *pkt_mbuf_alloc(struct rte_mempool *mp)
{
    struct rte_mbuf *mbuf = rte_pktmbuf_alloc(mp);

    if (likely(mbuf != NULL))
        mbuf->udata64 = 0;

    return mbuf;
}

/*****************************************************************************
 * pkt_mbuf_free()
 *      Wrapper on top of rte_pktmbuf_free.
 ****************************************************************************/
static inline void pkt_mbuf_free(struct rte_mbuf *mbuf)
{
    rte_pktmbuf_free(mbuf);
}

/*****************************************************************************
 * pkt_mbuf_free_seg()
 *      Wrapper on top of rte_pktmbuf_free_seg.
 ****************************************************************************/
static inline void pkt_mbuf_free_seg(struct rte_mbuf *mbuf)
{
    rte_pktmbuf_free_seg(mbuf);
}

#endif /* _H_TPG_MBUF_ */

