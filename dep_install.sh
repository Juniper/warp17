#!/usr/bin/env bash
#
# DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER
#
# Copyright (c) 2019, Juniper Networks, Inc. All rights reserved.
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
#     dep_install.sh
#
# Description:
#     Warp17 dep installation for Ubuntu
#
# Author:
#     Matteo Triggiani
#
# Initial Created:
#     06/13/2019
#
# Notes:
#     Scripts which installs warp17 deps included unsupported protobuf2

# Parse args
source common/common.sh
set -e

workdir="/tmp/deps"
dep_file="Dependencies.zip"
urls=(
    "https://doc-0s-94-docs.googleusercontent.com/docs/securesc/ha0ro937gcuc7l7deffksulhg5h7mbp1/hp3ug5t346k6kn76virmb55ubkk84r3s/1560931200000/09252655687098329908/*/1-DG5b-zLLq3DrjykzxhAPF00qbvbSnts?e=download" 
    "https://doc-0s-94-docs.googleusercontent.com/docs/securesc/ha0ro937gcuc7l7deffksulhg5h7mbp1/e6js7kogp3fu008pptiqv47qob6r4tgr/1560931200000/09252655687098329908/*/1-MHS78RcFdf8y4QjSEOtsjse5Cy9bfim?e=download"
    "https://doc-14-94-docs.googleusercontent.com/docs/securesc/ha0ro937gcuc7l7deffksulhg5h7mbp1/rt0p830ai5fhr99af1usltsmqtl4j48v/1560931200000/09252655687098329908/*/1-DAQK4OJIzLaE4gcUwonmLZApYqzaP3K?e=download"
    "https://doc-08-94-docs.googleusercontent.com/docs/securesc/ha0ro937gcuc7l7deffksulhg5h7mbp1/20pp5lhuc6bdufb5m5ipkkpl4mgra7fi/1560931200000/09252655687098329908/*/1-BK5tI-R2UPT6xf9EXhe35L_uoZQmIZb?e=download"
    "https://doc-08-94-docs.googleusercontent.com/docs/securesc/ha0ro937gcuc7l7deffksulhg5h7mbp1/ommhcoute3ifk9fclmrrikvmkh06906o/1560931200000/09252655687098329908/*/1-Rlc1N8f3YnUmuGuTIalK3g896Mhw_lA?e=download"
    "https://doc-0o-94-docs.googleusercontent.com/docs/securesc/ha0ro937gcuc7l7deffksulhg5h7mbp1/voa0ttn10bpolj13p295fq6ec0jkdpjm/1560931200000/09252655687098329908/*/1-2Qkq9RllXUnZGlc3_rp-3-f4o4FpbBQ?e=download"
    "https://doc-0k-94-docs.googleusercontent.com/docs/securesc/ha0ro937gcuc7l7deffksulhg5h7mbp1/ffuuq0cp7rl26ria8kce5a7opijj195m/1560931200000/09252655687098329908/*/1--pRSs8ixWk6IqCCjVL5uqzfugLaor4x?e=download"
    "https://doc-0k-94-docs.googleusercontent.com/docs/securesc/ha0ro937gcuc7l7deffksulhg5h7mbp1/qec6sa56oqdqfoh77fifb9t4j4t8bpll/1560931200000/09252655687098329908/*/1-T5PrlZ7JbJilwth64EscDOms1W2MScQ?e=download"
    "https://doc-0o-94-docs.googleusercontent.com/docs/securesc/ha0ro937gcuc7l7deffksulhg5h7mbp1/32emp7voo0ksqus39gucrtrov8umlge4/1560931200000/09252655687098329908/*/1-VKIs68HtuygznHN_v4g15xEpCeZTw0u?e=download"
)

# Warp17 and protobuf2 dependencies
function update {
    apt-get update
    apt-get install build-essential libnuma-dev python ncurses-dev zlib1g-dev python-pkg-resources
}

function get_packets {
    cd $workdir
    let a=1
    for i in ${urls[@]}; do
        exec_cmd "Downloading $i" "curl -G $i > $a.deb"
        let a+=1
    done
}

function install {
    update
    exec_cmd "Preparing the working directory" mkdir -p $workdir
    get_packets

    exec_cmd "Installing protobuf2 packets" dpkg -i *.deb
    exec_cmd "Cleaning the working directory" rm -rf $workdir

}

check_root
install
exit
