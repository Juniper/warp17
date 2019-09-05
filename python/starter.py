#
# DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER
#
# Copyright (c) 2018, Juniper Networks, Inc. All rights reserved.
#
#
# The contents of this file are subject to the terms of the BSD 3 clause
# License (the "License"). You may not use this file except in compliance
# with the License.
#
# You can obtain a copy of the license at
# https://github.com/Juniper/warp17/blob/master/LICENSE.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
#
# 1. Redistributions of source code must retain the above copyright notice,
# this list of conditions and the following disclaimer.
#
# 2. Redistributions in binary form must reproduce the above copyright
# notice, this list of conditions and the following disclaimer in the
# documentation and/or other materials provided with the distribution.
#
# 3. Neither the name of the copyright holder nor the names of its
# contributors may be used to endorse or promote products derived from this
# software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
# LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
# CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
# SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
# INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
# CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
# ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
# POSSIBILITY OF SUCH DAMAGE.
#
# File name:
#     starter.py
#
# Description:
#     Starter that automatically configure the args given to warp17.
#
# Author:
#     Matteo Triggiani
#
# Initial Created:
#     28/02/2018
#
# Notes:
#     ATTENTION: This is not meant to substitute the actual test config
#

import subprocess
import commands
import copy
import imp
import sys
import os
import argparse
from bitarray import bitarray
import logging

# criteria types
criteria_types = ['run-time', 'servers-up', 'clients-up', 'clients-estab',
                  'data-MB']

dpdk_devbind = imp.load_source("dpdk-devbind",
                               os.environ['RTE_SDK'] +
                               '/usertools/dpdk-devbind.py')

GET_INTERFACE_NUMA_NODE = "cat /sys/bus/pci/devices/%s/numa_node"
GET_MEMORY_NUMA_NODE = "cat /sys/devices/system/node/node%s/meminfo | grep HugePages_Total"
GET_MEMORY_N = "cat /proc/meminfo | grep HugePages_Total"
GET_MEMORYSZ = "cat /proc/meminfo | grep Hugepagesize"
# Those values are mentioned in Bytes.
global_cfg_mbuf = 2359296
global_pkt_mbuf = 905969664
global_pkt_mbuf_tx_hdr = 85458944
global_pkt_mbuf_clone = 50331648


class Nic:
    def __init__(self, Slot, Active, Name):
        self.Name = Name
        self.Slot = Slot
        self.Socket = None
        self.Active = Active
        self.warp17_id = None

    @property
    def warp17_id(self):
        return self.warp17_id

    @warp17_id.setter
    def warp17_id(self, warp17_id):
        self.warp17_id = warp17_id


