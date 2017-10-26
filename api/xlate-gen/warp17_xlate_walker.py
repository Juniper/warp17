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
#     warp17_xlate_walker.py
#
# Description:
#     Module for walking protoc Request objects.
#
# Author:
#     Dumitru Ceara, Eelco Chaudron
#
# Initial Created:
#     01/19/2016
#
# Notes:
#
#

import sys
sys.path.append('./generated/py/')

from warp17_common_pb2 import warp17_constants
from warp17_common_pb2 import warp17_union_name, warp17_union_anon, warp17_array_size
from google.protobuf.compiler import plugin_pb2 as plugin
from google.protobuf.descriptor_pb2 import FieldDescriptorProto

class Warp17ParseException(Exception):
    def __init__(self, msg):
        self.msg = msg
    def __str__(self):
        return str(self.msg)

def enum_constants(item):
    return item.options.Extensions[warp17_constants]

# WARNING: This is a bit ugly but we force people not to use this as a union
#          name!
UNION_ANON_NAME = '__U_ANON'

def union_parse(field):
    uname = field.options.Extensions[warp17_union_name]
    if not uname is None and uname != '':
        if field.label != FieldDescriptorProto.LABEL_OPTIONAL:
            raise Warp17ParseException('Error: union field mulst be OPTIONAL: ' + \
                                       field.name)
        if uname == UNION_ANON_NAME:
            raise Warp17ParseException('Error: union name \'' + \
                                       UNION_ANON_NAME + '\' is reserved')
        return uname
    if field.options.Extensions[warp17_union_anon]:
        return UNION_ANON_NAME
    return None

def array_parse(field):
    size = field.options.Extensions[warp17_array_size]

    if size is None or size == '':
        raise Warp17ParseException('Error: Missing warp17_array_size option or ' + \
                                   'size == \'\': ' + field.name)
    return size

def noop(*args):
    return []

class WalkerOps:

    def __init__(self,
                 f_enter        = noop,
                 f_leave        = noop,
                 e_constants    = noop,
                 e_process      = noop,
                 u_enter        = noop,
                 u_leave        = noop,
                 m_enter        = noop,
                 m_field        = noop,
                 m_leave        = noop):
        self.f_enter = f_enter
        self.f_leave = f_leave
        self.e_constants = e_constants
        self.e_process = e_process
        self.u_enter = u_enter
        self.u_leave = u_leave
        self.m_enter = m_enter
        self.m_field = m_field
        self.m_leave = m_leave

def xlate_enum_walker(enum_item, walker_ops):
    if enum_constants(enum_item):
        return walker_ops.e_constants(enum_item)
    return walker_ops.e_process(enum_item)

def xlate_msg_walker(msg_item, walker_ops):
    output = []
    union_current = None
    union_is_anon = False

    for field in msg_item.field:
        parsed_name = field.name

        new_union = union_parse(field)
        new_union_is_anon = True if new_union == UNION_ANON_NAME else False
        if not new_union is None:
            if union_current is None:
                output += walker_ops.u_enter(msg_item, new_union,
                                             new_union_is_anon)
            else:
                if union_current != new_union:
                    output += walker_ops.u_leave(union_current, union_is_anon)
                    output += walker_ops.u_enter(msg_item, new_union,
                                                 new_union_is_anon)
            union_current = new_union
            union_is_anon = new_union_is_anon
        else:
            if union_current is not None or union_is_anon:
                output += walker_ops.u_leave(union_current, union_is_anon)
                union_current = None
                union_is_anon = False

        # Strip the array tag if available and update the name
        if field.label == FieldDescriptorProto.LABEL_REPEATED:
            if not union_current is None:
                raise Warp17ParseException('Error: union array fields ' + \
                                           'not supported: ' + field.name)
            array_size = array_parse(field)
        else:
            array_size = None

        output += walker_ops.m_field(msg_item, field, parsed_name,
                                     union_current,
                                     union_is_anon,
                                     array_size)

    if not union_current is None:
        output += walker_ops.u_leave(union_current, union_is_anon)
    return output

def xlate_file_walk(proto_file, walker_ops):
    output = []

    (prefix, _, _) = proto_file.name.rpartition('.proto')
    if prefix is None:
        raise Warp17ParseException('Error: Unexpected proto filename format: ' + \
                                   proto_file.name)

    output += walker_ops.f_enter(proto_file, prefix)

    for enum_item in proto_file.enum_type:
        output += xlate_enum_walker(enum_item, walker_ops)

    for msg_item in proto_file.message_type:
        output += walker_ops.m_enter(msg_item)
        output += xlate_msg_walker(msg_item, walker_ops)
        output += walker_ops.m_leave(msg_item)

    output += walker_ops.f_leave(proto_file, prefix)

    return output

