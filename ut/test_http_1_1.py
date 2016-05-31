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
#     test_http_1_1.py
#
# Description:
#     HTTP 1.1 tests for WARP17
#
# Author:
#     Dumitru Ceara, Eelco Chaudron
#
# Initial Created:
#     03/23/2016
#
# Notes:
#
#

import errno
import sys
import unittest
import time
import os

sys.path.append('./lib')
sys.path.append('../python')
sys.path.append('../api/generated/py')

from warp17_ut   import Warp17UnitTestCase
from warp17_api  import Warp17Env
from b2b_setup import *

from warp17_common_pb2    import *
from warp17_l3_pb2        import *
from warp17_server_pb2    import *
from warp17_client_pb2    import *
from warp17_app_http_pb2  import *
from warp17_test_case_pb2 import *
from warp17_service_pb2   import *

class TestHttpCfg(Warp17UnitTestCase):
    """Tests the HTTP 1.1 configurations."""
    """Assumes a B2B setup with two ports."""
    """Port 0 <-> Port 1"""

    def _http_client_app(self, req_method, req_size):
        return AppClient(ac_app_proto=HTTP,
                         ac_http=HttpClient(hc_req_method=req_method,
                                            hc_req_object_name='/index.html',
                                            hc_req_host_name='www.foobar.net',
                                            hc_req_size=req_size))

    def _http_server_app(self, resp_code, resp_size):
        return AppServer(as_app_proto=HTTP,
                         as_http=HttpServer(hs_resp_code=resp_code,
                                            hs_resp_size=resp_size))

    def _configure_client_test_case(self, req_types, req_sizes, expected_err):
        l4_cfg = L4Client(l4c_proto=TCP,
                          l4c_tcp_udp=TcpUdpClient(tuc_sports=b2b_ports(1),
                                                   tuc_dports=b2b_ports(1)))
        rate_cfg = RateClient(rc_open_rate=Rate(r_value=100000000),
                              rc_close_rate=Rate(r_value=100000000),
                              rc_send_rate=Rate(r_value=100000000))

        delay_cfg = DelayClient(dc_init_delay=Delay(d_value=0),
                                dc_uptime=Delay(d_value=42),
                                dc_downtime=Delay(d_value=42))

        criteria = TestCriteria(tc_crit_type=RUN_TIME, tc_run_time_s=3600)

        tcid = 0
        tcs = []
        for req_type in req_types:
            for req_size in req_sizes:
                cl_cfg = Client(cl_src_ips=b2b_sips(0, 1),
                                cl_dst_ips=b2b_dips(0, 1),
                                cl_l4=l4_cfg,
                                cl_rates=rate_cfg,
                                cl_delays=delay_cfg,
                                cl_app=self._http_client_app(req_type,
                                                             req_size))
                tcs += [
                    TestCase(tc_type=CLIENT, tc_eth_port=0, tc_id=tcid,
                             tc_client=cl_cfg,
                             tc_criteria=criteria,
                             tc_async=False)
                ]

                tcid += 1

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

    def _configure_server_test_case(self, resp_codes, resp_sizes, expected_err):
        l4cfg = L4Server(l4s_proto=TCP,
                         l4s_tcp_udp=TcpUdpServer(tus_ports=b2b_ports(1)))
        criteria = TestCriteria(tc_crit_type=RUN_TIME, tc_run_time_s=3600)

        tcid = 0
        tcs = []
        for resp_code in resp_codes:
            for resp_size in resp_sizes:
                srv_cfg = Server(srv_ips=b2b_sips(1, 1),
                                 srv_l4=l4cfg,
                                 srv_app=self._http_server_app(resp_code,
                                                               resp_size))
                tcs += [
                    TestCase(tc_type=SERVER, tc_eth_port=1, tc_id=tcid,
                             tc_server=srv_cfg,
                             tc_criteria=criteria,
                             tc_async=False)
                ]

                tcid += 1

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

    def _configure_single_session(self, req_type, req_size, resp_type,
                                  resp_size, expected_xchg_cnt):
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

        l4_ccfg = L4Client(l4c_proto=TCP,
                           l4c_tcp_udp=TcpUdpClient(tuc_sports=b2b_ports(1),
                                                    tuc_dports=b2b_ports(1)))
        rate_ccfg = RateClient(rc_open_rate=Rate(),
                               rc_close_rate=Rate(),
                               rc_send_rate=Rate())

        delay_ccfg = DelayClient(dc_init_delay=Delay(d_value=0),
                                 dc_uptime=Delay(),
                                 dc_downtime=Delay())

        ccfg = TestCase(tc_type=CLIENT, tc_eth_port=0,
                        tc_id=0,
                        tc_client=Client(cl_src_ips=b2b_sips(0, 1),
                                         cl_dst_ips=b2b_dips(0, 1),
                                         cl_l4=l4_ccfg,
                                         cl_rates=rate_ccfg,
                                         cl_delays=delay_ccfg,
                                         cl_app=self._http_client_app(req_type,
                                                                      req_size)),
                        tc_criteria=TestCriteria(tc_crit_type=RUN_TIME,
                                                 tc_run_time_s=1),
                        tc_async=False)
        self.assertEqual(self.warp17_call('ConfigureTestCase', ccfg).e_code, 0,
                         'ConfigureTestCase')

        l4_scfg = L4Server(l4s_proto=TCP,
                           l4s_tcp_udp=TcpUdpServer(tus_ports=b2b_ports(1)))
        scfg = TestCase(tc_type=SERVER, tc_eth_port=1, tc_id=0,
                        tc_server=Server(srv_ips=b2b_sips(1, 1),
                                         srv_l4=l4_scfg,
                                         srv_app=self._http_server_app(resp_type,
                                                                       resp_size)),
                        tc_criteria=TestCriteria(tc_crit_type=SRV_UP,
                                                 tc_srv_up=1),
                        tc_async=False)
        self.assertEqual(self.warp17_call('ConfigureTestCase', scfg).e_code, 0,
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
        # should be done in way less than 4 seconds!
        time.sleep(4)
        # Check client test to be passed
        client_result = self.warp17_call('GetTestStatus',
                                         TestCaseArg(tca_eth_port=0,
                                                     tca_test_case_id=0))
        self.assertEqual(client_result.tsr_error.e_code, 0, 'GetTestStatus')
        self.assertEqual(client_result.tsr_state, PASSED, 'PortStatus PASSED')
        self.assertEqual(client_result.tsr_type, CLIENT, 'PortStatus CLIENT')

        self.assertEqual(client_result.tsr_stats.tcs_client.tccs_estab, 1,
                         'PortStatus ESTAB')

        self.lh.info('CL req cnt: %(cl_req)u CL resp cnt: %(cl_resp)u' % \
                     {'cl_req': client_result.tsr_app_stats.tcas_http.hsts_req_cnt,
                      'cl_resp': client_result.tsr_app_stats.tcas_http.hsts_resp_cnt})

        # Check number of req/resp exchanges.
        self.assertTrue(client_result.tsr_app_stats.tcas_http.hsts_req_cnt >= expected_xchg_cnt,
                        'PortStatus REQ CNT')
        self.assertTrue(client_result.tsr_app_stats.tcas_http.hsts_resp_cnt >= expected_xchg_cnt,
                        'PortStatus RESP CNT')

        # Check server test to be passed
        server_result = self.warp17_call('GetTestStatus',
                                          TestCaseArg(tca_eth_port=1,
                                                      tca_test_case_id=0))
        self.assertEqual(server_result.tsr_error.e_code, 0, 'GetTestStatus')
        self.assertEqual(server_result.tsr_state, PASSED, 'PortStatus PASSED')
        self.assertEqual(server_result.tsr_type, SERVER, 'PortStatus SERVER')

        self.lh.info('SRV req cnt: %(srv_req)u SRV resp cnt: %(srv_resp)u' % \
                     {'srv_req': server_result.tsr_app_stats.tcas_http.hsts_req_cnt,
                      'srv_resp': server_result.tsr_app_stats.tcas_http.hsts_resp_cnt})

        # Check number of req/resp exchanges.
        self.assertTrue(server_result.tsr_app_stats.tcas_http.hsts_req_cnt >= expected_xchg_cnt,
                        'PortStatus REQ CNT')
        self.assertTrue(server_result.tsr_app_stats.tcas_http.hsts_resp_cnt >= expected_xchg_cnt,
                        'PortStatus RESP CNT')

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

    def test_1_configure_test_case_valid_http_client(self):
        """Tests all valid HTTP client configurations."""

        self._configure_client_test_case(req_types=[GET, HEAD],
                                         req_sizes=[0, 42, 65535],
                                         expected_err=0)


    def test_2_configure_test_case_invalid_http_client_req(self):
        """Tests invalid HTTP client request types configs."""

        self._configure_client_test_case(req_types=[POST, PUT, DELETE, CONNECT,
                                                    OPTIONS, TRACE],
                                         req_sizes=[0],
                                         expected_err=-errno.EINVAL)


    def test_3_configure_test_case_valid_http_server(self):
        """Tests all valid HTTP server configurations."""

        self._configure_server_test_case(resp_codes=[OK_200, NOT_FOUND_404],
                                         resp_sizes=[0, 42, 65535],
                                         expected_err=0)

    def test_4_configure_test_case_invalid_http_server(self):
        """Tests invalid HTTP server configurations."""

        self._configure_server_test_case(resp_codes=[FORBIDDEN_403],
                                         resp_sizes=[0],
                                         expected_err=-errno.EINVAL)

    def test_5_configure_single_http_sessions(self):
        """Tests various request/response sizes and that the sessions work"""
        sizes = [10, 1024, 10000]

        for req_size in sizes:
            for resp_size in sizes:
                # For now keeep the expected_xcgh_rate fixed.
                self._configure_single_session(GET, req_size, OK_200, resp_size,
                                               3500)

