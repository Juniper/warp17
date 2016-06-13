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
#     test_1_http_4M.pl
#
# Description:
#     Example of using the WARP17 perl API.
#
# Author:
#     Dumitru Ceara, Eelco Chaudron
#
# Initial Created:
#     06/13/2016
#
# Notes:
#     Running the script from the top dir:
#     perl -Iperl examples/perl/test_1_http_4M.pl
#
#

use warp17_api;

# Two client IPs
$client_ip_min =  0x0a000001;
$client_ip_max =  0x0a000002;
$client_ip_mask = 0xffff0000;

# One server IP
$server_ip_min =  0x0a000101;
$server_ip_max =  0x0a000101;
$server_ip_mask = 0xffff0000;

sub set_ip_v4 {
    my ($ip, $ip_val) = @_;

    $ip->{ip_version} = 4;
    $ip->{ip_v4} = $ip_val;

    return $ip;
}

sub add_l3_intf {
    my ($pcfg, $ip, $mask) = @_;

    $pcfg->{pc_l3_intfs}->add();
    my $idx = py_len($pcfg->{pc_l3_intfs}) - 1;
    my $intfs = $pcfg->{pc_l3_intfs}->{_values};

    $intfs->[$idx]->{l3i_ip}->{ip_version} = 4;
    $intfs->[$idx]->{l3i_ip}->{ip_v4} = $ip;

    $intfs->[$idx]->{l3i_mask}->{ip_version} = 4;
    $intfs->[$idx]->{l3i_mask}->{ip_v4} = $mask;

    $intfs->[$idx]->{l3i_count} = 1;
}

sub add_l3_gw {
    my ($pcfg, $gw_ip) = @_;

    $pcfg->{pc_def_gw}->{ip_version} = 4;
    $pcfg->{pc_def_gw}->{ip_v4} = $gw_ip;
}

sub create_port {
    my ($args) = @_;

    my $pcfg = new PortCfg;
    $pcfg->{pc_eth_port} = $args->{port};
    add_l3_gw($pcfg, $args->{gw_ip});
    return $pcfg;
}

sub set_ip_v4_range {
    my ($range, $start, $end) = @_;

    $range->{ipr_start}->{ip_version} = warp17_enum_val(IpV, IPV4);
    $range->{ipr_start}->{ip_v4} = $start;

    $range->{ipr_end}->{ip_version} = warp17_enum_val(IpV, IPV4);
    $range->{ipr_end}->{ip_v4} = $end;
}

sub set_l4_port_range {
    my ($range, $start, $end) = @_;

    $range->{l4pr_start} = $start;
    $range->{l4pr_end} = $end;
}

sub init_test {
    # Configure the WARP17 mandatory environment variables. These could also be
    # set outside the script (through normal env variables) but for this example
    # we do it here..

    # Save the hostname and tcp port WARP17 listens on. Also the env.
    our $warp17_host;
    our $warp17_port;
    our $env = new Warp17Env('examples/perl/test_perl_api.ini');

    # First start WARP17 on the local machine, default IP and port.
    my $exec_bin = './build/warp17';
    $warp17_host = $env->get_host_name();
    $warp17_port = $env->get_rpc_port();

    # Ask for the output log to go to /tmp/test_1_api_example.out
    # Returns the process id of WARP17.
    # This will throw an exception if the output file can't be created or if
    # WARP17 can't be started.
    my $warp17_pid = warp17_start($env, $exec_bin,
                                  new Warp17OutputArgs('/tmp/test_1_api_example.out'));

    # Now wait for WARP17 to finish initializing
    # This will exit if WARP17 fails to initialize
    warp17_wait($env);
    return $warp17_pid;
}

