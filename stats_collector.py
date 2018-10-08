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
from warp17_api import *
import socket

from rpc_impl import *
from functools import partial
from time import sleep
from datetime import datetime
from b2b_setup import *

from uniq import get_uniq_stamp
from warp17_common_pb2 import *
from warp17_l3_pb2 import *
from warp17_app_raw_pb2 import *
from warp17_app_pb2 import *
from warp17_server_pb2 import *
from warp17_client_pb2 import *
from warp17_test_case_pb2 import *
from warp17_service_pb2 import *
from warp17_sockopt_pb2 import *

local_dir = os.getcwd()

env = Warp17Env('ut/ini/{}.ini'.format(socket.gethostname()))
warp17_call = partial(warp17_method_call, env.get_host_name(),
                      env.get_rpc_port(), Warp17_Stub)
bin = "{}/build/warp17".format(local_dir)


class Test():

    def __init__(self):
        # l3_config = {
        #     port : [
        #         def_gw,
        #         n_ip]
        # }
        self.l3_config = {
            0: [0,
                1],
            1: [0,
                1]
        }
        # self.l4_config = {
        #     port: n_ports
        # }
        self.l4_config = {
            0: 1,
            1: 1,
        }
        self.rate_ccfg = RateClient(rc_open_rate=Rate(),
                                    rc_close_rate=Rate(),
                                    rc_send_rate=Rate())
        self.app_ccfg = App()

        self.l4_ccfg = L4Client()

        self.ccfg = TestCase()
        self.sr_test_criteria = TestCriteria(tc_crit_type=SRV_UP, tc_srv_up=1)

        self.l4_scfg = L4Server()
        self.scfg = TestCase()
        self.cl_port = 0
        self.sr_port = 1
        self.tc_id = 0

    def add_l3(self, port, def_gw, n_ip):
        self.l3_config[port] = [def_gw, n_ip]

    def add_config(self):
        for port in self.l3_config:
            def_gw, n_ip = self.l3_config[port]
            pcfg = b2b_port_add(port, def_gw=Ip(ip_version=IPV4, ip_v4=def_gw))
            b2b_port_add_intfs(pcfg,
                               [(Ip(ip_version=IPV4, ip_v4=b2b_ipv4(port, i)),
                                 Ip(ip_version=IPV4, ip_v4=b2b_mask(port, i)),
                                 b2b_count(port, i)) for i in range(0, n_ip)])
            warp17_call('ConfigurePort', pcfg)

        cl_src_ips = b2b_sips(self.cl_port, self.l3_config[self.cl_port][1])
        cl_dst_ips = b2b_sips(self.sr_port, self.l3_config[self.sr_port][1])

        # todo: enable multiple server/client tests
        self.l4_ccfg = L4Client(l4c_proto=self.proto,
                                l4c_tcp_udp=TcpUdpClient(
                                    tuc_sports=b2b_ports(self.l4_config[0]),
                                    tuc_dports=b2b_ports(self.l4_config[1])))

        uptime_delay = Delay(d_value=120)
        downtime_delay = Delay(d_value=0)
        self.ccfg = TestCase(tc_type=CLIENT, tc_eth_port=self.cl_port,
                             tc_id=self.tc_id,
                             tc_client=Client(cl_src_ips=cl_src_ips,
                                              cl_dst_ips=cl_dst_ips,
                                              cl_l4=self.l4_ccfg,
                                              cl_rates=self.rate_ccfg),
                             tc_app=self.app_ccfg,
                             tc_uptime=uptime_delay,
                             tc_downtime=downtime_delay,
                             tc_criteria=self.cl_test_criteria)
        warp17_call('ConfigureTestCase', self.ccfg)

        self.l4_scfg = L4Server(l4s_proto=self.proto,
                                l4s_tcp_udp=TcpUdpServer(
                                    tus_ports=b2b_ports(1)))
        srv_ips = b2b_sips(self.sr_port, self.l3_config[self.sr_port][1])
        self.scfg = TestCase(tc_type=SERVER, tc_eth_port=self.sr_port,
                             tc_id=self.tc_id,
                             tc_server=Server(srv_ips=srv_ips,
                                              srv_l4=self.l4_scfg),
                             tc_app=self.app_scfg,
                             tc_criteria=self.sr_test_criteria)
        warp17_call('ConfigureTestCase', self.scfg)

    def run(self):
        self.add_config()
        self.start()
        results = self.check_results()
        self.stop()
        return results

    def check_results(self):
        results = {}
        sleep(5)
        for port in self.l3_config:
            results[port] = warp17_call('GetTestStatus',
                                        TestCaseArg(tca_eth_port=port,
                                                    tca_test_case_id=self.tc_id))

        return results

    def stop(self):
        for port in self.l3_config:
            warp17_call('PortStop', PortArg(pa_eth_port=port))

    def start(self):
        for port in self.l3_config:
            warp17_call('PortStart', PortArg(pa_eth_port=port))
        # wait for the test to start running
        sleep(2)

    @staticmethod
    def passed(results):
        """Check if the tests has passed"""
        client_result = results[0]
        server_result = results[1]
        if (server_result.tsr_state != PASSED or
                client_result.tsr_state != PASSED):
            return False
        return True


