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

from functools import partial

sys.path.append('../../python')
sys.path.append('../../api/generated/py')
sys.path.append('../../ut/lib')

from warp17_api import *

# We hijack a bit the back2back definitions that we use for testing.
# Ideally the user should have his own ways scripting the configured values.
from b2b_setup import *

from warp17_common_pb2    import *
from warp17_l3_pb2        import *
from warp17_server_pb2    import *
from warp17_client_pb2    import *
from warp17_app_raw_pb2   import *
from warp17_app_http_pb2  import *
from warp17_test_case_pb2 import *
from warp17_service_pb2   import *

# 10M sessions
sip_cnt = 1
dip_cnt = 1
sport_cnt = 40000
dport_cnt = 100

sess_cnt = sip_cnt * dip_cnt * sport_cnt * dport_cnt
serv_cnt = dip_cnt * dport_cnt

expected_rate = 1000000
run_cnt = 3

def die(msg):
    sys.stderr.write(msg + ' Should cleanup but we just exit..\n')
    sys.exit(1)

def get_server_test_case(protocol, app, req_size, resp_size):
    l4_scfg = L4Server(l4s_proto=protocol,
                       l4s_tcp_udp=TcpUdpServer(tus_ports=b2b_ports(dport_cnt)))
    app_scfg = {
        RAW: AppServer(as_app_proto=RAW,
                       as_raw=RawServer(rs_req_plen=req_size,
                                        rs_resp_plen=resp_size)),
        HTTP: AppServer(as_app_proto=HTTP,
                        as_http=HttpServer(hs_resp_code=OK_200,
                                           hs_resp_size=resp_size))
    }.get(app)

    return TestCase(tc_type=SERVER, tc_eth_port=1, tc_id=0,
                    tc_server=Server(srv_ips=b2b_sips(1, sip_cnt),
                                     srv_l4=l4_scfg,
                                     srv_app=app_scfg),
                    tc_criteria=TestCriteria(tc_crit_type=SRV_UP,
                                             tc_srv_up=serv_cnt),
                    tc_async=False)

