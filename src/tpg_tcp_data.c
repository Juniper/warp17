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
 *     tpg_tcp_data.c
 *
 * Description:
 *     TCP data processing.
 *
 * Author:
 *     Dumitru Ceara, Eelco Chaudron
 *
 * Initial Created:
 *     10/05/2015
 *
 * Notes:
 *
 */

/*****************************************************************************
 * Include files
 ****************************************************************************/
#include "tcp_generator.h"

/*****************************************************************************
 * tcp_data_store_send()
 ****************************************************************************/
static uint32_t tcp_data_store_send(tcp_control_block_t *tcb, tsm_data_arg_t *data)
{
    struct rte_mbuf *data_mbuf = data->tda_mbuf;
    tcb_retrans_t   *retrans;

    retrans = &tcb->tcb_retrans;

    if (data_mbuf->pkt_len > TCB_AVAIL_SEND(tcb)) {
        pkt_mbuf_free(data_mbuf);
        return 0;
    }

    if (retrans->tr_last_mbuf) {
        retrans->tr_last_mbuf->next = data_mbuf;
        retrans->tr_data_mbufs->nb_segs += data_mbuf->nb_segs;
    } else {
        retrans->tr_data_mbufs = data_mbuf;
    }

    retrans->tr_last_mbuf = rte_pktmbuf_lastseg(data_mbuf);

    retrans->tr_total_size += data_mbuf->pkt_len;
    retrans->tr_data_mbufs->pkt_len = retrans->tr_total_size;
    return data_mbuf->pkt_len;
}

/*****************************************************************************
 * tcp_data_get_unsent_size()
 ****************************************************************************/
static uint32_t tcp_data_get_unsent_size(tcp_control_block_t *tcb)
{
    if (tcb->tcb_retrans.tr_total_size == 0)
        return 0;

    return tcb->tcb_retrans.tr_total_size -
           (SEG_DIFF(tcb->tcb_snd.nxt, tcb->tcb_snd.una));
}

/*****************************************************************************
 * tcp_data_get_unsent()
 * WARNING: we can't return a clone here because dpdk doesn't support clones
 * of clones (i.e., indirect buffers referencing indirect buffers). The mbuf
 * chain we return here will be segmented later using rte_pktmbuf_clone!
 * This is why we return an offset inside the real data.
 ****************************************************************************/
static struct rte_mbuf *tcp_data_get_unsent(tcp_control_block_t *tcb,
                                            uint32_t *mbuf_data_offset)
{
    struct rte_mbuf *mbuf;
    tcb_retrans_t   *retrans;
    uint32_t         unsent_size;
    uint32_t         unsent_global_offset;
    uint32_t         offset;

    retrans = &tcb->tcb_retrans;
    unsent_size = tcp_data_get_unsent_size(tcb);
    unsent_global_offset = retrans->tr_total_size - unsent_size;

    mbuf = retrans->tr_data_mbufs;
    offset = 0;

    while (mbuf->data_len <= (unsent_global_offset - offset)) {
        offset += mbuf->data_len;
        mbuf = mbuf->next;
    }

    *mbuf_data_offset = (unsent_global_offset - offset);
    return mbuf;
}

/*****************************************************************************
 * tcp_data_send_segment()
 ****************************************************************************/
static bool tcp_data_send_segment(tcp_control_block_t *tcb,
                                  struct rte_mbuf *seg_data,
                                  uint32_t seg_data_len,
                                  uint32_t data_offset,
                                  uint32_t sseq,
                                  uint32_t snd_flags)
{
    struct rte_mbuf *clone;

    clone = data_clone_seg(seg_data, seg_data_len, data_offset);
    if (unlikely(!clone)) {
        INC_STATS(STATS_LOCAL(tpg_tcp_statistics_t, tcb->tcb_l4.l4cb_interface),
                  ts_failed_data_clone);
        return false;
    }

    return tcp_send_data_pkt(tcb, sseq, snd_flags, clone);
}


