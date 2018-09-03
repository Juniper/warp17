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

from warp17_ut import Warp17UnitTestCase
from warp17_ut import Warp17TrafficTestCase

from warp17_common_pb2    import *
from warp17_server_pb2    import *
from warp17_client_pb2    import *
from warp17_app_http_pb2  import *
from warp17_app_raw_pb2   import *
from warp17_app_pb2       import *
from warp17_test_case_pb2 import *
from warp17_service_pb2   import *

class TestHttpCfg(Warp17TrafficTestCase, Warp17UnitTestCase):

    # Allow test cases to run for a while so we actually see requests/responses
    RUN_TIME_S = 3

    def _http_client_cfg(self, method=GET, req_size=42,
                         fields='Content-Type: plain/text'):
        return App(app_proto=HTTP_CLIENT,
                   app_http_client=HttpClient(hc_req_method=method,
                                              hc_req_object_name='/index.html',
                                              hc_req_host_name='www.foobar.net',
                                              hc_req_size=req_size,
                                              hc_req_fields=fields))

    def _http_server_cfg(self, resp_code=OK_200, resp_size=42,
                         fields='Content-Type: plain/text'):
        return App(app_proto=HTTP_SERVER,
                   app_http_server=HttpServer(hs_resp_code=resp_code,
                                              hs_resp_size=resp_size,
                                              hs_resp_fields=fields))

    #####################################################
    # Overrides of Warp17TrafficTestCase specific to HTTP
    #####################################################
    def get_l3_intf_count(self):
        if Warp17UnitTestCase.env.get_ring_ports() > 0:
            return 1
        return super(TestHttpCfg, self).get_l3_intf_count()

    def get_l4_port_count(self):
        if Warp17UnitTestCase.env.get_ring_ports() > 0:
            return 1
        return super(TestHttpCfg, self).get_l4_port_count()

    def get_tc_retry_count(self):
        """Allow the test to actually finish. Don't be too aggressive with"""
        """retrying"""
        return self.RUN_TIME_S + 2

    def get_client_app_cfg(self, eth_port, tc_id):
        return self._http_client_cfg()

    def get_client_criteria_cfg(self, eth_port, tc_id, l3_intf_count,
                                l4_port_count):
        return TestCriteria(tc_crit_type=RUN_TIME, tc_run_time_s=self.RUN_TIME_S)

    def get_server_app_cfg(self, eth_port, tc_id):
        return self._http_server_cfg()

    def get_updates(self):
        for (method, req_size, resp_code, resp_size) in \
            [(method, req_size, resp_code, resp_size)
             for method in [GET, HEAD]
             for req_size in [0, 42, 65535]
             for resp_code in [OK_200, NOT_FOUND_404]
             for resp_size in [0, 42, 65535]]:
            self.lh.info('REQ METHOD {} REQSZ {} RESP CODE {} RESP SZ {}'.
                        format(str(method), req_size, str(resp_code),
                               resp_size))
            yield (self._http_client_cfg(method, req_size),
                   self._http_server_cfg(resp_code, resp_size))

    def get_invalid_updates(self):
        for (method, resp_code) in \
            [(method, resp_code)
             for method in [POST, PUT, DELETE, CONNECT, OPTIONS, TRACE]
             for resp_code in [FORBIDDEN_403]]:
            self.lh.info('REQ METHOD {} RESP CODE {}'.
                        format(str(method), str(resp_code)))
            yield (self._http_client_cfg(method),
                   self._http_server_cfg(resp_code))
        for fields in ['Content-Length: 42']:
            self.lh.info('FIELDS {}'.format(fields))
            yield (self._http_client_cfg(fields=fields),
                   self._http_server_cfg(fields=fields))

    def _update(self, descr, tc_arg, app, expected_err):
        self.lh.info('Run Update {}'.format(descr))

        update_arg = UpdateAppArg(uaa_tc_arg=tc_arg, uaa_app=app)
        err = self.warp17_call('UpdateTestCaseApp', update_arg)
        self.assertEqual(err.e_code, expected_err)

        if expected_err == 0:
            result = self.warp17_call('GetTestCaseApp', update_arg.uaa_tc_arg)
            self.assertEqual(result.tcar_error.e_code, 0)
            self.assertTrue(result.tcar_app == app)

    def update_client(self, tc_arg, http_client, expected_err=0):
        self._update('update_client', tc_arg, http_client, expected_err)

    def update_server(self, tc_arg, http_server, expected_err=0):
        self._update('update_server', tc_arg, http_server, expected_err)

    def verify_stats(self, cl_result, srv_result, cl_update, srv_update):
        req_cnt = cl_result.tsr_app_stats.as_http.hsts_req_cnt
        resp_cnt = cl_result.tsr_app_stats.as_http.hsts_resp_cnt
        self.lh.info('cl req_cnt: {} resp_cnt: {}'.format(req_cnt, resp_cnt))
        self.assertTrue(req_cnt > 0)
        self.assertTrue(resp_cnt > 0)

        req_cnt = srv_result.tsr_app_stats.as_http.hsts_req_cnt
        resp_cnt = srv_result.tsr_app_stats.as_http.hsts_resp_cnt
        self.lh.info('srv req_cnt: {} resp_cnt: {}'.format(req_cnt, resp_cnt))
        self.assertTrue(req_cnt > 0)
        self.assertTrue(resp_cnt > 0)

    def test_multiple_test_cases(self):
        """Test multiple HTTP simultaneous test cases"""

        # Create an additional client test case to run in parallel.
        client_tc2 = self.get_client_test_case(eth_port=0, tc_id=1)

        # Just change the source ports to make sure they don't overlap
        l4_sports = client_tc2.tc_client.cl_l4.l4c_tcp_udp.tuc_sports

        # Use a single new source port (i.e., last source port of the first
        # test case + 1)
        client_tc2.tc_client.cl_l4.l4c_tcp_udp.tuc_sports.l4pr_start = l4_sports.l4pr_end + 1
        client_tc2.tc_client.cl_l4.l4c_tcp_udp.tuc_sports.l4pr_end = l4_sports.l4pr_end + 2

        client_tc1_arg = TestCaseArg(tca_eth_port=0, tca_test_case_id=0)
        client_tc2_arg = TestCaseArg(tca_eth_port=0, tca_test_case_id=1)

        # Configure the new test case, start the tests and expect all test
        # cases to pass.
        self.configureTestCase(client_tc2, 'Client2')

        self.startPorts()

        self.check_test_case_status(client_tc1_arg)
        self.check_test_case_status(client_tc2_arg)

        self.stopPorts()

        # Cleanup the additional client test case we created.
        self.delTestCase(client_tc2_arg, 'Client2')


