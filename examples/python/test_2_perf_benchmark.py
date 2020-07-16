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
#     test_2_perf_benchmark.py
#
# Description:
#     Script acting as a performance benchmark for WARP17.
#
# Author:
#     Dumitru Ceara, Eelco Chaudron
#
# Initial Created:
#     05/03/2016
#
# Notes:
#     You can run this script with the following command:
#     python -u test_2_perf_benchmark.py > ../../benchmark/benchmark.csv
#

import sys
import time
import argparse

from functools import partial

from warp17_api import *

# We hijack a bit the back2back definitions that we use for testing.
# Ideally the user should have his own ways scripting the configured values.
from b2b_setup import *

from warp17_common_pb2    import *
from warp17_l3_pb2        import *
from warp17_server_pb2    import *
from warp17_client_pb2    import *
from warp17_app_pb2       import *
from warp17_app_raw_pb2   import *
from warp17_app_http_pb2  import *
from warp17_test_case_pb2 import *
from warp17_sockopt_pb2   import *
from warp17_service_pb2   import *

# 10M sessions
sip_cnt = 1
dip_cnt = 1
sport_cnt = 50000
dport_cnt = 200

sess_cnt = sip_cnt * dip_cnt * sport_cnt * dport_cnt
serv_cnt = dip_cnt * dport_cnt

expected_rate = 3000000

# Run the test for a bit to get TX/RX rates
runtime_s = 4

# Scale of the TCP send window
tcp_win_size = 32000

run_cnt = 3

def die(msg):
    sys.stderr.write(msg + ' Should cleanup but we just exit..\n')
    sys.exit(1)

def server_test_case(protocol, app_cfg):
    l4_cfg = L4Server(l4s_proto=protocol,
                      l4s_tcp_udp=TcpUdpServer(tus_ports=b2b_ports(dport_cnt)))
    return TestCase(
        tc_type=SERVER, tc_eth_port=1, tc_id=0,
        tc_server=Server(srv_ips=b2b_sips(1, sip_cnt), srv_l4=l4_cfg),
        tc_app=app_cfg,
        tc_criteria=TestCriteria(tc_crit_type=SRV_UP,
                                 tc_srv_up=serv_cnt),
        tc_async=False)

def server_raw_app_cfg(req_size):
    return App(app_proto=RAW_SERVER,
               app_raw_server=RawServer(rs_req_plen=req_size,
                                        rs_resp_plen=req_size))

def server_http_app_cfg(req_size):
    return App(app_proto=HTTP_SERVER,
               app_http_server=HttpServer(hs_resp_code=OK_200,
                                          hs_resp_size=req_size))

def server_udp_app_cfg(req_size):
    return App(app_proto=RAW_SERVER,
               app_raw_server=RawServer(rs_req_plen=req_size,
                                        rs_resp_plen=0))

def client_test_case(protocol, app_cfg, criteria):
    l4_cfg = L4Client(l4c_proto=protocol,
                      l4c_tcp_udp=TcpUdpClient(tuc_sports=b2b_ports(sport_cnt),
                                               tuc_dports=b2b_ports(dport_cnt)))
    rate_cfg = RateClient(rc_open_rate=Rate(),
                          rc_close_rate=Rate(),
                          rc_send_rate=Rate())

    return TestCase(tc_type=CLIENT, tc_eth_port=0,
                    tc_id=0,
                    tc_client=Client(cl_src_ips=b2b_sips(0, sip_cnt),
                                     cl_dst_ips=b2b_dips(0, dip_cnt),
                                     cl_l4=l4_cfg,
                                     cl_rates=rate_cfg),
                    tc_app=app_cfg,
                    tc_criteria=criteria,
                    tc_async=False)

def client_raw_app_cfg(req_size):
    return App(app_proto=RAW_CLIENT,
               app_raw_client=RawClient(rc_req_plen=req_size,
                                        rc_resp_plen=req_size))

def client_http_app_cfg(req_size):
    return App(app_proto=HTTP_CLIENT,
               app_http_client=HttpClient(hc_req_method=GET,
                                          hc_req_object_name='/index.html',
                                          hc_req_host_name='www.foobar.net',
                                          hc_req_size=req_size))

def client_udp_app_cfg(req_size):
    return App(app_proto=RAW_CLIENT,
               app_raw_client=RawClient(rc_req_plen=req_size,
                                        rc_resp_plen=0))

