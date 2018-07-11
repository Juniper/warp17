#
# DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER
#
# Copyright (c) 2016, Juniper Networks, Inc. All rights reserved.
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
#     test_sockopt.py
#
# Description:
#     Tests for the WARP17 socket options layer.
#
# Author:
#     Dumitru Ceara, Eelco Chaudron
#
# Initial Created:
#     07/01/2016
#
# Notes:
#
#

from warp17_ut import Warp17UnitTestCase
from warp17_ut import Warp17TrafficTestCase
from warp17_ut import Warp17PortTestCase
from warp17_ut import Warp17NoTrafficTestCase

from warp17_sockopt_pb2 import *
from warp17_service_pb2 import *

class TestPortSockOpt(Warp17PortTestCase, Warp17UnitTestCase):

    #############################################################
    # Overrides of Warp17TrafficTestCase specific to port options
    #############################################################

    def get_l4_port_count(self):
        """Lower the session count (especially due to big MTU tests)"""
        if Warp17UnitTestCase.env.get_ring_ports() > 0:
            return 1
        return 10

    def get_l3_intf_count(self):
        """Lower the session count (especially due to big MTU tests)"""
        if Warp17UnitTestCase.env.get_ring_ports() > 0:
            return 1
        return 4

    def get_updates(self):
        for mtu in [128, 256, 1500, 9198]:
            self.lh.info('MTU %(arg)u' % {'arg': mtu})
            yield (PortOptions(po_mtu=mtu), PortOptions(po_mtu=mtu))

    # We use to have many other testcases with highter mtu but since we don't
    #  force the Max mtu anymore and we are not yet able to determine in
    #  advace which NIC we are using, we cannot test the maximum anymore
    def get_invalid_updates(self):
        for mtu in [0, 67]:
            yield (PortOptions(po_mtu=mtu), PortOptions(po_mtu=mtu))

    def update(self, eth_port, port_opts, expected_err):

        port_arg = PortArg(pa_eth_port=eth_port)
        err = self.warp17_call('SetPortOptions',
                               PortOptionsArg(poa_port=port_arg,
                                              poa_opts=port_opts))
        self.assertEqual(err.e_code, expected_err)

        if expected_err == 0:
            port_opts_res = self.warp17_call('GetPortOptions', port_arg)
            self.assertEqual(port_opts_res.por_error.e_code, 0)
            self.assertEqual(port_opts_res.por_opts.po_mtu, port_opts.po_mtu)

    def update_client(self, tc_arg, cl_port_opts, expected_err=0):
        self.update(tc_arg.tca_eth_port, cl_port_opts, expected_err)

    def update_server(self, tc_arg, srv_port_opts, expected_err=0):
        self.update(tc_arg.tca_eth_port, srv_port_opts, expected_err)

class TestTcpSockOpt(Warp17TrafficTestCase, Warp17UnitTestCase):

    ##################################################################
    # Overrides of Warp17TrafficTestCase specific to TCP stack options
    ##################################################################
    def get_updates(self):
        # Window size in reverse order. We don't want to test relaxed timers
        # with a big window because we might end up running out of mbufs..
        for win_size in [65535, 2048, 1024]:
            self.lh.info('TCP Win Size %(arg)u' % {'arg': win_size})
            yield (TcpSockopt(to_win_size=win_size),
                   TcpSockopt(to_win_size=win_size))

        retry_values = [24, 84, 128]
        for syn_retry in retry_values:
            self.lh.info('TCP Syn Retry %(arg)u' % {'arg': syn_retry})
            yield (TcpSockopt(to_syn_retry_cnt=syn_retry),
                   TcpSockopt(to_syn_retry_cnt=syn_retry))

        for syn_ack_retry in retry_values:
            self.lh.info('TCP Syn/Ack Retry %(arg)u' % {'arg': syn_ack_retry})
            yield (TcpSockopt(to_syn_ack_retry_cnt=syn_ack_retry),
                   TcpSockopt(to_syn_ack_retry_cnt=syn_ack_retry))

        for data_retry in retry_values:
            self.lh.info('TCP Data Retry %(arg)u' % {'arg': data_retry})
            yield (TcpSockopt(to_data_retry_cnt=data_retry),
                   TcpSockopt(to_data_retry_cnt=data_retry))

        for retry in retry_values:
            self.lh.info('TCP Retry %(arg)u' % {'arg': retry})
            yield (TcpSockopt(to_retry_cnt=retry),
                   TcpSockopt(to_retry_cnt=retry))

        for rto in [100, 500, 1000]:
            self.lh.info('TCP RTO %(arg)u' % {'arg': rto})
            yield (TcpSockopt(to_rto=rto), TcpSockopt(to_rto=rto))

        for fin_to in [100, 500, 1000]:
            self.lh.info('TCP FIN TO %(arg)u' % {'arg': fin_to})
            yield (TcpSockopt(to_fin_to=fin_to), TcpSockopt(to_fin_to=fin_to))

        for twait_to in [500, 5000, 10000]:
            self.lh.info('TCP TWAIT TO %(arg)u' % {'arg': twait_to})
            yield (TcpSockopt(to_twait_to=twait_to),
                   TcpSockopt(to_twait_to=twait_to))

        for orphan_to in [100, 500, 2000]:
            self.lh.info('TCP ORPHAN TO %(arg)u' % {'arg': orphan_to})
            yield (TcpSockopt(to_orphan_to=orphan_to),
                   TcpSockopt(to_orphan_to=orphan_to))

    def get_invalid_updates(self):
        self.lh.info('TCP Win Size')
        yield (TcpSockopt(to_win_size=65536),
               TcpSockopt(to_win_size=65536))

        self.lh.info('TCP Syn Retry')
        yield (TcpSockopt(to_syn_retry_cnt=129),
               TcpSockopt(to_syn_retry_cnt=129))

        self.lh.info('TCP Syn/Ack Retry')
        yield (TcpSockopt(to_syn_ack_retry_cnt=129),
               TcpSockopt(to_syn_ack_retry_cnt=129))

        self.lh.info('TCP Data Retry')
        yield (TcpSockopt(to_data_retry_cnt=129),
               TcpSockopt(to_data_retry_cnt=129))

        self.lh.info('TCP Retry')
        yield (TcpSockopt(to_retry_cnt=129),
               TcpSockopt(to_retry_cnt=129))

    def update(self, tc_arg, tcp_opts, expected_err):
        err = self.warp17_call('SetTcpSockopt',
                               TcpSockoptArg(toa_tc_arg=tc_arg,
                                             toa_opts=tcp_opts))
        self.assertEqual(err.e_code, expected_err)

        if expected_err == 0:
            tcp_opts_res = self.warp17_call('GetTcpSockopt', tc_arg)
            self.assertEqual(tcp_opts_res.tor_error.e_code, 0)
            self.assertTrue(self.compare_opts(tcp_opts, tcp_opts_res.tor_opts))

    def update_client(self, tc_arg, cl_tcp_opts, expected_err=0):
        self.update(tc_arg, cl_tcp_opts, expected_err)

    def update_server(self, tc_arg, srv_tcp_opts, expected_err=0):
        self.update(tc_arg, srv_tcp_opts, expected_err)

