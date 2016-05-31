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
#     test_api.py
#
# Description:
#     Tests for the WARP17 API.
#
# Author:
#     Dumitru Ceara, Eelco Chaudron
#
# Initial Created:
#     02/12/2016
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
from b2b_setup import *

from warp17_common_pb2    import *
from warp17_l3_pb2        import *
from warp17_app_raw_pb2   import *
from warp17_server_pb2    import *
from warp17_client_pb2    import *
from warp17_test_case_pb2 import *
from warp17_service_pb2   import *

class TestApi(Warp17UnitTestCase):
    """Tests the functionality of the API."""
    """Assumes a B2B setup with two ports."""
    """Port 0 <-> Port 1"""
    PORT_CNT = 2

    def test_configure_port_valid_no_intf_no_gw(self):
        """Tests the ConfigurePort API with 0 interfaces and no default gw"""

        for eth_port in range(0, self.PORT_CNT):
            pcfg = b2b_configure_port(eth_port, def_gw=Ip(ip_version=IPV4, ip_v4=0))
            error = self.warp17_call('ConfigurePort', pcfg)
            self.assertEqual(error.e_code, 0, 'ConfigurePort')

        for eth_port in range(0, self.PORT_CNT):
            result = self.warp17_call('GetPortCfg',
                                      PortArg(pa_eth_port=eth_port))
            self.assertEqual(error.e_code, 0, 'GetPortCfg')
            self.assertEqual(len(result.pcr_cfg.pc_l3_intfs), 0, 'L3IntfCnt')
            self.assertTrue(result.pcr_cfg.pc_def_gw == Ip(ip_version=IPV4,
                                                            ip_v4=0),
                            'DefGw')

    def test_configure_port_valid_max_intf_no_gw(self):
        """Tests the ConfigurePort API with max interfaces and no default gw"""

        for eth_port in range(0, self.PORT_CNT):
            pcfg = b2b_configure_port(eth_port,
                                      def_gw=Ip(ip_version=IPV4, ip_v4=0),
                                      l3_intf_count=TPG_TEST_MAX_L3_INTF)
            error = self.warp17_call('ConfigurePort', pcfg)
            self.assertEqual(error.e_code, 0, 'ConfigurePort')

        for eth_port in range(0, self.PORT_CNT):
            result = self.warp17_call('GetPortCfg',
                                      PortArg(pa_eth_port=eth_port))
            self.assertEqual(result.pcr_error.e_code, 0, 'GetPortCfg')
            self.assertEqual(len(result.pcr_cfg.pc_l3_intfs),
                             TPG_TEST_MAX_L3_INTF,
                             'L3IntfCnt')
            self.assertTrue(result.pcr_cfg.pc_def_gw == Ip(ip_version=IPV4,
                                                            ip_v4=0),
                            'DefGw')

            for i in range(0, TPG_TEST_MAX_L3_INTF):
                self.assertTrue(result.pcr_cfg.pc_l3_intfs[i] ==
                                    L3Intf(l3i_ip=Ip(ip_version=IPV4,
                                                     ip_v4=b2b_ipv4(eth_port, i)),
                                           l3i_mask=Ip(ip_version=IPV4,
                                                       ip_v4=b2b_mask(eth_port, i)),
                                           l3i_count=b2b_count(eth_port, i)),
                                'L3Intf')

    @unittest.expectedFailure
    def test_configure_port_invalid_gt_max_intf(self):
        """Tests the ConfigurePort API with more than max interfaces"""
        """"WARP17 validates the total interface count but doesn't return an error yet!"""

        for eth_port in range(0, self.PORT_CNT):
            pcfg = b2b_configure_port(eth_port,
                                      def_gw = Ip(ip_version=IPV4, ip_v4=b2b_def_gw(eth_port)),
                                      l3_intf_count = TPG_TEST_MAX_L3_INTF + 1)
            error = self.warp17_call('ConfigurePort', pcfg)
            self.assertEqual(error.e_code, -errno.EINVAL, 'ConfigurePort')

    def test_configure_test_case_invalid_eth_port(self):
        """Tests the ConfigureTestCase API with invalid eth_port"""

        eth_port = self.PORT_CNT + 1

        l4cfg = L4Server(l4s_proto=UDP,
                         l4s_tcp_udp=TcpUdpServer(tus_ports=L4PortRange(l4pr_start=1,
                                                                        l4pr_end=1)))

        appcfg = AppServer(as_app_proto=RAW, as_raw=RawServer(rs_req_plen=0,
                                                              rs_resp_plen=0))

        scfg = Server(srv_ips=IpRange(ipr_start=Ip(ip_version=IPV4,
                                                   ip_v4=b2b_ipv4(eth_port, 0)),
                                      ipr_end=Ip(ip_version=IPV4,
                                                 ip_v4=b2b_ipv4(eth_port, 0) + 1)),
                      srv_l4=l4cfg,
                      srv_app=appcfg)

        critcfg = TestCriteria(tc_crit_type=SRV_UP, tc_srv_up=1)

        tccfg = TestCase(tc_type=SERVER, tc_eth_port=eth_port,
                         tc_id=0,
                         tc_server=scfg,
                         tc_criteria=critcfg,
                         tc_async=False)

        error = self.warp17_call('ConfigureTestCase', tccfg)
        self.assertEqual(error.e_code, -errno.EINVAL, 'ConfigureTestCase')

        tcdel = TestCaseArg(tca_eth_port = 0,
                            tca_test_case_id=TPG_TEST_MAX_ENTRIES + 1)
        error = self.warp17_call('DelTestCase', tcdel)
        self.assertEqual(error.e_code, -errno.EINVAL, 'DelTestCase')

    def test_configure_test_case_invalid_tcid(self):
        """Tests the ConfigureTestCase API with invalid tcid"""

        l4cfg = L4Server(l4s_proto=UDP,
                         l4s_tcp_udp=TcpUdpServer(tus_ports=L4PortRange(l4pr_start=1,
                                                                        l4pr_end=1)))

        appcfg = AppServer(as_app_proto=RAW,
                           as_raw=RawServer(rs_req_plen=0, rs_resp_plen=0))

        scfg = Server(srv_ips=IpRange(ipr_start=Ip(ip_version=IPV4,
                                                   ip_v4=b2b_ipv4(0, 0)),
                                      ipr_end=Ip(ip_version=IPV4,
                                                 ip_v4=b2b_ipv4(0, 0) + 1)),
                      srv_l4=l4cfg,
                      srv_app=appcfg)

        critcfg = TestCriteria(tc_crit_type=SRV_UP, tc_srv_up=1)

        tccfg = TestCase(tc_type=SERVER, tc_eth_port=0,
                         tc_id=TPG_TEST_MAX_ENTRIES + 1,
                         tc_server=scfg,
                         tc_criteria=critcfg,
                         tc_async=False)

        error = self.warp17_call('ConfigureTestCase', tccfg)
        self.assertEqual(error.e_code, -errno.EINVAL, 'ConfigureTestCase')

        tcdel = TestCaseArg(tca_eth_port=0,
                            tca_test_case_id=TPG_TEST_MAX_ENTRIES + 1)
        error = self.warp17_call('DelTestCase', tcdel)
        self.assertEqual(error.e_code, -errno.EINVAL, 'DelTestCase')

    def _configure_client_test_cases(self, ip_count, l4_proto, l4_port_count,
                                     req_plen, resp_plen, criteria, async,
                                     expected_err):

        l4cfg = L4Client(l4c_proto=l4_proto,
                         l4c_tcp_udp=TcpUdpClient(tuc_sports=b2b_ports(l4_port_count),
                                                  tuc_dports=b2b_ports(l4_port_count)))

        rate_cfg = RateClient(rc_open_rate=Rate(r_value=42),
                              rc_close_rate=Rate(r_value=42),
                              rc_send_rate=Rate(r_value=42))

        delay_cfg = DelayClient(dc_init_delay=Delay(d_value=42),
                                dc_uptime=Delay(d_value=42),
                                dc_downtime=Delay(d_value=42))

        app_cfg = AppClient(ac_app_proto=RAW,
                            ac_raw=RawClient(rc_req_plen=req_plen,
                                             rc_resp_plen=resp_plen))

        tcs = [
            TestCase(tc_type=CLIENT, tc_eth_port=eth_port,
                     tc_id=tcid,
                     tc_client=Client(cl_src_ips=b2b_sips(eth_port, ip_count),
                                      cl_dst_ips=b2b_dips(eth_port, ip_count),
                                      cl_l4=l4cfg,
                                      cl_rates=rate_cfg,
                                      cl_delays=delay_cfg,
                                      cl_app=app_cfg),
                     tc_criteria=criteria,
                     tc_async=async)
            for eth_port in range(0, self.PORT_CNT)
            for tcid in range (0, TPG_TEST_MAX_ENTRIES)
        ]

        for tccfg in tcs:
            error = self.warp17_call('ConfigureTestCase', tccfg)
            self.assertEqual(error.e_code, expected_err, 'ConfigureTestCase')

        if expected_err != 0:
            return

        for tccfg in tcs:
            tcget = TestCaseArg(tca_eth_port=tccfg.tc_eth_port,
                                tca_test_case_id=tccfg.tc_id)
            tcget_result = self.warp17_call('GetTestCase', tcget)
            self.assertEqual(tcget_result.tcr_error.e_code, 0, 'GetTestCase')
            self.assertTrue(tcget_result.tcr_cfg == tccfg, 'GetTestCaseCfg')

            tcdel = TestCaseArg(tca_eth_port=tccfg.tc_eth_port,
                                tca_test_case_id=tccfg.tc_id)
            error = self.warp17_call('DelTestCase', tcdel)
            self.assertEqual(error.e_code, 0, 'DelTestCase')


    def test_configure_test_case_tcp_udp_client(self):
        """Tests the ConfigureTestCase API with TCP & UDP client testcases"""
        for l4_proto in [TCP, UDP]:
            for async in [True, False]:
                # Positive test cases
                criteria = TestCriteria(tc_crit_type=RUN_TIME,
                                        tc_run_time_s=3600)
                self._configure_client_test_cases(1, l4_proto, 1, 10, 10,
                                                  criteria,
                                                  async,
                                                  0)

                criteria = TestCriteria(tc_crit_type=SRV_UP, tc_srv_up=100)
                self._configure_client_test_cases(1, l4_proto, 1, 10, 10,
                                                  criteria,
                                                  async,
                                                  0)

                criteria = TestCriteria(tc_crit_type=CL_UP, tc_cl_up=100)
                self._configure_client_test_cases(1, l4_proto, 1, 10, 10,
                                                  criteria,
                                                  async,
                                                  0)

                criteria = TestCriteria(tc_crit_type=CL_ESTAB,
                                        tc_cl_estab=100)
                self._configure_client_test_cases(1, l4_proto, 1, 10, 10,
                                                  criteria,
                                                  async,
                                                  0)

                criteria = TestCriteria(tc_crit_type=DATAMB_SENT,
                                        tc_data_mb_sent=100)
                self._configure_client_test_cases(1, l4_proto, 1, 10, 10,
                                                  criteria,
                                                  async,
                                                  0)

                # Negative test cases
                criteria = TestCriteria(tc_crit_type=RUN_TIME,
                                        tc_run_time_s=3600)
                self._configure_client_test_cases(0, l4_proto, 1, 10, 10,
                                                  criteria,
                                                  async,
                                                  -errno.EINVAL)

                criteria = TestCriteria(tc_crit_type=RUN_TIME,
                                        tc_run_time_s=3600)
                self._configure_client_test_cases(1, l4_proto, 0, 10, 10,
                                                  criteria,
                                                  async,
                                                  -errno.EINVAL)

    def _configure_server_test_cases(self, ip_count, l4_proto, l4_port_count,
                                     req_plen, resp_plen,
                                     criteria, async, expected_err):

        l4cfg = L4Server(l4s_proto=l4_proto,
                         l4s_tcp_udp=TcpUdpServer(tus_ports=b2b_ports(l4_port_count)))

        app_cfg = AppServer(as_app_proto=RAW,
                            as_raw=RawServer(rs_req_plen=req_plen,
                                             rs_resp_plen=resp_plen))

        tcs = [
            TestCase(tc_type=SERVER, tc_eth_port=eth_port,
                     tc_id=tcid,
                     tc_server=Server(srv_ips=b2b_sips(eth_port, ip_count),
                                      srv_l4=l4cfg,
                                      srv_app=app_cfg),
                     tc_criteria=criteria,
                     tc_async=async)
            for eth_port in range(0, self.PORT_CNT)
            for tcid in range(0, TPG_TEST_MAX_ENTRIES)
        ]

        for tccfg in tcs:
            error = self.warp17_call('ConfigureTestCase', tccfg)
            self.assertEqual(error.e_code, expected_err, 'ConfigureTestCase')

        if expected_err != 0:
            return

        for tccfg in tcs:
            tcget = TestCaseArg(tca_eth_port=tccfg.tc_eth_port,
                                tca_test_case_id=tccfg.tc_id)
            tcget_result = self.warp17_call('GetTestCase', tcget)
            self.assertEqual(tcget_result.tcr_error.e_code, 0, 'GetTestCase')
            self.assertTrue(tcget_result.tcr_cfg == tccfg, 'GetTestCaseCfg')

            tcdel = TestCaseArg(tca_eth_port=tccfg.tc_eth_port,
                                tca_test_case_id=tccfg.tc_id)
            error = self.warp17_call('DelTestCase', tcdel)
            self.assertEqual(error.e_code, 0, 'DelTestCase')

    def test_configure_test_case_tcp_udp_server(self):
        """Tests the ConfigureTestCase API with TCP & UDP server testcases"""
        for l4_proto in [TCP, UDP]:
            for async in [True, False]:
                # Positive test cases
                criteria = TestCriteria(tc_crit_type=RUN_TIME,
                                        tc_run_time_s=3600)
                self._configure_server_test_cases(1, l4_proto, 1, 10, 10,
                                                  criteria,
                                                  async,
                                                  0)

                criteria = TestCriteria(tc_crit_type=SRV_UP, tc_srv_up=100)
                self._configure_server_test_cases(1, l4_proto, 1, 10, 10,
                                                  criteria,
                                                  async,
                                                  0)

                criteria = TestCriteria(tc_crit_type=CL_UP, tc_cl_up=100)
                self._configure_server_test_cases(1, l4_proto, 1, 10, 10,
                                                  criteria,
                                                  async,
                                                  0)

                criteria = TestCriteria(tc_crit_type=CL_ESTAB, tc_cl_estab=100)
                self._configure_server_test_cases(1, l4_proto, 1, 10, 10,
                                                  criteria,
                                                  async,
                                                  0)

                criteria = TestCriteria(tc_crit_type=DATAMB_SENT,
                                        tc_data_mb_sent=100)
                self._configure_server_test_cases(1, l4_proto, 1, 10, 10,
                                                  criteria,
                                                  async,
                                                  0)

                # Negative test cases
                criteria = TestCriteria(tc_crit_type=RUN_TIME,
                                        tc_run_time_s=3600)
                self._configure_server_test_cases(0, l4_proto, 1, 10, 10,
                                                  criteria,
                                                  async,
                                                  -errno.EINVAL)

                criteria = TestCriteria(tc_crit_type=RUN_TIME,
                                        tc_run_time_s=3600)
                self._configure_server_test_cases(1, l4_proto, 0, 10, 10,
                                                  criteria,
                                                  async,
                                                  -errno.EINVAL)

    def test_port_start_negative(self):
        """Try to start a non-existent test"""
        self.assertEqual(self.warp17_call('PortStart',
                                          PortArg(pa_eth_port=0)).e_code,
                         -errno.ENOENT,
                         'PortStart')

    def test_port_stop_negative(self):
        """Try to stop a non-existent test"""
        self.assertEqual(self.warp17_call('PortStop',
                                          PortArg(pa_eth_port=0)).e_code,
                         -errno.ENOENT,
                         'PortStop')

    def test_single_session(self):
        """Setup a single UDP/TCP session and check that the test passed"""
        """Port 0 is the client, Port 1 is the server"""

        # No def gw
        no_def_gw = Ip(ip_version=IPV4, ip_v4=0)

        # Setup interfaces on port 0
        pcfg = b2b_port_add(0, def_gw = no_def_gw)
        b2b_port_add_intfs(pcfg, [(Ip(ip_version=IPV4, ip_v4=b2b_ipv4(0, 0)),
                           Ip(ip_version=IPV4, ip_v4=b2b_mask(0, 0)),
                           b2b_count(0, 0))])
        self.assertEqual(self.warp17_call('ConfigurePort',pcfg).e_code, 0,
                         'ConfigurePort')

        # Setup interfaces on port 1
        pcfg = b2b_port_add(1, def_gw = no_def_gw)
        b2b_port_add_intfs(pcfg, [(Ip(ip_version=IPV4, ip_v4=b2b_ipv4(1, 0)),
                           Ip(ip_version=IPV4, ip_v4=b2b_mask(1, 0)),
                           b2b_count(1, 0))])
        self.assertEqual(self.warp17_call('ConfigurePort', pcfg).e_code, 0,
                         'ConfigurePort')

        rate_ccfg = RateClient(rc_open_rate=Rate(),
                               rc_close_rate=Rate(),
                               rc_send_rate=Rate())

        delay_ccfg = DelayClient(dc_init_delay=Delay(d_value=0),
                                 dc_uptime=Delay(),
                                 dc_downtime=Delay())

        app_ccfg = AppClient(ac_app_proto=RAW,
                             ac_raw=RawClient(rc_req_plen=10,
                                              rc_resp_plen=10))

        app_scfg = AppServer(as_app_proto=RAW,
                             as_raw=RawServer(rs_req_plen=10,
                                              rs_resp_plen=10))

        for l4_proto in [TCP, UDP]:
            l4_ccfg = L4Client(l4c_proto=l4_proto,
                               l4c_tcp_udp=TcpUdpClient(tuc_sports=b2b_ports(1),
                                                        tuc_dports=b2b_ports(1)))
            ccfg = TestCase(tc_type=CLIENT, tc_eth_port=0,
                            tc_id=0,
                            tc_client=Client(cl_src_ips=b2b_sips(0, 1),
                                             cl_dst_ips=b2b_dips(0, 1),
                                             cl_l4=l4_ccfg,
                                             cl_rates=rate_ccfg,
                                             cl_delays=delay_ccfg,
                                             cl_app=app_ccfg),
                            tc_criteria=TestCriteria(tc_crit_type=RUN_TIME,
                                                     tc_run_time_s=1),
                            tc_async=False)
            self.assertEqual(self.warp17_call('ConfigureTestCase', ccfg).e_code,
                             0,
                             'ConfigureTestCase')

            l4_scfg = L4Server(l4s_proto=l4_proto,
                               l4s_tcp_udp=TcpUdpServer(tus_ports=b2b_ports(1)))
            scfg = TestCase(tc_type=SERVER, tc_eth_port=1, tc_id=0,
                            tc_server=Server(srv_ips=b2b_sips(1, 1),
                                             srv_l4=l4_scfg,
                                             srv_app=app_scfg),
                            tc_criteria=TestCriteria(tc_crit_type=SRV_UP,
                                                     tc_srv_up=1),
                            tc_async=False)
            self.assertEqual(self.warp17_call('ConfigureTestCase', scfg).e_code,
                             0,
                             'ConfigureTestCase')

            # Start server test
            self.assertEqual(self.warp17_call('PortStart',
                                              PortArg(pa_eth_port=1)).e_code,
                             0,
                             'PortStart')
            # Start client test
            self.assertEqual(self.warp17_call('PortStart',
                                              PortArg(pa_eth_port=0)).e_code,
                             0,
                             'PortStart')
            # should be done in way less than 5 seconds!
            time.sleep(5)
            # Check client test to be passed
            client_result = self.warp17_call('GetTestStatus',
                                             TestCaseArg(tca_eth_port=0,
                                                         tca_test_case_id=0))
            self.assertEqual(client_result.tsr_error.e_code, 0, 'GetTestStatus')
            self.assertEqual(client_result.tsr_state, PASSED,
                             'PortStatus PASSED')
            self.assertEqual(client_result.tsr_type, CLIENT,
                             'PortStatus CLIENT')
            self.assertEqual(client_result.tsr_l4_proto, l4_proto,
                             'PortStatus L4')

            if l4_proto == TCP:
                self.assertEqual(client_result.tsr_stats.tcs_client.tccs_estab,
                                 1,
                                 'PortStatus ESTAB')

            # Check server test to be passed
            server_result = self.warp17_call('GetTestStatus',
                                              TestCaseArg(tca_eth_port=1,
                                                          tca_test_case_id=0))
            self.assertEqual(server_result.tsr_error.e_code, 0, 'GetTestStatus')
            self.assertEqual(server_result.tsr_state, PASSED,
                             'PortStatus PASSED')
            self.assertEqual(server_result.tsr_type, SERVER,
                             'PortStatus SERVER')
            self.assertEqual(server_result.tsr_l4_proto, l4_proto,
                             'PortStatus L4')
            self.assertEqual(server_result.tsr_stats.tcs_server.tcss_estab,
                             1,
                             'PortStatus ESTAB')

            # Stop server test
            self.assertEqual(self.warp17_call('PortStop',
                                              PortArg(pa_eth_port=1)).e_code,
                             0,
                             'PortStop')

            # Fail to stop client test (already passed)
            self.assertEqual(self.warp17_call('PortStop',
                                              PortArg(pa_eth_port=1)).e_code,
                             -errno.ENOENT,
                             'PortStop')

            # Delete client test
            self.assertEqual(self.warp17_call('DelTestCase',
                                              TestCaseArg(tca_eth_port=0,
                                                          tca_test_case_id=0)).e_code,
                             0,
                             'DelTestCase')

            # Delete server test
            self.assertEqual(self.warp17_call('DelTestCase',
                                              TestCaseArg(tca_eth_port=1,
                                                          tca_test_case_id=0)).e_code,
                             0,
                             'DelTestCase')


