#
# File name:
#     rpc_client.py
#
# Description:
#     Script which ises rpc method to configure and start/stop warp
#

import sys
import time

from warp17_api import *
from warp17_ut import Warp17UnitTestCase
from warp17_ut import Warp17TrafficTestCase

from b2b_setup import *
import netaddr
from warp17_common_pb2    import *
from warp17_l3_pb2        import *
from warp17_server_pb2    import *
from warp17_app_pb2       import *
from warp17_client_pb2    import *
from warp17_app_raw_pb2   import *
from warp17_app_http_pb2  import *
from warp17_test_case_pb2 import *
from warp17_service_pb2   import *
from warp17_sockopt_pb2   import *


def die(msg):
    sys.stderr.write(msg + ' Should cleanup but we just exit..\n')
    sys.exit(1)


def init_test():
    global warp17_host
    global warp17_port
    global env
    global args
    global filename

    filename = sys.argv[0]
    args = dict([arg.split('=') for arg in sys.argv[1:]])
    print filename
    print args
    env = Warp17Env(path='./rpc_client.ini')

    exec_bin = '../../build/warp17'
    warp17_host = env.get_host_name()
    warp17_port = env.get_rpc_port()

    warp17_pid = warp17_start(env=env, exec_file=exec_bin,
                              output_args=Warp17OutputArgs(
                                  out_file='/tmp/rpc_client_api_example.out'))

    warp17_wait(env)
    return warp17_pid


def configure_testcase_port0():
    ip_count = 1

    err = warp17_method_call(warp17_host, warp17_port, Warp17_Stub, 'PortStop',
                             PortArg(pa_eth_port=int(args['port'])))
    if err.e_code != 0:
        die('Error stopping port' + args['port'] + ' test cases.')

    sport_range = L4PortRange(l4pr_start=int(args['spr_start']),
                              l4pr_end=int(args['spr_end']))
    dport_range = L4PortRange(l4pr_start=int(args['dpr_start']),
                              l4pr_end=int(args['dpr_end']))

    if args['proto'] == 'TCP':
        l4_ccfg = L4Client(l4c_proto=TCP,
                           l4c_tcp_udp=TcpUdpClient(tuc_sports=sport_range,
                                                    tuc_dports=dport_range))
    if args['proto'] == 'UDP':
        l4_ccfg = L4Client(l4c_proto=UDP,
                           l4c_tcp_udp=TcpUdpClient(tuc_sports=sport_range,
                                                    tuc_dports=dport_range))

    rate_ccfg = RateClient(rc_open_rate=Rate(r_value=int(args['open_rate'])),
                           # no rate limiting
                           rc_close_rate=Rate(r_value=int(args['close_rate'])),
                           rc_send_rate=Rate(r_value=int(
                               args['send_rate'])))  # no rate limiting

    init_delay = Delay(d_value=int(args['init_delay']))  # No initial delay
    uptime_delay = Delay(d_value=int(args['uptime']))  # Infinite uptime
    downtime_delay = Delay(d_value=int(args['downtime']))

    if args['app_proto'] == 'HTTP':
        app_ccfg = App(app_proto=HTTP_CLIENT,
                       app_http_client=HttpClient(hc_req_method=GET,
                                                  hc_req_object_name=args[
                                                      'object'],
                                                  hc_req_host_name=args['host'],
                                                  hc_req_size=int(args['size'])))  # configure HTTP requests of size 2K
    else:
        app_ccfg = App(app_proto=RAW_CLIENT,
                       app_raw_client=RawClient(
                           rc_req_plen=int(args['req_size']),
                           rc_resp_plen=int(args['resp_size'])))
    # Prepare the Client test case criteria.
    # Let the test case run for one hour.
    ccrit = TestCriteria(tc_crit_type=RUN_TIME,
                         tc_run_time_s=int(args['tc_run_time']))
    b2b_sips = IpRange(ipr_start=Ip(ip_version=IPV4, ip_v4=int(
        netaddr.IPAddress(args['source_ip_start']))),
                       ipr_end=Ip(ip_version=IPV4, ip_v4=int(netaddr.IPAddress(
                           args['source_ip_end'])) + ip_count - 1))
    b2b_dips = IpRange(ipr_start=Ip(ip_version=IPV4, ip_v4=int(
        netaddr.IPAddress(args['dest_ip_start']))),
                       ipr_end=Ip(ip_version=IPV4, ip_v4=int(netaddr.IPAddress(
                           args['dest_ip_end'])) + ip_count - 1))

    if args['multi'] == 'None':
        multi = False
    else:
        multi = args['multi']

    ccfg = TestCase(tc_type=CLIENT, tc_eth_port=int(args['port']),
                    tc_id=int(args['tc_id']),
                    tc_client=Client(cl_src_ips=b2b_sips,
                                     cl_dst_ips=b2b_dips,
                                     cl_l4=l4_ccfg,
                                     cl_rates=rate_ccfg,
                                     cl_mcast_src=bool(multi)),
                    tc_init_delay=init_delay,
                    tc_uptime=uptime_delay,
                    tc_downtime=downtime_delay,
                    tc_app=app_ccfg,
                    tc_criteria=ccrit,
                    tc_async=bool(args['async']))

    err = warp17_method_call(warp17_host, warp17_port, Warp17_Stub,
                             'ConfigureTestCase', ccfg)
    if err.e_code != 0:
        die('Error configuring client test case.')

    print 'Clients configured successfully!\n'


