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
#     warp17_api.pm
#
# Description:
#     Simple wrapper on top of the python API support.
#
# Author:
#     Dumitru Ceara, Eelco Chaudron
#
# Initial Created:
#     06/13/2016
#
# Notes:
#
#

use Inline Python => <<PY_END;

import sys

sys.path.append('python')
sys.path.append('api/generated/py')

from warp17_api import *

from warp17_common_pb2    import *
from warp17_l3_pb2        import *
from warp17_link_pb2      import *
from warp17_test_case_pb2 import *
from warp17_server_pb2    import *
from warp17_client_pb2    import *
from warp17_app_http_pb2  import *
from warp17_app_raw_pb2   import *
from warp17_test_case_pb2 import *
from warp17_service_pb2   import *

# Wrapper on top of warp17_method_call to avoid weird stuff while getting
# the Warp17_Stub from perl.
def warp17_call(host, port, method_name, arg):
    return warp17_method_call(host, port, Warp17_Stub, method_name, arg)

# Wrapper to be used for determining the length of repeated protobuf fields.
def py_len(lst):
    return len(lst)

# Wrapper to be used for returning the values of protobuf enum fields.
def warp17_enum_val(enum_type, val):
    return eval(enum_type).DESCRIPTOR.values_by_name[val].number

def warp17_infinite():
    return 0xFFFFFFFF

PY_END

package warp17_api;

1;