class TestHttpRaw(Warp17TrafficTestCase, Warp17UnitTestCase):

    # Allow test cases to run for a while so we actually see requests/responses
    RUN_TIME_S = 3

    def _raw_client_cfg(self):
        return App(app_proto=RAW_CLIENT,
                   app_raw_client=RawClient(rc_req_plen=10000,
                                            rc_resp_plen=20000))

    def _http_server_cfg(self, resp_code=OK_200, resp_size=42,
                         fields='Content-Type: plain/text'):
        return App(app_proto=HTTP_SERVER,
                   app_http_server=HttpServer(hs_resp_code=resp_code,
                                              hs_resp_size=resp_size,
                                              hs_resp_fields=fields))

    #####################################################
    # Overrides of Warp17TrafficTestCase specific to HTTP
    #####################################################
    def get_l3_intf_count(self):
        if Warp17UnitTestCase.env.get_ring_ports() > 0:
            return 1
        return super(Warp17TrafficTestCase, self).get_l3_intf_count()

    def get_l4_port_count(self):
        if Warp17UnitTestCase.env.get_ring_ports() > 0:
            return 1
        return super(Warp17TrafficTestCase, self).get_l4_port_count()

    def get_tc_retry_count(self):
        """Allow the test to actually finish. Don't be too aggressive with"""
        """retrying"""
        return self.RUN_TIME_S + 2

    def get_client_app_cfg(self, eth_port, tc_id):
        return self._raw_client_cfg()

    def get_client_criteria_cfg(self, eth_port, tc_id, l3_intf_count,
                                l4_port_count):
        return TestCriteria(tc_crit_type=RUN_TIME, tc_run_time_s=self.RUN_TIME_S)

    def get_server_app_cfg(self, eth_port, tc_id):
        return self._http_server_cfg()

    def get_updates(self):
        yield self._raw_client_cfg(), self._http_server_cfg(OK_200, 0)

    def get_invalid_updates(self):
        for _ in []: yield ()

    def _update(self, descr, tc_arg, app, expected_err):
        self.lh.info('Run Update {}'.format(descr))

        update_arg = UpdateAppArg(uaa_tc_arg=tc_arg, uaa_app=app)
        err = self.warp17_call('UpdateTestCaseApp', update_arg)
        self.assertEqual(err.e_code, expected_err)

        if expected_err == 0:
            result = self.warp17_call('GetTestCaseApp', update_arg.uaa_tc_arg)
            self.assertEqual(result.tcar_error.e_code, 0)
            self.assertTrue(result.tcar_app == app)

    def update_client(self, tc_arg, raw_client, expected_err=0):
        self._update('update_client', tc_arg, raw_client, expected_err)

    def update_server(self, tc_arg, http_server, expected_err=0):
        self._update('update_server', tc_arg, http_server, expected_err)

    def verify_stats(self, cl_result, srv_result, cl_update, srv_update):
        req_cnt = cl_result.tsr_app_stats.as_raw.rsts_req_cnt
        resp_cnt = cl_result.tsr_app_stats.as_raw.rsts_resp_cnt
        self.lh.info('req_cnt: {} resp_cnt: {}'.format(req_cnt, resp_cnt))
        self.assertEqual(req_cnt, 0)
        self.assertEqual(resp_cnt, 0)

        req_cnt = srv_result.tsr_app_stats.as_http.hsts_req_cnt
        resp_cnt = srv_result.tsr_app_stats.as_http.hsts_resp_cnt
        self.lh.info('srv req_cnt: {} resp_cnt: {}'.format(req_cnt, resp_cnt))
        self.assertEqual(req_cnt, 0)
        self.assertEqual(resp_cnt, 0)

