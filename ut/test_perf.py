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
#     test_perf.py
#
# Description:
#     Performance tests for WARP17.
#
# Author:
#     Dumitru Ceara, Eelco Chaudron
#
# Initial Created:
#     02/15/2016
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

from warp17_ut  import Warp17UnitTestCase
from warp17_api import Warp17Env
from b2b_setup  import *

from warp17_common_pb2    import *
from warp17_l3_pb2        import *
from warp17_app_raw_pb2   import *
from warp17_app_http_pb2  import *
from warp17_server_pb2    import *
from warp17_client_pb2    import *
from warp17_test_case_pb2 import *
from warp17_service_pb2   import *
from warp17_sockopt_pb2   import *

class TestPerfNames:
    # On some port types we might want to skip sending big data
    SKIP_BIG_DATA = 'skip-big-data'

    # WARP17 Rates and Session values
    SETUP_RLIMIT   = 'setup-rate-limit'
    SEND_RLIMIT    = 'send-rate-limit'
    ALLOWED_FAILED = 'allowed-failed-sessions'

    TCP_SETUP_RATE      = 'tcp-setup-rate'
    TCP_DATA_SETUP_RATE = 'tcp-data-setup-rate'

    UDP_DATA_SETUP_RATE       = 'udp-data-setup-rate'
    UDP_MCAST_DATA_SETUP_RATE = 'udp-mcast-data-setup-rate'

    HTTP_DATA_SETUP_RATE = 'http-data-setup-rate'

    TIMESTAMP_TCP_RATE = 'timestamp_tcp_rate'

    TIMESTAMP_UDP_RATE = 'timestamp_udp_rate'