##############################################################################
# Partial Get/Update APIs.
##############################################################################
def _get_dummy_server_test(eth_port, tc_id):
    l4_scfg = L4Server(l4s_proto=TCP,
                       l4s_tcp_udp=TcpUdpServer(tus_ports=b2b_ports(1)))
    app_scfg = AppServer(as_app_proto=RAW,
                         as_raw=RawServer(rs_req_plen=42,
                                          rs_resp_plen=42))
    return TestCase(tc_type=SERVER, tc_eth_port=eth_port, tc_id=tc_id,
                    tc_server=Server(srv_ips=b2b_sips(1, 1),
                                     srv_l4=l4_scfg,
                                     srv_app=app_scfg),
                    tc_criteria=TestCriteria(tc_crit_type=SRV_UP,
                                             tc_srv_up=1),
                    tc_async=False)

def _get_dummy_client_test(eth_port, tc_id):
    l4cfg = L4Client(l4c_proto=TCP,
                     l4c_tcp_udp=TcpUdpClient(tuc_sports=b2b_ports(1),
                                              tuc_dports=b2b_ports(1)))

    rate_cfg = RateClient(rc_open_rate=Rate(r_value=42),
                          rc_close_rate=Rate(r_value=42),
                          rc_send_rate=Rate(r_value=42))

    delay_cfg = DelayClient(dc_init_delay=Delay(d_value=42),
                            dc_uptime=Delay(d_value=42),
                            dc_downtime=Delay(d_value=42))

    app_cfg = AppClient(ac_app_proto=RAW,
                        ac_raw=RawClient(rc_req_plen=1,
                                         rc_resp_plen=1))

    return TestCase(tc_type=CLIENT, tc_eth_port=eth_port,
                    tc_id=tc_id,
                    tc_client=Client(cl_src_ips=b2b_sips(eth_port, 1),
                                     cl_dst_ips=b2b_dips(eth_port, 1),
                                     cl_l4=l4cfg,
                                     cl_rates=rate_cfg,
                                     cl_delays=delay_cfg,
                                     cl_app=app_cfg),
                    tc_criteria=TestCriteria(tc_crit_type=RUN_TIME,
                                             tc_run_time_s=42),
                    tc_async=False)

