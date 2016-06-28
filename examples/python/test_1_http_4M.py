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
#     test_1_http_4M.py
#
# Description:
#     Example of using the WARP17 python API.
#
# Author:
#     Dumitru Ceara, Eelco Chaudron
#
# Initial Created:
#     04/13/2016
#
# Notes:
#
#

import sys
import time

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
from warp17_app_http_pb2  import *
from warp17_test_case_pb2 import *
from warp17_service_pb2   import *

def die(msg):
    sys.stderr.write(msg + ' Should cleanup but we just exit..\n')
    sys.exit(1)

def init_test():
    # Configure the WARP17 mandatory environment variables. These could also be
    # set outside the script (through normal env variables) but for this example
    # we do it here..

    # Save the hostname and tcp port WARP17 listens on. Also the env.
    global warp17_host
    global warp17_port
    global env

    env = Warp17Env(path='./test_1_http_4M.ini')

    # First start WARP17 on the local machine, default IP and port.
    exec_bin = '../../build/warp17'
    warp17_host = env.get_host_name()
    warp17_port = env.get_rpc_port()

    # Ask for the output log to go to /tmp/test_1_api_example.out
    # Returns the process id of WARP17.
    # This will throw an exception if the output file can't be created or if
    # WARP17 can't be started.
    warp17_pid = warp17_start(env=env, exec_file=exec_bin,
                              output_args=Warp17OutputArgs(out_file='/tmp/test_1_api_example.out'))

    # Now wait for WARP17 to finish initializing
    # This will exit if WARP17 fails to initialize
    warp17_wait(env)
    return warp17_pid

def configure_client_port():
    # Configure 2 client IP interfaces and no default gateway on port 0.
    # First the default gw
    pcfg = b2b_port_add(eth_port=0, def_gw =Ip(ip_version=IPV4, ip_v4=0))

    intf1 = (Ip(ip_version=IPV4, ip_v4=b2b_ipv4(eth_port=0, intf_idx=0)),
             Ip(ip_version=IPV4, ip_v4=b2b_mask(eth_port=0, intf_idx=0)),
             b2b_count(eth_port=0, intf_idx=0))
    intf2 = (Ip(ip_version=IPV4, ip_v4=b2b_ipv4(eth_port=0, intf_idx=1)),
             Ip(ip_version=IPV4, ip_v4=b2b_mask(eth_port=0, intf_idx=1)),
             b2b_count(eth_port=0, intf_idx=1))
    b2b_port_add_intfs(pcfg, [intf1, intf2])

    # Ask WARP17 to add them to the config.
    err = warp17_method_call(warp17_host, warp17_port, Warp17_Stub, 'ConfigurePort', pcfg)
    if err.e_code != 0:
        die('Error configuring port 0.')

    # Prepare the L4 Client config (eth_port 0)
    # source ports in the range: [10001, 30000]
    # destination ports in the range: [101, 200]
    # In total (per client IP): 2M sessions.
    # In total: 4M sessions
    sport_range=L4PortRange(l4pr_start=10001, l4pr_end=30000)
    dport_range=L4PortRange(l4pr_start=101, l4pr_end=200)
    l4_ccfg = L4Client(l4c_proto=TCP,
                       l4c_tcp_udp=TcpUdpClient(tuc_sports=sport_range,
                                                tuc_dports=dport_range))

    rate_ccfg = RateClient(rc_open_rate=Rate(),  # no rate limiting
                           rc_close_rate=Rate(), # no rate limiting
                           rc_send_rate=Rate())  # no rate limiting

    delay_ccfg = DelayClient(dc_init_delay=Delay(d_value=0),
                             dc_uptime=Delay(d_value=40),   # clients stay up for 40s
                             dc_downtime=Delay(d_value=10)) # clients reconnect after 10s

    # Prepare the HTTP Client config
    http_ccfg = AppClient(ac_app_proto=HTTP,
                          ac_http=HttpClient(hc_req_method=GET,
                                             hc_req_object_name='/index.html',
                                             hc_req_host_name='www.foobar.net',
                                             hc_req_size=2048)) # configure HTTP requests of size 2K

    # Prepare the Client test case criteria.
    # Let the test case run for one hour.
    ccrit = TestCriteria(tc_crit_type=RUN_TIME, tc_run_time_s=3600)

    # Put the whole test case config together.
    ccfg = TestCase(tc_type=CLIENT, tc_eth_port=0,
                    tc_id=0,
                    tc_client=Client(cl_src_ips=b2b_sips(eth_port=0, ip_count=2),
                                     cl_dst_ips=b2b_dips(eth_port=0, ip_count=1),
                                     cl_l4=l4_ccfg,
                                     cl_rates=rate_ccfg,
                                     cl_delays=delay_ccfg,
                                     cl_app=http_ccfg),
                    tc_criteria=ccrit,
                    tc_async=True)

    # Ask WARP17 to add the test case config
    err = warp17_method_call(warp17_host, warp17_port, Warp17_Stub, 'ConfigureTestCase', ccfg)
    if err.e_code != 0:
        die('Error configuring client test case.')

    print 'Clients configured successfully!\n'

