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
#     warp17_ut.py
#
# Description:
#     WARP17 UT related wrappers and base definitions.
#
# Author:
#     Dumitru Ceara, Eelco Chaudron
#
# Initial Created:
#     02/11/2016
#
# Notes:
#
#

import os
import sys
import unittest

from functools import partial

sys.path.append('../../python')
sys.path.append('../../api/generated/py')

from helpers  import LogHelper
from warp17_api import *

# WARP17 Unit Test base class. Sets up the common variables.
class Warp17UnitTestCase(unittest.TestCase):

    @classmethod
    def setUpClass(cls):
        env = Warp17Env()
        tstamp = env.get_uniq_stamp()
        dirpath = '/tmp/warp17-test-'+ tstamp

        if not os.path.exists(dirpath):
            os.mkdir(dirpath)

        print "Logs and outputs in " + dirpath

        out_file = dirpath + '/' + cls.__name__ + '.out'
        log_file = dirpath + '/' + cls.__name__ + '.log'

        oargs = Warp17OutputArgs(out_file)
        Warp17UnitTestCase.lh = LogHelper(name=cls.__name__, filename=log_file)

        Warp17UnitTestCase.env = env

        Warp17UnitTestCase.warp17_call = partial(warp17_method_call,
                                                 env.get_host_name(),
                                                 env.get_rpc_port(),
                                                 Warp17_Stub)

        Warp17UnitTestCase.warp17_proc = warp17_start(env=env, output_args=oargs)
        # Wait until WARP17 actually starts.
        warp17_wait(env=env, logger=Warp17UnitTestCase.lh)
        # Detailed error messages
        Warp17UnitTestCase.longMessage = True

    @classmethod
    def tearDownClass(cls):
        warp17_stop(Warp17UnitTestCase.env, Warp17UnitTestCase.warp17_proc)