def delete_testcase():
    err = warp17_method_call(warp17_host, warp17_port, Warp17_Stub,
                             'DelTestCase',
                             TestCaseArg(tca_eth_port=int(args['port']),
                                         tca_test_case_id=int(args['tc_id'])))
    if err.e_code != 0:
        die('Error Deleting test case.')


def configure_l3_port():
    # First the default gw
    pcfg = b2b_port_add(eth_port=int(args['port']),
                        def_gw=Ip(ip_version=IPV4,
                                  ip_v4=int(netaddr.IPAddress(args['gateway']))))

    if args['vlan'] == 'None':
        vlan_enable = False
    else:
        vlan_enable = True
        vlan_id = args['vlan_id']

    i = 0
    ip_range = iprange(args['ip_start'], args['ip_end'])

    if not vlan_enable:
        for ip in ip_range:
            intf1 = (Ip(ip_version=IPV4, ip_v4=int(netaddr.IPAddress(ip))),
                     Ip(ip_version=IPV4,
                        ip_v4=int(netaddr.IPAddress(args['mask']))),
                     b2b_count(eth_port=0, intf_idx=i))
            b2b_port_add_intfs(pcfg, [intf1])
            i += 1
    else:
        for ip in ip_range:
            intf1 = (Ip(ip_version=IPV4, ip_v4=int(netaddr.IPAddress(ip))),
                     Ip(ip_version=IPV4,
                        ip_v4=int(netaddr.IPAddress(args['mask']))),
                     b2b_count(eth_port=0, intf_idx=i),
                     int(vlan_id) + i,
                     Ip(ip_version=IPV4,
                        ip_v4=int(netaddr.IPAddress(args['gateway']))))
            b2b_port_add_intfs(pcfg, [intf1], vlan_enable)
            i += 1

    # Ask WARP17 to add them to the config.
    err = warp17_method_call(warp17_host, warp17_port, Warp17_Stub,
                             'ConfigurePort', pcfg)
    if err.e_code != 0:
        die('Error configuring port 1.')


def configure_l2_port():
    port_options = PortOptions(po_mtu=int(args['mtu']))
    err = warp17_method_call(warp17_host, warp17_port, Warp17_Stub,
                             'SetPortOptions', PortOptionsArg(
            poa_port=PortArg(pa_eth_port=int(args['port'])),
            poa_opts=port_options))
    if err.e_code != 0:
        die('Error configuring mtu on port')
    else:
        return True


