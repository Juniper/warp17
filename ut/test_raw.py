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

from warp17_ut import Warp17UnitTestCase
from warp17_ut import Warp17TrafficTestCase

from warp17_common_pb2    import *
from warp17_server_pb2    import *
from warp17_client_pb2    import *
from warp17_app_raw_pb2   import *
from warp17_app_pb2       import *
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

        return App(app_proto=RAW_CLIENT, app_raw_client=rc)

    def _raw_server_cfg(self, req_size, resp_size, rx_ts, tx_ts):
        rs = RawServer(rs_req_plen=req_size, rs_resp_plen=resp_size)

        if not rx_ts is None:
            rs.rs_rx_tstamp = rx_ts
        if not tx_ts is None:
            rs.rs_tx_tstamp = tx_ts

        return App(app_proto=RAW_SERVER, app_raw_server=rs)

    #####################################################
    # Overrides of Warp17TrafficTestCase specific to RAW
    #####################################################
    def get_l3_intf_count(self):
        if Warp17UnitTestCase.env.get_ring_ports() > 0:
            return 1
        return super(TestRaw, self).get_l3_intf_count()

    def get_l4_port_count(self):
        if Warp17UnitTestCase.env.get_ring_ports() > 0:
            return 1
        return super(TestRaw, self).get_l4_port_count()

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

    def update_server(self, tc_arg, raw_server, expected_err=0):
        self._update('update_server', tc_arg, raw_server, expected_err)

    def verify_stats(self, cl_result, srv_result, raw_client, raw_server):
        super(TestRaw, self).verify_stats(cl_result, srv_result,
                                          raw_client,
                                          raw_server)

        # If rx timestamping was enabled check that we could actually compute
        # latency.
        if raw_client.app_raw_client.HasField('rc_rx_tstamp') and \
                raw_client.app_raw_client.rc_rx_tstamp:
            latency_stats = cl_result.tsr_stats.gs_latency_stats.gls_stats
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
        if raw_server.app_raw_server.HasField('rs_rx_tstamp') and \
                raw_server.app_raw_server.rs_rx_tstamp:
            latency_stats = srv_result.tsr_stats.gs_latency_stats.gls_stats
            self.assertLess(latency_stats.ls_min_latency, UINT32MAX,
                            'ls_min_latency')
            self.assertGreater(latency_stats.ls_max_latency, 0,
                               'ls_max_latency')
            self.assertGreater(latency_stats.ls_sum_latency, 0,
                               'ls_sum_latency')
            self.assertGreater(latency_stats.ls_samples_count, 0,
                               'ls_samples_count')

