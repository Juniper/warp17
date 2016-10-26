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
 *     Kernel Networking Interface PMD adaption layer so the KNI device
 *     will look like its a normal port.
 *
 * Author:
 *     Dumitru Ceara, Eelco Chaudron
 *
 * Initial Created:
 *     10/12/2016
 *
 * Notes:
 *     Kernel Network Interfaces PMD virtual driver
 *
 */

/*****************************************************************************
 * Includes
 ****************************************************************************/
#include "tcp_generator.h"

#include <unistd.h>
#include <linux/if.h>
#include <linux/netlink.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <sys/ioctl.h>

/*****************************************************************************
 * Static variables
 ****************************************************************************/
static const  char         *kni_pmd_driver_name = "WARP KNI PMD";

static struct rte_eth_link  kni_pmd_link = {
    .link_speed   = ETH_SPEED_NUM_10G,
    .link_duplex  = ETH_LINK_FULL_DUPLEX,
    .link_status  = ETH_LINK_DOWN,
    .link_autoneg = ETH_LINK_SPEED_AUTONEG
};


/*****************************************************************************
 * Local definitions
 ****************************************************************************/
typedef struct nl_req_s {
    struct nlmsghdr  hdr;
    struct ifinfomsg ifi;
} nl_req_t;

/*****************************************************************************
 * kni_get_kernel_if_mac()
 ****************************************************************************/
static bool kni_get_kernel_if_mac(uint32_t port, struct ether_addr *mac)
{
    int          fd;
    struct ifreq if_req;

    memset(&if_req, 0, sizeof(if_req));

    fd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd != -1) {
        snprintf(if_req.ifr_name, sizeof(if_req.ifr_name),
                 "warp%u", port);

        if (ioctl(fd, SIOCGIFHWADDR, &if_req) != 0) {
            RTE_LOG(DEBUG, PMD,
                    "Requesting if mac for warp%u failed, errno = %d\n",
                    port, errno);

            close(fd);
            return false;
        }
        close(fd);
    }

    memcpy(mac->addr_bytes, if_req.ifr_hwaddr.sa_data, sizeof(mac->addr_bytes));
    RTE_LOG(DEBUG, PMD, "Requesting if mac for warp%u: %02X:%02X:%02X:%02X:%02X:%02X\n",
            port,
            mac->addr_bytes[0],
            mac->addr_bytes[1],
            mac->addr_bytes[2],
            mac->addr_bytes[3],
            mac->addr_bytes[4],
            mac->addr_bytes[5]);

    return true;
}

/*****************************************************************************
 * kni_get_kernel_if_stats()
 ****************************************************************************/
static bool kni_get_kernel_if_stats(uint32_t port,
                                    struct rtnl_link_stats64 *nl_stats)
{
    int                if_index;
    int                nl_fd;
    struct nl_req_s    nl_req;
    struct msghdr      nl_msg;
    struct sockaddr_nl src_addr;
    struct sockaddr_nl dst_addr;
    struct iovec       io;
    struct ifreq       ifr;
    char               reply_buffer[8*1024];
    ssize_t            length;

    /*
     * Get interface's kernel ifindex
     */

    nl_fd = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (nl_fd < 0)
        return false;

    snprintf(ifr.ifr_name, sizeof(ifr.ifr_name), "warp%u", port);

    if (ioctl(nl_fd, SIOCGIFINDEX, &ifr) < 0) {
        close(nl_fd);
        return false;
    }

    if_index = ifr.ifr_ifindex;

    close(nl_fd);