def configure_tcp_options():
    tc_arg = TestCaseArg(tca_eth_port=int(args['port']),
                         tca_test_case_id=int(args['tc_id']))

    if args['tcp_option'] == 'to_syn_ack_retry_cnt':
        err = warp17_method_call(warp17_host, warp17_port, Warp17_Stub,
                                 'SetTcpSockopt',
                                 TcpSockoptArg(toa_tc_arg=tc_arg,
                                               toa_opts=TcpSockopt(
                                                   to_syn_ack_retry_cnt=int(args['tcp_option_value']))))
        if err.e_code != 0:
            die('Error configuring tcp options on port')
    if args['tcp_option'] == 'to_win_size':
        err = warp17_method_call(warp17_host, warp17_port, Warp17_Stub,
                                 'SetTcpSockopt',
                                 TcpSockoptArg(toa_tc_arg=tc_arg,
                                               toa_opts=TcpSockopt(
                                                   to_win_size=int(args['tcp_option_value']))))
        if err.e_code != 0:
            die('Error configuring tcp options on port')
    if args['tcp_option'] == 'to_data_retry_cnt':
        err = warp17_method_call(warp17_host, warp17_port, Warp17_Stub,
                                 'SetTcpSockopt',
                                 TcpSockoptArg(toa_tc_arg=tc_arg,
                                               toa_opts=TcpSockopt(
                                                   to_data_retry_cnt=int(args['tcp_option_value']))))
        if err.e_code != 0:
            die('Error configuring tcp options on port')
    if args['tcp_option'] == 'to_retry_cnt':
        err = warp17_method_call(warp17_host, warp17_port, Warp17_Stub,
                                 'SetTcpSockopt',
                                 TcpSockoptArg(toa_tc_arg=tc_arg,
                                               toa_opts=TcpSockopt(
                                                   to_retry_cnt=int(args['tcp_option_value']))))
        if err.e_code != 0:
            die('Error configuring tcp options on port')
    if args['tcp_option'] == 'to_rto':
        err = warp17_method_call(warp17_host, warp17_port, Warp17_Stub,
                                 'SetTcpSockopt',
                                 TcpSockoptArg(toa_tc_arg=tc_arg,
                                               toa_opts=TcpSockopt(to_rto=int(
                                                   args['tcp_option_value']))))
        if err.e_code != 0:
            die('Error configuring tcp options on port')
    if args['tcp_option'] == 'to_fin_to':
        err = warp17_method_call(warp17_host, warp17_port, Warp17_Stub,
                                 'SetTcpSockopt',
                                 TcpSockoptArg(toa_tc_arg=tc_arg,
                                               toa_opts=TcpSockopt(
                                                   to_fin_to=int(args['tcp_option_value']))))
        if err.e_code != 0:
            die('Error configuring tcp options on port')
    if args['tcp_option'] == 'to_twait_to':
        err = warp17_method_call(warp17_host, warp17_port, Warp17_Stub,
                                 'SetTcpSockopt',
                                 TcpSockoptArg(toa_tc_arg=tc_arg,
                                               toa_opts=TcpSockopt(
                                                   to_twait_to=int(args['tcp_option_value']))))
        if err.e_code != 0:
            die('Error configuring tcp options on port')
    if args['tcp_option'] == 'to_orphan_to':
        err = warp17_method_call(warp17_host, warp17_port, Warp17_Stub,
                                 'SetTcpSockopt',
                                 TcpSockoptArg(toa_tc_arg=tc_arg,
                                               toa_opts=TcpSockopt(
                                                   to_orphan_to=int(args['tcp_option_value']))))
        if err.e_code != 0:
            die('Error configuring tcp options on port')


def dscp_ecn_to_tos(dscp_val, ecn_val):
    return ((dscp_val << 2) | ecn_val)


