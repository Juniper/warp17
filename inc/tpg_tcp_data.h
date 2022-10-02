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
 *     tpg_tcp_data.h
 *
 * Description:
 *     TCP data processing
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
 * Multiple include protection
 ****************************************************************************/
#ifndef _H_TPG_TCP_DATA_
#define _H_TPG_TCP_DATA_

/*****************************************************************************
 * TCP MTU related macros
 ****************************************************************************/
/* TODO: doesn't include any potential options (IP+TCP). */
#define TCB_MIN_HDRS_SZ                                   \
    (sizeof(struct ether_hdr) + sizeof(struct ipv4_hdr) + \
     sizeof(struct tcp_hdr) + ETHER_CRC_LEN)

/*
 * Quite an ugly hack to test that we have room to store the
 * receive buffer header in the received mbufs. We make the assumption
 * that the L2-L4 headers will always be part of the first mbuf.
 */
static_assert(sizeof(tcb_buf_hdr_t) <= TCB_MIN_HDRS_SZ,
              "Not enough headroom in the mbuf!");

#define TCP_MTU(port_info, sockopt)                                         \
    ((port_info)->pi_mtu - ipv4_get_sockopt((sockopt))->ip4so_hdr_opt_len - \
     vlan_get_sockopt((sockopt))->vlanso_hdr_opt_len -                      \
     TCB_MIN_HDRS_SZ)

#define TCP_GLOBAL_MTU(port, sockopt) \
    TCP_MTU(&port_dev_info[(port)], (sockopt))

#define TCB_MTU(tcb)                                                           \
    TCP_MTU(&RTE_PER_LCORE(local_port_dev_info)[(tcb)->tcb_l4.l4cb_interface], \
    &(tcb)->tcb_l4.l4cb_sockopt)

/*****************************************************************************
 * TCP Send related macros
 ****************************************************************************/
/* In theory we could store more than the window size but that would
 * just waste memory. Keep it like this for now.
 */
#define TCB_MAX_TX_BUF_SZ(sockopt) \
    (tcp_get_sockopt((sockopt))->tcpo_win_size)

#define TCB_SEGS_PER_SEND GCFG_TCP_SEGS_PER_SEND

#define TCP_AVAIL_SEND(port_info, sockopt, retrans_size)         \
    TPG_MIN(TCB_MAX_TX_BUF_SZ(sockopt) - (retrans_size),         \
            TCB_SEGS_PER_SEND * TCP_MTU((port_info), (sockopt)))

#define TCP_GLOBAL_AVAIL_SEND(port, sockopt) \
    TCP_AVAIL_SEND(&port_dev_info[(port)], (sockopt), 0)

#define TCB_AVAIL_SEND(tcb)                                                           \
    TCP_AVAIL_SEND(&RTE_PER_LCORE(local_port_dev_info)[(tcb)->tcb_l4.l4cb_interface], \
                   &(tcb)->tcb_l4.l4cb_sockopt,                                       \
                   (tcb)->tcb_retrans.tr_total_size)

/* TODO: we only support PUSH SEND for now but when we support more this should
 * be rethought.
 */
#define TCB_PSH_THRESH(tcb) TCB_MTU(tcb)

extern int      tcp_data_send(tcp_control_block_t *tcb, tsm_data_arg_t *data);

extern uint32_t tcp_data_handle(tcp_control_block_t *tcb,
                                packet_control_block_t *pcb,
                                uint32_t seg_seq,
                                uint32_t seg_len,
                                bool urgent);

extern uint32_t tcp_data_retrans(tcp_control_block_t *tcb);

#endif /* _H_TPG_TCP_DATA_ */