    /*
     * Open netlink socket
     */
    nl_fd = socket(PF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
    if (nl_fd < 0)
        return false;

    memset(&src_addr, 0, sizeof(src_addr));
    src_addr.nl_family = AF_NETLINK;
    src_addr.nl_pid = getpid();
    bind(nl_fd, (struct sockaddr *)&src_addr, sizeof(src_addr));

    /*
     * Prepare message to send
     */
    memset(&io, 0, sizeof(io));
    memset(&nl_msg, 0, sizeof(nl_msg));
    memset(&dst_addr, 0, sizeof(dst_addr));
    memset(&nl_req, 0, sizeof(nl_req));

    dst_addr.nl_family = AF_NETLINK;

    nl_req.hdr.nlmsg_len = NLMSG_LENGTH(sizeof(struct ifinfomsg));
    nl_req.hdr.nlmsg_type = RTM_GETLINK;
    nl_req.hdr.nlmsg_flags = NLM_F_REQUEST;
    nl_req.hdr.nlmsg_seq = 1;
    nl_req.hdr.nlmsg_pid = getpid();
    nl_req.ifi.ifi_family = PF_UNSPEC;
    nl_req.ifi.ifi_index = if_index;

    io.iov_base = &nl_req;
    io.iov_len = nl_req.hdr.nlmsg_len;
    nl_msg.msg_iov = &io;
    nl_msg.msg_iovlen = 1;
    nl_msg.msg_name = &dst_addr;
    nl_msg.msg_namelen = sizeof(dst_addr);

    /*
     * Send request
     */

    if (sendmsg(nl_fd, (struct msghdr *) &nl_msg, 0) < 0) {
        close(nl_fd);
        return false;
    }

    /*
     * Get response
     */
    memset(&io, 0, sizeof(io));
    memset(&nl_msg, 0, sizeof(nl_msg));
    io.iov_base = reply_buffer;
    io.iov_len = sizeof(reply_buffer);
    nl_msg.msg_iov = &io;
    nl_msg.msg_iovlen = 1;
    nl_msg.msg_name = &dst_addr;
    nl_msg.msg_namelen = sizeof(dst_addr);

    length = recvmsg(nl_fd, &nl_msg, 0);

    /*
     * Process response, and grep stats
     */

    if (length > 0) {
        struct nlmsghdr *msg_hdr;

        for (msg_hdr = (struct nlmsghdr *) reply_buffer;
             NLMSG_OK(msg_hdr, length);
             msg_hdr = NLMSG_NEXT(msg_hdr, length)) {

            if (msg_hdr->nlmsg_type == RTM_NEWLINK) {

                int               if_msg_len;
                struct ifinfomsg *if_msg;
                struct rtattr    *attribute;

                if_msg = NLMSG_DATA(msg_hdr);
                if_msg_len = msg_hdr->nlmsg_len - NLMSG_LENGTH(sizeof(*if_msg));

                for (attribute = IFLA_RTA(if_msg);
                     RTA_OK(attribute, if_msg_len);
                     attribute = RTA_NEXT(attribute, if_msg_len)) {

                    switch (attribute->rta_type) {
                    case IFLA_IFNAME:
                        /*
                         * Here we could get the interface ID, and name...
                         *   printf("Interface index %d : %s\n", if_msg->ifi_index, RTA_DATA(attribute));
                         */
                        break;

                    case IFLA_STATS:
                        /*
                         * This is the 32 bit version of the stats,
                         * however we need the 64 bit values.
                         */
                        break;

                    case IFLA_STATS64:
                        memcpy(nl_stats, RTA_DATA(attribute),
                               sizeof(struct rtnl_link_stats64));
                        break;
                    }
                }
            }
        }
    }

    close(nl_fd);

    return true;
}

/*****************************************************************************
 * kni_get_kernel_if_flags()
 ****************************************************************************/
static int kni_get_kernel_if_flags(uint32_t port)
{
    int          fd;
    struct ifreq if_req;

    memset(&if_req, 0, sizeof(if_req));

    fd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd != -1) {
        snprintf(if_req.ifr_name, sizeof(if_req.ifr_name),
                 "warp%u", port);

        if (ioctl(fd, SIOCGIFFLAGS, &if_req) != 0) {
            RTE_LOG(DEBUG, PMD,
                    "Requesting if_flags for warp%u failed, errno = %d\n",
                    port, errno);
        }
        close(fd);
    }
    RTE_LOG(DEBUG, PMD, "Requesting if_flags for warp%u: 0x%x\n",
            port, if_req.ifr_flags);

    return if_req.ifr_flags;
}

/*****************************************************************************
 * kni_set_kernel_if_flags()
 ****************************************************************************/
