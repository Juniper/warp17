#!/usr/bin/python
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
#     warp17_xlate_gen.py
#
# Description:
#     protoc plugin which will generate a translation layer between protoc-c
#     generated structures and what WARP17 needs.
#
# Author:
#     Dumitru Ceara, Eelco Chaudron
#
# Initial Created:
#     01/15/2016
#
# Notes:
#
#

import sys
sys.path.append('./generated/py/')

import re

from google.protobuf.compiler import plugin_pb2 as plugin
from google.protobuf.descriptor_pb2 import FieldDescriptorProto

from warp17_common_pb2 import warp17_xlate_tpg
from warp17_xlate_walker import *

XLATE_H = '.xlate.h'
XLATE_C = '.xlate.c'

def cstringify(str):
    return str.replace('-', '_').replace('.', '_').upper()

def uncamel(name):
    s1 = re.sub('(.)([A-Z][a-z]+)', r'\1_\2', name)
    return re.sub('([a-z0-9])([A-Z])', r'\1_\2', s1).lower()

def extract_name(name):
    (_, _, result) = name.partition('.')
    if result is None:
        raise Warp17ParseException('Invalid name: ' + str(name))
    return result

def extract_field_name(field):
    return extract_name(field.type_name)

def line(s):
    return s + '\n'

def generate_h_guard_name(filename):
    return 'H_WARP17_RPC_GEN__' + cstringify(filename) + '_'

def generate_guard_begin(guard_name):
    return line('#ifndef ' + guard_name) + \
                line('#define ' + guard_name)

def generate_guard_end(guard_name):
    return line(line('#endif /* ' + guard_name + ' */'))

def generate_h_include(filename):
    return line(line('#include "' + filename + '"'))

def generate_standard_includes():
    return \
        line('#include <stdbool.h>') + \
        line('#include <stdint.h>') + \
        line('#include <string.h>') + \
        line('#include <errno.h>') + \
        line('#include <rte_malloc.h>')

def generate_name(name):
    return 'tpg_' + uncamel(name) + '_s'

def generate_fwname(name):
    return 'tpg_' + uncamel(name) + '_t'

def generate_xlate_protoc_name(name):
    return 'tpg_xlate_protoc_' + name

def generate_xlate_tpg_name(name):
    return 'tpg_xlate_tpg_' + name

def generate_xlate_tpg_union_name(name):
    return 'tpg_xlate_tpg_union_' + name

def generate_xlate_free_name(name):
    return 'tpg_xlate_free_' + name

def generate_xlate_sig(name, ret_type, in_name, out_name):
    return ret_type + ' ' + name +'(' + \
           'const ' + in_name + ' *in, ' + \
           out_name + ' *out)'

def generate_xlate_protoc_sig(name):
    return generate_xlate_sig(generate_xlate_protoc_name(name),
                              'void',
                              name,
                              generate_fwname(name))

def generate_xlate_tpg_sig(name):
    return generate_xlate_sig(generate_xlate_tpg_name(name),
                              'int',
                              generate_fwname(name),
                              name)

def generate_xlate_tpg_union_sig(name):
    return generate_xlate_sig(generate_xlate_tpg_union_name(name),
                              'int',
                              generate_fwname(name),
                              name)

def generate_xlate_free_sig(name):
    return 'void ' + generate_xlate_free_name(name) + \
           '(' + name + ' *ptr' + ')'

def generate_xlate_protoc_h(item):
    return line(line(generate_xlate_protoc_sig(item.name) + ';'))

def generate_xlate_tpg_h(item):
    return line(line(generate_xlate_tpg_sig(item.name) + ';'))

def generate_xlate_tpg_union_h(item):
    return line(line(generate_xlate_tpg_union_sig(item.name) + ';'))

def generate_xlate_free_h(item):
    return line(line(generate_xlate_free_sig(item.name) + ';'))

def generate_xlate_h(item):
    return generate_xlate_protoc_h(item)    + \
           generate_xlate_tpg_h(item)       + \
           generate_xlate_tpg_union_h(item) + \
           generate_xlate_free_h(item)

def generate_xlate_guard_name(name):
    return 'TPG_XLATE_' + cstringify(name)

def xlate_generate_guard_begin(name):
    return line('#ifndef ' + name)

def xlate_generate_guard_end(name):
    return line('#endif /* ' + name + ' */')

