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
 *     tpg_kni_if.c
 *
 * Description:
 *     Kernel Networking Interface interface support for running WARP17 with a
 *     interface into the underlying linux kernel.
 *
 * Author:
 *     Dumitru Ceara, Eelco Chaudron
 *
 * Initial Created:
 *     10/12/2016
 *
 * Notes:
 *     Kernel Network Interfaces only allowed if TPG_KNI_IF is defined.
 *
 */

/*****************************************************************************
 * Includes
 ****************************************************************************/
#include "tcp_generator.h"

/*****************************************************************************
 * Definitions
 ****************************************************************************/
static int kni_change_mtu(uint8_t port, unsigned mtu);
static int kni_config_network_if(uint8_t port, uint8_t if_state);

/*****************************************************************************
 * Globals
 ****************************************************************************/
static uint32_t         kni_interfaces;
static struct rte_kni **kni_if;

/*****************************************************************************
 * kni_if_get_count()
 ****************************************************************************/
uint32_t kni_if_get_count(void)
{
    return kni_interfaces;
}

/*****************************************************************************
 * kni_get_first_kni_interface()
 ****************************************************************************/
uint32_t kni_get_first_kni_interface(void)
{
    return rte_eth_dev_count() - kni_if_get_count();
}

/*****************************************************************************
 * kni_handle_kernel_status_requests()
 ****************************************************************************/
void kni_handle_kernel_status_requests(void)
{
    uint32_t kni_port;

    for (kni_port = 0; kni_port < kni_if_get_count(); kni_port++)
        rte_kni_handle_request(kni_if[kni_port]);
}

/*****************************************************************************
 * kni_cores_in_mask()
 ****************************************************************************/
static unsigned int kni_cores_in_mask(uint64_t mask)
{
    int          x;
    unsigned int bits_set = 0;

    for (x = 0; x < 64; x++) {
        if (((1LL << x) & mask) != 0)
            bits_set++;
    }
    return bits_set;
}

/*****************************************************************************
 * kni_if_handle_cmdline_opt()
 ****************************************************************************/
cmdline_arg_parser_res_t kni_if_handle_cmdline_opt(const char *opt_name,
                                                   char *opt_arg)
{
    unsigned long  val;
    char          *endptr;

    if (strcmp(opt_name, "kni-ifs"))
        return CAPR_IGNORED;

#if !defined(TPG_KNI_IF)
    printf("ERROR: Recompile with TPG_KNI_IF set for Kernel Networking Interface support!\n");
    return CAPR_ERROR;
#endif /* !defined(TPG_KNI_IF) */

    errno = 0;
    val = strtoul(opt_arg, &endptr, 10);
    if ((errno == ERANGE && val == ULONG_MAX) ||
            (errno != 0 && val == 0) ||
            *endptr != '\0' ||
            val > UINT32_MAX) {
        printf("ERROR: Invalid Kernel Networking Interface count %s!\n",
               opt_arg);
        return CAPR_ERROR;
    }

    kni_interfaces = val;
    return CAPR_CONSUMED;
}

/*****************************************************************************
 * kni_if_init()
 ****************************************************************************/
