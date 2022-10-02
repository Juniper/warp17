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
#     warp17_ut.py
#
# Description:
#     WARP17 UT related wrappers and base definitions.
#
# Author:
#     Dumitru Ceara, Eelco Chaudron
#
# Initial Created:
#     02/11/2016
#
# Notes:
#
#

import errno
import os
import sys
import unittest
import time

from functools import partial

from helpers    import LogHelper
from b2b_setup  import *
from warp17_api import *

from warp17_common_pb2    import *
from warp17_l3_pb2        import *
from warp17_app_raw_pb2   import *
from warp17_app_pb2       import *
from warp17_server_pb2    import *
from warp17_client_pb2    import *
from warp17_test_case_pb2 import *
from warp17_service_pb2   import *

class Warp17BaseUnitTestCase(unittest.TestCase):
    """WARP17 Unit Test base class. Sets up the common variables."""

    @classmethod
    def setUpClass(cls):
        env = Warp17Env()
        tstamp = env.get_uniq_stamp()
        dirpath = '/tmp/warp17-test-'+ tstamp

        if not os.path.exists(dirpath):
            os.mkdir(dirpath)

        print "Logs and outputs in " + dirpath

        out_file = dirpath + '/' + cls.__name__ + '.out'
        log_file = dirpath + '/' + cls.__name__ + '.log'

        Warp17BaseUnitTestCase.oargs = Warp17OutputArgs(out_file)

        Warp17BaseUnitTestCase.lh = LogHelper(name=cls.__name__,
                                              filename=log_file)

        Warp17BaseUnitTestCase.env = env

        Warp17BaseUnitTestCase.warp17_call = partial(warp17_method_call,
                                                     env.get_host_name(),
                                                     env.get_rpc_port(),
                                                     Warp17_Stub)
        Warp17UnitTestCase.longMessage = True


    @classmethod
    def cleanEnv(self):
        """cleans the Warp17BaseUnitTestCase enviroment"""
        Warp17BaseUnitTestCase.env = Warp17Env()

class Warp17UnitTestCase(Warp17BaseUnitTestCase):
    """WARP17 Unit Test base class. Sets up the common variables."""

    @classmethod
    def setUpClass(cls):
        super(Warp17UnitTestCase, cls).setUpClass()
        Warp17UnitTestCase.warp17_proc = warp17_start(env=Warp17BaseUnitTestCase.env,
                                                      output_args=Warp17BaseUnitTestCase.oargs)
        # Wait until WARP17 actually starts.
        warp17_wait(env=Warp17BaseUnitTestCase.env,
                    logger=Warp17BaseUnitTestCase.lh)
        # Detailed error messages

    @classmethod
    def tearDownClass(cls):
        warp17_stop(Warp17UnitTestCase.env, Warp17UnitTestCase.warp17_proc)

