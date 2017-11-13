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
 *     tpg_test_app_data.h
 *
 * Description:
 *     Application data send/receive helpers and types.
 *
 * Author:
 *     Dumitru Ceara, Eelco Chaudron
 *
 * Initial Created:
 *     03/09/2016
 *
 * Notes:
 *
 */

/*****************************************************************************
 * Multiple include protection
 ****************************************************************************/
#ifndef _H_TPG_TEST_APP_DATA_
#define _H_TPG_TEST_APP_DATA_

/*****************************************************************************
 * APP_FOR_EACH_DATA_BUF_START()
 *  Walks the mbuf chain skipping the first part of the first mbuf (according
 *  to the value of the offset.
 *  Updates offset and the current mbuf. Will store in dptr and dlen a pointer
 *  to the data of the current mbuf, respectively the size of the data in the
 *  current mbuf.
 ****************************************************************************/
#define APP_FOR_EACH_DATA_BUF_START(mbuf_chain, offset, mbuf, dptr, dlen)      \
    for ((mbuf) = (mbuf_chain); (mbuf); (mbuf) = (mbuf)->next, (offset) = 0) { \
        (dptr) = rte_pktmbuf_mtod((mbuf), char *) + (offset);                  \
        (dlen) = (mbuf)->data_len - (offset);                                  \

#define APP_FOR_EACH_DATA_BUF_END() \
    }


/*****************************************************************************
 * app_data_get_from_offset()
 *  Will return the mbuf corresponding to the given offset and will store at
 *  out_offset the offset inside the returned mbuf corresponding to the given
 *  input offset.
 ****************************************************************************/
static inline
struct rte_mbuf *app_data_get_from_offset(struct rte_mbuf *mbuf,
                                          uint32_t offset,
                                          uint32_t *out_offset)
{
    if (likely(offset != mbuf->pkt_len)) {
        while (offset >= mbuf->data_len) {
            offset -= mbuf->data_len;
            mbuf = mbuf->next;
        }
    }
    *out_offset = offset;
    return mbuf;
}

/*****************************************************************************
 * Application recv/send pointers
 *  Should be used by applications that want to delay processing of incoming
 *  data until a big enough chunk is received. Similarly, the applications might
 *  want to remember where they left off while sending data.
 ****************************************************************************/
typedef struct app_recv_ptr_s {

    uint32_t arcvp_scan_offset;

} app_recv_ptr_t;

#define APP_RECV_SCAN_OFFSET(recv_ptr) ((recv_ptr)->arcvp_scan_offset)

#define APP_RECV_PTR_INIT(recv_ptr) \
    (APP_RECV_SCAN_OFFSET((recv_ptr)) = 0)

typedef struct app_send_ptr_s {

    struct rte_mbuf *asp_mbuf_chain;
    uint32_t         asp_data_offset;
    uint32_t         asp_data_len;

} app_send_ptr_t;

#define APP_SEND_PTR_LEN(send_ptr) \
    ((send_ptr)->asp_data_len)

#define APP_SEND_PTR_OFFSET(send_ptr) \
   ((send_ptr)->asp_data_offset)

#define APP_SEND_PTR(send_ptr) \
   ((send_ptr)->asp_mbuf_chain)

#define APP_SEND_PTR_INIT(send_ptr, tx_mbuf)               \
    do {                                                   \
        APP_SEND_PTR((send_ptr)) = (tx_mbuf);              \
        APP_SEND_PTR_LEN((send_ptr)) = (tx_mbuf)->pkt_len; \
        APP_SEND_PTR_OFFSET((send_ptr)) = 0;               \
    } while (0)

/*****************************************************************************
 * app_send_ptr_advance()
 ****************************************************************************/
static inline void app_send_ptr_advance(app_send_ptr_t *send, uint32_t count)
{
    uint32_t first_elem_size;

    /* Update the final length in advance so we can reuse the count variable. */
    APP_SEND_PTR_LEN(send) -= count;

    first_elem_size = APP_SEND_PTR(send)->data_len - APP_SEND_PTR_OFFSET(send);

    if (APP_SEND_PTR_OFFSET(send)) {
        if (count < first_elem_size) {
            APP_SEND_PTR_OFFSET(send) += count;
            return;
        } else if (count == first_elem_size) {
            APP_SEND_PTR_OFFSET(send) = 0;
            APP_SEND_PTR(send) = APP_SEND_PTR(send)->next;
            return;
        } else {
            APP_SEND_PTR_OFFSET(send) = 0;
            APP_SEND_PTR(send) = APP_SEND_PTR(send)->next;
            count -= first_elem_size;
        }
    }

    while (count && count >= APP_SEND_PTR(send)->data_len) {
        count -= APP_SEND_PTR(send)->data_len;
        APP_SEND_PTR(send) = APP_SEND_PTR(send)->next;
    }

    APP_SEND_PTR_OFFSET(send) = count;
}

#endif /* _H_TPG_TEST_APP_DATA_ */