/*****************************************************************************
 * tcp_data_send_segments()
 * NOTE:
 *      segs SHOULDN'T be freed so we clone the mbufs we need to send
 ****************************************************************************/
static uint32_t tcp_data_send_segments(tcp_control_block_t *tcb,
                                       struct rte_mbuf *segs,
                                       uint32_t data_len,
                                       uint32_t data_offset,
                                       uint32_t sseq,
                                       uint32_t snd_flags)
{
    uint32_t sent_segs = 0;
    uint32_t sent_data = 0;

    while (sent_segs <= TCB_SEGS_PER_SEND && data_len > 0) {

        uint32_t seg_data_len = TPG_MIN(data_len, TCB_MTU(tcb));

        if (!tcp_data_send_segment(tcb, segs, seg_data_len, data_offset, sseq,
                                   snd_flags)) {
            break;
        }

        /* Update the SND.NXT pointer with the data we sent. */
        if (sseq == tcb->tcb_snd.nxt)
            tcb->tcb_snd.nxt += seg_data_len;

        sseq += seg_data_len;
        data_len -= seg_data_len;

        /* First check if we sent more than one mbuf:
         * Careful as MTU can be way bigger than the mbuf size!
         */
        while (data_offset + seg_data_len > segs->data_len) {
            seg_data_len -= segs->data_len - data_offset;
            data_offset = 0;
            segs = segs->next;
        }

        /* Update the data offset as we know for sure now that seg_data_len
         * won't take us to another mbuf.
         */
        data_offset += seg_data_len;

        sent_segs++;
        sent_data += seg_data_len;
    }

    /* returns how much we sent */
    return sent_data;
}

/*****************************************************************************
 * tcp_data_send()
 ****************************************************************************/
int tcp_data_send(tcp_control_block_t *tcb, tsm_data_arg_t *data)
{
    uint32_t         snd_flags = 0;
    uint32_t         stored_bytes;
    struct rte_mbuf *data_to_send;
    uint32_t         unsent_size;
    uint32_t         data_offset = 0;

    *data->tda_data_sent = 0;

    if (data->tda_push)
        snd_flags |= TCP_PSH_FLAG;

    if (data->tda_urg)
        snd_flags |= TCP_URG_FLAG;

    stored_bytes = tcp_data_store_send(tcb, data);
    if (stored_bytes == 0)
        return -ENOMEM;

    unsent_size = tcp_data_get_unsent_size(tcb);

    if (unlikely(!data->tda_push && unsent_size < TCB_PSH_THRESH(tcb)))
        goto done;

    /* Don't send more than the window allows us! */
    unsent_size = TPG_MIN(unsent_size, SEG_DIFF(tcb->tcb_snd.wnd,
                                                SEG_DIFF(tcb->tcb_snd.nxt,
                                                         tcb->tcb_snd.una)));

    data_to_send = tcp_data_get_unsent(tcb, &data_offset);
    if (unlikely(!data_to_send))
        assert(data_to_send);

    tcp_data_send_segments(tcb, data_to_send, unsent_size, data_offset,
                           tcb->tcb_snd.nxt,
                           TCP_ACK_FLAG | snd_flags);

done:
    if (data->tda_data_len == stored_bytes) {
        *data->tda_data_sent = stored_bytes;
        return 0;
    }

    return -EAGAIN;
}

/*****************************************************************************
 * tcp_data_handle()
 * NOTE:
 *      Should be called when a new segment is received. It might overlap with
 *      something we received before.
 *      Returns the number of bytes that were delivered.
 *      Should update tcb->tcb_rcv.nxt to the next missing sequence (even if
 *      we stored newer sequences than that).
 *      Merges the mbufs that are consecutive.
 ****************************************************************************/
