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
#     test_multi_runner.py
#
# Description:
#     Config runner and data aggregator.
#
# Author:
#     Matteo Triggiani, Dumitru Ceara
#
# Initial Created:
#     04/27/2018
#
# Notes:
#
#

import sys
import argparse
import csv
import collections

from warp17_api           import *
from warp17_common_pb2    import *
from warp17_service_pb2   import *
from warp17_test_case_pb2 import *

class Warp17Exeception(BaseException):
    def __init__(self, msg='', errno=1):
        sys.stderr.write('Error {}: {}'.format(errno, msg))
        sys.stderr.write('Should cleanup but we just exit..\n')

TestStats = collections.namedtuple('TestStats',
                                   'tx_rate, rx_rate, ' +
                                   'sess_up, sess_estab, sess_down, sess_failed, ' +
                                   'rate_estab, rate_closed, rate_send, ' +
                                   'lat_min, lat_max, lat_avg, lat_avg_jitter, ' +
                                   'lat_max_exc, lat_max_avg_exc')

def kill(msg, errno=1):
    raise Warp17Exeception(msg, errno)

def init_csv(csv_file):
    return csv.writer(csv_file, delimiter=',', quoting=csv.QUOTE_MINIMAL)

def write_cmd_file_hdr(csv_writer, cmd_file):
    csv_writer.writerow(['{}:'.format(cmd_file)])

def write_test_case_hdr(csv_writer):
    csv_writer.writerow(['',     '',           'Link Stats', '',        'Global Stats', '',               '',          '',            'Latency Stats',       '',                  '',                    '',                         '',                     '',                     'Rate stats'])
    csv_writer.writerow(['Port', 'TestCaseId', 'Tx Rate',    'Rx Rate', 'Sess Up',      'Sess Estab',     'Sess Down', 'Sess Failed', 'Latency min (usec)', 'Latency Max (usec)', 'Latency Avg (usec)', 'Latency Jitter Avg (usec)', 'Latency Max Exceeded', 'Latency Avg Exceeded', 'Sess Estab/sec', 'Sess Closed/sec', 'Sess Send/sec'])

def write_new_line(csv_writer):
    csv_writer.writerow([''])

def write_test_case_stats(csv_writer, port, tcid, stat):
    csv_writer.writerow([port, tcid,
                         stat.tx_rate, stat.rx_rate,

                         stat.sess_up, stat.sess_estab,
                         stat.sess_down, stat.sess_failed,

                         stat.lat_min, stat.lat_max,
                         stat.lat_avg, stat.lat_avg_jitter,
                         stat.lat_max_exc, stat.lat_max_avg_exc,

                         stat.rate_estab,
                         stat.rate_closed,
                         stat.rate_send])

def init_tests(cfg_file):
    # Configure the WARP17 mandatory environment variables. These could also be
    # set outside the script (through normal env variables) but for this example
    # we do it here..

    return Warp17Env(path=cfg_file)

def start_test(env, exec_file, log_prefix, cmd_file):
    print('Starting test case: {}'.format(cmd_file))

    log_name = os.path.splitext(os.path.basename(cmd_file))[0]
    log_file = '{}/warp17-{}-log.out'.format(log_prefix, log_name)

    warp17_pid = warp17_start(env=env, exec_file=exec_file,
                              optional_args=(['--cmd-file', cmd_file]),
                              output_args=Warp17OutputArgs(out_file=log_file))

    warp17_wait(env)
    return warp17_pid

def stop_test(env, warp17_pid):
    # Ask WARP17 to stop but force it if it refuses..
    warp17_stop(env, warp17_pid, force=True)

def build_test_case_map(env, port_cnt):
    # Try to guess which test cases are configured.

    test_cases = {}

    for port in range(0, port_cnt):
        test_cases[port] = []

        for tcid in range(0, TPG_TEST_MAX_ENTRIES):
            result = warp17_method_call(env.get_host_name(), env.get_rpc_port(),
                                        Warp17_Stub,
                                        'GetTestCase',
                                        TestCaseArg(tca_eth_port=port,
                                                    tca_test_case_id=tcid))
            if result.tcr_error.e_code == 0:
                test_cases[port].append(tcid)

    return test_cases