class TestPartialPortApi(Warp17UnitTestCase):
    """Tests the functionality of the partial update/get port config APIs."""
    """Assumes a B2B setup with even two ports."""
    """Port 0 <-> Port 1"""
    PORT_CNT = 2

    def setUp(self):
        self._pcfg = b2b_configure_port(eth_port=0,
                                        def_gw=Ip(ip_version=IPV4, ip_v4=42),
                                        l3_intf_count=TPG_TEST_MAX_L3_INTF)

    def tearDown(self):
        clean_pcfg = b2b_configure_port(eth_port=0,
                                        def_gw=Ip(ip_version=IPV4, ip_v4=0),
                                        l3_intf_count=0)
        self.warp17_call('ConfigurePort', clean_pcfg)

    def test_configure_l3_intf_valid(self):
        """Tests the ConfigureL3Intf API with valid config"""

        l3_arg = L3IntfArg(lia_eth_port=0,
                           lia_l3_intf=self._pcfg.pc_l3_intfs[0])
        self.assertEqual(self.warp17_call('ConfigureL3Intf', l3_arg).e_code, 0,
                         'Connfigure Single L3 Intf')
        result = self.warp17_call('GetPortCfg', PortArg(pa_eth_port=0))
        self.assertEqual(result.pcr_error.e_code, 0, 'GetPortCfg Single')
        self.assertEqual(len(result.pcr_cfg.pc_l3_intfs), 1, 'L3Intf Single Cnt')
        self.assertTrue(self._pcfg.pc_l3_intfs[0] == result.pcr_cfg.pc_l3_intfs[0],
                        'L3Intf Single Eq')

    def test_configure_l3_intf_invalid_port(self):
        """Tests the ConfigureL3Intf API with invalid port"""

        l3_arg = L3IntfArg(lia_eth_port=self.PORT_CNT + 1,
                           lia_l3_intf=self._pcfg.pc_l3_intfs[0])
        self.assertEqual(self.warp17_call('ConfigureL3Intf', l3_arg).e_code,
                         -errno.EINVAL,
                         'L3Intf invalid port')

    def test_configure_l3_intf_duplicate(self):
        """Tests the ConfigureL3Intf API when trying to add a duplicate"""

        l3_arg = L3IntfArg(lia_eth_port=0,
                           lia_l3_intf=self._pcfg.pc_l3_intfs[0])
        self.assertEqual(self.warp17_call('ConfigureL3Intf', l3_arg).e_code, 0,
                         'Configure First L3 Intf')
        self.assertEqual(self.warp17_call('ConfigureL3Intf', l3_arg).e_code,
                         -errno.EEXIST,
                         'Configure Duplicate L3 Intf')

    def test_configure_l3_intf_test_running(self):
        """Tests the ConfigureL3Intf API when tests are already running"""

        self.assertEqual(self.warp17_call('ConfigureTestCase',
                                          _get_dummy_server_test(0, 0)).e_code,
                         0,
                         'Configure Test Case')
        self.assertEqual(self.warp17_call('PortStart', PortArg(pa_eth_port=0)).e_code, 0,
                         'Port Start')

        l3_arg = L3IntfArg(lia_eth_port=0,
                           lia_l3_intf=self._pcfg.pc_l3_intfs[0])

        self.assertEqual(self.warp17_call('ConfigureL3Intf', l3_arg).e_code,
                         -errno.EALREADY)

        # Test already running cleanup
        self.warp17_call('PortStop', PortArg(pa_eth_port=0))
        self.warp17_call('DelTestCase', TestCaseArg(tca_eth_port=0, tca_test_case_id=0))

    def test_configure_l3_intf_max_reached(self):
        """Tests the ConfigureL3Intf API when trying to add more than max"""

        error = self.warp17_call('ConfigurePort', self._pcfg)
        self.assertEqual(error.e_code, 0, 'ConfigurePort')

        l3_arg = L3IntfArg(lia_eth_port=0,
                           lia_l3_intf=L3Intf(l3i_ip=Ip(ip_version=IPV4, ip_v4=b2b_ipv4(1, 0)),
                                              l3i_mask=Ip(ip_version=IPV4, ip_v4=b2b_mask(1, 0)),
                                              l3i_count=1))
        self.assertEqual(self.warp17_call('ConfigureL3Intf', l3_arg).e_code,
                         -errno.ENOMEM,
                         'Configure MAX L3 Intf')

    def test_configure_l3_gw_valid(self):
        """Tests the ConfigureL3Gw API with valid config"""

        l3_gw_arg = L3GwArg(lga_eth_port=0, lga_gw=self._pcfg.pc_def_gw)
        self.assertEqual(self.warp17_call('ConfigureL3Gw', l3_gw_arg).e_code, 0,
                         'Connfigure Valid Gw')
        result = self.warp17_call('GetPortCfg', PortArg(pa_eth_port=0))
        self.assertEqual(result.pcr_error.e_code, 0, 'GetPortCfg Single')
        self.assertTrue(self._pcfg.pc_def_gw == result.pcr_cfg.pc_def_gw,
                        'Gw Eq')

    def test_configure_l3_gw_invalid_port(self):
        """Tests the ConfigureL3Gw API with invalid port"""

        l3_gw_arg = L3GwArg(lga_eth_port=4242, lga_gw=self._pcfg.pc_def_gw)
        self.assertEqual(self.warp17_call('ConfigureL3Gw', l3_gw_arg).e_code,
                         -errno.EINVAL,
                         'Connfigure Gw invalid port')

    def test_configure_l3_gw_test_running(self):
        """Tests the ConfigureL3Gw API when tests are already running"""

        self.assertEqual(self.warp17_call('ConfigureTestCase',
                                          _get_dummy_server_test(0, 0)).e_code,
                         0,
                         'Configure Test Case')
        self.assertEqual(self.warp17_call('PortStart', PortArg(pa_eth_port=0)).e_code, 0,
                         'Port Start')

        l3_gw_arg = L3GwArg(lga_eth_port=0, lga_gw=self._pcfg.pc_def_gw)

        self.assertEqual(self.warp17_call('ConfigureL3Gw', l3_gw_arg).e_code,
                         -errno.EALREADY)

        # Test already running cleanup
        self.warp17_call('PortStop', PortArg(pa_eth_port=0))
        self.warp17_call('DelTestCase', TestCaseArg(tca_eth_port=0, tca_test_case_id=0))

