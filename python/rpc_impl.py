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
#     rpc_impl.py
#
# Description:
#     RPC implementation that's compatible with protobuf-rpc-c
#
# Author:
#     Dumitru Ceara, Eelco Chaudron
#
# Initial Created:
#     01/12/2016
#
# Notes:
#
#

import sys
sys.path.append('../api/generated/py')
import warp17_service_pb2

from google.protobuf.service import RpcChannel
from google.protobuf.service import RpcController

import socket
import struct
import random

from helpers import LogHelper
from helpers import Warp17Exception

WARP17_RPC_LOG_FILE='/tmp/warp17-rpc.log'

lh = LogHelper(name=__name__, filename=WARP17_RPC_LOG_FILE)

def read_bytes_from_sock(sock, bytes_to_read, msg, controller):
    buf = bytearray(bytes_to_read)
    view = memoryview(buf)
    while bytes_to_read:
        byte_cnt = sock.recv_into(view, bytes_to_read)
        if byte_cnt == 0:
            controller.SetFailed('Failed to receive all the ' + msg + ' data')
            return None
        view = view[byte_cnt:]
        bytes_to_read -= byte_cnt

    return buf

class Warp17RpcDefines():
    PROTOBUF_C_STATUS_CODE_SUCCESS = 0
    PROTOBUF_C_STATUS_CODE_SERVICE_FAILED = 1
    PROTOBUF_C_STATUS_CODE_TOO_MANY_PENDING = 2

    MAX_REQ_ID = 1024
    RPC_REQ_HDR1_FMT = '<II'  # method_index, req_len
    RPC_REQ_HDR2_FMT = '=I'   # req_id
    RPC_REQ_HDR1_SIZE = len(struct.pack(RPC_REQ_HDR1_FMT, 0, 1))
    RPC_REQ_HDR2_SIZE = len(struct.pack(RPC_REQ_HDR2_FMT, 2))
    RPC_REQ_HDR_SIZE = RPC_REQ_HDR1_SIZE + RPC_REQ_HDR2_SIZE

    RPC_RESP_HDR1_FMT = '<III' # status code, method_index, resp_len
    RPC_RESP_HDR2_FMT = '=I'   # req_id
    RPC_RESP_HDR1_SIZE = len(struct.pack(RPC_RESP_HDR1_FMT, 0, 1, 2))
    RPC_RESP_HDR2_SIZE = len(struct.pack(RPC_RESP_HDR2_FMT, 3))
    RPC_RESP_HDR_SIZE = RPC_RESP_HDR1_SIZE + RPC_RESP_HDR2_SIZE

class Warp17RpcException(Warp17Exception):
    pass

class Warp17Controller(RpcController):

    def __init__(self):
        super(Warp17Controller, self).__init__()
        self.reason = None

    def Reset(self):
        self.reason = None

    def Failed(self):
        return not self.reason is None

    def ErrorText(self):
        return self.reason

    def SetFailed(self, reason):
        self.reason = reason

class Warp17SyncRpcChannel(RpcChannel):

    @staticmethod
    def ValidateStatus(status):
        if status == Warp17RpcDefines.PROTOBUF_C_STATUS_CODE_SERVICE_FAILED:
            return 'Received Status: Failed'
        elif status == Warp17RpcDefines.PROTOBUF_C_STATUS_CODE_TOO_MANY_PENDING:
            return 'Received Status: Too Many Pending'
        elif status != Warp17RpcDefines.PROTOBUF_C_STATUS_CODE_SUCCESS:
            return 'Received Unknown Status'

        return None

    @staticmethod
    def ValidateResp(resp_hdr, req_hdr):
        (resp_midx, resp_req_id) = resp_hdr
        (req_midx, req_req_id) = req_hdr

        if resp_midx != req_midx or resp_req_id != req_req_id:
            return 'Received invalid response'

        return None

    @staticmethod
    def ValidateHdr(status, resp_hdr, req_hdr, controller):
        controller.SetFailed(Warp17SyncRpcChannel.ValidateStatus(status))
        if not controller.Failed():
            controller.SetFailed(Warp17SyncRpcChannel.ValidateResp(resp_hdr, req_hdr))

    def __init__(self, host, port):
        self.host = host
        self.port = port

    def __enter__(self):
        lh.debug('Connecting to RPC server ' + self.host + ':' + str(self.port))
        self.client_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.client_sock.connect((self.host, self.port))
        return self

    def __exit__(self, exc_type, exc_value, traceback):
        self.client_sock.close()

    def CallMethod(self, method_desc, controller, req, resp_class, done):
        req_str = req.SerializeToString()
        req_str_len = len(req_str)
        hdr1 = struct.pack(Warp17RpcDefines.RPC_REQ_HDR1_FMT,
                           method_desc.index,
                           req_str_len)

        req_id = random.randrange(Warp17RpcDefines.MAX_REQ_ID)
        hdr2 = struct.pack(Warp17RpcDefines.RPC_REQ_HDR2_FMT, req_id)
        self.client_sock.sendall(hdr1 + hdr2 + req_str)

        # wait for the response
        # first read the header
        resp_hdr_buf = read_bytes_from_sock(self.client_sock,
                                            Warp17RpcDefines.RPC_RESP_HDR_SIZE,
                                            'HDR',
                                            controller)
        if controller.Failed(): return None

        (resp_status, resp_method_index, resp_len) = \
            struct.unpack_from(Warp17RpcDefines.RPC_RESP_HDR1_FMT, resp_hdr_buf)
        (resp_req_id, ) = \
            struct.unpack_from(Warp17RpcDefines.RPC_RESP_HDR2_FMT, resp_hdr_buf,
                               Warp17RpcDefines.RPC_RESP_HDR1_SIZE)

        # validate header
        Warp17SyncRpcChannel.ValidateHdr(resp_status, (resp_method_index, resp_req_id),
                                       (method_desc.index, req_id),
                                       controller)
        if controller.Failed(): return None


        # read the response body
        resp_buf = read_bytes_from_sock(self.client_sock, resp_len,
                                               'DATA',
                                               controller)
        if controller.Failed(): return None

        resp = resp_class()
        resp.ParseFromString(str(resp_buf))
        return resp

def warp17_method_call(host, port, service_stub_class, method_name, arg):
    try:
        lh.debug('Calling ' + str(service_stub_class) + '.' + method_name)
        with Warp17SyncRpcChannel(host, port) as chan:
            controller = Warp17Controller()
            service = service_stub_class(chan)
            response = getattr(service, method_name)(controller, arg, None)
            if not controller.Failed():
                return response
            else:
                lh.error('Request failed: ' + controller.ErrorText())
                raise Warp17RpcException(controller.ErrorText())

    except Exception, ex:
        lh.error(ex.__str__())
        raise Warp17RpcException(str(ex))

