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
 *     tpg_data.h
 *
 * Description:
 *     Data helper functions.
 *
 * Author:
 *     Dumitru Ceara, Eelco Chaudron
 *
 * Initial Created:
 *     10/23/2015
 *
 * Notes:
 *
 */

 /*****************************************************************************
 * Multiple include protection
 ****************************************************************************/
#ifndef _H_TPG_DATA_
#define _H_TPG_DATA_

/*****************************************************************************
 * data_seg_attach()
 * NOTE:
 *      In case m was marked as STATIC this function emulates what
 *      rte_pktmbuf_attach does but doesn't mark mi to be indirect and
 *      doesn't increment the refcount on m. This is safe because the payload
 *      memory pointed to by m->buf_addr should NEVER be freed.
 *      Please see the contract for the APP send API in tpg_test_app.h for
 *      more information.
 ****************************************************************************/
static inline void data_seg_attach(struct rte_mbuf *mi, struct rte_mbuf *m)
{
    if (DATA_IS_STATIC(m)) {
        mi->priv_size = m->priv_size;
        mi->buf_physaddr = m->buf_physaddr;
        mi->buf_addr = m->buf_addr;
        mi->buf_len = m->buf_len;

        mi->next = m->next;
        mi->data_off = m->data_off;
        mi->data_len = m->data_len;
        mi->port = m->port;
        mi->vlan_tci = m->vlan_tci;
        mi->vlan_tci_outer = m->vlan_tci_outer;
        mi->tx_offload = m->tx_offload;
        mi->hash = m->hash;

        mi->next = NULL;
        mi->pkt_len = mi->data_len;
        mi->nb_segs = 1;
        mi->ol_flags = m->ol_flags;
        mi->packet_type = m->packet_type;
    } else {
        rte_pktmbuf_attach(mi, m);
    }
}

/*****************************************************************************
 * data_seg_tstamp_attach()
 * NOTE:
 *      Returns true if the cloned mbuf (mi) overlaps with a portion of the
 *      direct mbuf (m) which requires timestamping. In that case also update
 *      mi with timestamping information.
 ****************************************************************************/
static inline bool
data_seg_tstamp_attach(struct rte_mbuf *mi, struct rte_mbuf *m,
                       uint32_t m_data_offset)
{
    uint32_t tstamp_offset;
    uint32_t tstamp_size;

    if (likely(!DATA_IS_TSTAMP(m)))
        return false;

    tstamp_offset = DATA_GET_TSTAMP_OFFSET(m);
    tstamp_size = DATA_GET_TSTAMP_SIZE(m);

    /* If the indirect mbuf overlaps with the timestamp location then update
     * the indirect mbuf.
     */
    if (tstamp_offset >= m_data_offset &&
            tstamp_offset < m_data_offset + mi->data_len) {
        tstamp_tx_pkt(mi, tstamp_offset - m_data_offset, tstamp_size);
        return true;
    }

    return false;
}

/*****************************************************************************
 * data_alloc_chain
 ****************************************************************************/
static inline struct rte_mbuf *data_alloc_chain(struct rte_mempool *mpool,
                                                uint32_t data_len)
{
    struct rte_mbuf  *data_mbufs = NULL;
    struct rte_mbuf  *data_mbuf = NULL;
    struct rte_mbuf **prev_mbuf = NULL;
    uint32_t          nb_segs = 0;
    uint32_t          pkt_len = data_len;

    data_mbuf = pkt_mbuf_alloc(mpool);
    if (unlikely(!data_mbuf))
        goto done;

    data_mbufs = data_mbuf;
    prev_mbuf = &data_mbuf->next;

    do {
        uint32_t req_len = TPG_MIN(data_len, rte_pktmbuf_tailroom(data_mbuf));

        nb_segs++;
        rte_pktmbuf_append(data_mbuf, req_len);
        *prev_mbuf = data_mbuf;
        prev_mbuf = &data_mbuf->next;
        data_len -= req_len;
    } while (data_len && (data_mbuf = pkt_mbuf_alloc(mpool)));

    *prev_mbuf = NULL;
    data_mbufs->nb_segs = nb_segs;
    data_mbufs->pkt_len = pkt_len;

done:
    if (data_mbufs && !data_mbuf) {
        /* Failed! pkt_mbuf_free frees the whole chain!!! */
        pkt_mbuf_free(data_mbufs);
        return NULL;
    }

    return data_mbufs;
}