class Socket:
    def __init__(self, nics, lcores, numa_code):
        self.id = numa_code

        self._nics = nics
        self._n_nics = len(nics)
        self._lcores = copy.copy(lcores)
        self._n_lcores = len(lcores)
        self.free_lcores = lcores
        self._n_free_lcores = len(lcores)
        self._n_hugepages = -1

        self.warp17_mgmt_lcores = {}
        self.warp17_pkt_lcores = {}

    @property
    def nics(self):
        return self._nics

    @nics.setter
    def nics(self, nics):
        self._nics = nics
        self._n_nics = len(nics)

    @property
    def n_hugepages(self):
        if self._n_hugepages is not -1:
            return self._n_hugepages
        (rc, output) = commands.getstatusoutput(
            GET_MEMORY_NUMA_NODE % self.id)
        if "No" in output.split(' ')[2]:
            self._n_hugepages = 0
            logging.warning(
                "{} hugepages on socket {}".format(self._n_hugepages, self.id))
        else:
            self._n_hugepages = int(output.split(' ')[-1])
            logging.debug(
                "{} hugepages on socket {}".format(self._n_hugepages, self.id))
        return self._n_hugepages

    @property
    def lcores(self):
        return self._lcores

    @lcores.setter
    def lcores(self, lcores):
        self._lcores = lcores
        self._n_lcores = len(lcores)

    def is_free(self):
        return not self.has_ports()

    def has_ports(self):
        return self._n_nics != 0

    @staticmethod
    def create_socketmap(given_ports=None):
        port_map = {}
        i = 0
        # port_map will be a dict with socket as k and a list of nics as value
        #     {socket: [Nic, ...], ...}
        Socket.get_dpdk_drivers(port_map, given_ports)

        cores_map = dict()
        # socket_map will be filled as follow:
        #     {socket: (core, [lcore list]),...}

        Socket.read_cpu(cores_map)
        # list_of_sockets = [{socket: [[Nics], [lcores]]}]

        list_of_sockets = [socket for socket in cores_map]
        for socket in list_of_sockets:
            if socket not in port_map:
                nics = []
            else:
                nics = [nic for nic in port_map[socket]]
            lcores = [lcore for cores in cores_map[socket] for lcore in cores]
            list_of_sockets[i] = Socket(nics, lcores, socket)
            i += 1

        return list_of_sockets

    @staticmethod
    def get_dpdk_drivers(port_map, given_ports):
        warp17_nics = []
        dpdk_nics = []

        # Procedures to initialize dpdk data structures.
        dpdk_devbind.check_modules()
        dpdk_devbind.get_device_details(dpdk_devbind.network_devices)
        dpdk_devbind.get_device_details(dpdk_devbind.crypto_devices)
        dpdk_devbind.get_device_details(dpdk_devbind.eventdev_devices)
        dpdk_devbind.get_device_details(dpdk_devbind.mempool_devices)
        warp17_nic_index = 0
        supported_drivers = dpdk_devbind.dpdk_drivers
        # Mellanox doesn't require bound driver
        supported_drivers.append('mlx5_core')

        # Split our list of network devices into the three categories above.
        for d in dpdk_devbind.devices.keys():
            nic = Nic(dpdk_devbind.devices[d]['Slot'],
                      dpdk_devbind.devices[d]['Active'],
                      dpdk_devbind.devices[d]['Device_str'])

            # Skipping Active NICs
            if "Active" in nic.Active:
                logging.debug("{} is {}".format(nic.Slot, nic.Active))
                continue
            logging.debug("NIC {} added.".format(nic.Slot))
            dpdk_nics.append(nic)

        for nic in dpdk_nics:
            (rc, output) = commands.getstatusoutput(
                GET_INTERFACE_NUMA_NODE % nic.Slot)
            if rc is 0:
                # If NUMA is not supported output will be -1.
                print "output {}".format(output)
                if int(output) is -1:
                    nic.Socket = 0
                else:
                    nic.Socket = int(output)
                logging.debug("NIC {} is on {}".format(nic.Slot, nic.Socket))
            else:
                sys.exit(rc)

        if given_ports is None:
            for nic in dpdk_nics:
                ans = raw_input(
                    "Do you want to add {} from this pci {}?\n".format(
                        nic.Name, nic.Slot)).lower()
                if ans in ["yes", "y"]:
                    nic.warp17_id = warp17_nic_index
                    warp17_nic_index += 1
                    warp17_nics.append(nic)
        else:
            for wnics in given_ports:
                for nic in dpdk_nics:
                    if wnics[0] in nic.Slot:
                        nic.warp17_id = warp17_nic_index
                        warp17_nic_index += 1
                        warp17_nics.append(nic)
            if len(warp17_nics) != len(given_ports):
                logging.error("Some ports you are trying to associate are not correct")
                exit(-1)

        sockets = []
        for nic in warp17_nics:
            if nic.Socket not in sockets:
                sockets.append(nic.Socket)

        for socket in sockets:
            port_map[socket] = [port for port in warp17_nics if
                                port.Socket is socket]

    @staticmethod
    def read_cpu(socket_map):
        sockets = []
        cores = []
        core_map = {}
        # core_map will be filled as follow:
        #     {(socket, core): [lcore list],...}

        cpuinfo_file = open("/proc/cpuinfo")
        cpuinfo_lines = cpuinfo_file.readlines()
        cpuinfo_file.close()

        core_details = []
        core_lines = {}
        for line in cpuinfo_lines:
            if len(line.strip()) is not 0:
                name, value = line.split(":", 1)
                core_lines[name.strip()] = value.strip()
            else:
                core_details.append(core_lines)
                core_lines = {}

        for core in core_details:
            for field in ["processor", "core id", "physical id"]:
                if field not in core:
                    logging.error("unable to get '%s' value from " \
                                  "/proc/cpuinfo" % field)
                    sys.exit(1)
                core[field] = int(core[field])

            if core["core id"] not in cores:
                cores.append(core["core id"])
            if core["physical id"] not in sockets:
                sockets.append(core["physical id"])
            key = (core["physical id"], core["core id"])
            if key not in core_map:
                core_map[key] = []
            core_map[key].append(core["processor"])

        for (socket, _), core in core_map.iteritems():
            socket_map[socket] = socket_map.get(socket, []) + [core]

    def bind_warp17_mgmt_lcores(self, lcore, warp17_lcore):
        # ATTENTION: the management cores are always the first two
        if lcore not in range(0, 2):
            raise BaseException
        logging.debug("Binding {} to WP17 as mgmt core".format(lcore))
        self.warp17_mgmt_lcores[lcore] = warp17_lcore

    def bind_warp17_pkt_lcores(self, lcore, warp17_lcore):
        # ATTENTION: the pkt cores can't be the same as mgmt cores
        if lcore in range(0, 2):
            raise BaseException
        logging.debug("Binding {} to WP17 as pkt core".format(lcore))
        self.warp17_pkt_lcores[lcore] = warp17_lcore


