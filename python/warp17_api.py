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
#     warp17_api.py
#
# Description:
#     WARP17 api related helpers:
#           - start/stop WARP17
#           - WARP17 environment variables
#
# Author:
#     Dumitru Ceara, Eelco Chaudron
#
# Initial Created:
#     04/14/2016
#
# Notes:
#
#

import os
import sys
import string
import time

from subprocess   import Popen
from subprocess   import PIPE
from enum         import Enum
from configparser import ConfigParser, DEFAULTSECT

sys.path.append('../../api/generated/py')

from helpers  import LogHelper
from helpers  import Warp17Exception
from uniq     import get_uniq_stamp
from rpc_impl import Warp17RpcException
from rpc_impl import warp17_method_call

from warp17_service_pb2 import *

EXIT_FAILURE = 1

class Warp17Env():

    INI_FILE_ENV     = 'WARP17_INI_FILE'
    UNIQ_STAMP       = 'WARP17_UNIQ_STAMP'
    BIN              = 'WARP17_BIN'
    HOST             = 'WARP17_HOST'
    RPC_PORT         = 'WARP17_PORT'
    INIT_RETRY       = 'WARP17_INIT_RETRY'

    COREMASK         = 'coremask'
    NCHAN            = 'nchan'
    MEMORY           = 'memory'
    PORTS            = 'ports'
    QMAP_DEFAULT     = 'qmap-default'
    QMAP             = 'qmap'
    MBUF_SZ          = 'mbuf-sz'
    MBUF_POOL_SZ     = 'mbuf-pool-sz'
    MBUF_HDR_POOL_SZ = 'mbuf-hdr-pool-sz'
    TCB_POOL_SZ      = 'tcb-pool-sz'
    UCB_POOL_SZ      = 'ucb-pool-sz'

    def __init__(self, path=None):
        if path is None:
            path = os.environ.get(Warp17Env.INI_FILE_ENV)
        if path is None:
            raise Warp17Exception('Missing ' + Warp17Env.INI_FILE_ENV + ' ' + \
                                  'environment variable!')
        self._config = ConfigParser()
        if not os.path.exists(path):
            raise Warp17Exception('Missing ' + path + ' config file!')

        self._config.read_file(open(path))

        self._uniq_stamp = os.environ.get(Warp17Env.UNIQ_STAMP, get_uniq_stamp())
        self._bin_name = os.environ.get(Warp17Env.BIN, '../build/warp17')
        self._host_name = os.environ.get(Warp17Env.HOST, 'localhost')
        self._rpc_port = os.environ.get(Warp17Env.RPC_PORT, '42424')
        self._init_retry = os.environ.get(Warp17Env.INIT_RETRY, '30')

    def get_value(self, name, section=DEFAULTSECT, mandatory=False, cast=None):
        val = self._config[section].get(name)

        if mandatory and val is None:
            raise Warp17Exception('Missing mandatory ' + name + ' variable ' + \
                                  'in section ' + section + '!')

        if not val is None and not cast is None:
            return cast(val)
        return val

    def set_value(self, key, value, section=DEFAULTSECT):
        """Exports into the enviroment che KEY with a certain value"""
        self._config.set(section, key, str(value))

    def get_coremask(self):
        return self.get_value(Warp17Env.COREMASK, mandatory=True)

    def get_nchan(self):
        return int(self.get_value(Warp17Env.NCHAN, mandatory=True))

    def get_memory(self):
        return int(self.get_value(Warp17Env.MEMORY, mandatory=True))

    def get_ports(self):
        return string.split(self.get_value(Warp17Env.PORTS, mandatory=True))

    def get_qmap(self):
        qmap_default = self.get_value(Warp17Env.QMAP_DEFAULT)

        if not qmap_default is None:
            return '--qmap-default ' + qmap_default
        ports = self.get_ports()

        return '--qmap '.join([str(idx) + '.' + \
                               self.get_value(Warp17Env.QMAP, section=port) for
                               idx, port in enumerate(ports)])

    def get_mbuf_sz(self):
        return self.get_value(Warp17Env.MBUF_SZ)

    def get_mbuf_pool_sz(self):
        return self.get_value(Warp17Env.MBUF_POOL_SZ)

    def get_mbuf_hdr_pool_sz(self):
        return self.get_value(Warp17Env.MBUF_HDR_POOL_SZ)

    def get_tcb_pool_sz(self):
        return self.get_value(Warp17Env.TCB_POOL_SZ)

    def get_ucb_pool_sz(self):
        return self.get_value(Warp17Env.UCB_POOL_SZ)

    def get_uniq_stamp(self):
        return self._uniq_stamp

    def get_bin_name(self):
        return self._bin_name

    def get_host_name(self):
        return self._host_name

    def get_rpc_port(self):
        return int(self._rpc_port)

    def get_init_retry(self):
        return int(self._init_retry)

    def get_exec_args(self):
        args = '-c ' + self.get_coremask()                           + ' ' + \
               '-n ' + str(self.get_nchan())                         + ' ' + \
               '-m ' + str(self.get_memory())                        + ' ' + \
               ' '.join(['-w ' + port for port in self.get_ports()]) + ' ' + \
               '--'                                                  + ' ' + \
               self.get_qmap()
        mbuf_sz = self.get_mbuf_sz()
        if not mbuf_sz is None:
            args += ' --mbuf-sz ' + str(mbuf_sz)
        tcb_pool_sz = self.get_tcb_pool_sz()
        if not tcb_pool_sz is None:
            args += ' --tcb-pool-sz ' + str(tcb_pool_sz)
        ucb_pool_sz = self.get_ucb_pool_sz()
        if not ucb_pool_sz is None:
            args += ' --ucb-pool-sz ' + str(ucb_pool_sz)
        mbuf_pool_sz = self.get_mbuf_pool_sz()
        if not mbuf_pool_sz is None:
            args += ' --mbuf-pool-sz ' + str(mbuf_pool_sz)
        mbuf_hdr_pool_sz = self.get_mbuf_hdr_pool_sz()
        if not mbuf_hdr_pool_sz is None:
            args += ' --mbuf-hdr-pool-sz ' + str(mbuf_hdr_pool_sz)
        return args

