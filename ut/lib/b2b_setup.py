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
#     b2b_setup.py
#
# Description:
#     Helpers for configuring a back to back setup.
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

from warp17_common_pb2 import *
from warp17_l3_pb2 import *
from warp17_service_pb2 import *

def b2b_port_add(eth_port, def_gw):
    return PortCfg(pc_eth_port = eth_port, pc_def_gw = def_gw)

def b2b_port_add_intfs(pcfg, intf_cfg_list, vlan_enable=False):
    if not vlan_enable:
        for (ip, mask, count) in intf_cfg_list:
            pcfg.pc_l3_intfs.add(l3i_ip=ip, l3i_mask=mask, l3i_count=count)
    else:
        for (ip, mask, count, vlan_id, gw) in intf_cfg_list:
            pcfg.pc_l3_intfs.add(l3i_ip=ip, l3i_mask=mask, l3i_count=count,
                                 l3i_vlan_id=vlan_id, l3i_gw=gw)

def b2b_ipv4(eth_port, intf_idx):
    '''10.eth_port.0.x'''
    return (10 << 24) + (eth_port << 16) + intf_idx + 1

def b2b_mask(eth_port, intf_idx):
    return 0xff000000

def b2b_count(eth_port, intf_idx):
    return 1

def b2b_def_gw(eth_port):
    '''10.eth_port.0.254'''

    return b2b_ipv4(eth_port, 253)

def b2b_b2b_port(eth_port):
    return eth_port - 1 if eth_port % 2 == 1 else eth_port + 1

def b2b_sips(eth_port, ip_count):
    return IpRange(ipr_start=Ip(ip_version=IPV4, ip_v4=b2b_ipv4(eth_port, 0)),
                   ipr_end=Ip(ip_version=IPV4, ip_v4=b2b_ipv4(eth_port, 0) + ip_count - 1))

def b2b_dips(eth_port, ip_count):
    return IpRange(ipr_start=Ip(ip_version=IPV4, ip_v4=b2b_ipv4(b2b_b2b_port(eth_port), 0)),
                   ipr_end=Ip(ip_version=IPV4, ip_v4=b2b_ipv4(b2b_b2b_port(eth_port), 0) + ip_count - 1))

def b2b_ports(l4_port_count):
    return L4PortRange(l4pr_start=1, l4pr_end=l4_port_count)

def b2b_configure_port(eth_port, def_gw, l3_intf_count = 0, vlan_enable=False):
    pcfg = b2b_port_add(eth_port, def_gw = def_gw)

    if not vlan_enable:
        intf_cfg_list = [(Ip(ip_version=IPV4, ip_v4=b2b_ipv4(eth_port, i)),
                      Ip(ip_version=IPV4, ip_v4=b2b_mask(eth_port, i)),
                      b2b_count(eth_port, i)) for i in range(0, l3_intf_count)]
    else:
        vlan_id = 1000
        intf_cfg_list = [(Ip(ip_version=IPV4, ip_v4=b2b_ipv4(eth_port, i)),
                          Ip(ip_version=IPV4, ip_v4=b2b_mask(eth_port, i)),
                          b2b_count(eth_port, i),
                          vlan_id+i,
                          Ip(ip_version=IPV4, ip_v4=b2b_def_gw(eth_port))) for i in range(0, l3_intf_count)]

    b2b_port_add_intfs(pcfg, intf_cfg_list, vlan_enable)
    return pcfg
