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
#     test_imix.py
#
# Description:
#     IMIX tests for WARP17
#
# Author:
#     Dumitru Ceara
#
# Initial Created:
#     02/14/2017
#
# Notes:
#
#

import sys
import errno
import unittest

sys.path.append('./lib')
sys.path.append('../python')
sys.path.append('../api/generated/py')

from warp17_ut import Warp17UnitTestCase
from warp17_ut import Warp17TrafficTestCase

from warp17_common_pb2    import *
from warp17_server_pb2    import *
from warp17_client_pb2    import *
from warp17_app_http_pb2  import *
from warp17_app_pb2       import *
from warp17_test_case_pb2 import *
from warp17_service_pb2   import *

class ImixBase():
    def build_app_client(self, size):
        return App(app_proto=HTTP_CLIENT,
                   app_http_client=HttpClient(hc_req_method=GET,
                                              hc_req_object_name='/index.html',
                                              hc_req_host_name='www.foobar.net',
                                              hc_req_size=size))

    def build_app_server(self, size):
        return App(app_proto=HTTP_SERVER,
                   app_http_server=HttpServer(hs_resp_code=OK_200,
                                              hs_resp_size=size))

    def build_imix_group(self, imix_id, apps):
        imix_group = ImixGroup(imix_id=imix_id)
        for app in apps:
            imix_group.imix_apps.add(ia_weight=app.ia_weight, ia_app=app.ia_app)
        return imix_group

    def configure_imix_group(self, msg, imix_group, expected_err=0):
        err = self.warp17_call('ConfigureImixGroup', imix_group).e_code
        self.assertEqual(err, expected_err,
                         'ConfigureImixGroup: {}'.format(msg))
        if expected_err == 0:
            result_group = self.get_imix_group(msg, imix_group.imix_id)
            self.assertEqual(imix_group, result_group,
                             'GetImixGroup (compare): {}'.format(msg))

    def get_imix_group(self, msg, imix_id):
        result = self.warp17_call('GetImixGroup', ImixArg(ia_imix_id=imix_id))
        self.assertEqual(result.ir_error.e_code, 0,
                         'GetImixGroup: {}'.format(msg))
        return result.ir_imix_group

    def delete_imix_group(self, msg, imix_id, check=True):
        err = self.warp17_call('DelImixGroup', ImixArg(ia_imix_id=imix_id))
        if check:
            self.assertEqual(err.e_code, 0, 'DelImixGroup: {}'.format(msg))

class TestImixGroup(Warp17UnitTestCase, ImixBase):

    def tearDown(self):
        """Cleanup any potential IMIX groups"""
        for imix_id in range(0, TPG_IMIX_MAX_GROUPS):
            self.delete_imix_group('TearDown', imix_id, check=False)

    def test_cfg_invalid_imix_id(self):
        """Tests configuring an IMIX group with an invalid id"""

        apps = [ImixApp(ia_weight=1, ia_app=self.build_app_client(10))]
        imix_group = self.build_imix_group(TPG_IMIX_MAX_GROUPS, apps)
        self.configure_imix_group('Invalid IMIX ID', imix_group, -errno.EINVAL)

    def test_cfg_valid_app_count_0(self):
        """Tests configuring a valid IMIX group id but app count 0."""

        apps = []
        imix_group = self.build_imix_group(0, apps)
        self.configure_imix_group('Valid IMIX ID, 0 APPs', imix_group,
                                  -errno.EINVAL)

    @unittest.expectedFailure
    def test_cfg_valid_app_count_max(self):
        """Tests configuring a valid IMIX group id but app count TPG_IMIX_MAX_APPS + 1."""
        """WARP17 validates the total app count but doesn't return an error"""
        """yet!"""

        single_app = ImixApp(ia_weight=1, ia_app=self.build_app_client(10))
        apps = [single_app] * (TPG_IMIX_MAX_APPS + 1)
        imix_group = self.build_imix_group(0, apps)
        self.configure_imix_group('Valid IMIX ID, MAX APPs', imix_group,
                                  -errno.EINVAL)

    def test_cfg_valid_imix(self):
        """Tests configuring a valid IMIX group."""

        apps = [ImixApp(ia_weight=1, ia_app=self.build_app_client(10))]
        imix_group = self.build_imix_group(0, apps)
        self.configure_imix_group('Valid IMIX ID', imix_group, 0)

    def test_cfg_exists_imix(self):
        """Tests configuring a valid IMIX group which already exists."""

        apps = [ImixApp(ia_weight=1, ia_app=self.build_app_client(10))]
        imix_group = self.build_imix_group(0, apps)
        self.configure_imix_group('New IMIX ID', imix_group, 0)
        self.configure_imix_group('Existing IMIX ID', imix_group, -errno.EEXIST)

    def test_cfg_valid_weight_0(self):
        """Tests configuring a valid IMIX group but with weight 0."""

        apps = [ImixApp(ia_weight=0, ia_app=self.build_app_client(10))]
        imix_group = self.build_imix_group(0, apps)
        self.configure_imix_group('Weight 0', imix_group, -errno.EINVAL)

    def test_cfg_valid_weight_total(self):
        """Tests configuring a valid IMIX group but with weight > TPG_IMIX_MAX_TOTAL_APP_WEIGHT."""

        apps = [ImixApp(ia_weight=1, ia_app=self.build_app_client(10))]
        imix_group = self.build_imix_group(TPG_IMIX_MAX_TOTAL_APP_WEIGHT + 1,
                                           apps)
        self.configure_imix_group('Weight 0', imix_group, -errno.EINVAL)