class Warp17OutputArgs():
    # Default values
    WARP17_DEFAULT_OUTPUT_FILE = '/tmp/warp17.out'

    def __init__(self, out_file = None):
        if out_file is None:
            self.out_file = Warp17OutputArgs.WARP17_DEFAULT_OUTPUT_FILE
        else:
            self.out_file = out_file

def warp17_start(env, exec_file = None, output_args = None):

    if exec_file is None: exec_file = env.get_bin_name()
    if output_args is None: output_args = Warp17OutputArgs()

    args = [exec_file] + string.split(env.get_exec_args().__str__())

    # Should we handle exceptions or let the callers do it for us?
    ofile = open(output_args.out_file, 'w')

    return Popen(args, bufsize=-1, stdout=ofile, stdin=PIPE)

def warp17_wait(env, logger = None):

    if logger is None:
        print_stdout = sys.stdout.write
        print_stderr = sys.stdout.write
    else:
        print_stdout = logger.info
        print_stderr = logger.error

    # Wait until WARP17 comes up
    for i in range(0, env.get_init_retry()):
        try:
            response = warp17_method_call(env.get_host_name(), env.get_rpc_port(),
                                          Warp17_Stub,
                                          'GetVersion',
                                          GetVersionArg(gva_unused=42))
            if response.vr_version is None:
                print_stderr('Unexpected WARP17 version result! Dying..\n')
                sys.exit(1)
            else:
                print_stdout('WARP17 (%(ver)s) started!\n' % \
                             {'ver': response.vr_version})
                return
        except Warp17RpcException, ex:
            print_stdout('WARP17 not up yet. Sleeping for a bit...\n')
            time.sleep(1)

    print_stderr('WARP17 seems to be down!! Dying..\n')
    sys.exit(1)

def warp17_stop(env, proc, force=False):
    # Should we handle exceptions or let the unittest module do it for us?
    proc.stdin.close()
    ret = proc.poll()
    if ret is None:
        if force:
            proc.kill()
            return EXIT_FAILURE
        else:
            return proc.wait()
    else:
        return ret