class Config:
    def __init__(self, socket_list, args):
        self._socket_list = socket_list
        self._free_sockets = self._get_free_socks()
        self._mask_len = reduce(
            lambda total_len, socket: total_len + len(socket.lcores),
            self._socket_list, 0)
        self._memory = -1
        self.VirtualMachine = self.running_in_VM()
        logging.debug("Am I running in a vm?\t{}".format(
            "Yes" if self.VirtualMachine is True else "No"))
        self._hugesz = -1
        self._n_total_pkt_cores = -1
        # Memory warp17 needs reserved for it's own in MB
        self.reserved_mem = int(args.reserved_memory[0] if type(
            args.reserved_memory) is list else args.reserved_memory)

    @property
    def memory(self):
        if self._memory is not -1:
            return self._memory
        self._memory = int(Config._get_huge_total() * self.hugesz) / 1024
        return self._memory

    @property
    def hugesz(self):
        if self._hugesz is not -1:
            return self._hugesz
        (rc, output) = commands.getstatusoutput(GET_MEMORYSZ)
        self._hugesz = int(output.split(' ')[4])
        return self._hugesz

    def _get_nics(self, per_socket_mask):
        nics = []

        for socket in per_socket_mask:
            for nic in per_socket_mask[socket]:
                nics.append(nic)

        return nics

    def get_core_mask_arg(self):
        self._core_association()

        mask = bitarray(self._mask_len)
        mask.setall(False)
        for socket in self._socket_list:
            for lcore in socket.warp17_mgmt_lcores:
                mask[lcore] = True
            if not socket.has_ports() and not self.VirtualMachine:
                continue
            for lcore in socket.warp17_pkt_lcores:
                logging.debug("adding {} to cores mask".format(lcore))
                mask[lcore] = True
        mask.reverse()  # Change from low to high valuable order.
        return ['-c', str(hex(int(mask.to01(), 2)))]

    def get_qmaps_arg(self):
        per_socket_mask = {socket: Config._create_masks(socket, self._mask_len)
                           for socket in self._socket_list}
        # Creating the string.
        qmaps = []
        for socket in per_socket_mask:
            if len(socket.nics) > len(socket.warp17_pkt_lcores):
                # We are going to let warp17 assign cores.
                # (eventyally from other sockets)
                return ['--qmap-default', 'max-c']
            for nic in socket.nics:
                q = per_socket_mask[socket][nic]
                qmaps += ['--qmap',
                          "{}.{}".format(nic.warp17_id, hex(int(q, 2)))]

        return qmaps

    def get_ports_arg(self):
        ports = []
        for socket in self._socket_list:
            for nic in socket.nics:
                ports += ['-w', "{}".format(nic.Slot)]

        return ports

    @staticmethod
    def _get_huge_total():
        (rc, output) = commands.getstatusoutput(GET_MEMORY_N)
        output = output.split(' ')[-1]
        logging.debug("Total Hugepages {}".format(output))
        return int(output)

    @property
    def n_total_pkt_cores(self):
        if self._n_total_pkt_cores != -1:
            return self._n_total_pkt_cores
        self._n_total_pkt_cores = 0
        for socket in self._socket_list:
            self._n_total_pkt_cores += len(socket.warp17_pkt_lcores)
            logging.debug("{} pkt cores on socket {}".format(
                len(socket.warp17_pkt_lcores), socket.id))

        return self._n_total_pkt_cores

    def get_memory_arg(self):
        res = []
        memsocket = []

        # Preventing to assign memory to socket where you don't have ports
        socket_list = [socket for socket in self._socket_list if socket.has_ports()]

        if len(socket_list) > 1:
            first = True
            memsocket = ['--socket-mem']
            # Creating the socket-mem string per each socket.
            for socket in socket_list:
                if socket.n_hugepages != 0:
                    if first is False:
                        memsocket += ","
                    memsocket += [str(int(socket.n_hugepages * self.hugesz /
                                          1024))]
                    first = False
                else:
                    # If we have even one socket without hugepages we fallback.
                    logging.debug(
                        "{} hugepages on socket {}".format(socket.n_hugepages,
                                                           socket.id))
                    memsocket = []
                    break

        # If we have only 1 socket or we don't have hugepages on all the
        # sockets.
        if len(socket_list) <= 1 or len(memsocket) == 0:
            res = ['-m']
            res += [str(self.memory)]
        else:
            res = memsocket
        return res

    # You can use this function only if you've already calc the memory
    # It will be based on the bigger structure (TCB)
    def _calc_tcb_ucb_max(self):
        # Available_mem in kB
        available_mem = self.memory - self.reserved_mem
        # Available_mem in B
        available_mem = available_mem * 1024 * 1024
        # Split the memory "per core"
        available_mem = available_mem / self.n_total_pkt_cores

        available_mem -= global_cfg_mbuf
        available_mem -= global_pkt_mbuf
        available_mem -= global_pkt_mbuf_tx_hdr
        available_mem -= global_pkt_mbuf_clone

        total_ucb_tcb = available_mem / 512

        return total_ucb_tcb / 312  # Dimension for TCB, max(TCB, UCB)

    def _get_mbuf_config(self):
        mbuf_sz = 2176  # Minimum mbuf_sz
        ret = ['--mbuf-sz', str(mbuf_sz)]
        mbuf_pool_sz = 1
        ret += ['--mbuf-pool-sz', str(mbuf_pool_sz)]
        mbuf_hdr_pool_sz = 1
        ret += ['--mbuf-hdr-pool-sz', str(mbuf_hdr_pool_sz)]
        return []

    def get_warp17_arg(self, args):
        _max = self._calc_tcb_ucb_max()
        tcp_udp_pool = ['--tcb-pool', str(_max / 2), '--ucb-pool',
                        str(_max / 2)]

        if args.test_type is not None:
            if "UDP" in args.test_type[0].upper():
                tcp_udp_pool = ['--tcb-pool', str(1), '--ucb-pool', str(_max)]
            elif "TCP" in args.test_type[0].upper():
                tcp_udp_pool = ['--tcb-pool', str(_max), '--ucb-pool', str(1)]

        return tcp_udp_pool + self._get_mbuf_config() + ['--mpool-any-sock']

    @staticmethod
    def _create_masks(socket, _mask_len):
        list_of_masks = {}
        mask = bitarray(_mask_len)
        mask.setall(False)

        if socket.is_free():
            logging.debug(
                "Socket {} don't have any port bound".format(socket.id))
            return list_of_masks

        core_per_ports = len(socket.warp17_pkt_lcores) / len(socket.nics)
        if len(socket.warp17_pkt_lcores) < len(socket.nics):
            logging.warning("You have less than one core ({}) " \
                            "per NIC ({}) on socket {}".format(
                len(socket.warp17_pkt_lcores), len(socket.nics), socket.id))
            return {}

        cores = core_per_ports
        ports = iter(socket.nics)
        port = ports.next()
        for lcore in socket.warp17_pkt_lcores:
            mask[socket.warp17_pkt_lcores[lcore]] = True
            cores -= 1
            if cores <= 0:
                cores = core_per_ports
                mask.reverse()  # Change from LSB 0 bit ordering to MSB 0.
                list_of_masks[port] = mask.to01()
                mask.setall(False)
                try:
                    port = ports.next()
                except StopIteration:
                    break

        return list_of_masks

    def running_in_VM(self):
        vmtypes = ["VMware Virtual Platform", "VirtualBox", "KVM", "Bochs"]
        output = True

        try:
            output = subprocess.check_output(
                ['dmidecode', '-s', 'system-product-name']).strip("\n")
        except subprocess.CalledProcessError as E:
            logging.warning("You can't check if you are running in a vm")
            logging.debug("{}".format(E))

        if output in vmtypes:
            return True
        else:
            return False

    def _get_free_socks(self):
        return [socket for socket in self._socket_list if socket.is_free()]

    def _core_association(self):

        for socket in self._socket_list:
            toberemoved_lcores = []
            if not socket.has_ports() and socket.id is not 0:
                continue
            # Mgmt cores
            for lcore in socket.free_lcores:
                if lcore in range(0, 2):
                    socket.bind_warp17_mgmt_lcores(lcore, lcore)
                    toberemoved_lcores.append(lcore)
            for lcore in toberemoved_lcores:
                socket.free_lcores.remove(lcore)
            # Pkt cores
            while socket.free_lcores:
                lcore = socket.free_lcores.pop()
                socket.bind_warp17_pkt_lcores(lcore, lcore)

    # TODO: create a new function which will suggest an optimal hw config