uint32_t tcp_data_handle(tcp_control_block_t *tcb, packet_control_block_t *pcb,
                         uint32_t seg_seq,
                         uint32_t seg_len,
                         bool urgent __rte_unused)
{
    tcb_buf_hdr_t     new_hdr;
    tcb_buf_hdr_t    *cur;
    tcb_buf_hdr_t    *prev = NULL;
    tcb_buf_hdr_t    *seg  = NULL;
    uint32_t          delivered = 0;
    tpg_app_proto_t   app_id = tcb->tcb_l4.l4cb_app_data.ad_type;
    app_deliver_cb_t  app_deliver_cb;
    test_case_info_t *tc_info;

    if (SEG_LE(seg_seq + seg_len, tcb->tcb_rcv.nxt))
        return 0;

    /* The first part of the data might be a retransmission so just skip it. */
    if (unlikely(SEG_LT(seg_seq, tcb->tcb_rcv.nxt) &&
                    SEG_GT(seg_seq + seg_len, tcb->tcb_rcv.nxt))) {
        if (unlikely(!data_mbuf_adj(pcb->pcb_mbuf,
                                    SEG_DIFF(tcb->tcb_rcv.nxt, seg_seq)))) {
            assert(false);
        }
        seg_len -= SEG_DIFF(tcb->tcb_rcv.nxt, seg_seq);
        seg_seq = tcb->tcb_rcv.nxt;
    }

    tcb_buf_hdr_init(&new_hdr, pcb->pcb_mbuf, pcb->pcb_tstamp, seg_seq);

    /* Walk the list of tcb buffers to see where the start of the seg fits.
     * After this loop seg will point to the segment extended by the new data or
     * NULL if the list is empty.
     * This can also mean that the new segment was completely inserted as a
     * separate node in the list (in case seg_seq falls in between two already
     * existing segments).
     */
    LIST_FOREACH(cur, &tcb->tcb_rcv_buf, tbh_entry) {
        /* If it's already contained in a segment that we know then we can
         * safely try to deliver the data again.
         */
        if (TCB_RCVBUF_CONTAINED(&new_hdr, cur))
            goto deliver_data;

        /* Either we fit before the current buffer, immediately after or
         * somewhere inside it.
         */
        if (SEG_LT(new_hdr.tbh_seg_seq, cur->tbh_seg_seq)) {
            MBUF_STORE_RCVBUF_HDR(new_hdr.tbh_mbuf, &new_hdr);
            seg = MBUF_TO_RCVBUF_HDR(new_hdr.tbh_mbuf);

            if (prev == NULL)
                LIST_INSERT_HEAD(&tcb->tcb_rcv_buf, seg, tbh_entry);
            else
                LIST_INSERT_AFTER(prev, seg, tbh_entry);

            break;
        } else if (SEG_EQ(cur->tbh_seg_seq + cur->tbh_mbuf->pkt_len,
                          new_hdr.tbh_seg_seq)) {
            data_mbuf_merge(cur->tbh_mbuf, new_hdr.tbh_mbuf);
            seg = cur;
            break;
        } else if (SEG_LE(cur->tbh_seg_seq, new_hdr.tbh_seg_seq) &&
                        SEG_LT(new_hdr.tbh_seg_seq,
                               cur->tbh_seg_seq + cur->tbh_mbuf->pkt_len)) {
            if (unlikely(!data_mbuf_adj(new_hdr.tbh_mbuf,
                                        SEG_DIFF(cur->tbh_seg_seq + cur->tbh_mbuf->pkt_len,
                                                 new_hdr.tbh_seg_seq)))) {
                assert(false);
            }
            data_mbuf_merge(cur->tbh_mbuf, new_hdr.tbh_mbuf);
            seg = cur;
            break;
        }

        prev = cur;
    }

    /* Mark the pcb mbuf as stored so it doesn't get freed under our feet. */
    pcb->pcb_mbuf_stored = true;

    if (!seg) {
        MBUF_STORE_RCVBUF_HDR(new_hdr.tbh_mbuf, &new_hdr);
        seg = MBUF_TO_RCVBUF_HDR(new_hdr.tbh_mbuf);

        if (!prev)
            LIST_INSERT_HEAD(&tcb->tcb_rcv_buf, seg, tbh_entry);
        else
            LIST_INSERT_AFTER(prev, seg, tbh_entry);
    } else {
        tcb_buf_hdr_t  *old_seg = seg->tbh_entry.le_next;
        uint32_t        seg_end = seg->tbh_seg_seq + seg->tbh_mbuf->pkt_len;

        /* Consume all the following segments which are included in this one. */
        while (old_seg && SEG_GE(seg_end, old_seg->tbh_seg_seq)) {
            tcb_buf_hdr_t *tmp;

            /* If there's still something useful in the old seg then adj it. */
            if (SEG_LT(seg_end, old_seg->tbh_seg_seq + old_seg->tbh_mbuf->pkt_len)) {
                if (unlikely(!data_mbuf_adj(old_seg->tbh_mbuf,
                                            SEG_DIFF(seg_end,
                                                     old_seg->tbh_seg_seq)))) {
                    assert(false);
                }
                break;
            }

            tmp = old_seg;
            old_seg = old_seg->tbh_entry.le_next;

            LIST_REMOVE(tmp, tbh_entry);
            pkt_mbuf_free(tmp->tbh_mbuf);
        }
    }

deliver_data:
    /* Check if the head of the list is the next sequence we were waiting for
     * and then deliver the data.
     */
    if (unlikely(LIST_EMPTY(&tcb->tcb_rcv_buf)))
        assert(false);

    app_deliver_cb = APP_CALL(deliver, app_id);

    /* It's a bit ugly to use the test case here but this is the only
     * place where we need to look it up so it would be inneficient to avoid
     * doing it here but then look it up every time in the app implementation.
     */
    tc_info = TEST_GET_INFO(tcb->tcb_l4.l4cb_interface,
                            tcb->tcb_l4.l4cb_test_case_id);

    while (!LIST_EMPTY(&tcb->tcb_rcv_buf) &&
                SEG_EQ(tcb->tcb_rcv.nxt,
                       tcb->tcb_rcv_buf.lh_first->tbh_seg_seq)) {
        uint32_t seg_delivered;

        seg = tcb->tcb_rcv_buf.lh_first;
        seg_delivered = app_deliver_cb(&tcb->tcb_l4, &tcb->tcb_l4.l4cb_app_data,
                                       tc_info->tci_app_stats,
                                       seg->tbh_mbuf,
                                       seg->tbh_tstamp);
        delivered += seg_delivered;
        tcb->tcb_rcv.nxt += seg_delivered;

        if (seg_delivered == seg->tbh_mbuf->pkt_len) {
            LIST_REMOVE(seg, tbh_entry);
            /* Free the whole chain. */
            pkt_mbuf_free(seg->tbh_mbuf);
        } else {
            /* Advance in the segment with the data we delivered. */
            seg->tbh_mbuf = data_adj_chain(seg->tbh_mbuf, seg_delivered);
            break;
        }
    }

    return delivered;
}

/*****************************************************************************
 * tcp_data_retrans()
 ****************************************************************************/
uint32_t tcp_data_retrans(tcp_control_block_t *tcb)
{
    tcb_retrans_t *retrans;
    uint32_t       retrans_bytes;

    retrans = &tcb->tcb_retrans;
    if (retrans->tr_data_mbufs == NULL)
        return 0;

    retrans_bytes = tcp_data_send_segments(tcb, retrans->tr_data_mbufs,
                                           TPG_MIN(retrans->tr_total_size,
                                                   tcb->tcb_snd.wnd),
                                           0,
                                           tcb->tcb_snd.una,
                                           TCP_ACK_FLAG);
    return retrans_bytes;
}

