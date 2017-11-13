#
# DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER
#
# Copyright (c) 2017, Juniper Networks, Inc. All rights reserved.
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
#     test_raw.py
#
# Description:
#     RAW tests for WARP17
#
# Author:
#     Dumitru Ceara
#
# Initial Created:
#     11/09/2017
#
# Notes:
#
#

import sys

sys.path.append('./lib')
sys.path.append('../python')
sys.path.append('../api/generated/py')

from warp17_ut import Warp17UnitTestCase
from warp17_ut import Warp17TrafficTestCase

from warp17_common_pb2    import *
from warp17_server_pb2    import *
from warp17_client_pb2    import *
from warp17_app_raw_pb2   import *
from warp17_test_case_pb2 import *
from warp17_service_pb2   import *

UINT32MAX = 0xFFFFFFFF

class TestRaw(Warp17TrafficTestCase, Warp17UnitTestCase):
    MIN_TS_SIZE = 16

    def _raw_client_cfg(self, req_size, resp_size, rx_ts, tx_ts):
        rc = RawClient(rc_req_plen=req_size, rc_resp_plen=resp_size)

        if not rx_ts is None:
            rc.rc_rx_tstamp = rx_ts
        if not tx_ts is None:
            rc.rc_tx_tstamp = tx_ts

        return AppClient(ac_app_proto=RAW, ac_raw=rc)

    def _raw_server_cfg(self, req_size, resp_size, rx_ts, tx_ts):
        rs = RawServer(rs_req_plen=req_size, rs_resp_plen=resp_size)

        if not rx_ts is None:
            rs.rs_rx_tstamp = rx_ts
        if not tx_ts is None:
            rs.rs_tx_tstamp = tx_ts

        return AppServer(as_app_proto=RAW, as_raw=rs)

    #####################################################
    # Overrides of Warp17TrafficTestCase specific to RAW
    #####################################################
    def get_updates(self):
        for (req_size, resp_size, rx_ts, tx_ts) in \
            [   # small payload + no timestmap
                (self.MIN_TS_SIZE - 1, self.MIN_TS_SIZE - 1, None, None),
                # small payload + no timestmap
                (self.MIN_TS_SIZE - 1, self.MIN_TS_SIZE - 1, False, None),
                # small payload + no timestmap
                (self.MIN_TS_SIZE - 1, self.MIN_TS_SIZE - 1, None, False),
                # bigger response + rx-ts
                (self.MIN_TS_SIZE - 1, self.MIN_TS_SIZE, True, None),
                # bigger request + tx-ts
                (self.MIN_TS_SIZE, self.MIN_TS_SIZE - 1, None, True),
                # bigger request + tx-ts
                (self.MIN_TS_SIZE, self.MIN_TS_SIZE, True, True)
            ]:
            yield (self._raw_client_cfg(req_size, resp_size, rx_ts, tx_ts),
                   self._raw_server_cfg(req_size, resp_size, tx_ts, rx_ts))

    def get_invalid_updates(self):
        for (req_size, resp_size, rx_ts, tx_ts) in \
            [
                # small req + tx timestmap
                (self.MIN_TS_SIZE - 1, 0, None, True),
                # small resp + rx timestmap
                (0, self.MIN_TS_SIZE - 1, True, None),
                # small req + tx timestmap & small resp + rx timestamp
                (self.MIN_TS_SIZE - 1, self.MIN_TS_SIZE - 1, True, True)
            ]:
            yield (self._raw_client_cfg(req_size, resp_size, rx_ts, tx_ts),
                   self._raw_server_cfg(req_size, resp_size, tx_ts, rx_ts))

    def update_client(self, tc_arg, raw_client, expected_err=0):
        err = self.warp17_call('UpdateTestCaseAppClient',
                               UpdClientArg(uca_tc_arg=tc_arg,
                                            uca_cl_app=raw_client))
        self.assertEqual(err.e_code, expected_err)

        if expected_err == 0:
            cl_result = self.warp17_call('GetTestCaseAppClient', tc_arg)
            self.assertEqual(cl_result.tccr_error.e_code, 0)
            self.assertTrue(cl_result.tccr_cl_app == raw_client)

    def update_server(self, tc_arg, raw_server, expected_err=0):
        err = self.warp17_call('UpdateTestCaseAppServer',
                               UpdServerArg(usa_tc_arg=tc_arg,
                                            usa_srv_app=raw_server))
        self.assertEqual(err.e_code, expected_err)

        if expected_err == 0:
            srv_result = self.warp17_call('GetTestCaseAppServer', tc_arg)
            self.assertEqual(srv_result.tcsr_error.e_code, 0)
            self.assertTrue(srv_result.tcsr_srv_app == raw_server)

    def verify_stats(self, cl_result, srv_result, raw_client, raw_server):
        super(TestRaw, self).verify_stats(cl_result, srv_result,
                                          raw_client,
                                          raw_server)

        # If rx timestamping was enabled check that we could actually compute
        # latency.
        if raw_client.ac_raw.HasField('rc_rx_tstamp') and \
                raw_client.ac_raw.rc_rx_tstamp:
            latency_stats = cl_result.tsr_stats.tcs_latency_stats.tcls_stats
            self.assertLess(latency_stats.ls_min_latency, UINT32MAX,
                            'ls_min_latency')
            self.assertGreater(latency_stats.ls_max_latency, 0,
                               'ls_max_latency')
            self.assertGreater(latency_stats.ls_sum_latency, 0,
                               'ls_sum_latency')
            self.assertGreater(latency_stats.ls_samples_count, 0,
                               'ls_samples_count')

        # If tx timestamping was enabled check that we could actually compute
        # latency.
        if raw_server.as_raw.HasField('rs_rx_tstamp') and \
                raw_server.as_raw.rs_rx_tstamp:
            latency_stats = srv_result.tsr_stats.tcs_latency_stats.tcls_stats
            self.assertLess(latency_stats.ls_min_latency, UINT32MAX,
                            'ls_min_latency')
            self.assertGreater(latency_stats.ls_max_latency, 0,
                               'ls_max_latency')
            self.assertGreater(latency_stats.ls_sum_latency, 0,
                               'ls_sum_latency')
            self.assertGreater(latency_stats.ls_samples_count, 0,
                               'ls_samples_count')