def server_port_cfg():
    pcfg = b2b_port_add(eth_port=1, def_gw=Ip(ip_version=IPV4, ip_v4=0))
    b2b_port_add_intfs(pcfg,
                       [
                        (Ip(ip_version=IPV4, ip_v4=b2b_ipv4(eth_port=1, intf_idx=i)),
                         Ip(ip_version=IPV4, ip_v4=b2b_mask(eth_port=1, intf_idx=i)),
                         b2b_count(eth_port=1, intf_idx=i)) for i in range(0, dip_cnt)
                       ])
    return pcfg

def client_port_cfg():
    pcfg = b2b_port_add(eth_port=0, def_gw=Ip(ip_version=IPV4, ip_v4=0))
    b2b_port_add_intfs(pcfg,
                       [
                        (Ip(ip_version=IPV4, ip_v4=b2b_ipv4(eth_port=0, intf_idx=i)),
                         Ip(ip_version=IPV4, ip_v4=b2b_mask(eth_port=0, intf_idx=i)),
                         b2b_count(eth_port=0, intf_idx=i)) for i in range(0, sip_cnt)
                       ])
    return pcfg

def run_setup_test(w17_call, cl_cfg, srv_cfg):
    if w17_call('ConfigureTestCase', srv_cfg).e_code != 0:
        die('Error configuring server test case')

    if w17_call('ConfigureTestCase', cl_cfg).e_code != 0:
        die('Error configuring client test case')

    if w17_call('PortStart', PortArg(pa_eth_port=1)).e_code != 0:
        die('Error starting server test cases!')

    if w17_call('PortStart', PortArg(pa_eth_port=0)).e_code != 0:
        die('Error starting client test cases!')

    timeout_s = int(sess_cnt / float(expected_rate)) + 1

    time.sleep(timeout_s)

    result = w17_call('GetTestStatus', TestCaseArg(tca_eth_port=0,
                                                   tca_test_case_id=0))

    stats_result = w17_call('GetStatistics', PortArg(pa_eth_port=0))

    if result.tsr_state != PASSED:
        die('Test case didn\'t pass: ' + str(result))

    start_time = result.tsr_stats.gs_start_time
    end_time = result.tsr_stats.gs_end_time

    # start and stop ts are in usecs
    duration = (end_time - start_time) / float(1000000)
    return sess_cnt / duration

def run_rate_test(w17_call, cl_cfg, srv_cfg, tcp_opts_cfg):
    if w17_call('ConfigureTestCase', srv_cfg).e_code != 0:
        die('Error configuring server test case')

    if w17_call('ConfigureTestCase', cl_cfg).e_code != 0:
        die('Error configuring client test case')

    if tcp_opts_cfg:
        if w17_call('SetTcpSockopt',
                    TcpSockoptArg(toa_tc_arg=TestCaseArg(tca_eth_port=0,
                                  tca_test_case_id=0),
                                  toa_opts=tcp_opts_cfg)).e_code != 0:
            die('Error setting client tcp sockopt!')
        if w17_call('SetTcpSockopt',
                    TcpSockoptArg(toa_tc_arg=TestCaseArg(tca_eth_port=1,
                                  tca_test_case_id=0),
                                  toa_opts=tcp_opts_cfg)).e_code != 0:
            die('Error setting server tcp sockopt!')

    if w17_call('PortStart', PortArg(pa_eth_port=1)).e_code != 0:
        die('Error starting server test cases!')

    if w17_call('PortStart', PortArg(pa_eth_port=0)).e_code != 0:
        die('Error starting client test cases!')

    timeout_s = runtime_s + 1

    time.sleep(timeout_s)

    result = w17_call('GetTestStatus', TestCaseArg(tca_eth_port=0,
                                                   tca_test_case_id=0))

    stats_p0 = w17_call('GetStatistics', PortArg(pa_eth_port=0))
    stats_p1 = w17_call('GetStatistics', PortArg(pa_eth_port=1))

    if result.tsr_state != PASSED:
        die('Test case didn\'t pass: ' + str(result))

    duration = float(runtime_s)

    txr = stats_p0.sr_phy.pys_tx_pkts / duration
    rxr = stats_p1.sr_phy.pys_tx_pkts / duration
    link_speed_bytes = float(stats_p0.sr_phy.pys_link_speed) * 1000 * 1000 / 8
    tx_usage = min(float(stats_p0.sr_phy.pys_tx_bytes) * 100 / duration / link_speed_bytes, 100.0)
    rx_usage = min(float(stats_p1.sr_phy.pys_tx_bytes) * 100 / duration / link_speed_bytes, 100.0)

    return (txr, rxr, tx_usage, rx_usage)

