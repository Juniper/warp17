#
# DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER
#
# Copyright (c) 2017, Juniper Networks, Inc. All rights reserved.
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
#     test_input.py
#
# Description:
#     Tests for the WARP17 input.
#
# Author:
#     Matteo Triggiani
#
# Initial Created:
#     10/04/2017
#
# Notes:
#
#

import sys
import os

sys.path.append('./lib')
sys.path.append('../python')
sys.path.append('../api/generated/py')

import warp17_api

from warp17_ut import Warp17BaseUnitTestCase
from warp17_ut import warp17_start
from warp17_ut import warp17_stop

UINT32MAX = 0xFFFFFFFF

class Warp17InputTestCase(Warp17BaseUnitTestCase):

    def _test_invalid_input(self, tname, key, value):
        Warp17BaseUnitTestCase.env.set_value(key, value)

        warp17_proc = warp17_start(env=Warp17BaseUnitTestCase.env,
                                   output_args=Warp17BaseUnitTestCase.oargs)

        ret = warp17_stop(Warp17BaseUnitTestCase.env, warp17_proc)
        self.assertEqual(ret, warp17_api.EXIT_FAILURE,
                         'Error in {0}:"{1}" occurred\n'.format(tname, ret))

    def test_tcb_pool_sz(self):
        """Test 'tcb-pool-sz' input, its maximum value has to be uint32max / 1024"""
        self._test_invalid_input('test_tcb_pool_sz', 'tcb-pool-sz',
                                 UINT32MAX / 1024 + 1)
        self._test_invalid_input('test_tcb_pool_sz-nonum', 'tcb-pool-sz', '1X')

    def test_ucb_pool_sz(self):
        """Test 'ucb-pool-sz' input, its maximum value has to be uint32max / 1024"""
        self._test_invalid_input('test_ucb_pool_sz', 'ucb-pool-sz',
                                 UINT32MAX / 1024 + 1)
        self._test_invalid_input('test_ucb_pool_sz-nonum', 'ucb-pool-sz', '1X')

    def test_mbuf_sz(self):
        """Test 'mbuf-sz' input, its maximum value has to be PORT_MAX_MTU"""
        """and the minimum GCFG_MBUF_SIZE"""
        PORT_MAX_MTU = 9198

        #TODO: reading RTE_PKTMBUF_HEADROOM from dpdk config
        RTE_PKTMBUF_HEADROOM = 128
        GCFG_MBUF_SIZE = 2048 + RTE_PKTMBUF_HEADROOM

        # if (mbuf_size > GCFG_MBUF_SIZE && ) {

        # Case one: mbuf_size < PORT_MAX_MTU #
        self._test_invalid_input('test_mbuf_sz-max', 'mbuf-sz',
                                 PORT_MAX_MTU + 1)

        # Case two: mbuf_size > GCFG_MBUF_SIZE #
        self._test_invalid_input('test_mbuf_sz-min', 'mbuf-sz',
                                 GCFG_MBUF_SIZE - 1)

    def test_mbuf_pool_sz(self):
        """Test 'mbuf-pool-sz' input, its maximum value has to be uint32max / 1024"""
        self._test_invalid_input('test_mbuf_pool_sz', 'mbuf-pool-sz',
                                 UINT32MAX / 1024 + 1)
        self._test_invalid_input('test_mbuf_pool_sz-nonum', 'mbuf-pool-sz',
                                 '1X')

    def test_mbuf_hdr_pool_sz(self):
        """Test 'mbuf-hdr-pool-sz' input, its maximum value has to be uint32max / 1024"""
        self._test_invalid_input('test_mbuf_hdr_pool_sz', 'mbuf-hdr-pool-sz',
                                 UINT32MAX / 1024 + 1)
        self._test_invalid_input('test_mbuf_hdr_pool_sz-nonum',
                                 'mbuf-hdr-pool-sz', '1X')

    def tearDown(self):
        """For each tests we need to clean the enviroment"""
        Warp17BaseUnitTestCase.cleanEnv()