static bool kni_set_kernel_if_flags(uint32_t port, int flags)
{
    int          fd;
    struct ifreq if_req;

    memset(&if_req, 0, sizeof(if_req));

    fd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd != -1) {
        if_req.ifr_flags = flags;
        snprintf(if_req.ifr_name, sizeof(if_req.ifr_name),
                 "warp%u", port);

        if (ioctl(fd, SIOCSIFFLAGS, &if_req) != 0) {
            RTE_LOG(DEBUG, PMD,
                    "Setting if_flags for warp%u failed, errno = %s(%d)\n",
                    port, strerror(errno), errno);

            close(fd);
            return false;
        }
        close(fd);
    }
    RTE_LOG(DEBUG, PMD, "Setting if_flags for warp%u to 0x%x\n",
            port, if_req.ifr_flags);

    return true;
}

/*****************************************************************************
 * kni_set_kernel_if_mtu()
 ****************************************************************************/
static bool kni_set_kernel_if_mtu(uint32_t port, uint16_t mtu)
{
    int          fd;
    struct ifreq if_req;

    memset(&if_req, 0, sizeof(if_req));

    fd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd != -1) {
        if_req.ifr_mtu = mtu;
        snprintf(if_req.ifr_name, sizeof(if_req.ifr_name),
                 "warp%u", port);

        if (ioctl(fd, SIOCSIFMTU, &if_req) != 0) {
            RTE_LOG(DEBUG, PMD,
                    "Setting if_mtu for warp%u failed, errno = %s(%d)\n",
                    port, strerror(errno), errno);

            close(fd);
            return false;
        }
        close(fd);
    }
    RTE_LOG(DEBUG, PMD, "Setting if_mtu for warp%u to %u\n",
            port, if_req.ifr_mtu);

    return true;
}

/*****************************************************************************
 * kni_atomic_write_link_status()
 ****************************************************************************/
static int kni_atomic_write_link_status(struct rte_eth_dev *dev,
                                        struct rte_eth_link *link)
{
    struct rte_eth_link *dst = &(dev->data->dev_link);
    struct rte_eth_link *src = link;

    if (rte_atomic64_cmpset((uint64_t *)dst, *(uint64_t *)dst,
                            *(uint64_t *)src) == 0)
        return -1;

    return 0;
}

/*****************************************************************************
 * kni_eth_link_update()
 ****************************************************************************/
static int kni_eth_link_update(struct rte_eth_dev *dev,
                               int wait_to_complete __rte_unused)
{
    struct rte_eth_link link;
    int                 flags;

    memcpy(&link, &kni_pmd_link, sizeof(link));
    flags = kni_get_kernel_if_flags(dev->data->port_id);

    if ((flags & IFF_UP) != 0)
        link.link_status = ETH_LINK_UP;

    return kni_atomic_write_link_status(dev, &link);
}

/*****************************************************************************
 * kni_eth_dev_set_link_down()
 ****************************************************************************/
static int kni_eth_dev_set_link_down(struct rte_eth_dev *dev)
{
    struct rte_eth_link link;
    int                 flags;

    memcpy(&link, &kni_pmd_link, sizeof(link));
    flags = kni_get_kernel_if_flags(dev->data->port_id);

    if ((flags & IFF_UP) != 0)
        link.link_status = ETH_LINK_UP;

    flags = kni_get_kernel_if_flags(dev->data->port_id);
    flags &= ~IFF_UP;
    kni_set_kernel_if_flags(dev->data->port_id, flags);

    link.link_status = ETH_LINK_DOWN;
    kni_atomic_write_link_status(dev, &link);

    return 0;
}

/*****************************************************************************
 * kni_eth_dev_set_link_up()
 ****************************************************************************/
static int kni_eth_dev_set_link_up(struct rte_eth_dev *dev)
{
    struct rte_eth_link link;
    int                 flags;

    memcpy(&link, &kni_pmd_link, sizeof(link));
    flags = kni_get_kernel_if_flags(dev->data->port_id);

    if ((flags & IFF_UP) != 0)
        link.link_status = ETH_LINK_UP;

    flags = kni_get_kernel_if_flags(dev->data->port_id);
    flags |= IFF_UP;
    if (kni_set_kernel_if_flags(dev->data->port_id, flags)) {
        link.link_status = ETH_LINK_UP;
        kni_atomic_write_link_status(dev, &link);
    }

    return 0;
}