/*****************************************************************************
 * data_copy_chain()
 ****************************************************************************/
static inline struct rte_mbuf *data_copy_chain(struct rte_mbuf *original,
                                               struct rte_mempool *mpool)
{
    struct rte_mbuf  *data_mbufs = NULL;
    struct rte_mbuf  *data_mbuf = NULL;
    struct rte_mbuf **prev_mbuf = NULL;
    uint32_t          nb_segs = 0;
    uint32_t          pkt_len = original->pkt_len;

    data_mbuf = pkt_mbuf_alloc(mpool);
    if (unlikely(!data_mbuf))
        goto done;

    data_mbufs = data_mbuf;
    prev_mbuf = &data_mbuf->next;

    do {
        char *new_seg_data;

        nb_segs++;
        new_seg_data = rte_pktmbuf_append(data_mbuf, original->data_len);

        *prev_mbuf = data_mbuf;

        if (unlikely(!new_seg_data))
            goto done;
        rte_memcpy(new_seg_data, rte_pktmbuf_mtod(original, char *),
                   original->data_len);

        prev_mbuf = &data_mbuf->next;
        original = original->next;
    } while (original && (data_mbuf = pkt_mbuf_alloc(mpool)));

    *prev_mbuf = NULL;
    data_mbufs->nb_segs = nb_segs;
    data_mbufs->pkt_len = pkt_len;

done:
    if (data_mbufs && !data_mbuf) {
        /* Failed! pkt_mbuf_free frees the whole chain!!! */
        pkt_mbuf_free(data_mbufs);
        return NULL;
    }

    return data_mbufs;
}

/*****************************************************************************
 * data_adj_chain()
 *  In some specific situation (for instance ring interfaces) the mbuf chain
 *  is fragmented or it has len 0 (mbufs?). Here we try to fix the whole data
 *  into a consistent chain
 ****************************************************************************/
static inline
struct rte_mbuf *data_adj_chain(struct rte_mbuf *mbuf, uint32_t len)
{
    struct rte_mbuf *prev;
    uint32_t         new_pkt_len = mbuf->pkt_len - len;

    while (len > 0 && len >= mbuf->data_len) {
        prev = mbuf;
        len -= mbuf->data_len;
        mbuf = mbuf->next;
        pkt_mbuf_free_seg(prev);
    }

    if (len != 0) {
        assert(mbuf != NULL && mbuf->data_len >= len);
        rte_pktmbuf_adj(mbuf, len);
    }

    if (mbuf)
        mbuf->pkt_len = new_pkt_len;

    return mbuf;
}

/* WARNING: this is a hack to avoid copying data!
 * Might not be very portable!!!
 */
#define DATA_MBUF_FROM_TEMPLATE(m, t_addr, t_phys, len) \
    do {                                                \
        (m)->buf_addr = (t_addr);                       \
        (m)->buf_physaddr = (t_phys);                   \
        (m)->data_off = 0;                              \
        (m)->buf_len = (len);                           \
        (m)->data_len = (len);                          \
    } while (0)

/*****************************************************************************
 * data_chain_from_static_template()
 *      NOTE: duplicates the template as many times needed to fill the
 *            requested data len. Already marks all the mbufs in the chain as
 *            STATIC.
 ****************************************************************************/