class TestPerf(Warp17UnitTestCase):
    """Tests the WARP17 performance."""
    """Assumes a B2B setup with two ports."""
    """Port 0 <-> Port 1"""
    PERF_RUN_COUNT = 8

    CFG_SEC_UNIT_TEST = 'unit-test'

    def _get_skip_big_data(self):
        val = Warp17UnitTestCase.env.get_value(TestPerfNames.SKIP_BIG_DATA,
                                               section=self.CFG_SEC_UNIT_TEST,
                                               mandatory=True,
                                               cast=int)
        return False if val is None else val != 0

    def _get_setup_rlimit(self):
        val = Warp17UnitTestCase.env.get_value(TestPerfNames.SETUP_RLIMIT,
                                               section=self.CFG_SEC_UNIT_TEST,
                                               cast=int)
        return None if val is None else val

    def _get_send_rlimit(self):
        val = Warp17UnitTestCase.env.get_value(TestPerfNames.SEND_RLIMIT,
                                               section=self.CFG_SEC_UNIT_TEST,
                                               cast=int)
        return None if val is None else val

    def _get_sess_allowed_failed(self):
        val = Warp17UnitTestCase.env.get_value(TestPerfNames.ALLOWED_FAILED,
                                               section=self.CFG_SEC_UNIT_TEST,
                                               cast=int)
        return 0 if val is None else val

    def _get_expected_tcp_setup(self):
        return Warp17UnitTestCase.env.get_value(TestPerfNames.TCP_SETUP_RATE,
                                                section=self.CFG_SEC_UNIT_TEST,
                                                cast=int)

    def _get_expected_tcp_tstamp(self):
        return Warp17UnitTestCase.env.get_value(TestPerfNames.TIMESTAMP_TCP_RATE,
                                                section=self.CFG_SEC_UNIT_TEST,
                                                cast=int)

    def _get_expected_udp_tstamp(self):
        return Warp17UnitTestCase.env.get_value(TestPerfNames.TIMESTAMP_UDP_RATE,
                                                section=self.CFG_SEC_UNIT_TEST,
                                                cast=int)

    def _get_expected_tcp_data_setup(self):
        return Warp17UnitTestCase.env.get_value(TestPerfNames.TCP_DATA_SETUP_RATE,
                                                section=self.CFG_SEC_UNIT_TEST,
                                                cast=int)

    def _get_expected_udp_data_setup(self):
        return Warp17UnitTestCase.env.get_value(TestPerfNames.UDP_DATA_SETUP_RATE,
                                                section=self.CFG_SEC_UNIT_TEST,
                                                cast=int)

    def _get_expected_udp_mcast_setup(self):
        return Warp17UnitTestCase.env.get_value(TestPerfNames.UDP_MCAST_DATA_SETUP_RATE,
                                                section=self.CFG_SEC_UNIT_TEST,
                                                cast=int)
    def _get_expected_http_setup(self):
        return Warp17UnitTestCase.env.get_value(TestPerfNames.HTTP_DATA_SETUP_RATE,
                                                section=self.CFG_SEC_UNIT_TEST,
                                                cast=int)

    def _get_open_rate(self):
        setup_rlimit = self._get_setup_rlimit()
        if setup_rlimit is None:
            return Rate()
        return Rate(r_value=int(setup_rlimit))

    def _get_send_rate(self):
        send_rlimit = self._get_send_rlimit()
        if send_rlimit is None:
            return Rate()
        return Rate(r_value=int(send_rlimit))

    def _get_tc_stats(self, eth_port, test_case_id, tc_expected, timeout_s):
        time.sleep(timeout_s)
        # Check test case state
        tc_result = self.warp17_call('GetTestStatus',
                                      TestCaseArg(tca_eth_port=eth_port,
                                                  tca_test_case_id=test_case_id))
        self.assertEqual(tc_result.tsr_error.e_code, 0, 'GetTestStatus')
        self.assertEqual(tc_result.tsr_state, tc_expected,
                         'PortStatus Expected')
        return tc_result

    def _set_tstamp(self, eth_port, test_case_id):

        if eth_port == 0:
            ip4_opt = Ipv4Sockopt(ip4so_tx_tstamp=True)
        elif eth_port == 1:
            ip4_opt = Ipv4Sockopt(ip4so_rx_tstamp=True)

        ip4_opt_arg = Ipv4SockoptArg(i4sa_tc_arg=TestCaseArg(tca_eth_port=eth_port,
                                                             tca_test_case_id=test_case_id),
                                     i4sa_opts=ip4_opt)

        self.assertEqual(
            self.warp17_call('SetIpv4Sockopt', ip4_opt_arg).e_code,
            0, 'SetIpv4Sockopt')

    def _sess_setup_rate(self, sip_cnt, dip_cnt, sport_cnt, dport_cnt, l4_proto,
                         rate_ccfg, app_ccfg, app_scfg, expected_rate, tstamp,
                         recent_stats):
        sess_cnt = sip_cnt * dip_cnt * sport_cnt * dport_cnt
        s_latency = TestCaseLatency()
        # Adjust the session count so we allow some failures
        sess_cnt -= self._get_sess_allowed_failed()

        serv_cnt = dip_cnt * dport_cnt

        # Add two extra seconds..
        timeout_cl_s = int(sess_cnt / float(expected_rate)) + 2

        # No def gw
        no_def_gw = Ip(ip_version=IPV4, ip_v4=0)

        # Setup interfaces on port 0
        pcfg = b2b_port_add(0, def_gw = no_def_gw)
        b2b_port_add_intfs(pcfg, [(Ip(ip_version=IPV4, ip_v4=b2b_ipv4(0, i)),
                                   Ip(ip_version=IPV4, ip_v4=b2b_mask(0, i)),
                                   b2b_count(0, i)) for i in range(0, sip_cnt)])
        self.assertEqual(self.warp17_call('ConfigurePort',pcfg).e_code, 0,
                         'ConfigurePort')

        # Setup interfaces on port 1
        pcfg = b2b_port_add(1, def_gw = no_def_gw)
        b2b_port_add_intfs(pcfg,
                           [(Ip(ip_version=IPV4, ip_v4=b2b_ipv4(1, i)),
                             Ip(ip_version=IPV4, ip_v4=b2b_mask(1, i)),
                             b2b_count(1, i)) for i in range(0, dip_cnt)])
        self.assertEqual(self.warp17_call('ConfigurePort', pcfg).e_code, 0,
                         'ConfigurePort')

        l4_ccfg = L4Client(l4c_proto=l4_proto,
                           l4c_tcp_udp=TcpUdpClient(tuc_sports=b2b_ports(sport_cnt),
                                                    tuc_dports=b2b_ports(dport_cnt)))
        delay_ccfg = DelayClient(dc_init_delay=Delay(d_value=0),
                                 dc_uptime=Delay(),
                                 dc_downtime=Delay())

        ccfg = TestCase(tc_type=CLIENT, tc_eth_port=0,
                        tc_id=0,
                        tc_client=Client(cl_src_ips=b2b_sips(0, sip_cnt),
                                         cl_dst_ips=b2b_dips(0, dip_cnt),
                                         cl_l4=l4_ccfg,
                                         cl_rates=rate_ccfg,
                                         cl_delays=delay_ccfg,
                                         cl_app=app_ccfg),
                        tc_criteria=TestCriteria(tc_crit_type=CL_ESTAB,
                                                  tc_cl_estab=sess_cnt),
                        tc_async=False)
        self.assertEqual(self.warp17_call('ConfigureTestCase', ccfg).e_code, 0,
                         'ConfigureTestCase')
        if tstamp:
            self._set_tstamp(eth_port=0, test_case_id=0)
        if recent_stats:
            s_latency.tcs_samples = 1000
        l4_scfg = L4Server(l4s_proto=l4_proto,
                           l4s_tcp_udp=TcpUdpServer(tus_ports=b2b_ports(dport_cnt)))
        scfg = TestCase(tc_type=SERVER, tc_eth_port=1, tc_id=0,
                        tc_server=Server(srv_ips=b2b_sips(1, 1),
                                         srv_l4=l4_scfg,
                                         srv_app=app_scfg),
                        tc_criteria=TestCriteria(tc_crit_type=SRV_UP,
                                                 tc_srv_up=serv_cnt),
                        tc_async=False, tc_latency=s_latency)
        self.assertEqual(self.warp17_call('ConfigureTestCase', scfg).e_code, 0,
                         'ConfigureTestCase')
        if tstamp:
            self._set_tstamp(eth_port=1, test_case_id=0)
        # Start server test
        self.assertEqual(self.warp17_call('PortStart',
                                          PortArg(pa_eth_port=1)).e_code,
                         0,
                         'PortStart')

        # Wait for servers to come up
        sresult = self._get_tc_stats(eth_port=1, test_case_id=0,
                                     tc_expected=PASSED,
                                     timeout_s=1)
        self.assertEqual(sresult.tsr_stats.tcs_server.tcss_up, serv_cnt,
                         'PortStart SERVER UP')

        # Start client test
        self.assertEqual(self.warp17_call('PortStart',
                                          PortArg(pa_eth_port=0)).e_code,
                         0,
                         'PortStart')

        # Wait for clients to establish
        cresult = self._get_tc_stats(eth_port=0, test_case_id=0,
                                     tc_expected=PASSED,
                                     timeout_s=timeout_cl_s)
        self.assertTrue(cresult.tsr_stats.tcs_client.tccs_estab >= sess_cnt,
                        'CL ESTAB')

        # check client setup rate
        start_time = cresult.tsr_stats.tcs_start_time
        end_time = cresult.tsr_stats.tcs_end_time

        # start and stop ts are in usecs
        duration = (end_time - start_time) / float(1000000)
        rate = sess_cnt / duration
        self.lh.info('Session count: %(sess)u Duration: %(dur).2f Rate %(rate)u' % \
                     {'sess': sess_cnt, 'dur': duration, 'rate': rate})

        # Stop server test
        self.assertEqual(self.warp17_call('PortStop', PortArg(pa_eth_port=1)).e_code,
                         0,
                         'PortStop')

        # Fail to stop client test (already passed)
        self.assertEqual(self.warp17_call('PortStop', PortArg(pa_eth_port=1)).e_code,
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

        return rate

    def _sess_setup_rate_averaged(self, sip_cnt, dip_cnt, sport_cnt, dport_cnt,
                                  l4_proto, rate_ccfg, app_ccfg, app_scfg,
                                  expected_rate=Rate(), tstamp=False,
                                  recent_stats=False):
        self.assertTrue(self.PERF_RUN_COUNT > 2)
        (min, max, total) = \
                reduce(lambda (min, max, total), rate: \
                            (min if min <= rate else rate,
                             max if max >= rate else rate,
                             total + rate),
                       [self._sess_setup_rate(sip_cnt, dip_cnt, sport_cnt,
                                              dport_cnt, l4_proto, rate_ccfg,
                                              app_ccfg, app_scfg, expected_rate,
                                              tstamp,recent_stats)
                        for i in range(0, self.PERF_RUN_COUNT)],
                       (sys.maxint, 0, 0))
        avg_rate = (total - min - max) / float(self.PERF_RUN_COUNT - 2)

        self.lh.info('Average Rate %(rate)u' % {'rate': avg_rate})
        self.assertTrue(avg_rate >= expected_rate, 'Average Rate!')

    def _get_rates_client(self, rate_limit=True):
        open_rate = self._get_open_rate() if rate_limit else Rate()
        send_rate = self._get_send_rate() if rate_limit else Rate()

        return RateClient(rc_open_rate=open_rate,
                          rc_close_rate=Rate(),
                          rc_send_rate=send_rate)

    def _get_raw_app_client(self, data_size):
        return AppClient(ac_app_proto=RAW,
                         ac_raw=RawClient(rc_req_plen=data_size,
                                          rc_resp_plen=data_size))

    def _get_raw_app_server(self, data_size):
        return AppServer(as_app_proto=RAW,
                         as_raw=RawServer(rs_req_plen=data_size,
                                          rs_resp_plen=data_size))

    def _get_http_app_client(self, req_method, req_size):
        return AppClient(ac_app_proto=HTTP,
                         ac_http=HttpClient(hc_req_method=req_method,
                                            hc_req_object_name='/index.html',
                                            hc_req_host_name='www.foobar.net',
                                            hc_req_size=req_size))

    def _get_http_app_server(self, resp_code, resp_size):
        return AppServer(as_app_proto=HTTP,
                         as_http=HttpServer(hs_resp_code=resp_code,
                                            hs_resp_size=resp_size))

    def test_01_4M_tcp_sess_setup_rate(self):
        """Tests setting up 4M TCP sessions (no traffic)."""
        """Port 0 is the client, Port 1 is the server"""

        self.lh.info('Test test_01_4M_tcp_sess_setup_rate')
        self._sess_setup_rate_averaged(sip_cnt=2, dip_cnt=1, sport_cnt=20000,
                                       dport_cnt=100, l4_proto=TCP,
                                       rate_ccfg=self._get_rates_client(),
                                       app_ccfg=self._get_raw_app_client(0),
                                       app_scfg=self._get_raw_app_server(0),
                                       expected_rate=self._get_expected_tcp_setup())

    def test_02_8M_tcp_sess_setup_rate(self):
        """Tests setting up 8M TCP sessions (no traffic)."""

        self.lh.info('Test test_02_8M_tcp_sess_setup_rate')
        self._sess_setup_rate_averaged(sip_cnt=4, dip_cnt=1, sport_cnt=20000,
                                       dport_cnt=100, l4_proto=TCP,
                                       rate_ccfg=self._get_rates_client(),
                                       app_ccfg=self._get_raw_app_client(0),
                                       app_scfg=self._get_raw_app_server(0),
                                       expected_rate=self._get_expected_tcp_setup())

    def test_03_10M_tcp_sess_setup_rate(self):
        """Tests setting up 10M TCP sessions (no traffic)."""

        self.lh.info('Test test_03_10M_tcp_sess_setup_rate')
        self._sess_setup_rate_averaged(sip_cnt=5, dip_cnt=1, sport_cnt=20000,
                                       dport_cnt=100, l4_proto=TCP,
                                       rate_ccfg=self._get_rates_client(),
                                       app_ccfg=self._get_raw_app_client(0),
                                       app_scfg=self._get_raw_app_server(0),
                                       expected_rate=self._get_expected_tcp_setup())

    def test_04_4M_tcp_sess_data_10b_setup_rate(self):
        """Tests setting up 4M TCP sessions + 10 bytes packet data."""

        self.lh.info('Test test_04_4M_tcp_sess_data_10b_setup_rate')
        self._sess_setup_rate_averaged(sip_cnt=2, dip_cnt=1, sport_cnt=20000,
                                       dport_cnt=100, l4_proto=TCP,
                                       rate_ccfg=self._get_rates_client(),
                                       app_ccfg=self._get_raw_app_client(10),
                                       app_scfg=self._get_raw_app_server(10),
                                       expected_rate=self._get_expected_tcp_data_setup())

    def test_05_4M_tcp_sess_data_1024b_setup_rate(self):
        """Tests setting up 4M TCP sessions + 1024 byte packet data."""

        if self._get_skip_big_data():
            self.skipTest('Big packet data tests skipped')

        self.lh.info('Test test_05_4M_tcp_sess_data_1024b_setup_rate')
        self._sess_setup_rate_averaged(sip_cnt=2, dip_cnt=1, sport_cnt=20000,
                                       dport_cnt=100, l4_proto=TCP,
                                       rate_ccfg=self._get_rates_client(),
                                       app_ccfg=self._get_raw_app_client(1024),
                                       app_scfg=self._get_raw_app_server(1024),
                                       expected_rate=self._get_expected_tcp_data_setup())

    def test_06_4M_tcp_sess_data_1300b_setup_rate(self):
        """Tests setting up 4M TCP sessions + 1300 bytes packet data."""

        if self._get_skip_big_data():
            self.skipTest('Big packet data tests skipped')

        self.lh.info('Test test_06_4M_tcp_sess_data_1300b_setup_rate')
        self._sess_setup_rate_averaged(sip_cnt=2, dip_cnt=1, sport_cnt=20000,
                                       dport_cnt=100, l4_proto=TCP,
                                       rate_ccfg=self._get_rates_client(),
                                       app_ccfg=self._get_raw_app_client(1300),
                                       app_scfg=self._get_raw_app_server(1300),
                                       expected_rate=self._get_expected_tcp_data_setup())

    def test_07_4M_udp_sess_data_10b_setup_rate(self):
        """Tests setting up 4M UDP sessions + 10 byte packet data."""

        self.lh.info('Test test_07_4M_udp_sess_data_10b_setup_rate')
        # No rate limiting for UDP!
        self._sess_setup_rate_averaged(sip_cnt=2, dip_cnt=1, sport_cnt=20000,
                                       dport_cnt=100, l4_proto=UDP,
                                       rate_ccfg=self._get_rates_client(
                                           rate_limit=False),
                                       app_ccfg=self._get_raw_app_client(10),
                                       app_scfg=self._get_raw_app_server(10),
                                       expected_rate=self._get_expected_udp_data_setup())

    def test_08_4M_http_sess_data_10b_setup_rate(self):
        """Tests setting up 4M HTTP GET/200OK sessions + 10 byte packet data."""

        self.lh.info('Test test_08_4M_http_sess_data_10b_setup_rate')
        self._sess_setup_rate_averaged(sip_cnt=2, dip_cnt=1, sport_cnt=20000,
                                       dport_cnt=100, l4_proto=TCP,
                                       rate_ccfg=self._get_rates_client(),
                                       app_ccfg=self._get_http_app_client(GET,
                                                                          10),
                                       app_scfg=self._get_http_app_server(
                                           OK_200, 10),
                                       expected_rate=self._get_expected_http_setup())

    def test_09_4M_udp_mcast_flows_data_10b_setup_rate(self):
        """Tests setting up 4M UDP mcast sessions + 10 byte packet data."""

        self.lh.info('Test test_09_4M_udp_mcast_flows_data_10b_setup_rate')
        self._sess_setup_rate_averaged(sip_cnt=2, dip_cnt=1, sport_cnt=20000,
                                       dport_cnt=100, l4_proto=UDP,
                                       rate_ccfg=self._get_rates_client(),
                                       app_ccfg=self._get_raw_app_client(10),
                                       app_scfg=self._get_raw_app_server(0),
                                       expected_rate=self._get_expected_udp_mcast_setup())

    def test_10_timestamp_4M_tcp_sess_setup_rate(self):
        """Tests setting up 4M TCP sessions (no traffic)."""
        """Port 0 is the client, Port 1 is the server"""

        self.lh.info('Test test_10_timestamp_4M_tcp_sess_setup_rate')
        self._sess_setup_rate_averaged(sip_cnt=2, dip_cnt=1, sport_cnt=20000,
                                       dport_cnt=100, l4_proto=TCP,
                                       rate_ccfg=self._get_rates_client(),
                                       app_ccfg=self._get_raw_app_client(0),
                                       app_scfg=self._get_raw_app_server(0),
                                       expected_rate=self._get_expected_tcp_tstamp(),
                                       tstamp=True)

    def test_11_timestamp_4M_udp_sess_data_10b_setup_rate(self):
        """Tests setting up 4M UDP sessions + 10 byte packet data."""

        self.lh.info('Test test_11_timestamp_4M_udp_sess_data_10b_setup_rate')
        # No rate limiting for UDP!
        self._sess_setup_rate_averaged(sip_cnt=2, dip_cnt=1, sport_cnt=20000,
                                       dport_cnt=100, l4_proto=UDP,
                                       rate_ccfg=self._get_rates_client(
                                           rate_limit=False),
                                       app_ccfg=self._get_raw_app_client(10),
                                       app_scfg=self._get_raw_app_server(10),
                                       expected_rate=self._get_expected_udp_tstamp(),
                                       tstamp=True)

    def test_12_recent_timestamp_4M_tcp_sess_setup_rate(self):
        """Tests setting up 4M TCP sessions (no traffic)."""
        """Port 0 is the client, Port 1 is the server"""

        self.lh.info('Test test_12_recent_timestamp_4M_tcp_sess_setup_rate')
        self._sess_setup_rate_averaged(sip_cnt=2, dip_cnt=1, sport_cnt=20000,
                                       dport_cnt=100, l4_proto=TCP,
                                       rate_ccfg=self._get_rates_client(),
                                       app_ccfg=self._get_raw_app_client(0),
                                       app_scfg=self._get_raw_app_server(0),
                                       expected_rate=self._get_expected_tcp_tstamp(),
                                       tstamp=True, recent_stats=True)

    def test_13_recent_timestamp_4M_udp_sess_data_10b_setup_rate(self):
        """Tests setting up 4M UDP sessions + 10 byte packet data."""

        self.lh.info('Test test_13_recent_timestamp_4M_udp_sess_data_10b_setup_rate')
        # No rate limiting for UDP!
        self._sess_setup_rate_averaged(sip_cnt=2, dip_cnt=1, sport_cnt=20000,
                                       dport_cnt=100, l4_proto=UDP,
                                       rate_ccfg=self._get_rates_client(
                                           rate_limit=False),
                                       app_ccfg=self._get_raw_app_client(10),
                                       app_scfg=self._get_raw_app_server(10),
                                       expected_rate=self._get_expected_udp_tstamp(),
                                       tstamp=True, recent_stats=True)