/*****************************************************************************
 * kni_eth_dev_start()
 ****************************************************************************/
static int kni_eth_dev_start(struct rte_eth_dev *dev)
{
    /*
     * Depending on when this is called it might not succeed.
     * For example when called during the init phase as the mgmt thread is
     * not yet ready to process the kernel status requests.
     */
    return kni_eth_dev_set_link_up(dev);
}

/*****************************************************************************
 * kni_eth_dev_stop()
 ****************************************************************************/
static void kni_eth_dev_stop(struct rte_eth_dev *dev)
{
    kni_eth_dev_set_link_down(dev);
}

/*****************************************************************************
 * kni_eth_rx_queue_setup()
 ****************************************************************************/
static int kni_eth_rx_queue_setup(struct rte_eth_dev *dev,
                                  uint16_t rx_queue_id __rte_unused,
                                  uint16_t nb_rx_desc __rte_unused,
                                  unsigned int socket_id __rte_unused,
                                  const struct rte_eth_rxconf *rx_conf __rte_unused,
                                  struct rte_mempool *mb_pool __rte_unused)
{
    /*
     * KNI only has a single software queue, and we reference it directly by
     * the kni handle. Which is stored in dev->data->dev_private.
     */
    dev->data->nb_rx_queues = 1;
    dev->data->rx_queues[0] = dev->data->dev_private;

    return 0;
}

/*****************************************************************************
 * kni_eth_tx_queue_setup()
 ****************************************************************************/
static int kni_eth_tx_queue_setup(struct rte_eth_dev *dev,
                                  uint16_t tx_queue_id __rte_unused,
                                  uint16_t nb_tx_desc __rte_unused,
                                  unsigned int socket_id __rte_unused,
                                  const struct rte_eth_txconf *tx_conf __rte_unused)
{
    /*
     * KNI only has a single software queue, and we reference it directly by
     * the kni handle. Which is stored in dev->data->dev_private.
     */
    dev->data->nb_tx_queues = 1;
    dev->data->tx_queues[0] = dev->data->dev_private;
    return 0;
}


/*****************************************************************************
 * kni_eth_dev_configure()
 ****************************************************************************/
static int kni_eth_dev_configure(struct rte_eth_dev *dev __rte_unused)
{
    /*
     * We have nothing to setup at this time...
     * Just mention to DPDK that we are happy ;)
     */
    return 0;
}

/*****************************************************************************
 * kni_eth_rx_queue_release()
 ****************************************************************************/
static void kni_eth_rx_queue_release(void *rxq __rte_unused)
{
    /*
     * We have no HW queues to release...
     */
    return;
}

/*****************************************************************************
 * kni_eth_tx_queue_release()
 ****************************************************************************/
static void kni_eth_tx_queue_release(void *txq __rte_unused)
{
    /*
     * We have no HW queues to release...
     */
    return;
}


/*****************************************************************************
 * kni_eth_mac_addr_remove()
 ****************************************************************************/
static void kni_eth_mac_addr_remove(struct rte_eth_dev *dev,
                                    uint32_t index)
{
    /*
     * Silently ignore removal of MAC addresses.
     */
    RTE_LOG(DEBUG, PMD,
            "Requesting adding mac index %u removal for warp%u\n",
            index,
            dev->data->port_id);
}

/*****************************************************************************
 * kni_eth_mac_addr_add()
 ****************************************************************************/
static void kni_eth_mac_addr_add(struct rte_eth_dev *dev,
                                 struct ether_addr *mac,
                                 uint32_t index,
                                 uint32_t vmdq __rte_unused)
{
    RTE_LOG(WARNING, PMD,
            "WARNING[NOT SUPPORTED]: Requesting adding mac at index %u for warp%u: %02X:%02X:%02X:%02X:%02X:%02X\n",
            index,
            dev->data->port_id,
            mac->addr_bytes[0],
            mac->addr_bytes[1],
            mac->addr_bytes[2],
            mac->addr_bytes[3],
            mac->addr_bytes[4],
            mac->addr_bytes[5]);
}