def ipv4_options():
    err = warp17_method_call(warp17_host, warp17_port, Warp17_Stub, 'PortStop',
                             PortArg(pa_eth_port=int(args['port'])))
    if err.e_code == 0 or err.e_code == -2:
        dscp_values = {
            'af11': 0x0A,
            'af12': 0x0C,
            'af13': 0x0E,
            'af21': 0x12,
            'af22': 0x14,
            'af23': 0x16,
            'af31': 0x1A,
            'af32': 0x1C,
            'af33': 0x1E,
            'af41': 0x22,
            'af42': 0x24,
            'af43': 0x26,
            'be': 0x00,
            'cs1': 0x08,
            'cs2': 0x10,
            'cs3': 0x18,
            'cs4': 0x20,
            'cs5': 0x28,
            'cs6': 0x30,
            'cs7': 0x38,
            'ef': 0x2E
        }

        ecn_values = {
            'Non-ECT': 0x0,
            'ECT0': 0x2,
            'ECT1': 0x1,
            'CE': 0x3
        }

        if 'tos' in args:
            tos = int(args['tos'])
        else:
            dscp = dscp_values[args['dscp']]
            ecn = ecn_values[args['ecn']]
            tos = dscp_ecn_to_tos(dscp, ecn)

        tc_arg = TestCaseArg(tca_eth_port=int(args['port']),
                             tca_test_case_id=int(args['tc_id']))
        err = warp17_method_call(warp17_host, warp17_port, Warp17_Stub,
                                 'SetIpv4Sockopt',
                                 Ipv4SockoptArg(i4sa_tc_arg=tc_arg,
                                                i4sa_opts=Ipv4Sockopt(
                                                    ip4so_tos=tos)))
        if err.e_code != 0:
            die('Error configuring ipv4 options on port')


def vlan_options():
    err = warp17_method_call(warp17_host, warp17_port, Warp17_Stub, 'PortStop',
                             PortArg(pa_eth_port=int(args['port'])))
    if err.e_code == 0 or err.e_code == -2:
        vlan_opts = VlanSockopt(vlanso_id=int(args['vlan']),
                                vlanso_pri=int(args['vlan_pri']))
        tc_arg = TestCaseArg(tca_eth_port=int(args['port']),
                             tca_test_case_id=int(args['tc_id']))
        err = warp17_method_call(warp17_host, warp17_port, Warp17_Stub,
                                 'SetVlanSockopt',
                                 VlanSockoptArg(vosa_tc_arg=tc_arg,
                                                vosa_opts=vlan_opts))
        if err.e_code != 0:
            die('Error configuring vlan options on port')


def configure_testcase_port1():
    port_range = L4PortRange(l4pr_start=int(args['dpr_start']),
                             l4pr_end=int(args['dpr_end']))
    if args['proto'] == 'TCP':
        l4_scfg = L4Server(l4s_proto=TCP,
                           l4s_tcp_udp=TcpUdpServer(tus_ports=port_range))
    if args['proto'] == 'UDP':
        l4_scfg = L4Server(l4s_proto=UDP,
                           l4s_tcp_udp=TcpUdpServer(tus_ports=port_range))
    ip_count = 1

    if args['app_proto'] == 'HTTP':
        app_scfg = App(app_proto=HTTP_SERVER,
                       app_http_server=HttpServer(
                           hs_resp_code=int(args['resp_code']),
                           hs_resp_size=int(args['resp_size'])))
    else:
        app_scfg = App(app_proto=RAW_SERVER,
                       app_raw_server=RawServer(
                           rs_req_plen=int(args['req_size']),
                           rs_resp_plen=int(args['resp_size'])))

    scrit = TestCriteria(tc_crit_type=SRV_UP,
                         tc_srv_up=int(args['tc_run_time']),
                         tc_run_time_s=int(args['tc_run_time']))
    b2b_dips = IpRange(ipr_start=Ip(ip_version=IPV4, ip_v4=int(
        netaddr.IPAddress(args['dest_ip_start']))),
                       ipr_end=Ip(ip_version=IPV4, ip_v4=int(netaddr.IPAddress(
                           args['dest_ip_end'])) + ip_count - 1))
    # Put the whole test case config together
    scfg = TestCase(tc_type=SERVER, tc_eth_port=int(args['port']),
                    tc_id=int(args['tc_id']),
                    tc_server=Server(srv_ips=b2b_dips,
                                     srv_l4=l4_scfg),
                    tc_app=app_scfg,
                    tc_criteria=scrit,
                    tc_async=bool(args['async']))
    print(scfg)
    # Ask WARP17 to add the test case config
    err = warp17_method_call(warp17_host, warp17_port, Warp17_Stub,
                             'ConfigureTestCase', scfg)
    if err.e_code != 0:
        die('Error configuring server test case.')

    print 'Servers configured successfully!\n'