def search_mimimum_memory(pivot, R):
    """Binary search the minimum memory needed to run the test"""
    if R < precision:
        return
    print("Running warp17 on {}Mb memory".format(pivot))
    not_started = False
    env.set_value(env.MEMORY, pivot)

    try:
        oarg = Warp17OutputArgs(out_file=log_file)
        proc = warp17_start(env=env, exec_file=bin, output_args=oarg)

    except BaseException as E:
        print("Error occurred: {}".format(E))
        exit(-1)

    try:
        warp17_wait(env)

        results = test.run()

    except BaseException as E:
        print("Error occurred: {}".format(E))
        not_started = True
        pass

    warp17_stop(env, proc)

    if not not_started:
        if test.passed(results):
            message = "Success run with {}Mb memory\n".format(pivot)
            result.write(message)
            search_mimimum_memory(pivot - R / 2, R / 2)
            return
    message = "Failed run with {}Mb memory\n".format(pivot)
    result.write(message)
    search_mimimum_memory(pivot + R / 2, R / 2)
    return


def test_10m_sessions():
    """Configures a test to run 10 million sessions"""
    test_10m = Test()
    test_10m.add_l3(0, 167837697, 200)  # 10.1.0.1
    test_10m.add_l3(1, 167772161, 1)  # 10.0.0.1
    test_10m.l4_config[0] = 50000
    test_10m.l4_config[1] = 1  # not really needed
    test_10m.proto = TCP
    test_10m.cl_port = 0
    test_10m.sr_port = 1

    test_10m.cl_test_criteria = TestCriteria(tc_crit_type=CL_ESTAB,
                                             tc_cl_estab=10000000)

    test_10m.app_ccfg = App(app_proto=RAW_CLIENT,
                            app_raw_client=RawClient(rc_req_plen=10,
                                                     rc_resp_plen=10))
    test_10m.app_scfg = App(app_proto=RAW_SERVER,
                            app_raw_server=RawServer(rs_req_plen=10,
                                                     rs_resp_plen=10))

    start_memory = int(env.get_memory())

    env.set_value(env.TCB_POOL_SZ, 20000)
    env.set_value(env.UCB_POOL_SZ, 0)

    out_folder = "/tmp/10m-test-{}/".format(get_uniq_stamp())

    return test_10m, start_memory, out_folder


test, start_memory, out_folder = test_10m_sessions()  # set your test here
res_file = "{}res.txt".format(out_folder)
log_file = "{}out.log".format(out_folder)
if not os.path.exists(out_folder):
    os.mkdir(out_folder)
print "Logs and outputs in " + out_folder
result = open(res_file, "w")
result.write("Start binary search {}\n".format(datetime.today()))
precision = 1000
search_mimimum_memory(start_memory / 2, start_memory / 2)
result.write("Finish\n")
result.close()