class Test:
    def __init__(self):
        self.client_port = 0
        self.server_port = 0
        self.l3_client_ips = []
        self.l3_server_ips = []
        self.l4_type = ''
        self.l4_client_ports = []
        self.l4_server_ports = []
        self.test_criteria = ''
        self.l5_type = ''

    def set_l4type(self, l4_type):
        if l4_type.upper() in ['TCP', 'UDP']:
            self.l4_type = l4_type.lower()
        else:
            raise BaseException

    def set_l5type(self, l5_type):
        if l5_type.upper() in ['RAW', 'HTTP']:
            self.l5_type = l5_type.lower()
        else:
            raise BaseException

    def set_timeouts(self, init=0, up=0, down=0):
        self.init = init
        self.up = up
        self.down = down

    def set_criteria(self, type, value):
        if type in criteria_types:
            self.test_criteria = [type, value]


def write_config(tests):
    test_path = "/tmp/warp17/"
    test_file = "test.cfg"
    id = 0

    if not os.path.exists(test_path):
        os.makedirs(test_path)

    with open(test_path + test_file, "w") as file:
        text = ""
        for test in tests:
            # L3 port 0
            text += "add tests l3_intf port {} ip {} mask 255.255.255.0\n".format(
                test.client_port, test.l3_client_ips[0])
            # L4 client
            text += "add tests client {} port {} test-case-id {} src {} {} sport {} {} dest {} {} dport {} {}\n".format(
                test.l4_type, test.client_port, id, test.l3_client_ips[0],
                test.l3_client_ips[1], test.l4_client_ports[0],
                test.l4_client_ports[1], test.l3_server_ips[0],
                test.l3_server_ips[1], test.l4_server_ports[0],
                test.l4_server_ports[1])
            # L5 client
            if test.l5_type not in '':
                if 'raw' in test.l5_type:
                    l5_opt = 'data-req-plen 100 data-resp-plen 200'
                elif 'http' in test.l5_type:
                    l5_opt = 'GET google.com /index.html req-size 10'
                else:
                    l5_opt = ''

                text += "set tests client {} port {} test-case-id {} {}\n".format(
                    test.l5_type, test.client_port, id, l5_opt)

            # Test details
            text += "set tests timeouts port {} test-case-id {} init {}\n".format(
                test.client_port, id, test.init)
            text += "set tests timeouts port {} test-case-id {} uptime {}\n".format(
                test.client_port, id, test.up)
            text += "set tests timeouts port {} test-case-id {} downtime {}\n".format(
                test.client_port, id, test.down)
            if test.test_criteria is not None:
                text += "set tests criteria port {} test-case-id {} {} {}\n".format(
                    test.client_port, id, test.test_criteria[0],
                    test.test_criteria[1])
            # L3 port 1
            text += "add tests l3_intf port {} ip {} mask 255.255.255.0\n".format(
                test.server_port, test.l3_server_ips[0])
            # L4 server
            text += "add tests server {} port {} test-case-id {} src {} {} sport {} {}\n".format(
                test.l4_type, test.server_port, id, test.l3_server_ips[0],
                test.l3_server_ips[1], test.l4_server_ports[0],
                test.l4_server_ports[1])

            # L5 client
            if test.l5_type not in '':
                if 'raw' in test.l5_type:
                    l5_opt = 'data-req-plen 100 data-resp-plen 200'
                elif 'http' in test.l5_type:
                    l5_opt = '200-OK resp-size 20'
                else:
                    l5_opt = ''

                text += "set tests server {} port {} test-case-id {} {}\n".format(
                    test.l5_type, test.server_port, id, l5_opt)

            id += 1

        text += "start tests port {}\n".format(test.client_port)
        text += "start tests port {}\n".format(test.server_port)
        text += "show tests ui\n"
        logging.debug("config:\n{}".format(text))
        file.write(text)