/*****************************************************************************
 * kni_eth_stats_get()
 ****************************************************************************/
static void kni_eth_stats_get(struct rte_eth_dev *dev,
                              struct rte_eth_stats *stats)
{
    struct rtnl_link_stats64 nl_stats;

    /*
     * Get statistics from the Linux kernels KNI side
     */
    kni_get_kernel_if_stats(dev->data->port_id, &nl_stats);

    /*
     * Translate kernel statistics to the PMD one's.
     * NOTE: RX on kernel is TX on PMD, and visa versa
     */
    stats->ipackets = nl_stats.tx_packets;
    stats->opackets = nl_stats.rx_packets;
    stats->ibytes = nl_stats.tx_bytes;
    stats->obytes = nl_stats.rx_bytes;
    stats->imissed = nl_stats.tx_dropped;
    stats->ierrors = nl_stats.tx_errors;
    stats->oerrors = nl_stats.rx_errors;
    stats->rx_nombuf = 0;
}

/*****************************************************************************
 * kni_eth_stats_reset()
 ****************************************************************************/
static void kni_eth_stats_reset(struct rte_eth_dev *dev __rte_unused)
{
    /*
     * There is currently no support in the kernel to reset driver
     * statistics. However we could keep a value of the current counters
     * when a reset is requested, and subtract them.
     *
     * For now we do NOTHING as we do not support clearing counters in WARP17.
     */
}

/*****************************************************************************
 * kni_eth_dev_info()
 ****************************************************************************/
static void kni_eth_dev_info(struct rte_eth_dev *dev,
                             struct rte_eth_dev_info *dev_info)
{
    global_config_t *cfg;

    cfg = cfg_get_config();

    dev_info->driver_name = dev->data->name;
    dev_info->max_mac_addrs = 1;
    dev_info->max_rx_pktlen = cfg != NULL ?
        cfg->gcfg_mbuf_size - RTE_PKTMBUF_HEADROOM : PORT_MAX_MTU;
    dev_info->max_rx_queues = 1;
    dev_info->max_tx_queues = 1;
    dev_info->min_rx_bufsize = 0;
    dev_info->pci_dev = NULL;
}

/*****************************************************************************
 * kni_eth_set_mtu()
 ****************************************************************************/
static int kni_eth_set_mtu(struct rte_eth_dev *dev, uint16_t mtu)
{
    global_config_t *cfg = cfg_get_config();

    if (cfg == NULL || mtu > (cfg->gcfg_mbuf_size - RTE_PKTMBUF_HEADROOM) ||
        mtu < ETHER_MIN_LEN)
        return -EINVAL;

    if (!kni_set_kernel_if_mtu(dev->data->port_id, mtu))
        return -EINVAL;

    return 0;
}

/*****************************************************************************
 * Ethernet device operations table
 ****************************************************************************/
static const struct eth_dev_ops kni_pmd_ops = {
	.dev_start = kni_eth_dev_start,
	.dev_stop = kni_eth_dev_stop,
	.dev_set_link_up = kni_eth_dev_set_link_up,
	.dev_set_link_down = kni_eth_dev_set_link_down,
	.dev_configure = kni_eth_dev_configure,
	.dev_infos_get = kni_eth_dev_info,
        .rx_queue_setup = kni_eth_rx_queue_setup,
	.tx_queue_setup = kni_eth_tx_queue_setup,
	.rx_queue_release = kni_eth_rx_queue_release,
	.tx_queue_release = kni_eth_tx_queue_release,
	.link_update = kni_eth_link_update,
	.stats_get = kni_eth_stats_get,
        .stats_reset = kni_eth_stats_reset,
	.mac_addr_remove = kni_eth_mac_addr_remove,
	.mac_addr_add = kni_eth_mac_addr_add,
        .mtu_set = kni_eth_set_mtu
};

/*****************************************************************************
 * kni_rx_pkt_burst()
 ****************************************************************************/