def build_test_case_stats(tc_stats, port_stats):
    phystats =  port_stats.sr_phy_rate
    gstats   = tc_stats.tsr_stats
    rstats   = tc_stats.tsr_rate_stats
    lstats   = tc_stats.tsr_stats.gs_latency_stats.gls_stats

    if lstats.ls_samples_count != 0:
        jitter = lstats.ls_sum_jitter / lstats.ls_samples_count
        latency = lstats.ls_sum_latency / lstats.ls_samples_count
    else:
        jitter = 0
        latency = 0

    link_speed_bytes = float(phystats.pys_link_speed) * 1000 * 1000 / 8

    tx_usage = min(float(phystats.pys_tx_bytes) * 100 / link_speed_bytes, 100.0)
    rx_usage = min(float(phystats.pys_rx_bytes) * 100 / link_speed_bytes, 100.0)


    return TestStats(tx_rate=tx_usage, rx_rate=rx_usage,
                     sess_up=gstats.gs_up, sess_estab=gstats.gs_estab,
                     sess_down=gstats.gs_down, sess_failed=gstats.gs_failed,

                     rate_estab=rstats.rs_estab_per_s,
                     rate_closed=rstats.rs_closed_per_s,
                     rate_send=rstats.rs_data_per_s,

                     lat_min=lstats.ls_min_latency,
                     lat_max=lstats.ls_max_latency,
                     lat_avg=latency,
                     lat_avg_jitter=jitter,
                     lat_max_exc=lstats.ls_max_exceeded,
                     lat_max_avg_exc=lstats.ls_max_average_exceeded)

def poll_stats(env, duration):

    port_cnt = len(env.get_ports())
    test_cases = build_test_case_map(env, port_cnt)
    data = {}

    # Data
    for i in range(0, duration):
        time.sleep(1)

        print('Iteration: {}: Collecting stastistics'.format(i))

        for (port, tcs) in test_cases.items():
            for tcid in tcs:
                tc_stats = warp17_method_call(env.get_host_name(), env.get_rpc_port(),
                                              Warp17_Stub,
                                              'GetTestStatus',
                                              TestCaseArg(tca_eth_port=port,
                                                          tca_test_case_id=tcid))

                port_stats = warp17_method_call(env.get_host_name(), env.get_rpc_port(),
                                                Warp17_Stub,
                                                'GetStatistics',
                                                PortArg(pa_eth_port=port))

                tc_data = data.get((port, tcid), [])
                tc_data.append(build_test_case_stats(tc_stats, port_stats))

                data[(port, tcid)] = tc_data

    return data

def run_tests():
    parser = argparse.ArgumentParser()

    parser.add_argument('-w', '--warp17', help='path to warp17 binary',
                        required=True)
    parser.add_argument('-o', '--output', help='optput csv directory',
                        required=True)
    parser.add_argument('-c', '--config', help='config file', required=True)
    parser.add_argument('-t', '--cmd-file', nargs='+', help='test config file',
                        required=True)
    parser.add_argument('-d', '--test-duration', help='individual test duration',
                        type=int, default=60)
    parser.add_argument('-l', '--log-file-prefix', help='log files directory (default: /tmp)',
                        default='/tmp')
    args = parser.parse_args()

    env = init_tests(args.config)

    for cmd_file in args.cmd_file:

        # Start Warp17
        warp17_pid = start_test(env, args.warp17, args.log_file_prefix,
                                cmd_file)

        # Poll for stats
        data = poll_stats(env, args.test_duration)

        # Stop Warp17
        stop_test(env, warp17_pid)

        csv_name = os.path.splitext(os.path.basename(cmd_file))[0]
        csv_file = '{}/warp17-{}-results.csv'.format(args.output, csv_name)

        with open(csv_file, 'w') as csv_file:

            csv_writer = init_csv(csv_file)

            # Write stats data
            write_cmd_file_hdr(csv_writer, cmd_file)
            for ((port, tcid), stats) in sorted(data.items()):
                write_test_case_hdr(csv_writer)
                for stat in stats:
                    write_test_case_stats(csv_writer, port, tcid, stat)
                write_new_line(csv_writer)
            write_new_line(csv_writer)


if __name__ == '__main__':
    run_tests()