def get_client_test_case(protocol, app, req_size, resp_size):
    l4_ccfg = L4Client(l4c_proto=protocol,
                       l4c_tcp_udp=TcpUdpClient(tuc_sports=b2b_ports(sport_cnt),
                                                tuc_dports=b2b_ports(dport_cnt)))
    rate_ccfg = RateClient(rc_open_rate=Rate(),
                           rc_close_rate=Rate(),
                           rc_send_rate=Rate())

    delay_ccfg = DelayClient(dc_init_delay=Delay(d_value=0),
                             dc_uptime=Delay(),
                             dc_downtime=Delay())

    app_ccfg = {
        RAW: AppClient(ac_app_proto=RAW,
                       ac_raw=RawClient(rc_req_plen=req_size,
                                        rc_resp_plen=resp_size)),
        HTTP: AppClient(ac_app_proto=HTTP,
                        ac_http=HttpClient(hc_req_method=GET,
                                           hc_req_object_name='/index.html',
                                           hc_req_host_name='www.foobar.net',
                                           hc_req_size=req_size))
    }.get(app)

    return TestCase(tc_type=CLIENT, tc_eth_port=0,
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

def configure_server_port(warp17_call, protocol, app, req_size, resp_size):
    pcfg = b2b_port_add(eth_port=1, def_gw=Ip(ip_version=IPV4, ip_v4=0))
    b2b_port_add_intfs(pcfg,
                       [
                        (Ip(ip_version=IPV4, ip_v4=b2b_ipv4(eth_port=1, intf_idx=i)),
                         Ip(ip_version=IPV4, ip_v4=b2b_mask(eth_port=1, intf_idx=i)),
                         b2b_count(eth_port=1, intf_idx=i)) for i in range(0, dip_cnt)
                       ])

    if warp17_call('ConfigurePort', pcfg).e_code != 0:
        die('Error configuring port 1!')

    scfg = get_server_test_case(protocol, app, req_size, resp_size)
    if warp17_call('ConfigureTestCase', scfg).e_code != 0:
        die('Error configuring server test case')

def configure_client_port(warp17_call, protocol, app, req_size, resp_size):
    pcfg = b2b_port_add(eth_port=0, def_gw=Ip(ip_version=IPV4, ip_v4=0))
    b2b_port_add_intfs(pcfg,
                       [
                        (Ip(ip_version=IPV4, ip_v4=b2b_ipv4(eth_port=0, intf_idx=i)),
                         Ip(ip_version=IPV4, ip_v4=b2b_mask(eth_port=0, intf_idx=i)),
                         b2b_count(eth_port=0, intf_idx=i)) for i in range(0, sip_cnt)
                       ])

    if warp17_call('ConfigurePort', pcfg).e_code != 0:
        die('Error configuring port 0!')

    ccfg = get_client_test_case(protocol, app, req_size, resp_size)
    if warp17_call('ConfigureTestCase', ccfg).e_code != 0:
        die('Error configuring client test case')


def run_test(protocol, app, req_size, resp_size):
    env = Warp17Env(path='./test_2_perf_benchmark.ini')
    warp17_pid = warp17_start(env=env, exec_file='../../build/warp17',
                              output_args=Warp17OutputArgs(out_file='/tmp/test_2_perf.out'))
    warp17_wait(env=env, logger=LogHelper(name='benchmark',
                                          filename='/tmp/test_2_perf.log'))

    warp17_call = partial(warp17_method_call, env.get_host_name(),
                          env.get_rpc_port(), Warp17_Stub)

    configure_server_port(warp17_call, protocol, app, req_size, resp_size)
    configure_client_port(warp17_call, protocol, app, req_size, resp_size)
    timeout_s = int(sess_cnt / float(expected_rate)) + 2

    if warp17_call('PortStart', PortArg(pa_eth_port=1)).e_code != 0:
        die('Error starting server test cases!')

    if warp17_call('PortStart', PortArg(pa_eth_port=0)).e_code != 0:
        die('Error starting client test cases!')

    time.sleep(timeout_s)

    result = warp17_call('GetTestStatus', TestCaseArg(tca_eth_port=0,
                                                      tca_test_case_id=0))
    if result.tsr_state != PASSED:
        die('Test case didn\'t pass: ' + str(result))

    start_time = result.tsr_stats.tcs_start_time
    end_time = result.tsr_stats.tcs_end_time

    # start and stop ts are in usecs
    duration = (end_time - start_time) / float(1000000)
    rate = sess_cnt / duration
    txr = result.tsr_link_stats.ls_tx_pkts / duration
    rxr = result.tsr_link_stats.ls_rx_pkts / duration
    link_speed_bytes = float(result.tsr_link_stats.ls_link_speed) * 1024 * 1024 / 8
    tx_usage = min(float(result.tsr_link_stats.ls_tx_bytes) * 100 / duration / link_speed_bytes, 100.0)
    rx_usage = min(float(result.tsr_link_stats.ls_rx_bytes) * 100 / duration / link_speed_bytes, 100.0)

    warp17_stop(env, warp17_pid, force=True)
    return (rate, txr, rxr, tx_usage, rx_usage)

def run_test_averaged(descr, protocol, app, req_size, resp_size, run_cnt):
    results = [run_test(protocol, app, req_size, resp_size)
               for i in range(0, run_cnt)]
    avgs = [sum(result, 0.0) / run_cnt for result in zip(*results)]

    # Print as csv
    print '%(descr)s,%(req_size)u,%(resp_size)u,%(rate).0f,%(txr).0f,%(rxr).0f,%(txu).2f,%(rxu).2f' % \
           {
            'descr': descr, 'req_size': req_size, 'resp_size': resp_size,
            'rate': avgs[0], 'txr': avgs[1], 'rxr': avgs[2],
            'txu': avgs[3], 'rxu': avgs[4]
           }

def run():

    # Print csv header
    print 'Description, req_size, resp_size, rate, tx pps, rx pps, tx usage, rx usage'

    # TCP RAW
    tcp_raw_cfg = [(0, 0), (8, 8), (16, 16), (32, 32), (64, 64), (128, 128),
                   (256, 256), (256, 512), (256, 1024), (256, 2048), (256, 4096),
                   (256, 8192), (512, 8192), (1024, 8192), (2048, 8192)]

    for (req_size, resp_size) in tcp_raw_cfg:
        run_test_averaged('TCP request={req!s}b response={resp!s}b'.format(req=req_size,
                                                                           resp=resp_size),
                          TCP, RAW, req_size, resp_size, run_cnt)

    # HTTP
    http_cfg = [(0, 0), (8, 8), (16, 16), (32, 32), (64, 64), (128, 128),
                (256, 256), (256, 512), (256, 1024), (256, 2048), (256, 4096),
                (256, 8192), (256, 65536), (256, 1048576), (256, 10485760),
                (512, 10485760), (1024, 10485760), (2048, 10485760),
                (4096, 10485760), (8192, 10485760)]

    for (req_size, resp_size) in http_cfg:
        run_test_averaged('HTTP request={req!s}b response={resp!s}b'.format(req=req_size,
                                                                            resp=resp_size),
                          TCP, HTTP, req_size, resp_size, run_cnt)

    # UDP RAW
    udp_raw_cfg = [(0, 0), (8, 8), (16, 16), (32, 32), (64, 64), (128, 128),
                   (256, 256), (256, 512), (256, 2048), (512, 8192),
                   (2048, 8192), (4096, 8192), (8192, 8192)]

    for (req_size, resp_size) in udp_raw_cfg:
        run_test_averaged('UDP request={req!s}b response={resp!s}b'.format(req=req_size,
                                                                           resp=resp_size),
                          UDP, RAW, req_size, resp_size, run_cnt)

if __name__ == '__main__':
    run()

