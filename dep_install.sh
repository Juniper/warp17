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
url="https://drive.google.com/uc?export=download&id="
id=(
    "1-MHS78RcFdf8y4QjSEOtsjse5Cy9bfim" 
    "1-DG5b-zLLq3DrjykzxhAPF00qbvbSnts"
    "1-DAQK4OJIzLaE4gcUwonmLZApYqzaP3K"
    "1-BK5tI-R2UPT6xf9EXhe35L_uoZQmIZb"
    "1-Rlc1N8f3YnUmuGuTIalK3g896Mhw_lA"
    "1-2Qkq9RllXUnZGlc3_rp-3-f4o4FpbBQ"
    "1--pRSs8ixWk6IqCCjVL5uqzfugLaor4x"
    "1-T5PrlZ7JbJilwth64EscDOms1W2MScQ"
    "1-VKIs68HtuygznHN_v4g15xEpCeZTw0u"
)

files=(
    "libprotobuf-c0-dev.deb"
    "libprotobuf-c0.deb"
    "libprotobuf-dev.deb"
    "libprotobuf-lite8.deb"
    "libprotobuf8.deb"
    "libprotoc8.deb"
    "protobuf-c-compiler.deb"
    "protobuf-compiler.deb"
    "python-protobuf.deb"
)

# Warp17 and protobuf2 dependencies
function dep_install {
    apt install -y build-essential libnuma-dev python ncurses-dev zlib1g-dev python-pkg-resources
}

function get_packets {
    cd $workdir
    let a=1
    for i in ${id[@]}; do
        exec_cmd "Downloading $i" "wget \"$url$i\" -O ${files[a-1]}"
        let a+=1
    done
}

# Freeze packets to prevent autoupdates
function freeze_updates {
    for fullfile in ${files[@]}; do
        filename=$(basename -- "$fullfile")
        extension="${filename##*.}"
        filename="${filename%.*}"
        apt-mark hold $filename
    done
}

function install {
    update
    dep_install
    exec_cmd "Preparing the working directory" mkdir -p $workdir
    get_packets

    exec_cmd "Installing protobuf2 packets" dpkg -i *.deb
    freeze_updates
    exec_cmd "Cleaning the working directory" rm -rf $workdir

}

while getopts "n:i" opt; do
    case $opt in
    n)
        dry_run=1
        ;;
    i)
        interactive=1
        ;;
    \?)
        usage $0
        ;;
    esac
done

check_root
install
exit