sub configure_client_port {
    # Configure 2 client IP interfaces and no default gateway on port 0.
    my $pcfg = create_port({port => 0, gw_ip => 0});

    # Now L3 interfaces
    for (my $ip = $client_ip_min; $ip <= $client_ip_max; $ip++) {
        add_l3_intf($pcfg, $ip, $client_ip_mask);
    }

    # Ask WARP17 to add the port config.
    my $err = warp17_call($warp17_host, $warp17_port, 'ConfigurePort', $pcfg);
    if ($err->{e_code} != 0) {
        die("Error configuring port 0!");
    }

    # Allocate the test case and then intialize everything about it
    my $tc = new TestCase;
    $tc->{tc_type} = warp17_enum_val(TestCaseType, CLIENT);
    $tc->{tc_eth_port} = 0;
    $tc->{tc_id} = 0;

    # Prepare the L4 Client config (eth_port 0)
    # source ports in the range: [10001, 30000]
    # destination ports in the range: [101, 200]
    # In total (per client IP): 2M sessions.
    # In total: 4M sessions

    my $tc_client = $tc->{tc_client};
    set_ip_v4_range($tc_client->{cl_src_ips}, $client_ip_min, $client_ip_max);
    set_ip_v4_range($tc_client->{cl_dst_ips}, $server_ip_min, $server_ip_max);

    my $l4_ccfg = $tc_client->{cl_l4};
    $l4_ccfg->{l4c_proto} = warp17_enum_val(L4Proto, TCP);
    set_l4_port_range($l4_ccfg->{l4c_tcp_udp}->{tuc_sports}, 10001, 30000);
    set_l4_port_range($l4_ccfg->{l4c_tcp_udp}->{tuc_dports}, 101, 200);

    # No rate limiting
    my $rate_ccfg = $tc_client->{cl_rates};
    $rate_ccfg->{rc_open_rate}->{r_value} = warp17_infinite();  # no rate limiting
    $rate_ccfg->{rc_close_rate}->{r_value} = warp17_infinite(); # no rate limiting
    $rate_ccfg->{rc_send_rate}->{r_value} = warp17_infinite();  # no rate limiting

    # Configure some timeouts in the client profile
    my $delay_ccfg = $tc_client->{cl_delays};
    $delay_ccfg->{dc_init_delay}->{d_value} = 0;
    $delay_ccfg->{dc_uptime}->{d_value} = 40;    # clients stay up for 40s
    $delay_ccfg->{dc_downtime}->{d_value} = 10;  # clients reconnect after 10s

    # Prepare the HTTP Client config
    my $http_ccfg = $tc_client->{cl_app};
    $http_ccfg->{ac_app_proto} = warp17_enum_val(AppProto, HTTP);
    $http_ccfg->{ac_http}->{hc_req_method} = warp17_enum_val(HttpMethod, GET);
    $http_ccfg->{ac_http}->{hc_req_object_name} = '/index.html';
    $http_ccfg->{ac_http}->{hc_req_host_name} = 'www.foobar.net';
    $http_ccfg->{ac_http}->{hc_req_size} = 2048;

    # Prepare the Client test case criteria.
    # Let the test case run for one hour.
    my $ccrit = $tc->{tc_criteria};
    $ccrit->{tc_crit_type} = warp17_enum_val(TestCritType, RUN_TIME);
    $ccrit->{tc_run_time_s} = 3600;

    $tc->{tc_async} = 1;

    # Ask WARP17 to add the test case config
    my $err = warp17_call($warp17_host, $warp17_port, 'ConfigureTestCase', $tc);
    if ($err->{e_code} != 0) {
        die("Error configuring client test case!");
    }

    print "Clients configured successfully!\n";
}