static uint16_t kni_rx_pkt_burst(void *q, struct rte_mbuf **mbufs,
                                 uint16_t num)
{
    struct rte_kni *kni = (struct rte_kni *)q;

    return rte_kni_rx_burst(kni, mbufs, num);
}

/*****************************************************************************
 * kni_tx_pkt_burst()
 ****************************************************************************/
static uint16_t kni_tx_pkt_burst(void *q, struct rte_mbuf **mbufs,
                                 uint16_t num)
{
    struct rte_kni *kni = (struct rte_kni *)q;

    return rte_kni_tx_burst(kni, mbufs, num);
}

/*****************************************************************************
 * kni_eth_from_kni
 ****************************************************************************/
bool kni_eth_from_kni(const char *kni_name, struct rte_kni *kni,
                      const unsigned int numa_node)
{
    struct rte_eth_dev       *eth_dev;
    struct rte_eth_dev_data  *eth_data = NULL;
    static struct ether_addr *mac_address;

    if (kni_name == NULL || kni == NULL)
        return false;

    RTE_LOG(INFO, PMD, "Creating KNI ethdev for KNI %s\n", kni_name);

    /*
     * Allocate ethernet device
     */
    eth_dev = rte_eth_dev_allocate(kni_name, RTE_ETH_DEV_VIRTUAL);
    if (eth_dev == NULL) {
        rte_errno = ENOSPC;
        goto error;
    }

    /*
     * Allocate needed memory
     */
    eth_data = rte_zmalloc_socket(kni_name, sizeof(*eth_data), 0, numa_node);
    if (eth_data == NULL) {
        rte_errno = ENOMEM;
        goto error;
    }

    eth_data->rx_queues = rte_zmalloc_socket(kni_name,
                                             sizeof(void *) * 1, 0, numa_node);
    if (eth_data->rx_queues == NULL) {
        rte_errno = ENOMEM;
        goto error;
    }

    eth_data->tx_queues = rte_zmalloc_socket(kni_name,
                                             sizeof(void *) * 1, 0, numa_node);
    if (eth_data->tx_queues == NULL) {
        rte_errno = ENOMEM;
        goto error;
    }

    mac_address = rte_zmalloc_socket(kni_name,
                                    sizeof(struct ether_addr), 0, numa_node);
    if (mac_address == NULL) {
        rte_errno = ENOMEM;
        goto error;
    }

    /*
     * Assign MAC from Kernel, on failure take zero mac with port number.
     */

    if (!kni_get_kernel_if_mac(eth_dev->data->port_id,
                               mac_address)) {
        memset(mac_address, 0, sizeof(struct ether_addr));
        mac_address->addr_bytes[5] = eth_dev->data->port_id;
    }

    /*
     * Initialize the ethernet data structure.
     *
     * We always have a single queue which point to the kni interface
     *
     */
    eth_data->dev_private = kni;
    eth_data->port_id = eth_dev->data->port_id;
    snprintf(eth_data->name, sizeof(eth_data->name), "%s", kni_name);
    eth_data->nb_rx_queues = 1;
    eth_data->nb_tx_queues = 1;
    eth_data->rx_queues[0] = eth_data->dev_private;
    eth_data->tx_queues[0] = eth_data->dev_private;

    eth_data->dev_link = kni_pmd_link;
    eth_data->mac_addrs = mac_address;
    eth_data->dev_flags = RTE_ETH_DEV_DETACHABLE;
    eth_data->kdrv = RTE_KDRV_NONE;
    eth_data->drv_name = kni_pmd_driver_name;
    eth_data->numa_node = numa_node;

    eth_dev->data = eth_data;
    eth_dev->driver = NULL;
    eth_dev->dev_ops = &kni_pmd_ops;
    eth_dev->rx_pkt_burst = kni_rx_pkt_burst;
    eth_dev->tx_pkt_burst = kni_tx_pkt_burst;

    TAILQ_INIT(&(eth_dev->link_intr_cbs));


    return true;

error:
    if (eth_data != NULL) {
        rte_free(eth_data->rx_queues);
        rte_free(eth_data->tx_queues);
        rte_free(eth_data);
    }
    return false;
}