def generate_message_type(field):
    return generate_fwname(extract_field_name(field))

def is_type_ptr(t):
    return t == FieldDescriptorProto.TYPE_BYTES or \
           t == FieldDescriptorProto.TYPE_STRING

def is_type_str(t):
    return t == FieldDescriptorProto.TYPE_STRING

def generate_type(field):
    type_str = {
        FieldDescriptorProto.TYPE_BOOL : 'bool',
        FieldDescriptorProto.TYPE_BYTES : 'char *',
        FieldDescriptorProto.TYPE_DOUBLE : 'double',
        FieldDescriptorProto.TYPE_ENUM : generate_message_type(field),
        FieldDescriptorProto.TYPE_FIXED32 : 'uint32_t',
        FieldDescriptorProto.TYPE_FIXED64 : 'uint64_t',
        FieldDescriptorProto.TYPE_FLOAT : 'float',
        FieldDescriptorProto.TYPE_INT32 : 'int32_t',
        FieldDescriptorProto.TYPE_INT64 : 'int64_t',
        FieldDescriptorProto.TYPE_MESSAGE : generate_message_type(field),
        FieldDescriptorProto.TYPE_SFIXED32 : 'int32_t',
        FieldDescriptorProto.TYPE_SFIXED64 : 'int64_t',
        FieldDescriptorProto.TYPE_SINT32 : 'int32_t',
        FieldDescriptorProto.TYPE_SINT64 : 'int64_t',
        FieldDescriptorProto.TYPE_STRING : 'char *',
        FieldDescriptorProto.TYPE_UINT32 : 'uint32_t',
        FieldDescriptorProto.TYPE_UINT64 : 'uint64_t'
    }.get(field.type);

    if type_str is None:
        raise Warp17ParseException('Unsupported type: ' + str(field))

    return type_str

def generate_constant_define(name, val):
    return line('#define ' + name + ' ' + val)

def generate_struct_begin(item):
    return line('typedef struct ' + generate_name(item.name) + ' {')

def generate_struct_end(item):
    return line(line('} ' + generate_fwname(item.name) + ';'))

def generate_union_begin(name):
    return line('\tunion {')

def generate_union_end(name):
    return line('\t} ' + name + ';')

def generate_return(err = None):
    if err is None:
        return line('return;')
    return line('return ' + '-' + err + ';')

def generate_assign(dest, src):
    return line(dest + ' = ' + src + ';')

def generate_if(cond, true_inst, false_inst = None):
    output = \
        [line('if (' + cond + ') {')] + \
        ['\t' + inst for inst in true_inst] + \
        [line('}')]

    if not false_inst is None:
        output += \
            [line('else {')] + \
            ['\t' + inst for inst in false_inst] + \
            [line('}')]

    return output

def generate_for(var, start, end, body_inst = None):
    return \
        [line('for (' + var + ' = ' + start + '; ' + \
                   var + ' < ' + end + '; ' + \
                   var + '++) {')] + \
        ['\t' + inst for inst in body_inst] + \
        [line('}')]

def generate_alloc(var, size):
    return line(var + ' = rte_zmalloc("TPG_RPC_GEN", ' + size + ', 0);')

def generate_alloc_err(var):
    return generate_if('!' + var,
                       true_inst = [generate_return('ENOMEM')])

def generate_free(var):
    return line ('if (' + var + ') rte_free(' + var + ');')

def generate_min(a, b):
    return '((' + a + ') <= (' + b + ') ? (' + a + ') : (' + b + '))'

###############################################################################
# XLATE HDR
###############################################################################
def hdr_f_enter(proto_file, prefix):
    return [
        generate_guard_begin(generate_h_guard_name(prefix)),
        generate_standard_includes(),
        generate_h_include(prefix + '.pb-c.h')
    ]

def hdr_f_leave(proto_file, prefix):
    return [
        generate_guard_end(generate_h_guard_name(prefix))
    ]

def hdr_e_constants(item):
    return [
        generate_constant_define(v.name, str(v.number)) for v in item.value
    ]

def hdr_e_process(item):
    return [
        line(line('typedef ' + item.name + ' ' + generate_fwname(item.name) + ';'))
    ]

def hdr_m_enter(item):
    return [generate_struct_begin(item)]

def hdr_u_enter(item, union_name, union_anon):
    return [generate_union_begin(union_name)]