class TestIPv4SockOpt(Warp17TrafficTestCase, Warp17UnitTestCase):

    dscp_values = [
        ('af11', 0x0A),
        ('af12', 0x0C),
        ('af13', 0x0E),
        ('af21', 0x12),
        ('af22', 0x14),
        ('af23', 0x16),
        ('af31', 0x1A),
        ('af32', 0x1C),
        ('af33', 0x1E),
        ('af41', 0x22),
        ('af42', 0x24),
        ('af43', 0x26),
        ('be',   0x00),
        ('cs1',  0x08),
        ('cs2',  0x10),
        ('cs3',  0x18),
        ('cs4',  0x20),
        ('cs5',  0x28),
        ('cs6',  0x30),
        ('cs7',  0x38),
        ('ef',   0x2E)
    ]

    ecn_values = [
        ('Non-ECT', 0x0),
        ('ECT0',    0x2),
        ('ECT1',    0x1),
        ('CE',      0x3)
    ]

    MAX_TOS = 0xFF

    @staticmethod
    def dscp_ecn_to_tos(dscp_val, ecn_val):
        return ((dscp_val << 2) | ecn_val)

    ###################################################################
    # Overrides of Warp17TrafficTestCase specific to IPv4 stack options
    ###################################################################
    def get_updates(self):
        for (dscp_name, dscp) in TestIPv4SockOpt.dscp_values:
            for (ecn_name, ecn) in TestIPv4SockOpt.ecn_values:
                self.lh.info('IPv4 DSCP {} ECN {}'.format(dscp_name, ecn_name))
                tos = self.dscp_ecn_to_tos(dscp, ecn)
                yield(Ipv4Sockopt(ip4so_tos=tos), Ipv4Sockopt(ip4so_tos=tos))

        self.lh.info('IPV4 ip4so_rx_tstamp')
        for opt in [True, False]:
            yield (Ipv4Sockopt(ip4so_rx_tstamp=opt),
                   Ipv4Sockopt(ip4so_rx_tstamp=opt))

        self.lh.info('IPV4 ip4so_rx_tstamp')
        for opt in [True, False]:
            yield (Ipv4Sockopt(ip4so_tx_tstamp=opt),
                   Ipv4Sockopt(ip4so_tx_tstamp=opt))


    def get_invalid_updates(self):
        yield(Ipv4Sockopt(ip4so_tos=TestIPv4SockOpt.MAX_TOS+1),
              Ipv4Sockopt(ip4so_tos=TestIPv4SockOpt.MAX_TOS+1))

    def update(self, tc_arg, ipv4_opts, expected_err):
        err = self.warp17_call('SetIpv4Sockopt',
                               Ipv4SockoptArg(i4sa_tc_arg=tc_arg,
                                              i4sa_opts=ipv4_opts))
        self.assertEqual(err.e_code, expected_err)

        if expected_err == 0:
            ipv4_opts_res = self.warp17_call('GetIpv4Sockopt', tc_arg)
            self.assertEqual(ipv4_opts_res.i4sr_error.e_code, 0)

            self.assertTrue(
                self.compare_opts(ipv4_opts, ipv4_opts_res.i4sr_opts))

    def update_client(self, tc_arg, cl_ipv4_opts, expected_err=0):
        self.update(tc_arg, cl_ipv4_opts, expected_err)

    def update_server(self, tc_arg, srv_ipv4_opts, expected_err=0):
        self.update(tc_arg, srv_ipv4_opts, expected_err)