class Warp17TrafficTestCase():
    """Base class for testing various functionalities with various args."""
    """Assumes a B2B setup with even two ports."""
    """Port 0 <-> Port 1"""

    # Test cases should pass after a fixed time
    RETRY_CNT = 5

    L4_PORT_COUNT = 100
    L3_INTF_COUNT = 4

    def get_tc_retry_count(self):
        """Override in child class if a specific value is needed."""

        return self.RETRY_CNT

    def get_l4_port_count(self):
        """Override in child class if a specific value is needed."""

        return self.L4_PORT_COUNT

    def get_l3_intf_count(self):
        """Override in child class if a specific value is needed."""

        return self.L3_INTF_COUNT

    def get_open_rate(self):
        """Override in child class if a specific value is needed."""
        return Rate()

    def get_send_rate(self):
        """Override in child class if a specific value is needed."""
        return Rate()

    def get_close_rate(self):
        """Override in child class if a specific value is needed."""
        return Rate()

    def get_port_cfg(self, eth_port, vlan_id=0):
        # No def gw
        no_def_gw = Ip(ip_version=IPV4, ip_v4=0)

        # Setup interfaces on port 0
        pcfg = b2b_port_add(eth_port=eth_port, def_gw = no_def_gw)
        for i in range(0, self.L3_INTF_COUNT):
            pcfg.pc_l3_intfs.add(l3i_ip=Ip(ip_version=IPV4, ip_v4=b2b_ipv4(eth_port, i)),
                                 l3i_mask=Ip(ip_version=IPV4, ip_v4=b2b_mask(eth_port, i)),
                                 l3i_count=1)
        return pcfg

    def get_client_rate_cfg(self, eth_port, tc_id):
        """Override in child class if a specif client rate config is needed."""

        return RateClient(rc_open_rate=self.get_open_rate(),
                          rc_close_rate=self.get_close_rate(),
                          rc_send_rate=self.get_send_rate())

    def get_client_delay_cfg(self, eth_port, tc_id):
        """Override in child class if a specif client delay config is needed."""

        init_delay = Delay(d_value=0) # No initial delay
        uptime_delay = Delay()        # Infinite uptime
        downtime_delay = Delay()      # Infinite downtime

        return (init_delay, uptime_delay, downtime_delay)

    def get_client_l4_proto(self, eth_port, tc_id):
        """Override in child class if a specific client L4 proto is needed."""

        return TCP

    def get_client_l4_cfg(self, eth_port, tc_id, l4_port_count):
        """Override in child class if a specific client L4 config is needed."""

        return L4Client(l4c_proto=self.get_client_l4_proto(eth_port, tc_id),
                        l4c_tcp_udp=TcpUdpClient(tuc_sports=b2b_ports(l4_port_count),
                                                 tuc_dports=b2b_ports(l4_port_count)))

    def get_client_app_cfg(self, eth_port, tc_id):
        """Override in child class if a specific client app config is needed."""

        return App(app_proto=RAW_CLIENT,
                   app_raw_client=RawClient(rc_req_plen=10000,
                                            rc_resp_plen=20000))

    def get_client_criteria_cfg(self, eth_port, tc_id, l3_intf_count,
                                l4_port_count):
        """Override in child class if a specific client criteria is needed."""

        cl_sess_count = l3_intf_count * l4_port_count * l3_intf_count * l4_port_count

        return TestCriteria(tc_crit_type=CL_ESTAB,
                            tc_cl_estab=cl_sess_count)

    def get_server_l4_proto(self, eth_port, tc_id):
        """Override in child class if a specific server L4 proto is needed."""

        return TCP

    def get_server_l4_cfg(self, eth_port, tc_id, l4_port_count):
        """Override in child class if a specific server L4 config is needed."""

        return L4Server(l4s_proto=self.get_server_l4_proto(eth_port, tc_id),
                        l4s_tcp_udp=TcpUdpServer(tus_ports=b2b_ports(l4_port_count)))

    def get_server_app_cfg(self, eth_port, tc_id):
        """Override in child class if a specific server app config is needed."""

        return App(app_proto=RAW_SERVER,
                   app_raw_server=RawServer(rs_req_plen=10000,
                                            rs_resp_plen=20000))

    def get_server_criteria_cfg(self, eth_port, tc_id, l3_intf_count,
                                l4_port_count):
        """Override in child class if a specific server criteria is needed."""

        srv_count = l3_intf_count * l4_port_count
        return TestCriteria(tc_crit_type=SRV_UP, tc_srv_up=srv_count)

    def get_client_test_case(self, eth_port, tc_id):
        """Override in child class if a specific config is needed."""

        l3_intf_count = self.get_l3_intf_count()
        l4_port_count = self.get_l4_port_count()

        l4_cfg = self.get_client_l4_cfg(eth_port, tc_id, l4_port_count)
        rate_cfg = self.get_client_rate_cfg(eth_port, tc_id)
        (init_delay, uptime, downtime) = self.get_client_delay_cfg(eth_port,
                                                                   tc_id)
        app_cfg = self.get_client_app_cfg(eth_port, tc_id)
        criteria = self.get_client_criteria_cfg(eth_port, tc_id, l3_intf_count,
                                                l4_port_count)

        return TestCase(tc_type=CLIENT, tc_eth_port=eth_port,
                        tc_id=tc_id,
                        tc_client=Client(cl_src_ips=b2b_sips(eth_port, l3_intf_count),
                                         cl_dst_ips=b2b_dips(eth_port, l3_intf_count),
                                         cl_l4=l4_cfg,
                                         cl_rates=rate_cfg),
                        tc_init_delay=init_delay,
                        tc_uptime=uptime,
                        tc_downtime=downtime,
                        tc_app=app_cfg,
                        tc_criteria=criteria,
                        tc_async=False)

    def get_server_test_case(self, eth_port, tc_id):
        """Override in child class if a specific config is needed."""

        l3_intf_count = self.get_l3_intf_count()
        l4_port_count = self.get_l4_port_count()
        l4_scfg = L4Server(l4s_proto=TCP,
                           l4s_tcp_udp=TcpUdpServer(tus_ports=b2b_ports(l4_port_count)))

        app_scfg = self.get_server_app_cfg(eth_port, tc_id)
        criteria = self.get_server_criteria_cfg(eth_port, tc_id, l3_intf_count,
                                                l4_port_count)

        return TestCase(tc_type=SERVER, tc_eth_port=eth_port, tc_id=tc_id,
                        tc_server=Server(srv_ips=b2b_sips(eth_port, l3_intf_count),
                                         srv_l4=l4_scfg),
                        tc_app=app_scfg,
                        tc_criteria=criteria,
                        tc_async=False)

    def verify_stats(self, cl_result, srv_result, cl_update, srv_update):
        """Override in child class if specific app stats should be verified."""

        req_cnt = cl_result.tsr_app_stats.as_raw.rsts_req_cnt
        resp_cnt = cl_result.tsr_app_stats.as_raw.rsts_resp_cnt
        self.assertTrue(req_cnt > 0, 'cl req_cnt: {}'.format(req_cnt))
        self.assertTrue(resp_cnt > 0, 'cl resp_cnt: {}'.format(resp_cnt))

        req_cnt = srv_result.tsr_app_stats.as_raw.rsts_req_cnt
        resp_cnt = srv_result.tsr_app_stats.as_raw.rsts_resp_cnt
        self.assertTrue(req_cnt > 0, 'srv req_cnt: {}'.format(req_cnt))
        self.assertTrue(resp_cnt > 0, 'srv resp_cnt: {}'.format(resp_cnt))

    def compare_opts(self, expected, result):
        for field in [f for f in expected.DESCRIPTOR.fields if expected.HasField(f.name)]:
            if not result.HasField(field.name):
                return False

            if getattr(expected, field.name) != getattr(result, field.name):
                return False

        return True

    def startPorts(self, expected=0):
        self.assertEqual(self.warp17_call('PortStart',
                                          self._tc_arg_server).e_code,
                         expected,
                         'Port Start Server')
        self.assertEqual(self.warp17_call('PortStart',
                                          self._tc_arg_client).e_code,
                         expected,
                         'Port Start Client')

    def stopPorts(self, expected_e_code=None):
        result = self.warp17_call('PortStop', self._tc_arg_client)
        if expected_e_code:
            self.assertEqual(result.e_code, expected_e_code, 'Port Stop Client')

        result = self.warp17_call('PortStop', self._tc_arg_server)
        if expected_e_code:
            self.assertEqual(result.e_code, expected_e_code, 'Port Stop Server')

    def check_test_case_status(self, tc_arg, expected_status=PASSED):
        for i in range(0, self.get_tc_retry_count()):
            cl_result = self.warp17_call('GetTestStatus', tc_arg)
            self.assertEqual(cl_result.tsr_error.e_code, 0, 'GetTestStatus')
            if cl_result.tsr_state == expected_status:
                break
            self.lh.debug('still waiting for test to pass...')
            time.sleep(1)

        self.assertTrue(cl_result.tsr_state == expected_status, 'Retry count')

        return cl_result

    def configurePort(self, port_cfg, descr, expected_e_code=0):
        self.assertEqual(self.warp17_call('ConfigurePort', port_cfg).e_code,
                         0,
                         'Configure {} Port'.format(descr))

    def configureTestCase(self, tc_cfg, descr, expected_e_code=0):
        self.assertEqual(self.warp17_call('ConfigureTestCase', tc_cfg).e_code,
                         expected_e_code,
                         'Configure {} Test Case'.format(descr))

    def delTestCase(self, tc_arg, descr, expected_e_code=0):
        self.assertEqual(self.warp17_call('DelTestCase', tc_arg).e_code,
                         expected_e_code,
                         'Delete {} Test Case'.format(descr))

    def setUp(self):
        self._port_cfg_client = self.get_port_cfg(eth_port=0)
        self._port_cfg_server = self.get_port_cfg(eth_port=1)

        self._tc_client = self.get_client_test_case(eth_port=0, tc_id=0)
        self._tc_server = self.get_server_test_case(eth_port=1, tc_id=0)

        self._port_arg_client = PortArg(pa_eth_port=self._tc_client.tc_eth_port)
        self._port_arg_server = PortArg(pa_eth_port=self._tc_server.tc_eth_port)

        self._tc_arg_client = TestCaseArg(tca_eth_port=self._tc_client.tc_eth_port,
                                          tca_test_case_id=self._tc_client.tc_id)
        self._tc_arg_server = TestCaseArg(tca_eth_port=self._tc_server.tc_eth_port,
                                          tca_test_case_id=self._tc_server.tc_id)

        # Configure ports
        self.configurePort(self._port_cfg_client, 'Client')
        self.configurePort(self._port_cfg_server, 'Server')

        # Configure test cases:
        self.configureTestCase(self._tc_client, 'Client')
        self.configureTestCase(self._tc_server, 'Server')

    def tearDown(self):
        # Stop test cases (in case they were running)
        self.stopPorts()

        # Delete test cases
        self.delTestCase(self._tc_arg_client, 'Client')
        self.delTestCase(self._tc_arg_server, 'Server')

    def test_update_valid(self):
        """Tests updates with valid configs."""

        self.lh.info('Running: test_update_valid')
        for (cl_update, srv_update) in self.get_updates():
            self.update_client(self._tc_arg_client, cl_update)
            self.update_server(self._tc_arg_server, srv_update)

    def test_update_invalid_port(self):
        """Tests updates with invalid ports"""

        self.lh.info('Running: test_update_invalid_port')
        tc_arg_client = TestCaseArg(tca_eth_port=3,
                                    tca_test_case_id=self._tc_arg_client.tca_test_case_id)
        tc_arg_server = TestCaseArg(tca_eth_port=3,
                                    tca_test_case_id=self._tc_arg_server.tca_test_case_id)

        for (cl_update, srv_update) in self.get_updates():
            self.update_client(tc_arg_client, cl_update,
                               expected_err=-errno.EINVAL)
            self.update_server(tc_arg_server, srv_update,
                               expected_err=-errno.EINVAL)

    def test_update_invalid_test_case(self):
        """Tests updates with invalid test case ids"""

        self.lh.info('Running: test_update_invalid_test_case')
        tc_arg_client = TestCaseArg(tca_eth_port=self._tc_arg_client.tca_eth_port,
                                    tca_test_case_id=TPG_TEST_MAX_ENTRIES + 1)
        tc_arg_server = TestCaseArg(tca_eth_port=self._tc_arg_server.tca_eth_port,
                                    tca_test_case_id=TPG_TEST_MAX_ENTRIES + 1)

        for (cl_update, srv_update) in self.get_updates():
            self.update_client(tc_arg_client, cl_update,
                               expected_err=-errno.EINVAL)
            self.update_server(tc_arg_server, srv_update,
                               expected_err=-errno.EINVAL)

    def test_update_no_test_case(self):
        """Tests updates with inexistent test case ids"""

        self.lh.info('Running: test_update_no_test_case')
        tc_arg_client = TestCaseArg(tca_eth_port=self._tc_arg_client.tca_eth_port,
                                    tca_test_case_id=self._tc_arg_client.tca_test_case_id + 1)
        tc_arg_server = TestCaseArg(tca_eth_port=self._tc_arg_server.tca_eth_port,
                                    tca_test_case_id=self._tc_arg_server.tca_test_case_id + 1)

        for (cl_update, srv_update) in self.get_updates():
            self.update_client(tc_arg_client, cl_update,
                               expected_err=-errno.ENOENT)
            self.update_server(tc_arg_server, srv_update,
                               expected_err=-errno.ENOENT)

    def test_update_test_running(self):
        """Tests updates when tests are already running"""

        self.lh.info('Running: test_update_test_running')

        for (cl_update, srv_update) in self.get_updates():
            self.startPorts()

            self.update_client(self._tc_arg_client, cl_update,
                               expected_err=-errno.EALREADY)
            self.update_server(self._tc_arg_server, srv_update,
                               expected_err=-errno.EALREADY)

            self.stopPorts(expected_e_code=0)

    def test_update_invalid_config(self):
        """Tests updates with invalid configs"""

        self.lh.info('Running: test_update_invalid_config')
        for (cl_update, srv_update) in self.get_invalid_updates():
            self.update_client(self._tc_arg_client, cl_update,
                               expected_err=-errno.EINVAL)
            self.update_server(self._tc_arg_server, srv_update,
                               expected_err=-errno.EINVAL)

    def test_update_run_traffic(self):
        """Tests updates and runs traffic"""

        self.lh.info('Running: test_update_run_traffic')
        for (cl_update, srv_update) in self.get_updates():
            self.update_client(self._tc_arg_client, cl_update)
            self.update_server(self._tc_arg_server, srv_update)

            self.startPorts()

            cl_result = self.check_test_case_status(self._tc_arg_client)
            srv_result = self.check_test_case_status(self._tc_arg_server)
            self.verify_stats(cl_result, srv_result, cl_update, srv_update)

            self.stopPorts()

class Warp17NoTrafficTestCase(Warp17TrafficTestCase):

    # Just don't do any traffic test:
    def test_update_run_traffic(self):
        pass

class Warp17PortTestCase(Warp17TrafficTestCase):

    # Just don't do any "WARP test case" related tests:
    def test_update_invalid_test_case(self):
        # port options do not apply to test cases
        pass

    def test_update_no_test_case(self):
        # port options do not apply to test cases
        pass
