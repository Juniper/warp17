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

import errno
import sys
import unittest
import time

sys.path.append('./lib')
sys.path.append('../python')
sys.path.append('../api/generated/py')

from warp17_ut import Warp17UnitTestCase
from warp17_ut import Warp17TrafficTestCase
from warp17_ut import Warp17PortTestCase

from warp17_sockopt_pb2   import *
from warp17_service_pb2   import *

class TestPortSockOpt(Warp17PortTestCase, Warp17UnitTestCase):

    #############################################################
    # Overrides of Warp17TrafficTestCase specific to port options
    #############################################################
    def get_updates(self):
        for mtu in [128, 256, 1500, 9198]:
            yield (PortOptions(po_mtu=mtu), PortOptions(po_mtu=mtu))

    def get_invalid_updates(self):
        for mtu in [0, 67, 9199, 15000, 65000]:
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