def start_port():
    err = warp17_method_call(warp17_host, warp17_port, Warp17_Stub, 'PortStart',
                             PortArg(pa_eth_port=int(args['port'])))
    if err.e_code != 0:
        die('Error starting port' + args['port'] + ' test cases.')

    print 'Traffic started successfully!\n'


def imix_config():
    if 'size' in args:
        size = list(args['size'].split(' '))
    if 'wieght' in args:
        wieght = list(args['wieght'].split(' '))
    if 'app_proto' in args:
        app_proto = list(args['app_proto'].split(' '))

    if 'multi' not in args:
        multi = False
    else:
        multi = args['multi']
    print("size list is " + str(size) + "\n")
    print("wieght list is " + str(wieght) + "\n")
    print("app_proto list is " + str(app_proto) + "\n")

    client_apps, server_apps = [], []
    tc_arg = TestCaseArg(tca_eth_port=int(args['port']),
                         tca_test_case_id=int(args['tc_id']))
    ip_count = 1
    err = warp17_method_call(warp17_host, warp17_port, Warp17_Stub, 'PortStop',
                             PortArg(pa_eth_port=int(args['port'])))
    if err.e_code != 0:
        die('Error stopping port operation.')

    if args.get('imix_type') == 'client':
        sport_range = L4PortRange(l4pr_start=int(args['spr_start']),
                                  l4pr_end=int(args['spr_end']))
        dport_range = L4PortRange(l4pr_start=int(args['dpr_start']),
                                  l4pr_end=int(args['dpr_end']))
        if args['proto'] == 'TCP':
            l4_ccfg = L4Client(l4c_proto=TCP,
                               l4c_tcp_udp=TcpUdpClient(tuc_sports=sport_range,
                                                        tuc_dports=dport_range))
        if args['proto'] == 'UDP':
            l4_ccfg = L4Client(l4c_proto=UDP,
                               l4c_tcp_udp=TcpUdpClient(tuc_sports=sport_range,
                                                        tuc_dports=dport_range))
        rate_ccfg = RateClient(
            rc_open_rate=Rate(r_value=int(args['open_rate'])),
            # no rate limiting
            rc_close_rate=Rate(r_value=int(args['close_rate'])),
            # no rate limiting
            rc_send_rate=Rate(r_value=int(args['send_rate'])))
        ccrit = TestCriteria(tc_crit_type=RUN_TIME,
                             tc_run_time_s=int(args['tc_run_time']))
        b2b_sips = IpRange(ipr_start=Ip(ip_version=IPV4, ip_v4=int(
            netaddr.IPAddress(args['source_ip_start']))),
                           ipr_end=Ip(ip_version=IPV4, ip_v4=int(
                               netaddr.IPAddress(
                                   args['source_ip_end'])) + ip_count - 1))
        b2b_dips = IpRange(ipr_start=Ip(ip_version=IPV4, ip_v4=int(
            netaddr.IPAddress(args['dest_ip_start']))),
                           ipr_end=Ip(ip_version=IPV4, ip_v4=int(
                               netaddr.IPAddress(
                                   args['dest_ip_end'])) + ip_count - 1))
        for i in range(0, len(size)):
            if app_proto[i] == 'HTTP':
                client_apps.append(ImixApp(ia_weight=int(wieght[i]),
                                           ia_app=build_http_app_client(
                                               int(size[i]))))
            else:
                client_apps.append(ImixApp(ia_weight=int(wieght[i]),
                                           ia_app=build_raw_app_client(
                                               int(size[i]))))
        _client_imix_group = build_imix_group(int(args['imix_id']), client_apps)
        err = warp17_method_call(warp17_host, warp17_port, Warp17_Stub,
                                 'ConfigureImixGroup', _client_imix_group)
        if err.e_code != 0:
            die('Error configuring imix group')
        tcarg = TestCase(tc_type=CLIENT, tc_eth_port=int(args['port']),
                         tc_id=int(args['tc_id']),
                         tc_client=Client(cl_src_ips=b2b_sips,
                                          cl_dst_ips=b2b_dips,
                                          cl_l4=l4_ccfg,
                                          cl_rates=rate_ccfg,
                                          cl_mcast_src=bool(multi)),
                         tc_app=App(app_proto=IMIX, app_imix=Imix(
                             imix_id=_client_imix_group.imix_id)),
                         tc_criteria=ccrit,
                         tc_async=bool(args['async']))
        err = warp17_method_call(warp17_host, warp17_port, Warp17_Stub,
                                 'ConfigureTestCase', tcarg)
        if err.e_code != 0:
            die('Error adding imix testcase')

        update_arg = UpdateAppArg(uaa_tc_arg=tc_arg, uaa_app=App(app_proto=IMIX,
                                                                 app_imix=Imix(
                                                                     imix_id=_client_imix_group.imix_id)))
        err = warp17_method_call(warp17_host, warp17_port, Warp17_Stub,
                                 'UpdateTestCaseApp', update_arg)
        if err.e_code != 0:
            die('Error starting imix on port')
    else:
        scrit = TestCriteria(tc_crit_type=SRV_UP,
                             tc_srv_up=int(args['tc_run_time']),
                             tc_run_time_s=int(args['tc_run_time']))
        port_range = L4PortRange(l4pr_start=int(args['dpr_start']),
                                 l4pr_end=int(args['dpr_end']))
        if args['proto'] == 'TCP':
            l4_scfg = L4Server(l4s_proto=TCP,
                               l4s_tcp_udp=TcpUdpServer(tus_ports=port_range))
        if args['proto'] == 'UDP':
            l4_scfg = L4Server(l4s_proto=UDP,
                               l4s_tcp_udp=TcpUdpServer(tus_ports=port_range))
        b2b_dips = IpRange(ipr_start=Ip(ip_version=IPV4, ip_v4=int(
            netaddr.IPAddress(args['dest_ip_start']))),
                           ipr_end=Ip(ip_version=IPV4, ip_v4=int(
                               netaddr.IPAddress(
                                   args['dest_ip_end'])) + ip_count - 1))
        for i in range(0, len(size)):
            if app_proto[i] == 'HTTP':
                server_apps.append(ImixApp(ia_weight=int(wieght[i]),
                                           ia_app=build_http_app_server(
                                               int(size[i]))))
            else:
                server_apps.append(ImixApp(ia_weight=int(wieght[i]),
                                           ia_app=build_raw_app_server(
                                               int(size[i]))))
        _server_imix_group = build_imix_group(int(args['imix_id']), server_apps)
        err = warp17_method_call(warp17_host, warp17_port, Warp17_Stub,
                                 'ConfigureImixGroup', _server_imix_group)
        if err.e_code != 0:
            die('Error configuring imix testcase')
        tcarg = TestCase(tc_type=SERVER, tc_eth_port=int(args['port']),
                         tc_id=int(args['tc_id']),
                         tc_server=Server(srv_ips=b2b_dips,
                                          srv_l4=l4_scfg),
                         tc_app=App(app_proto=IMIX, app_imix=Imix(
                             imix_id=_server_imix_group.imix_id)),
                         tc_criteria=scrit,
                         tc_async=bool(args['async']))

        err = warp17_method_call(warp17_host, warp17_port, Warp17_Stub,
                                 'ConfigureTestCase', tcarg)
        if err.e_code != 0:
            die('Error adding imix testcase')

        update_arg = UpdateAppArg(uaa_tc_arg=tc_arg, uaa_app=App(app_proto=IMIX,
                                                                 app_imix=Imix(
                                                                     imix_id=_server_imix_group.imix_id)))
        err = warp17_method_call(warp17_host, warp17_port, Warp17_Stub,
                                 'UpdateTestCaseApp', update_arg)
        if err.e_code != 0:
            die('Error starting imix on port')