def balance_sessions(sessions, server_start_ip, server_start_port,
                     client_start_ip, client_start_port):
    delta_server_ips = delta_client_ips = ''
    disposable_client_port = 65535 - client_start_port
    disposable_server_port = 65535 - server_start_port

    if sessions < disposable_client_port:
        delta_client_ports = int(sessions)
        delta_server_ports = 0
    elif sessions < (disposable_client_port * disposable_server_port):
        delta_client_ports = disposable_client_port
        delta_server_ports = server_start_port + (sessions /
                                                  disposable_client_port)
    else:
        raise BaseException

    return delta_server_ips, delta_server_ports, delta_client_ips, delta_client_ports


def parse_test_args(args):
    tests = []
    port = 0

    if args.test_definition is None:
        if args.example is True:
            logging.warning(
                "no test has been specified, example test will be run")
            example_test = Test()
            example_test.client_port = 0
            # TODO: add involve l3 addresses in sessions balancing
            # TODO: calc ips and ports based on a "n of sessions" flag
            example_test.l3_client_ips = ['192.168.1.1', '192.168.1.1']
            example_test.l4_client_ports = ['1', '5000']
            example_test.set_l4type('TCP')
            example_test.server_port = 1
            example_test.l3_server_ips = ['192.168.1.2', '192.168.1.2']
            example_test.l4_server_ports = ['1', '1']
            example_test.set_timeouts(0, 60, 0)
            example_test.set_criteria('run-time', 60)
            tests.append(example_test)
    else:
        for passed_test in args.test_definition:
            try:
                test = Test()
                test.client_port = 0
                sessions = int(passed_test[7])
                server_start_ip = str(passed_test[4])
                server_start_port = int(passed_test[5])
                client_start_ip = str(passed_test[1])
                client_start_port = int(passed_test[2])
                delta_server_ips, delta_server_ports, delta_client_ips, delta_client_ports = balance_sessions(
                    sessions, server_start_ip, server_start_port,
                    client_start_ip,
                    client_start_port)

                test.l3_client_ips = [client_start_ip,
                                      client_start_ip + delta_client_ips]
                test.l4_client_ports = [client_start_port, client_start_port +
                                        delta_client_ports]
                test.set_l4type(args.test_type[0])
                test.server_port = 1
                port += 1
                test.l3_server_ips = [server_start_ip,
                                      server_start_ip + delta_server_ips]
                test.l4_server_ports = [server_start_port, server_start_port +
                                        delta_server_ports]
                test.set_l5type(passed_test[8])
                test.set_timeouts(0, 1, 0)
                test.set_criteria('run-time', 60)
                tests.append(test)
            except BaseException as E:
                logging.warning(
                    "WARNING {}: args you given doesn't match with requirements".format(
                        E))

    return tests


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('-w', '--whitelist', nargs=1,
                        help="whitelist interface in format bus:slot.func",
                        action='append')
    parser.add_argument('-e', '--executable', nargs=1,
                        help="warp17 executable")
    parser.add_argument('-rm', '--reserved_memory', nargs=1, default=1536,
                        help="warp17 reserved memory")
    parser.add_argument('-E', '--example', help="autorun example",
                        action='store_true')
    parser.add_argument('-tt', '--test_type', nargs=1,
                        help="l4 test type [TCP/UDP]")
    parser.add_argument('-td', '--test_definition', nargs="*", action="append",
                        help="client <client ip> <client port> "
                             "server <server ip> <server port> "
                             "sessions <wanted sessions> "
                             "<l5 test type [RAW/HTTP]>")
    # ATTENTION: this will override any passed test
    parser.add_argument('-tf', '--test_file', nargs=1, help="test file")

    args = parser.parse_args()

    tests = parse_test_args(args)

    config = Config(Socket.create_socketmap(args.whitelist), args)

    # Dpdk args.
    lcore_res = config.get_core_mask_arg()
    memory_res = config.get_memory_arg()
    qmaps_res = config.get_qmaps_arg()
    ports_res = config.get_ports_arg()

    # Warp17 args.
    wp17_res = config.get_warp17_arg(args)

    if args.executable:
        executable = args.executable[0]
    else:
        executable = "../build/warp17"

    if not os.path.isfile(executable):
        logging.error("{} not found".format(executable))
        exit()

    if len(tests) is not 0 and args.test_file is None:
        write_config(tests)
        wp17_res += ['--cmd-file', '/tmp/warp17/test.cfg']
    elif args.test_file is not None:
        if len(tests) is not 0:
            logging.warning("the given tests will be ignored " \
                            "and instead {} will be run".format(
                args.test_file[0]))
        logging.debug("{} has been added".format(args.test_file[0]))
        wp17_res += ['--cmd-file', args.test_file[0]]

    dargs = lcore_res + memory_res + ports_res + ['--']
    wpargs = qmaps_res + wp17_res

    logging.debug("exec \"{}\" args\"{} {}\"".format(executable, dargs, wpargs))

    subprocess.call([executable] + dargs + wpargs)


if __name__ == "__main__":
    # Set DEBUG for debug prints.
    logging.basicConfig(level=logging.ERROR)
    main()