class TestVlanSockOpt(Warp17TrafficTestCase, Warp17UnitTestCase):

    MIN_VLAN = 1
    MAX_VLAN = 4094
    MIN_PRI = 0
    MAX_PRI = 7

    ###################################################################
    # Overrides of Warp17TrafficTestCase specific to IPv4 stack options
    ###################################################################
    def get_updates(self):
        self.lh.info('Vlan vlan-id')
        for opt in [TestVlanSockOpt.MIN_VLAN, TestVlanSockOpt.MAX_VLAN]:
            yield (VlanSockopt(vlanso_id=opt),
                   VlanSockopt(vlanso_id=opt))

        self.lh.info('Vlan vlan-pri')
        for opt in [TestVlanSockOpt.MIN_PRI, TestVlanSockOpt.MAX_PRI]:
            yield (VlanSockopt(vlanso_id=TestVlanSockOpt.MAX_VLAN,
                               vlanso_pri=opt),
                   VlanSockopt(vlanso_id=TestVlanSockOpt.MAX_VLAN,
                               vlanso_pri=opt))

    def get_invalid_updates(self):
        self.lh.info('VLAN id Min value')
        yield(VlanSockopt(vlanso_id=TestVlanSockOpt.MIN_VLAN-1),
              VlanSockopt(vlanso_id=TestVlanSockOpt.MIN_VLAN-1))

        self.lh.info('VLAN id Max value')
        yield(VlanSockopt(vlanso_id=TestVlanSockOpt.MAX_VLAN+1),
              VlanSockopt(vlanso_id=TestVlanSockOpt.MAX_VLAN+1))

        self.lh.info('Vlan vlan-pri & no VLAN id')
        for opt in [TestVlanSockOpt.MIN_PRI, TestVlanSockOpt.MAX_PRI]:
            yield (VlanSockopt(vlanso_pri=opt),
                   VlanSockopt(vlanso_pri=opt))

        #self.lh.info('VLAN pri Min value')
        #yield(VlanSockopt(vlanso_pri=TestVlanSockOpt.MIN_PRI-1),
        #      VlanSockopt(vlanso_pri=TestVlanSockOpt.MIN_PRI-1))

        self.lh.info('VLAN pri Max value')
        yield(VlanSockopt(vlanso_pri=TestVlanSockOpt.MAX_PRI+1),
              VlanSockopt(vlanso_pri=TestVlanSockOpt.MAX_PRI+1))

    def update(self, tc_arg, vlan_opts, expected_err):
        err = self.warp17_call('SetVlanSockopt',
                               VlanSockoptArg(vosa_tc_arg=tc_arg,
                                              vosa_opts=vlan_opts))
        self.assertEqual(err.e_code, expected_err)

        if expected_err == 0:
            vlan_opts_res = self.warp17_call('GetVlanSockopt', tc_arg)
            self.assertEqual(vlan_opts_res.vosr_error.e_code, 0)
            self.assertTrue(self.compare_opts(vlan_opts, vlan_opts_res.vosr_opts))

    def update_client(self, tc_arg, cl_vlan_opts, expected_err=0):
        self.update(tc_arg, cl_vlan_opts, expected_err)

    def update_server(self, tc_arg, srv_vlan_opts, expected_err=0):
        self.update(tc_arg, srv_vlan_opts, expected_err)

    def get_port_cfg(self, eth_port, vlan_id=0):
        pcfg = super(TestVlanSockOpt, self).get_port_cfg(eth_port, vlan_id)
        for i in range(0, self.L3_INTF_COUNT):
            pcfg.pc_l3_intfs[i].l3i_vlan_id = vlan_id

        return pcfg

    def test_update_run_traffic(self):
        """Tests updates and runs traffic"""

        self.lh.info('Running: test_update_run_traffic')
        for (cl_update, srv_update) in self.get_updates():

            # Configure ports
            port_cfg_client = self.get_port_cfg(0, cl_update.vlanso_id)
            self.update_client(self._tc_arg_client, cl_update)
            port_cfg_server = self.get_port_cfg(1, srv_update.vlanso_id)
            self.update_server(self._tc_arg_server, srv_update)

            self.configurePort(port_cfg_client, 'Client')
            self.configurePort(port_cfg_server, 'Server')

            self.startPorts()

            cl_result = self.check_test_case_status(self._tc_arg_client)
            srv_result = self.check_test_case_status(self._tc_arg_server)
            self.verify_stats(cl_result, srv_result, cl_update, srv_update)

            self.stopPorts()