def cleanup_test(w17_call):
    w17_call('PortStop', PortArg(pa_eth_port=1))
    w17_call('PortStop', PortArg(pa_eth_port=0))
    w17_call('DelTestCase', TestCaseArg(tca_eth_port=1, tca_test_case_id=0))
    w17_call('DelTestCase', TestCaseArg(tca_eth_port=0, tca_test_case_id=0))
    w17_call('ClearStatistics', PortArg(pa_eth_port=1))
    w17_call('ClearStatistics', PortArg(pa_eth_port=0))

def run_test(w17_call, cl_cfg_fn, srv_cfg_fn, tcp_opts_cfg):

    setup_crit = TestCriteria(tc_crit_type=CL_ESTAB, tc_cl_estab=sess_cnt)
    rate_crit  = TestCriteria(tc_crit_type=RUN_TIME, tc_run_time_s=runtime_s)

    cleanup_test(w17_call)

    rate = \
        run_setup_test(w17_call, cl_cfg_fn(setup_crit), srv_cfg_fn())

    cleanup_test(w17_call)

    (txr, rxr, tx_usage, rx_usage) = \
        run_rate_test(w17_call, cl_cfg_fn(rate_crit), srv_cfg_fn(),
                      tcp_opts_cfg)

    cleanup_test(w17_call)

    return (rate, txr, rxr, tx_usage, rx_usage)

def setup_ports(w17_call):
    if w17_call('ConfigurePort', client_port_cfg()).e_code != 0:
        die('Error configuring client port!')

    if w17_call('ConfigurePort', server_port_cfg()).e_code != 0:
        die('Error configuring server port!')

def run_test_averaged(w17_call, cl_cfg_fn, srv_cfg_fn, tcp_opts_cfg):

    results = [run_test(w17_call, cl_cfg_fn, srv_cfg_fn, tcp_opts_cfg)
               for i in range(0, run_cnt)]
    return [sum(result, 0.0) / run_cnt for result in zip(*results)]

def run():
    parser = argparse.ArgumentParser()

    parser.add_argument('-f', '--file', help='path to warp17 binary',
                        required=True)
    args = parser.parse_args()

    configs = [
        ('TCP',  TCP,  client_raw_app_cfg,  server_raw_app_cfg, TcpSockopt(to_win_size=tcp_win_size)),
        ('HTTP', TCP,  client_http_app_cfg, server_http_app_cfg, TcpSockopt(to_win_size=tcp_win_size)),
        ('UDP', UDP,  client_udp_app_cfg,  server_udp_app_cfg, None),
    ]

    payload_sizes = [0, 32, 64, 128, 256, 512, 1024, 4096, 8192]

    env = Warp17Env(path=os.path.join(os.path.dirname(__file__),
                                      './test_2_perf_benchmark.ini'))
    warp17_pid = warp17_start(env=env, exec_file=args.file,
                              output_args=Warp17OutputArgs(out_file='/tmp/test_2_perf.out'))
    warp17_wait(env=env, logger=LogHelper(name='benchmark',
                                          filename='/tmp/test_2_perf.log'))
    w17_call = partial(warp17_method_call, env.get_host_name(),
                       env.get_rpc_port(), Warp17_Stub)

    setup_ports(w17_call)

    # Print csv header
    print('Description, req_size, resp_size, rate, tx pps, rx pps, tx usage, rx usage')

    for (test_name, proto, cl_cfg_fn, srv_cfg_fn, tcp_opts) in configs:
        for payload in payload_sizes:
            descr = '{} request={}b response={}b'.format(test_name, payload, payload)

            avgs = run_test_averaged(w17_call,
                                     partial(client_test_case, proto, cl_cfg_fn(payload)),
                                     partial(server_test_case, proto, srv_cfg_fn(payload)),
                                     tcp_opts)
            # Print as csv
            print('%(descr)s,%(req_size)u,%(resp_size)u,%(rate).0f,%(txr).0f,%(rxr).0f,%(txu).2f,%(rxu).2f' % \
            {
                'descr': descr, 'req_size': payload, 'resp_size': payload,
                'rate': avgs[0], 'txr': avgs[1], 'rxr': avgs[2],
                'txu': avgs[3], 'rxu': avgs[4]
            })

    warp17_stop(env, warp17_pid, force=True)

if __name__ == '__main__':
    run()