def imix_del():
    err = warp17_method_call(warp17_host, warp17_port, Warp17_Stub,
                             'DelImixGroup',
                             ImixArg(ia_imix_id=int(args['imix_id'])))
    if err.e_code != 0:
        die('Error deleting imix on port')


def check_stats():
    # Just check client stats a couple of times (once a second and stop afterwards)..
    for i in range(0, 1):
        client_result = warp17_method_call(warp17_host, warp17_port,
                                           Warp17_Stub,
                                           'GetTestStatus',
                                           TestCaseArg(
                                               tca_eth_port=int(args['port']),
                                               tca_test_case_id=int(
                                                   args['tc_id'])))
        if client_result.tsr_error.e_code != 0:
            die('Error fetching client test case stats.')
        print 'client_result' + str(client_result) + '\n\n'
        print 'Client test case state: ' + str(client_result.tsr_state) + '\n'
        print 'Global stats:'
        print client_result.tsr_stats.tcs_client
        print 'Rate stats:'
        print client_result.tsr_rate_stats
        print 'HTTP Client stats:'
        print client_result.tsr_app_stats.tcas_http
        time.sleep(1)

    port_config = warp17_method_call(warp17_host, warp17_port, Warp17_Stub,
                                     'GetTestCase',
                                     TestCaseArg(tca_eth_port=int(args['port']),
                                                 tca_test_case_id=int(
                                                     args['tc_id'])))
    if port_config.tcr_error.e_code != 0:
        die('Error fetching client test case config.')
    print 'port_config' + str(port_config) + '\n\n'