def hdr_u_leave(union_name, union_anon):
    return [generate_union_end(union_name if not union_anon else '')]

def hdr_m_generate_required_field(item, field, field_name):
    return [line(generate_type(field) + ' ' + field_name + ';')]

def hdr_m_generate_optional_field(item, field, field_name):
    if is_type_ptr(field.type):
        return hdr_m_generate_required_field(item, field, field_name)

    if field.type == FieldDescriptorProto.TYPE_MESSAGE:
        return [line(generate_message_type(field) + ' *' + field_name + ';')]

    return \
        [line('bool has_' + field_name + ';'),
         line(generate_type(field) + ' ' + field_name + ';')]

def hdr_m_generate_repeated_field(item, field, field_name, array_size):
    return [ \
        line('uint32_t ' + field_name + '_count;'),
        line(generate_type(field) + ' ' + field_name + '[' + array_size + '];')
    ]

def hdr_m_generate_union_field(item, field, field_name, array_size):
    return ['\t' + l for l in hdr_m_generate_required_field(item, field,
                                                            field_name)]

def hdr_m_generate_field(item, field, field_name, array_size):
    if field.label == FieldDescriptorProto.LABEL_REQUIRED:
        return hdr_m_generate_required_field(item, field, field_name)
    elif field.label == FieldDescriptorProto.LABEL_OPTIONAL:
        return hdr_m_generate_optional_field(item, field, field_name)
    elif field.label == FieldDescriptorProto.LABEL_REPEATED:
        return hdr_m_generate_repeated_field(item, field, field_name,
                                             array_size)

def hdr_m_field(item, field, field_name, union_name, union_anon, array_size):
    if not union_name is None:
        return ['\t' + l for l in
                    hdr_m_generate_union_field(item, field, field_name,
                                               array_size)]
    else:
        return ['\t' + l for l in
                    hdr_m_generate_field(item, field, field_name, array_size)]

def hdr_m_leave(item):
    return [
        generate_struct_end(item),
        generate_xlate_h(item)
    ]

###############################################################################
# XLATE PROTOC
###############################################################################
def generate_msg_protoc_xlate(type_name, dst, src):
    type_name = extract_name(type_name)
    return line(generate_xlate_protoc_name(type_name) + \
                    '(' + src + ', ' + dst + ');')

def xlate_protoc_f_enter(proto_file, prefix):
    return [generate_h_include(proto_file.name + XLATE_H)]

def xlate_protoc_m_enter(item):
    return [ \
        xlate_generate_guard_begin(generate_xlate_guard_name(item.name)),
        line(generate_xlate_protoc_sig(item.name)),
        line('{'),
        line('\tuint32_t i;'),
        line('\t(void)i;')
    ]

def xlate_protoc_m_generate_msg_required(item, field, field_name):
    return [generate_msg_protoc_xlate(field.type_name,
                                      '&out->' + field_name,
                                      'in->' + field.name)]

def xlate_protoc_m_generate_scalar_required(item, field, field_name):
    if is_type_str(field.type):
        #TODO: we strdup but we never free!
        return [generate_assign('out->' + field_name, 'strdup(in->' + field.name + ')')]
    return [generate_assign('out->' + field_name, 'in->' + field.name)]

def xlate_protoc_m_generate_required(item, field, field_name):
    if field.type == FieldDescriptorProto.TYPE_MESSAGE:
        return xlate_protoc_m_generate_msg_required(item, field, field_name)
    return xlate_protoc_m_generate_scalar_required(item, field, field_name)

def xlate_protoc_m_generate_msg_optional(item, field, field_name, union_name,
                                         union_anon):
    if not union_anon and not union_name is None:
        field_name = union_name + '.' + field_name

    return generate_if('in->' + field.name + ' !=  NULL',
                       true_inst = [generate_msg_protoc_xlate(field.type_name,
                                                             '&out->' + field_name,
                                                             'in->' + field.name)])

def xlate_protoc_m_generate_scalar_optional(item, field, field_name, union_name,
                                            union_anon):
    if not union_anon and not union_name is None:
        field_name = union_name + '.' + field_name

    if is_type_ptr(field.type):
        if is_type_str(field.type):
            #TODO: we strdup but we never free!
            return [generate_assign('out->' + field_name, 'strdup(in->' + field.name + ')')]
        return [generate_assign('out->' + field_name, 'in->' + field.name)]

    output = []
    if union_name is None:
        output += [generate_assign('out->has_' + field_name,
                                   'in->has_' + field.name)]
    output += generate_if('in->has_' + field.name,
                          true_inst = [generate_assign('out->' + field_name,
                                                       'in->' + field.name)])
    return output

