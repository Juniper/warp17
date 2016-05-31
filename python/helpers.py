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
#     helpers.py
#
# Description:
#     Some python helpers we use throughout the codebase.
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

import os
import logging

WARP17_LOG_VAR = 'WARP17_LOG'
WARP17_LOG_LVL_VAR = 'WARP17_LOG_LVL'

class LogHelper():

    def __init__(self, name=__name__, filename=None):
        if os.environ.get(WARP17_LOG_VAR, '1') == '1':
            self.l = logging.getLogger(name)

            if not filename is None:
                handler = logging.FileHandler(filename=filename, mode='w')
            else:
                handler = logging.StreamHandler()

            desired_lvl = os.environ.get(WARP17_LOG_LVL_VAR, 'INFO')
            lvl = {
                'DEBUG'     : logging.DEBUG,
                'INFO'      : logging.INFO,
                'WARNING'   : logging.WARNING,
                'ERROR'     : logging.ERROR,
                'CRITICAL'  : logging.CRITICAL
            }.get(desired_lvl)

            handler.setLevel(lvl)
            handler.setFormatter(logging.Formatter(
                '%(asctime)s - %(name)s - %(levelname)s - %(message)s'))
            self.l.addHandler(handler)
            self.l.setLevel(lvl)

        else:
            self.l = None

    def debug(self, msg, *args, **kwargs):
        if self.l is None:
            return
        self.l.debug(msg, *args, **kwargs)

    def info(self, msg, *args, **kwargs):
        if self.l is None:
            return
        self.l.info(msg, *args, **kwargs)

    def warning(self, msg, *args, **kwargs):
        if self.l is None:
            return
        self.l.warning(msg, *args, **kwargs)

    def error(self, msg, *args, **kwargs):
        if self.l is None:
            return
        self.l.error(msg, *args, **kwargs)

    def critical(self, msg, *args, **kwargs):
        if self.l is None:
            return
        self.l.critical(msg, *args, **kwargs)

class Warp17Exception(Exception):
    def __init__(self, msg):
        self.msg = msg

    def __str__(self):
        return str(self.msg)