static inline
struct rte_mbuf *data_chain_from_static_template(uint32_t data_len,
                                                 uint8_t *template,
                                                 phys_addr_t template_physaddr,
                                                 uint32_t template_len)
{
    struct rte_mempool  *mpool = mem_get_mbuf_local_pool_clone();
    struct rte_mbuf     *data_mbufs = NULL;
    struct rte_mbuf     *data_mbuf = NULL;
    struct rte_mbuf    **prev_mbuf = NULL;
    uint32_t             nb_segs = 0;
    uint32_t             pkt_len;

    if (data_len == 0)
        return NULL;

    data_mbuf = pkt_mbuf_alloc(mpool);
    if (unlikely(!data_mbuf))
        goto done;

    /* If data length is less than the mbuf len */
    if (data_len <= template_len) {
        DATA_SET_STATIC(data_mbuf);
        DATA_MBUF_FROM_TEMPLATE(data_mbuf, template, template_physaddr,
                                data_len);
        data_mbuf->nb_segs = 1;
        data_mbuf->pkt_len = data_len;
        return data_mbuf;
    }


    pkt_len   = data_len;
    prev_mbuf = &data_mbufs;

    do {
        DATA_SET_STATIC(data_mbuf);
        DATA_MBUF_FROM_TEMPLATE(data_mbuf, template, template_physaddr,
                                template_len);
        nb_segs++;
        *prev_mbuf = data_mbuf;
        prev_mbuf = &data_mbuf->next;
        data_len -= data_mbuf->data_len;
    } while (data_len >= template_len && (data_mbuf = pkt_mbuf_alloc(mpool)));

    *prev_mbuf = NULL;

    if (!data_mbuf)
        goto done;

    /* Allocate the last smaller segment if required. */
    if (data_len && data_mbuf) {
        data_mbuf = pkt_mbuf_alloc(mpool);
        if (!data_mbuf)
            goto done;

        DATA_SET_STATIC(data_mbuf);
        DATA_MBUF_FROM_TEMPLATE(data_mbuf, template, template_physaddr,
                                data_len);
        nb_segs++;
        *prev_mbuf = data_mbuf;
        data_mbuf->next = NULL;
    }

    data_mbufs->nb_segs = nb_segs;
    data_mbufs->pkt_len = pkt_len;

done:
    if (data_mbufs && !data_mbuf) {
        /* Failed! pkt_mbuf_free frees the whole chain!!! */
        pkt_mbuf_free(data_mbufs);
        return NULL;
    }

    return data_mbufs;
}

/*****************************************************************************
 * data_chain_from_static_chain()
 ****************************************************************************/
static inline
struct rte_mbuf *data_chain_from_static_chain(struct rte_mbuf *static_chain,
                                              uint32_t data_offset,
                                              uint32_t data_len)
{
    struct rte_mempool  *mpool = mem_get_mbuf_local_pool_clone();
    struct rte_mbuf     *data_mbufs = NULL;
    struct rte_mbuf     *data_mbuf = NULL;
    struct rte_mbuf    **prev_mbuf = NULL;
    uint32_t             nb_segs = 0;

    if (data_len == 0)
        return NULL;

    if (static_chain == NULL)
        return NULL;

    data_mbuf = pkt_mbuf_alloc(mpool);
    if (unlikely(!data_mbuf))
        goto done;

    data_mbuf->pkt_len = data_len;
    prev_mbuf          = &data_mbufs;

    do {
        DATA_SET_STATIC(data_mbuf);

        /* Copy static data information. */
        data_mbuf->buf_addr = static_chain->buf_addr;
        data_mbuf->buf_physaddr = static_chain->buf_physaddr;
        data_mbuf->data_off = static_chain->data_off + data_offset;
        data_mbuf->buf_len = static_chain->buf_len;
        data_mbuf->data_len = TPG_MIN(static_chain->data_len - data_offset,
                                      data_len);

        nb_segs++;
        *prev_mbuf = data_mbuf;
        prev_mbuf = &data_mbuf->next;
        data_len -= data_mbuf->data_len;
        data_offset = 0;
    } while (data_len && (static_chain = static_chain->next) &&
                (data_mbuf = pkt_mbuf_alloc(mpool)));

    *prev_mbuf = NULL;
    data_mbufs->nb_segs = nb_segs;

done:
    if (data_mbufs && !data_mbuf) {
        /* Failed! pkt_mbuf_free frees the whole chain!!! */
        pkt_mbuf_free(data_mbufs);
        return NULL;
    }

    return data_mbufs;
}