def xlate_protoc_m_generate_optional(item, field, field_name, union_name,
                                     union_anon):
    if field.type == FieldDescriptorProto.TYPE_MESSAGE:
        return xlate_protoc_m_generate_msg_optional(item, field, field_name,
                                                    union_name,
                                                    union_anon)
    return xlate_protoc_m_generate_scalar_optional(item, field, field_name,
                                                   union_name,
                                                   union_anon)

def xlate_protoc_m_generate_msg_repeated(item, field, field_name, array_size):
    return generate_for('i', '0',
                        generate_min('in->n_' + field.name, array_size),
                        body_inst = [generate_msg_protoc_xlate(field.type_name,
                                                               '&out->' + field_name + '[i]',
                                                               'in->' + field.name + '[i]')])

def xlate_protoc_m_generate_scalar_repeated(item, field, field_name, array_size):
    if is_type_ptr(field.type):
        raise Warp17ParseException('Error: repeated pointer types ' + \
                                   'not supported: ' + field.name)
    return generate_for('i', '0', 'in->n_' + field.name,
                        body_inst = [generate_assign('out->' + field_name + '[i]',
                                                     'in->' + field.name + '[i]')])

def xlate_protoc_m_generate_repeated(item, field, field_name, array_size):
    output = [generate_assign('out->' + field_name + '_count',
                              generate_min('in->n_' + field.name, array_size))]
    if field.type == FieldDescriptorProto.TYPE_MESSAGE:
        output += xlate_protoc_m_generate_msg_repeated(item, field, field_name,
                                                       array_size)
    else:
        output += xlate_protoc_m_generate_scalar_repeated(item, field,
                                                          field_name,
                                                          array_size)
    return output

def xlate_protoc_m_field(item, field, field_name, union_name, union_anon,
                         array_size):
    if field.label == FieldDescriptorProto.LABEL_REQUIRED:
        return ['\t' + l for l in
                xlate_protoc_m_generate_required(item, field, field_name)]
    elif field.label == FieldDescriptorProto.LABEL_OPTIONAL:
        return ['\t' + l for l in
                xlate_protoc_m_generate_optional(item, field, field_name,
                                                 union_name,
                                                 union_anon)]
    elif field.label == FieldDescriptorProto.LABEL_REPEATED:
        return ['\t' + l for l in
                xlate_protoc_m_generate_repeated(item, field, field_name,
                                                 array_size)]
    else:
        raise Warp17ParseException('Error: Unexpected label type: ' + field.name)

def xlate_protoc_m_leave(item):
    return [ \
        line('}'),
        xlate_generate_guard_end(generate_xlate_guard_name(item.name))   + '\n',
        '\n'
    ]

###############################################################################
# XLATE TPG
###############################################################################
def generate_msg_tpg_xlate(type_name, dst, src):
    type_name = extract_name(type_name)
    return line(generate_xlate_tpg_name(type_name) + \
           '(' + src + ', ' + dst + ');')

def generate_msg_tpg_union_xlate(type_name, dst, src):
    return line(generate_xlate_tpg_union_name(type_name) + \
           '(' + src + ', ' + dst + ');')

def generate_xlate_tpg_init(item):
    return generate_assign('*out',
                           '(' + item.name + ')' + uncamel(item.name).upper() + '__INIT')

def xlate_tpg_m_enter(item):
    if not item.options.Extensions[warp17_xlate_tpg]: return []

    return [
        xlate_generate_guard_begin(generate_xlate_guard_name(item.name)),
        line(generate_xlate_tpg_sig(item.name)),
        line('{'),
        line('\tuint32_t i;'),
        line('\tint err;'),
        line('\t(void)i;'),
        line('\t(void)err;'),
        '\t' + generate_xlate_tpg_init(item)
    ]

def xlate_tpg_u_enter(item, union_name, union_anon):
    if not item.options.Extensions[warp17_xlate_tpg]: return []

    return [generate_assign('err', generate_msg_tpg_union_xlate(item.name,
                                                                'out',
                                                                'in'))] + \
           generate_if('err != 0',
                       true_inst = ['\t' + generate_return('err')])