def configure_server_port():
    # Configure 1 server IP interface and no default gateway on port 1.
    # First the default gw
    pcfg = b2b_port_add(eth_port=1, def_gw=Ip(ip_version=IPV4, ip_v4=0))

    intf1 = (Ip(ip_version=IPV4, ip_v4=b2b_ipv4(eth_port=1, intf_idx=0)),
             Ip(ip_version=IPV4, ip_v4=b2b_mask(eth_port=1, intf_idx=0)),
             b2b_count(eth_port=0, intf_idx=0))
    b2b_port_add_intfs(pcfg, [intf1])

    # Ask WARP17 to add them to the config.
    err = warp17_method_call(warp17_host, warp17_port, Warp17_Stub, 'ConfigurePort', pcfg)
    if err.e_code != 0:
        die('Error configuring port 1.')

    # Prepare the L4 Server config (eth_port 1)
    # ports in the range: [101, 200]
    port_range=L4PortRange(l4pr_start=101, l4pr_end=200)
    l4_scfg = L4Server(l4s_proto=TCP,
                       l4s_tcp_udp=TcpUdpServer(tus_ports=port_range))

    # Prepare the HTTP Server config
    http_scfg = AppServer(as_app_proto=HTTP,
                          as_http=HttpServer(hs_resp_code=OK_200,
                                             hs_resp_size=2048))

    # The server test case criteria is to have all servers in listen state.
    # However, server test cases are special and keep running even after the
    # PASS criteria is met
    scrit = TestCriteria(tc_crit_type=SRV_UP, tc_srv_up=100)

    # Put the whole test case config together
    scfg = TestCase(tc_type=SERVER, tc_eth_port=1, tc_id=0,
                    tc_server=Server(srv_ips=b2b_sips(eth_port=1, ip_count=1),
                                     srv_l4=l4_scfg,
                                     srv_app=http_scfg),
                    tc_criteria=scrit,
                    tc_async=False)

    # Ask WARP17 to add the test case config
    err = warp17_method_call(warp17_host, warp17_port, Warp17_Stub, 'ConfigureTestCase', scfg)
    if err.e_code != 0:
        die('Error configuring server test case.')

    print 'Servers configured successfully!\n'

def start_client_port():
    err = warp17_method_call(warp17_host, warp17_port, Warp17_Stub, 'PortStart',
                             PortArg(pa_eth_port=0))
    if err.e_code != 0:
        die('Error starting client test cases.')

    print 'Clients started successfully!\n'

def start_server_port():
    err = warp17_method_call(warp17_host, warp17_port, Warp17_Stub, 'PortStart',
                             PortArg(pa_eth_port=1))
    if err.e_code != 0:
        die('Error starting server test cases.')

    print 'Servers started successfully!\n'

def check_stats():
    # Just check client stats a couple of times (once a second and stop
    # afterwards)..
    for i in range(0, 10):
        client_result = warp17_method_call(warp17_host, warp17_port, Warp17_Stub,
                                           'GetTestStatus',
                                           TestCaseArg(tca_eth_port=0,
                                                       tca_test_case_id=0))
        if client_result.tsr_error.e_code != 0:
            die('Error fetching client test case stats.')

        print 'Client test case state: ' + str(client_result.tsr_state) + '\n'
        print 'Global stats:'
        print client_result.tsr_stats.tcs_client
        print 'Rate stats:'
        print client_result.tsr_rate_stats
        print 'HTTP Client stats:'
        print client_result.tsr_app_stats.tcas_http

        time.sleep(1)

def stop_client_port():
    err = warp17_method_call(warp17_host, warp17_port, Warp17_Stub, 'PortStop',
                             PortArg(pa_eth_port=0))
    if err.e_code != 0:
        die('Error stopping client test cases.')

    print 'Clients stopped successfully!\n'

def stop_server_port():
    err = warp17_method_call(warp17_host, warp17_port, Warp17_Stub, 'PortStop',
                             PortArg(pa_eth_port=1))
    if err.e_code != 0:
        die('Error stopping server test cases.')

    print 'Servers stopped successfully!\n'

def run_test():
    ''' Assumes a back to back topology with two 40G ports. '''
    ''' Port 0 emulates clients and port 1 emulates servers. '''

    warp17_pid = init_test()
    configure_client_port()
    configure_server_port()

    start_server_port()
    start_client_port()

    check_stats()

    stop_client_port()
    stop_server_port()

    # Cleanup: Ask WARP17 to stop
    warp17_stop(env, warp17_pid)

if __name__ == '__main__':
    run_test()

