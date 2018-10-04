#!/usr/bin/env python2

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
#
# INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
# CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
# ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
# POSSIBILITY OF SUCH DAMAGE.
#
# File name:
#    stats_collector.py
#
# Description:
#    Memory stats aggregator.
#
# Author:
#    Matteo Triggiani
#
# Initial Created:
#    10/03/2018
#
# Notes:
#


# ****************************************************************************
# Include files
# ****************************************************************************
import os
import warp17_api

from rpc_impl import *
from functools import partial
from time import sleep
from datetime import datetime
from b2b_setup import *

from warp17_common_pb2 import *
from warp17_l3_pb2 import *
from warp17_app_raw_pb2 import *
from warp17_app_pb2 import *
from warp17_server_pb2 import *
from warp17_client_pb2 import *
from warp17_test_case_pb2 import *
from warp17_service_pb2 import *
from warp17_sockopt_pb2 import *

debug = True
local_dir = os.getcwd()

env = warp17_api.Warp17Env('ut/ini/jspg3.ini')
start_memory = int(env.get_memory())

tcb_pool_sz = 20000
output_file = "/tmp/10m-res-test.txt"
warp17_call = partial(warp17_method_call, env.get_host_name(),
                      env.get_rpc_port(), Warp17_Stub)
precision = 1000


def passed(results):
    """Check if the tests has passed"""
    client_result, server_result = results
    if (server_result.tsr_state != PASSED or
            client_result.tsr_state != PASSED):
        return False
    return True


def collect_info(pivot, R):
    """Binary search the minimum memory needed to run the test"""
    if R < precision:
        return
    print("Running warp17 on {}Mb memory".format(pivot))
    not_started = False
    env.set_value(env.MEMORY, pivot)
    bin = "{}/build/warp17".format(local_dir)

    try:
        proc = warp17_api.warp17_start(env, bin)

    except BaseException as E:
        print(E)
        exit(-1)

    try:
        warp17_api.warp17_wait(env)
        results = config_test()
        warp17_api.warp17_stop(env, proc)

    except:
        not_started = True
        pass

    if not not_started:
        if passed(results):
            message = "Success run with {}Mb memory\n".format(pivot)
            log_res(message)
            collect_info(pivot - R / 2, R / 2)
            return
    message = "Failed run with {}Mb memory\n".format(pivot)
    log_res(message)
    collect_info(pivot + R / 2, R / 2)
    return


def log_res(message):
    """Log the results on a file"""
    log.write(message)


def config_test():
    """Configures a test to run 10 million sessions"""
    # Setup interfaces on port 0
    pcfg = b2b_port_add(0, def_gw=Ip(ip_version=IPV4, ip_v4=167837697))
    b2b_port_add_intfs(pcfg, [(Ip(ip_version=IPV4, ip_v4=b2b_ipv4(0, i)),
                               Ip(ip_version=IPV4, ip_v4=b2b_mask(0, i)),
                               b2b_count(0, i)) for i in range(0, 200)])
    warp17_call('ConfigurePort', pcfg)
    # Setup interfaces on port 1
    pcfg = b2b_port_add(1, def_gw=Ip(ip_version=IPV4, ip_v4=167772161))
    b2b_port_add_intfs(pcfg, [(Ip(ip_version=IPV4, ip_v4=b2b_ipv4(1, i)),
                               Ip(ip_version=IPV4, ip_v4=b2b_mask(1, i)),
                               b2b_count(1, i)) for i in range(0, 1)])
    warp17_call('ConfigurePort', pcfg)
    rate_ccfg = RateClient(rc_open_rate=Rate(),
                           rc_close_rate=Rate(),
                           rc_send_rate=Rate())
    app_ccfg = App(app_proto=RAW_CLIENT,
                   app_raw_client=RawClient(rc_req_plen=10,
                                            rc_resp_plen=10))
    app_scfg = App(app_proto=RAW_SERVER,
                   app_raw_server=RawServer(rs_req_plen=10,
                                            rs_resp_plen=10))

    l4_ccfg = L4Client(l4c_proto=TCP,
                       l4c_tcp_udp=TcpUdpClient(tuc_sports=b2b_ports(50000),
                                                tuc_dports=b2b_ports(1)))

    ccfg = TestCase(tc_type=CLIENT, tc_eth_port=0,
                    tc_id=0,
                    tc_client=Client(cl_src_ips=b2b_sips(0, 200),
                                     cl_dst_ips=b2b_dips(0, 1),
                                     cl_l4=l4_ccfg,
                                     cl_rates=rate_ccfg),
                    tc_app=app_ccfg,
                    tc_criteria=TestCriteria(tc_crit_type=CL_ESTAB,
                                             tc_cl_estab=10000000))
    warp17_call('ConfigureTestCase', ccfg)

    l4_scfg = L4Server(l4s_proto=TCP,
                       l4s_tcp_udp=TcpUdpServer(tus_ports=b2b_ports(1)))
    scfg = TestCase(tc_type=SERVER, tc_eth_port=1, tc_id=0,
                    tc_server=Server(srv_ips=b2b_sips(1, 1),
                                     srv_l4=l4_scfg),
                    tc_app=app_scfg,
                    tc_criteria=TestCriteria(tc_crit_type=SRV_UP,
                                             tc_srv_up=1))
    warp17_call('ConfigureTestCase', scfg)

    warp17_call('PortStart', PortArg(pa_eth_port=1))
    warp17_call('PortStart', PortArg(pa_eth_port=0))
    sleep(2)
    # Check client test to be passed
    while True:
        client_result = warp17_call('GetTestStatus',
                                    TestCaseArg(tca_eth_port=0,
                                                tca_test_case_id=0))

        server_result = warp17_call('GetTestStatus',
                                    TestCaseArg(tca_eth_port=1,
                                                tca_test_case_id=0))

        if (client_result.tsr_state == RUNNING):
            print("Waiting, client status is still {}".format(
                client_result.tsr_state))
            sleep(1)
        else:
            break

    warp17_call('PortStop', PortArg(pa_eth_port=0))
    warp17_call('PortStop', PortArg(pa_eth_port=1))
    return client_result, server_result


log = open(output_file, "w")
log.write("Start binary search {}\n".format(datetime.today()))
collect_info(start_memory / 2, start_memory / 2)
log.write("Finish\n")
log.close()
exit(0)