sub configure_server_port {
    # Configure 1 server IP interface and no default gateway on port 1.
    my $pcfg = create_port({port => 1, gw_ip => 0});

    # Now L3 interfaces
    for (my $ip = $server_ip_min; $ip <= $server_ip_max; $ip++) {
        add_l3_intf($pcfg, $ip, $server_ip_mask);
    }

    # Ask WARP17 to add the port config.
    my $err = warp17_call($warp17_host, $warp17_port, 'ConfigurePort', $pcfg);
    if ($err->{e_code} != 0) {
        die("Error configuring port 0!");
    }

    # Allocate the test case and then intialize everything about it
    my $tc = new TestCase;
    $tc->{tc_type} = warp17_enum_val(TestCaseType, SERVER);
    $tc->{tc_eth_port} = 1;
    $tc->{tc_id} = 0;

    # Prepare the L4 Server config (eth_port 1)
    # ports in the range: [101, 200]

    my $tc_srver = $tc->{tc_server};
    set_ip_v4_range($tc_srver->{srv_ips}, $server_ip_min, $server_ip_max);

    my $l4_scfg = $tc_srver->{srv_l4};
    $l4_scfg->{l4s_proto} = warp17_enum_val(L4Proto, TCP);
    set_l4_port_range($l4_scfg->{l4s_tcp_udp}->{tus_ports}, 101, 200);

    # Prepare the HTTP Server config
    my $http_scfg = $tc_srver->{srv_app};
    $http_scfg->{as_app_proto} = warp17_enum_val(AppProto, HTTP);
    $http_scfg->{as_http}->{hs_resp_code} = warp17_enum_val(HttpStatusCode, OK_200);
    $http_scfg->{as_http}->{hs_resp_size} = 2048;

    # Prepare the Server test case criteria.
    # The server test case criteria is to have all servers in listen state.
    # However, server test cases are special and keep running even after the
    # PASS criteria is met
    my $scrit = $tc->{tc_criteria};
    $scrit->{tc_crit_type} = warp17_enum_val(TestCritType, SRV_UP);
    $scrit->{tc_srv_up} = 100;

    $tc->{tc_async} = 0;

    # Ask WARP17 to add the test case config
    my $err = warp17_call($warp17_host, $warp17_port, 'ConfigureTestCase', $tc);
    if ($err->{e_code} != 0) {
        die("Error configuring client test case!");
    }

    print "Servers configured successfully!\n";
}

sub start_port {
    my ($port, $type_str) = @_;

    my $port_arg = new PortArg;
    $port_arg->{pa_eth_port} = $port;

    my $err = warp17_call($warp17_host, $warp17_port, 'PortStart', $port_arg);
    if ($err.e_code != 0) {
        die('Error starting $type_str test cases.');
    }

    print "$type_str port started successfully!\n"
}

sub stop_port {
    my ($port, $type_str) = @_;

    my $port_arg = new PortArg;
    $port_arg->{pa_eth_port} = $port;

    my $err = warp17_call($warp17_host, $warp17_port, 'PortStop', $port_arg);
    if ($err.e_code != 0) {
        die('Error stopping $type_str test cases.');
    }

    print "$type_str port stopped successfully!\n"
}

sub check_stats {
    # Just check client stats a couple of times (once a second and stop
    # afterwards)..

    my $tc_arg = new TestCaseArg;
    $tc_arg->{tca_eth_port} = 0;
    $tc_arg->{tca_test_case_id} = 0;

    for (my $i = 0; $i < 10; $i++) {
        my $cl_result = warp17_call($warp17_host, $warp17_port, 'GetTestStatus',
                                    $tc_arg);
        if ($cl_result->{tsr_error}->{e_code} != 0) {
            die('Error fetching client test case stats.');
        }

        print "Client test case state: $cl_result->{tsr_state}\n";
        print "Global stats:\n";
        print "$cl_result->{tsr_stats}->{tcs_client}\n";
        print "Rate stats:\n";
        print "$cl_result->{tsr_rate_stats}\n";
        print "HTTP Client stats:\n";
        print "$cl_result->{tsr_app_stats}->{tcas_http}\n";

        sleep(1);
    }
}

my $warp17_pid = init_test();
configure_client_port();
configure_server_port();

# Start servers first
start_port(1, "SERVER");
# Start clients too
start_port(0, "CLIENT");

# Check for stats for a while
check_stats();

# Stop both ports
stop_port(0, "CLIENT");
stop_port(1, "SERVER");

# Cleanup: Ask WARP17 to stop
warp17_stop($env, $warp17_pid);