class TestPartialApiBase:
    def test_partial_test_case_update_valid(self):
        """Tests the UpdateTestCase* API with valid configs"""
        self._update_calls(self._tc_arg, 0)

    def test_partial_test_case_update_invalid_port(self):
        """Tests the UpdateTestCase* API with invalid ports"""
        tc_arg = TestCaseArg(tca_eth_port=self.PORT_CNT + 1,
                             tca_test_case_id=self._tc_arg.tca_test_case_id)
        self._update_calls(tc_arg, -errno.EINVAL)

    def test_partial_test_case_update_invalid_test_case(self):
        """Tests the UpdateTestCase* API with invalid test case ids"""
        tc_arg = TestCaseArg(tca_eth_port=self._tc_arg.tca_eth_port,
                             tca_test_case_id=TPG_TEST_MAX_ENTRIES + 1)
        self._update_calls(tc_arg, -errno.EINVAL)

    def test_partial_test_case_update_no_test_case(self):
        """Tests the UpdateTestCase* API with inexistent test case ids"""
        tc_arg = TestCaseArg(tca_eth_port=self._tc_arg.tca_eth_port,
                             tca_test_case_id=self._tc_arg.tca_test_case_id + 1)
        self._update_calls(tc_arg, -errno.ENOENT)

    def test_partial_test_case_update_test_running(self):
        """Tests the UpdateTestCase* API when tests are already running"""
        err = self.warp17_call('PortStart', self._tc_arg)
        self.assertEqual(err.e_code, 0, 'PortStart')

        self._update_calls(self._tc_arg, -errno.EALREADY)