def xlate_tpg_m_generate_msg_required(item, field, field_name):
    return \
        [generate_alloc('out->' + field.name, 'sizeof(*out->' + field.name + ')')] + \
        generate_alloc_err('out->' + field.name) + \
        [generate_msg_tpg_xlate(field.type_name, 'out->' + field.name,
                                '&in->' + field_name)]

def xlate_tpg_m_generate_scalar_required(item, field, field_name):
    return [
        generate_assign('out->' + field_name, 'in->' + field.name)
    ]

def xlate_tpg_m_generate_required(item, field, field_name):
    if field.type == FieldDescriptorProto.TYPE_MESSAGE:
        return xlate_tpg_m_generate_msg_required(item, field, field_name)
    return xlate_tpg_m_generate_scalar_required(item, field, field_name)

def xlate_tpg_m_generate_optional(item, field, field_name, union_name,
                                  union_anon):
    # Can't translate unions. The user has to provide a function for that..
    if not union_name is None:
        return []

    if is_type_ptr(field.type):
        return xlate_tpg_m_generate_required(item, field, field_name)

    if field.type == FieldDescriptorProto.TYPE_MESSAGE:
        return generate_if('in->' + field_name + ' != NULL',
                           true_inst = [generate_alloc('out->' + field.name,
                                                       'sizeof(*out->' + field.name + ')')] + \
                                        generate_alloc_err('out->' + field.name) + \
                                       [generate_msg_tpg_xlate(field.type_name,
                                                              'out->' + field.name,
                                                              'in->' + field_name)])

    return generate_if('in->has_' + field_name,
                       true_inst = [generate_assign('out->' + field.name,
                                                   'in->' + field_name),
                                    generate_assign('out->has_' + field.name,
                                                    'in->has_' + field_name)])

def xlate_tpg_m_generate_msg_repeated(item, field, field_name, array_size):
    return generate_for('i', '0', 'in->' + field_name + '_count',
                        body_inst = \
                            [generate_alloc('out->' + field.name + '[i]',
                                           'sizeof(*out->' + field.name + '[i])')] + \
                             generate_alloc_err('out->' + field.name + '[i]') + \
                            [generate_msg_tpg_xlate(field.type_name,
                                                    'out->' + field.name + '[i]',
                                                    '&in->' + field_name + '[i]')])

def xlate_tpg_m_generate_scalar_repeated(item, field, field_name, array_size):
    return generate_for('i', '0', 'in->' + field_name + '_count',
                        body_inst = [generate_assign('out->' + field.name + '[i]',
                                                     'in->' + field_name + '[i]')])

def xlate_tpg_m_generate_repeated(item, field, field_name, array_size):
    output =  [generate_alloc('out->' + field.name,
                              array_size + ' * sizeof(*out->' + field.name + ')')]
    output +=  generate_alloc_err('out->' + field.name)
    output += [generate_assign('out->n_' + field.name,
                               'in->' + field_name + '_count')]

    if field.type == FieldDescriptorProto.TYPE_MESSAGE:
        output += xlate_tpg_m_generate_msg_repeated(item, field, field_name,
                                                    array_size)
    else:
        output += xlate_tpg_m_generate_scalar_repeated(item, field, field_name,
                                                       array_size)

    return output

def xlate_tpg_m_field(item, field, field_name, union_name, union_anon,
                      array_size):
    if not item.options.Extensions[warp17_xlate_tpg]: return []

    if field.label == FieldDescriptorProto.LABEL_REQUIRED:
        return ['\t' + l for l in
                xlate_tpg_m_generate_required(item, field, field_name)]
    elif field.label == FieldDescriptorProto.LABEL_OPTIONAL:
        return ['\t' + l for l in
                xlate_tpg_m_generate_optional(item, field, field_name,
                                              union_name,
                                              union_anon)]
    elif field.label == FieldDescriptorProto.LABEL_REPEATED:
        return ['\t' + l for l in
                xlate_tpg_m_generate_repeated(item, field, field_name,
                                              array_size)]
    else:
        raise Warp17ParseException('Error: Unexpected label type: ' + field.name)

def xlate_tpg_m_leave(item):
    if not item.options.Extensions[warp17_xlate_tpg]: return []

    return [ \
        '\t' + generate_return('0'),
        line('}'),
        xlate_generate_guard_end(generate_xlate_guard_name(item.name))   + '\n',
        '\n'
    ]