def stop_port():
    err = warp17_method_call(warp17_host, warp17_port, Warp17_Stub, 'PortStop',
                             PortArg(pa_eth_port=int(args['port'])))
    if err.e_code != 0:
        die('Error stopping port' + args['port'] + ' test cases.')

    print 'port' + args['port'] + ' stopped successfully!\n'


def build_http_app_client(size):
    return App(app_proto=HTTP_CLIENT,
               app_http_client=HttpClient(hc_req_method=GET,
                                          hc_req_object_name='/index.html',
                                          hc_req_host_name='www.foobar.net',
                                          hc_req_size=size))


def build_raw_app_client(size):
    return App(app_proto=RAW_CLIENT,
               app_raw_client=RawClient(rc_req_plen=size,
                                        rc_resp_plen=size))


def build_imix_group(imix_id, apps):
    imix_group = ImixGroup(imix_id=imix_id)
    for app in apps:
        imix_group.imix_apps.add(ia_weight=app.ia_weight, ia_app=app.ia_app)
    return imix_group


def build_http_app_server(size):
    return App(app_proto=HTTP_SERVER,
               app_http_server=HttpServer(hs_resp_code=OK_200,
                                          hs_resp_size=size))


def build_raw_app_server(size):
    return App(app_proto=RAW_SERVER,
               app_raw_server=RawServer(rs_req_plen=size,
                                        rs_resp_plen=size))


def run_test():
    ''' Assumes a back to back topology with two 40G ports. '''
    ''' Port 0 emulates clients and port 1 emulates servers. '''

    warp17_pid = init_test()
    result = None
    if args['function'] == 'l3_intf' and args['action'] == 'add':
        result = configure_l3_port()
    if args['function'] == 'clienttestcase' and args['action'] == 'add':
        result = configure_testcase_port0()
    if args['function'] == 'servertestcase' and args['action'] == 'add':
        result = configure_testcase_port1()
    if args['function'] == 'delete_testcase':
        result = delete_testcase()
    if args['function'] == 'l2_intf':
        result = configure_l2_port()
    if args['function'] == 'tcp_options':
        result = configure_tcp_options()
    if args['function'] == 'starttraffic':
        result = start_port()
    if args['function'] == 'stoptraffic':
        result = stop_port()
    if args['function'] == 'checkstatus':
        result = check_stats()
    if args['function'] == 'ipv4_options':
        result = ipv4_options()
    if args['function'] == 'vlan_options':
        result = vlan_options()
    if args['function'] == 'imix' and args['action'] == 'add':
        result = imix_config()
    if args['function'] == 'imix' and args['action'] == 'del':
        result = imix_del()
    if args['function'] == 'exit':  # Cleanup: Ask WARP17 to stop
        result = warp17_stop(env, warp17_pid)
    return result


if __name__ == '__main__':
    run_test()