class TestPartialClientApi(Warp17UnitTestCase, TestPartialApiBase):
    """Tests the functionality of the partial update/get client config APIs."""
    """Assumes a B2B setup with even two ports."""
    """Port 0 <-> Port 1"""
    PORT_CNT = 2

    def setUp(self):
        tc_cfg = _get_dummy_client_test(0, 0)
        self._port_arg = PortArg(pa_eth_port=tc_cfg.tc_eth_port)
        self._tc_arg = TestCaseArg(tca_eth_port=tc_cfg.tc_eth_port,
                                   tca_test_case_id=tc_cfg.tc_id)
        self.assertEqual(self.warp17_call('ConfigureTestCase', tc_cfg).e_code,
                         0,
                         'Configure Test Case')

    def tearDown(self):
        self.warp17_call('PortStop', self._port_arg)
        self.warp17_call('DelTestCase', self._tc_arg)

    def _update_calls(self, tc_arg, expected_err):
        invalid_err = -errno.EINVAL if expected_err == 0 else expected_err

        # Rate updates
        for rate_type in [OPEN, SEND, CLOSE]:
            err = self.warp17_call('UpdateTestCaseRate',
                                   UpdRateArg(ura_tc_arg=tc_arg,
                                              ura_rate_type=rate_type,
                                              ura_rate=Rate(r_value=42)))
            self.assertEqual(err.e_code, expected_err, 'UpdateRate')

        err = self.warp17_call('UpdateTestCaseRate',
                               UpdRateArg(ura_tc_arg=tc_arg,
                                          ura_rate_type=CLRT_MAX,
                                          ura_rate=Rate(r_value=42)))
        self.assertEqual(err.e_code, invalid_err, 'UpdateRate Invalid')

        # Timeout updates
        for delay_type in [INIT, UPTIME, DOWNTIME]:
            err = self.warp17_call('UpdateTestCaseDelay',
                                   UpdDelayArg(uda_tc_arg=tc_arg,
                                               uda_delay_type=delay_type,
                                               uda_delay=Delay(d_value=42)))
            self.assertEqual(err.e_code, expected_err, 'UpdateDelay')

        err = self.warp17_call('UpdateTestCaseDelay',
                               UpdDelayArg(uda_tc_arg=tc_arg,
                                           uda_delay_type=CLDE_MAX,
                                           uda_delay=Delay(d_value=42)))
        self.assertEqual(err.e_code, invalid_err, 'UpdateDelay Invalid')

        # Criteria updates
        err = self.warp17_call('UpdateTestCaseCriteria',
                               UpdCritArg(uca_tc_arg=tc_arg,
                                          uca_criteria=TestCriteria(tc_crit_type=RUN_TIME,
                                                                    tc_run_time_s=42)))
        self.assertEqual(err.e_code, expected_err, 'UpdateCriteria')

        # Async updates
        err = self.warp17_call('UpdateTestCaseAsync',
                               UpdAsyncArg(uaa_tc_arg=tc_arg,
                                           uaa_async=True))
        self.assertEqual(err.e_code, expected_err, 'UpdateAsync')

        # Client App updates
        cl_app = AppClient(ac_app_proto=RAW, ac_raw=RawClient(rc_req_plen=42,
                                                              rc_resp_plen=42))
        err = self.warp17_call('UpdateTestCaseAppClient',
                               UpdClientArg(uca_tc_arg=tc_arg,
                                            uca_cl_app=cl_app))
        self.assertEqual(err.e_code, expected_err, 'UpdateAppClient')

        if expected_err == 0:
            cl_result = self.warp17_call('GetTestCaseAppClient', tc_arg)
            self.assertEqual(cl_result.tccr_error.e_code, 0)
            self.assertTrue(cl_result.tccr_cl_app, cl_app)

    def test_partial_test_case_update_server_wrong_type(self):
        """Tests the UpdateTestCaseAppServer API when the underlying test is a client"""
        srv_app = AppServer(as_app_proto=RAW, as_raw=RawServer(rs_req_plen=42,
                                                               rs_resp_plen=42))
        err = self.warp17_call('UpdateTestCaseAppServer',
                               UpdServerArg(usa_tc_arg=self._tc_arg,
                                            usa_srv_app=srv_app))
        self.assertEqual(err.e_code, -errno.EINVAL, 'UpdateAppServer')