###############################################################################
# XLATE Free
###############################################################################
def xlate_free_m_enter(item):
    return [
        xlate_generate_guard_begin(generate_xlate_guard_name(item.name)),
        line(generate_xlate_free_sig(item.name)),
        line('{'),
        line('\tuint32_t i;'),
        line('\t(void)i;'),
        line('\t(void)ptr;')
    ]

def xlate_free_m_msg_repeated(item, field, field_name, array_size):
    return generate_for('i', '0', 'ptr->n_' + field.name,
                        body_inst = [generate_free('ptr->' + field.name + '[i]')])

def xlate_free_m_scalar_repeated(item, field, field_name, array_size):
    return []

def xlate_free_m_repeated(item, field, field_name, array_size):
    if field.type == FieldDescriptorProto.TYPE_MESSAGE:
        output = xlate_free_m_msg_repeated(item, field, field_name, array_size)
    else:
        output = xlate_free_m_scalar_repeated(item, field, field_name,
                                              array_size)
    return output + [
        generate_free('ptr->' + field.name)
    ]

def xlate_free_m_optional(item, field, field_name):
    if field.type != FieldDescriptorProto.TYPE_MESSAGE:
        return []

    return [
        generate_free('ptr->' + field.name)
    ]

def xlate_free_m_field(item, field, field_name, union_name, union_anon,
                       array_size):
    if field.label == FieldDescriptorProto.LABEL_REPEATED:
        return ['\t' + l for l in
                    xlate_free_m_repeated(item, field, field_name, array_size)]

    if field.label == FieldDescriptorProto.LABEL_OPTIONAL:
        return ['\t' + l for l in
                    xlate_free_m_optional(item, field, field_name)]

    return []

def xlate_free_m_leave(item):
    return [
        line('}'),
        xlate_generate_guard_end(generate_xlate_guard_name(item.name)),
        '\n'
    ]

###############################################################################
# main
###############################################################################
def generate(request, response):
    for proto_file in request.proto_file:
        filename = proto_file.name

        if not filename.startswith('warp17'):
            continue

        (prefix, _, _) = filename.rpartition('.proto')
        if prefix is None:
            raise Warp17ParseException(
                'Error: Unexpected proto filename format: ' + filename)

        f_hdr = response.file.add()
        f_hdr.name = filename + XLATE_H

        hdr_ops = WalkerOps(f_enter     = hdr_f_enter,
                            f_leave     = hdr_f_leave,
                            e_constants = hdr_e_constants,
                            e_process   = hdr_e_process,
                            u_enter     = hdr_u_enter,
                            u_leave     = hdr_u_leave,
                            m_enter     = hdr_m_enter,
                            m_field     = hdr_m_field,
                            m_leave     = hdr_m_leave)
        f_hdr.content = ''.join(xlate_file_walk(proto_file, hdr_ops))

        f_c = response.file.add()
        f_c.name = filename + XLATE_C

        xlate_protoc_ops = WalkerOps(f_enter   = xlate_protoc_f_enter,
                                     m_enter   = xlate_protoc_m_enter,
                                     m_field   = xlate_protoc_m_field,
                                     m_leave   = xlate_protoc_m_leave)

        f_c.content = ''.join(xlate_file_walk(proto_file, xlate_protoc_ops))

        xlate_tpg_ops = WalkerOps(m_enter   = xlate_tpg_m_enter,
                                  u_enter   = xlate_tpg_u_enter,
                                  m_field   = xlate_tpg_m_field,
                                  m_leave   = xlate_tpg_m_leave)
        f_c.content += ''.join(xlate_file_walk(proto_file, xlate_tpg_ops))

        xlate_free_ops = WalkerOps(m_enter   = xlate_free_m_enter,
                                   m_field   = xlate_free_m_field,
                                   m_leave   = xlate_free_m_leave)
        f_c.content += ''.join(xlate_file_walk(proto_file, xlate_free_ops))

if __name__ == '__main__':
    # Read request message from stdin
    data = sys.stdin.read()

    # Parse request
    request = plugin.CodeGeneratorRequest()
    request.ParseFromString(data)

    response = plugin.CodeGeneratorResponse()
    generate(request, response)
    sys.stdout.write(response.SerializeToString())

