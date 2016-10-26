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
 *     tpg_ring_if.c
 *
 * Description:
 *     Ring based interface support for running WARP17 without any eth
 *     interface.
 *
 * Author:
 *     Dumitru Ceara, Eelco Chaudron
 *
 * Initial Created:
 *     06/21/2016
 *
 * Notes:
 *     Ring based interfaces only allowed if TPG_RING_IF is defined.
 *
 */

/*****************************************************************************
 * Includes
 ****************************************************************************/
#include "tcp_generator.h"

#include <rte_eth_ring.h>

/*****************************************************************************
 * Definitions
 ****************************************************************************/
/* Name lengths used for memory allocation. */
#define TPG_RING_IF_RING_NAME_SIZE 24
#define TPG_RING_IF_NAME_SIZE      24

/* Size of the ring-if queues. */
#define TPG_RING_IF_RING_SIZE      256

/* When compiling with Ring Interface support make sure that the mbuf pools are
 * not created with the single producer flag as the mbufs will be freed on the
 * RX core!
 */
#if defined(TPG_RING_IF)
static_assert(!(MEM_MBUF_POOL_FLAGS & MEMPOOL_F_SP_PUT),
              "MEM_MBUF_POOL_FLAGS contains MEMPOOL_F_SP_PUT! This will corrupt memory when using Ring Interfaces!");
#endif

/*****************************************************************************
 * Globals
 ****************************************************************************/
static uint32_t ring_if_pairs;

/*****************************************************************************
 * Static functions
 ****************************************************************************/

/*****************************************************************************
 * Global functions
 ****************************************************************************/

/*****************************************************************************
 * ring_if_get_count()
 ****************************************************************************/
uint32_t ring_if_get_count(void)
{
    return ring_if_pairs * 2;
}

/*****************************************************************************
 * ring_if_handle_cmdline_opt()
 ****************************************************************************/
bool ring_if_handle_cmdline_opt(const char *opt_name, char *opt_arg)
{
    if (strcmp(opt_name, "ring-if-pairs"))
        return false;

#if !defined(TPG_RING_IF)
    TPG_ERROR_EXIT(EXIT_FAILURE,
                   "ERROR: Recompile with TPG_RING_IF enabled for ring interface support!\n");
    return false;
#endif /* !defined(TPG_RING_IF) */

    errno = 0;
    ring_if_pairs = strtol(opt_arg, NULL, 10);
    if (errno != 0)
        TPG_ERROR_EXIT(EXIT_FAILURE, "ERROR: Invalid ring interface count!\n");

    if (rte_eth_dev_count() + ring_if_pairs * 2 > TPG_ETH_DEV_MAX)
        TPG_ERROR_EXIT(EXIT_FAILURE,
                       "ERROR: Total number of ring interfaces and ethernet ports must be less than (or equal) %u!\n",
                       TPG_ETH_DEV_MAX);

    return true;
}

/*****************************************************************************
 * ring_if_init()
 ****************************************************************************/
bool ring_if_init(void)
{
    uint32_t ring_pair_idx;
    uint32_t eth_dev_count;

    char ring_name[TPG_RING_IF_RING_NAME_SIZE];
    char if_name[TPG_RING_IF_NAME_SIZE];

    eth_dev_count = rte_eth_dev_count();

    for (ring_pair_idx = 0; ring_pair_idx < ring_if_pairs; ring_pair_idx++) {

        uint32_t         rif1 = eth_dev_count + ring_pair_idx * 2;
        uint32_t         rif2 = rif1 + 1;

        struct rte_ring *rx_rings[port_port_cfg[rif1].ppc_q_cnt];
        struct rte_ring *tx_rings[port_port_cfg[rif2].ppc_q_cnt];

        uint32_t         ring_count = port_port_cfg[rif1].ppc_q_cnt;
        uint32_t         i;

        if (ring_count == 0) {
            RTE_LOG(ERR, USER1,
                    "ERROR: Empty qmap for ring interface %u\n", rif1);
            return false;
        }

        if (ring_count != port_port_cfg[rif2].ppc_q_cnt) {
            RTE_LOG(ERR, USER1,
                    "ERROR: Number of queues must be identical for members of the same ring-if-pair!\n");
            return false;
        }

        if (ring_count > RTE_PMD_RING_MAX_RX_RINGS) {
            RTE_LOG(ERR, USER1,
                    "ERROR: Number of rx_/tx_rings(%u) is larger than ring interfaces support(%u)!\n",
                    ring_count,
                    RTE_PMD_RING_MAX_RX_RINGS);
            return false;
        }

        /* Warn if the user provided qmaps that are not on the same socket. */
        for (i = 0; i < ring_count; i++) {
            if (port_get_socket(rif1, i) != port_get_socket(rif2, i)) {
                RTE_LOG(WARNING, USER1,
                        "WARNING: Ring interface TX rings allocated on different socket than RX rings!\n");
                break;
            }
        }

        for (i = 0; i < ring_count; i++) {
            snprintf(ring_name, TPG_RING_IF_RING_NAME_SIZE, "ring_if_r%u_%u",
                     rif1,
                     i);
            rx_rings[i] = rte_ring_create(ring_name, TPG_RING_IF_RING_SIZE,
                                          port_get_socket(rif1, i),
                                          RING_F_SP_ENQ|RING_F_SC_DEQ);
            if (rx_rings[i] == NULL) {
                RTE_LOG(ERR, USER1,
                        "ERROR: Failed to allocate rx ring for ring_if!\n");
                return false;
            }

            snprintf(ring_name, TPG_RING_IF_RING_NAME_SIZE, "ring_if_r%u_%u",
                     rif2,
                     i);
            tx_rings[i] = rte_ring_create(ring_name, TPG_RING_IF_RING_SIZE,
                                          port_get_socket(rif2, i),
                                          RING_F_SP_ENQ|RING_F_SC_DEQ);
            if (tx_rings[i] == NULL) {
                RTE_LOG(ERR, USER1,
                        "ERROR: Failed to allocate tx ring for ring_if!\n");
                return false;
            }
        }

        snprintf(if_name, TPG_RING_IF_NAME_SIZE, "ring_if_%u\n", rif1);
        if (rte_eth_from_rings(if_name, rx_rings, ring_count, tx_rings,
                               ring_count,
                               port_get_socket(rif1, 0)) == -1) {
            RTE_LOG(ERR, USER1, "ERROR: Failed to create 1st ring if!\n");
            return false;
        }
        rte_eth_dev_info_get(rif1, &port_dev_info[rif1].pi_dev_info);
        port_dev_info[rif1].pi_numa_node = port_get_socket(rif1, 0);
        port_dev_info[rif1].pi_adjusted_reta_size = PORT_QCNT(rif1);
        port_dev_info[rif1].pi_ring_if = true;

        snprintf(if_name, TPG_RING_IF_NAME_SIZE, "ring_if_%u\n", rif2);
        if (rte_eth_from_rings(if_name, tx_rings, ring_count, rx_rings,
                               ring_count,
                               port_get_socket(rif1, 0)) == -1) {
            RTE_LOG(ERR, USER1, "ERROR: Failed to create 2nd ring if!\n");
            return false;
        }
        rte_eth_dev_info_get(rif2, &port_dev_info[rif2].pi_dev_info);
        port_dev_info[rif2].pi_numa_node = port_get_socket(rif2, 0);
        port_dev_info[rif2].pi_adjusted_reta_size = PORT_QCNT(rif2);
        port_dev_info[rif2].pi_ring_if = true;
    }


    return true;
}