bool kni_if_init(void)
{
    uint32_t         kni_port;
    global_config_t *cfg;

    if (kni_if_get_count() == 0)
        return true;

    cfg = cfg_get_config();
    if (cfg == NULL)
        return false;

    RTE_LOG(WARNING, USER1,
            "WARNING: Changing the MTU for KNI ports MUST be done trough WARP17!\n");

    /*
     * This check we can not do at command line verification, as we do not
     * know how many ring interfaces we are going to create!
     *
     * NOTE: It's missing the ring_if_get_count() below, as it's init function
     *       has been called at this stage, so they are included in the
     *       rte_eth_dev_count()!
     */
    if ((rte_eth_dev_count() + kni_if_get_count()) > TPG_ETH_DEV_MAX) {
        TPG_ERROR_EXIT(EXIT_FAILURE,
                       "ERROR: Total number of virtual interfaces and ethernet ports must be less than (or equal to) %u!\n",
                       TPG_ETH_DEV_MAX);
        return false;
    }

    /*
     * Allocate memory to store kni pointers
     */

    kni_if = rte_zmalloc("kni_port",
                         sizeof(void *) * kni_if_get_count(), 0);

    if (kni_if == NULL) {
        RTE_LOG(ERR, USER1,
                "ERROR: Failed allocating memory for KNI interfaces!\n");
        return false;
    }

    /*
     * Initialize the KNI dpdk subsystem, no return codes :(
     * It will panic if the kernel module is not loaded...
     */
    rte_kni_init(kni_if_get_count());

    /*
     * Create the individual kernel interfaces
     */

    for (kni_port = 0; kni_port < kni_if_get_count(); kni_port++) {

        struct rte_kni         *kni;
        struct rte_kni_conf     conf;
        struct rte_kni_ops      ops;
        struct rte_eth_dev_info dev_info;
        uint32_t                port = rte_eth_dev_count();

        if (port_port_cfg[port].ppc_q_cnt != kni_cores_in_mask(port_port_cfg[port].ppc_core_mask))
            TPG_ERROR_ABORT("ERROR: Assigned lcores should be the same as number of queues!");

        if (kni_cores_in_mask(port_port_cfg[port].ppc_core_mask) != 1) {
            RTE_LOG(ERR, USER1,
                    "ERROR: Kernel Network Interfaces (warp%u), can only be assigned to a single core!\n",
                    port);
            return false;
        }

        memset(&conf, 0, sizeof(conf));

        conf.core_id = 0;     /* Currently don't bind kernel side to a core */
        conf.force_bind = 0;

        snprintf(conf.name, RTE_KNI_NAMESIZE,
                 "warp%u", port);
        conf.group_id = (uint16_t) port;

        /*
         * We subtract the RTE_PKTMBUF_HEADROOM from the size here,
         * as the KNI kernel driver does not look a the mbuf fields
         * to determine the bytes it can write. Passing the full size
         * of the mbuf will overwrite/trash memory.
         */
        conf.mbuf_size = cfg->gcfg_mbuf_size - RTE_PKTMBUF_HEADROOM;

        memset(&dev_info, 0, sizeof(dev_info));
        memset(&ops, 0, sizeof(ops));
        ops.port_id = port;
        ops.change_mtu = kni_change_mtu;
        ops.config_network_if = kni_config_network_if;

        kni = rte_kni_alloc(mem_get_mbuf_pool(port, 0), &conf, &ops);

        if (kni == NULL) {
            RTE_LOG(ERR, USER1,
                    "ERROR: Failed to create Kernel Networking Interface %u!\n",
                    port);
            return false;
        }

        kni_if[kni_port] = kni;

        if (!kni_eth_from_kni(conf.name, kni, port_get_socket(port, 0))) {
            RTE_LOG(ERR, USER1,
                    "ERROR: Failed to create Kernel Networking Interface %u's ethernet interface!\n",
                    port);
            return false;
        }

        rte_eth_dev_info_get(port, &port_dev_info[port].pi_dev_info);
        port_dev_info[port].pi_numa_node = port_get_socket(port, 0);
        port_dev_info[port].pi_adjusted_reta_size = PORT_QCNT(port);
        port_dev_info[port].pi_ring_if = false;
        port_dev_info[port].pi_kni_if = true;
    }

    return true;
}

/*****************************************************************************
 * kni_change_mtu()
 ****************************************************************************/
static int kni_change_mtu(uint8_t port, unsigned mtu)
{
    global_config_t *cfg;

    if (test_mgmt_get_port_env(port)->te_test_running) {
        RTE_LOG(ERR, USER1,
                "ERROR: Can't change MTU for port %u as test is running!\n",
                port);

        /*
         * Although we report the error here, its not propagated to the user/
         * kernel who tried to change the MTU. So the kernel sets the MTU just
         * fine, so we hope the error above is enough for now ;)
         */
        return -EINVAL;
    }

    cfg = cfg_get_config();
    if (cfg == NULL || mtu > (cfg->gcfg_mbuf_size - RTE_PKTMBUF_HEADROOM)) {

        RTE_LOG(ERR, USER1,
                "ERROR: Requested MTU, %u, for port %u higher than mbuf size, %u!\n",
                mtu,
                port,
                cfg->gcfg_mbuf_size - RTE_PKTMBUF_HEADROOM);

        return -EINVAL;
    }

    if (mtu < ETHER_MIN_LEN) {
        RTE_LOG(ERR, USER1,
                "ERROR: Requested MTU, %u, for port %u smaller than minimal ethernet size, %u!\n",
                mtu,
                port,
                ETHER_MIN_LEN);
        return -EINVAL;
    }

    /*
     * Copy new MTU to global structure, ports will get a copy on test
     * start in pktloop.
     */
    port_dev_info[port].pi_mtu = mtu;

    return 0;
}

/*****************************************************************************
 * kni_config_network_if()
 ****************************************************************************/
static int kni_config_network_if(uint8_t port, uint8_t if_state)
{
    RTE_LOG(DEBUG, USER1, "KNI: Configure network interface of %d %s\n",
            port, if_state ? "up" : "down");
    /*
     * Nothing needs to be done here, as we will call the internals from
     * DPDK to make the PMD port to be up.
     */
    return 0;
}