class TestImixTraffic(Warp17TrafficTestCase, Warp17UnitTestCase, ImixBase):

    CLIENT_IMIX_ID = 0
    SERVER_IMIX_ID = 1

    #####################################################
    # Overrides of Warp17TrafficTestCase specific to IMIX
    #####################################################
    def setUp(self):
        # Set up our client imix group
        client_apps = [
            ImixApp(ia_weight=10, ia_app=self.build_app_client(10)),
            ImixApp(ia_weight=20, ia_app=self.build_app_client(20)),
            ImixApp(ia_weight=30, ia_app=self.build_app_client(30)),
            ImixApp(ia_weight=40, ia_app=self.build_app_client(40)),
        ]

        self._client_imix_group = \
            self.build_imix_group(TestImixTraffic.CLIENT_IMIX_ID, client_apps)
        self.configure_imix_group('Client', self._client_imix_group)

        # Set up our server imix group
        server_apps = [
            ImixApp(ia_weight=10, ia_app=self.build_app_server(10)),
            ImixApp(ia_weight=20, ia_app=self.build_app_server(20)),
            ImixApp(ia_weight=30, ia_app=self.build_app_server(30)),
            ImixApp(ia_weight=40, ia_app=self.build_app_server(40)),
        ]

        self._server_imix_group = \
            self.build_imix_group(TestImixTraffic.SERVER_IMIX_ID, server_apps)
        self.configure_imix_group('Server', self._server_imix_group)

        # Once our imix groups are configured, let the base class do its work.
        super(TestImixTraffic, self).setUp()

    def tearDown(self):

        # First tear down any test cases built by the base class
        super(TestImixTraffic, self).tearDown()

        # Delete our imix groups
        self.delete_imix_group('Client', TestImixTraffic.CLIENT_IMIX_ID)
        self.delete_imix_group('Client', TestImixTraffic.SERVER_IMIX_ID)

    def get_updates(self):
        yield (App(app_proto=IMIX,
                   app_imix=Imix(imix_id=TestImixTraffic.CLIENT_IMIX_ID)),
               App(app_proto=IMIX,
                   app_imix=Imix(imix_id=TestImixTraffic.SERVER_IMIX_ID)))

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

    def update_client(self, tc_arg, app, expected_err=0):
        self._update('update_client', tc_arg, app, expected_err)

    def update_server(self, tc_arg, app, expected_err=0):
        self._update('update_server', tc_arg, app, expected_err)

    def verify_stats(self, cl_result, srv_result, cl_app, srv_app):
        self.verify_stats_imix('verify_stats_client', cl_result, cl_app)
        self.verify_stats_imix('verify_stats_client', srv_result, srv_app)

    def verify_stats_imix(self, msg, result, app):
        result = self.warp17_call('GetImixStatistics',
                                  ImixArg(ia_imix_id=app.app_imix.imix_id))
        self.assertEqual(result.isr_error.e_code, 0)
        self.assertEqual(result.isr_stats.ias_imix_id, app.app_imix.imix_id)

        imix_stats = result.isr_stats

        imix_group = self.get_imix_group('verify_stats_imix', app.app_imix.imix_id)

        self.assertEqual(len(imix_group.imix_apps), len(imix_stats.ias_apps))

        # Compute total weight, total requests and do some checks
        total_weight = 0
        total_requests = 0
        for i in range(0, len(imix_group.imix_apps)):
            self.assertEqual(imix_group.imix_apps[i].ia_app.app_proto,
                             imix_stats.ias_app_protos[i])

            total_weight += imix_group.imix_apps[i].ia_weight
            total_requests += imix_stats.ias_apps[i].as_http.hsts_req_cnt

        # Check the distribution
        for i in range(0, len(imix_stats.ias_apps)):
            percentage = imix_stats.ias_apps[i].as_http.hsts_req_cnt * 100 / total_requests
            expected = imix_group.imix_apps[i].ia_weight * 100 / total_weight

            self.assertTrue(abs(percentage - expected) < 5)