/*****************************************************************************
 * data_clone_seg()
 * NOTE:
 *      TODO: Need to optimize this?
 ****************************************************************************/
static inline struct rte_mbuf *data_clone_seg(struct rte_mbuf *seg_data,
                                              uint32_t seg_data_len,
                                              uint32_t data_offset)
{
    struct rte_mempool *clone_pool = mem_get_mbuf_local_pool_clone();
    struct rte_mbuf    *clone;
    struct rte_mbuf    *prev_clone = NULL;
    struct rte_mbuf    *clone_seg = NULL;
    uint32_t            nb_segs = 0;
    uint32_t            tstamp_count = 0;
    uint32_t            current_clone_seg_len = 0;

    if (unlikely(!seg_data_len))
        return NULL;

    while (data_offset >= seg_data->data_len) {
        data_offset -= seg_data->data_len;
        seg_data = seg_data->next;
    }

    do {
        clone = pkt_mbuf_alloc(clone_pool);
        if (unlikely(!clone))
            goto failed;

        data_seg_attach(clone, seg_data);

        /* If this is the first mbuf we might need to adjust the offset. */
        if (!clone_seg) {
            clone_seg = clone;

            assert(rte_pktmbuf_adj(clone, data_offset));
            clone_seg->pkt_len = seg_data_len;
        } else {
            prev_clone->next = clone;
        }

        if (seg_data_len <= clone->data_len) {
            clone->data_len = seg_data_len;
            clone->next = NULL;
            seg_data_len = 0;
        } else {
            seg_data_len -= clone->data_len;
        }

        /* If the underlying data was marked for timestamping then propagate
         * that information in the clone chain. However, careful as there
         * might be multiple segments that need timestamping.
         */
        if (unlikely(data_seg_tstamp_attach(clone, seg_data, data_offset))) {
            /* Timestamping is a one-time thing.
             * TODO: we assume that the cloned data will actually be sent on
             * the wire and that the "real" data will be timestamped. Revisit
             * this code when the behaviour of callers changes.
             */
            DATA_CLEAR_TSTAMP(seg_data);

            /* Already store in the first clone segment the tstamp position. */
            if (likely(tstamp_count == 0)) {
                tstamp_tx_pkt(clone_seg,
                              current_clone_seg_len +
                                    (uint32_t)DATA_GET_TSTAMP_OFFSET(clone),
                              (uint32_t)DATA_GET_TSTAMP_SIZE(clone));
            }
            tstamp_count++;
        }

        current_clone_seg_len += clone->data_len;
        prev_clone = clone;
        nb_segs++;
        seg_data = seg_data->next;
    } while (seg_data_len);

    clone_seg->nb_segs = nb_segs;

    if (unlikely(tstamp_count > 1)) {
        /* Mark that there are multiple timestamps to add in order to let the
         * caller know that the all segments in the clone chain must be checked
         * to see if timestamping should be performed.
         */
        DATA_SET_TSTAMP_MULTI(clone_seg);
    }

    return clone_seg;

failed:
    if (clone_seg) {
        /* Will free the whole chain!!! */
        pkt_mbuf_free(clone_seg);
    }
    return NULL;
}

/*****************************************************************************
 * data_mbuf_merge()
 ****************************************************************************/
static inline void data_mbuf_merge(struct rte_mbuf *dest, struct rte_mbuf *src)
{
    rte_pktmbuf_lastseg(dest)->next = src;
    dest->nb_segs += src->nb_segs;
    dest->pkt_len += src->pkt_len;
}

/*****************************************************************************
 * data_mbuf_mtod_offset()
 *      Extend the rte_pktmbuf_mtod_offset functionality for chains of mbufs.
 ****************************************************************************/
static inline char *data_mbuf_mtod_offset(struct rte_mbuf *mbuf,
                                          uint32_t offset)
{
    while (mbuf && offset > mbuf->data_len) {
        offset -= mbuf->data_len;
        mbuf = mbuf->next;
    }

    if (unlikely(mbuf == NULL))
        return NULL;

    return rte_pktmbuf_mtod_offset(mbuf, char *, offset);
}

#endif /* _H_TPG_DATA_ */