class TestPartialServerApi(Warp17UnitTestCase, TestPartialApiBase):
    """Tests the functionality of the partial update/get server config APIs."""
    """Assumes a B2B setup with even two ports."""
    """Port 0 <-> Port 1"""
    PORT_CNT = 2

    def setUp(self):
        tc_cfg = _get_dummy_server_test(0, 0)
        self._port_arg = PortArg(pa_eth_port=tc_cfg.tc_eth_port)
        self._tc_arg = TestCaseArg(tca_eth_port=tc_cfg.tc_eth_port,
                                   tca_test_case_id=tc_cfg.tc_id)
        self.assertEqual(self.warp17_call('ConfigureTestCase', tc_cfg).e_code,
                         0,
                         'Configure Test Case')

    def tearDown(self):
        self.warp17_call('PortStop', self._port_arg)
        self.warp17_call('DelTestCase', self._tc_arg)

    def _update_calls(self, tc_arg, expected_err):

        # Criteria updates
        err = self.warp17_call('UpdateTestCaseCriteria',
                               UpdCritArg(uca_tc_arg=tc_arg,
                                          uca_criteria=TestCriteria(tc_crit_type=SRV_UP,
                                                                    tc_srv_up=42)))
        self.assertEqual(err.e_code, expected_err, 'UpdateCriteria')

        # Async updates
        err = self.warp17_call('UpdateTestCaseAsync',
                               UpdAsyncArg(uaa_tc_arg=tc_arg,
                                           uaa_async=True))
        self.assertEqual(err.e_code, expected_err, 'UpdateAsync')

        # Server App updates
        srv_app = AppServer(as_app_proto=RAW, as_raw=RawServer(rs_req_plen=42,
                                                               rs_resp_plen=42))
        err = self.warp17_call('UpdateTestCaseAppServer',
                               UpdServerArg(usa_tc_arg=tc_arg,
                                            usa_srv_app=srv_app))
        self.assertEqual(err.e_code, expected_err, 'UpdateAppServer')

        if expected_err == 0:
            srv_result = self.warp17_call('GetTestCaseAppServer', tc_arg)
            self.assertEqual(srv_result.tcsr_error.e_code, 0)
            self.assertTrue(srv_result.tcsr_srv_app, srv_app)

    def test_partial_test_case_update_client_wrong_type(self):
        """Tests the UpdateTestCaseAppClient API when the underlying test is a server"""
        cl_app = AppClient(ac_app_proto=RAW, ac_raw=RawClient(rc_req_plen=42,
                                                              rc_resp_plen=42))
        err = self.warp17_call('UpdateTestCaseAppClient',
                               UpdClientArg(uca_tc_arg=self._tc_arg,
                                            uca_cl_app=cl_app))
        self.assertEqual(err.e_code, -errno.EINVAL, 'UpdateAppClient')

