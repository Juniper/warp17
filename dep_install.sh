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

# Warp17 and protobuf2 dependencies.
function dep_install {
    apt install -y build-essential libnuma-dev ncurses-dev libtool autoconf \
        automake python3-pkg-resources python3-netaddr \
        python3-pip python3-setuptools \
        libprotobuf-c-dev libprotobuf-c1 protobuf-c-compiler \
        libprotobuf-dev protobuf-compiler
}

# Install protobuf-c-rpc from sources.
function install_protobuf_c_rpc {
    exec_cmd "Preparing the working directory" mkdir -p $workdir

    pushd $workdir
    git clone https://github.com/protobuf-c/protobuf-c-rpc
    pushd protobuf-c-rpc
    ./autogen.sh && ./configure --libdir=/usr/lib/x86_64-linux-gnu/ && make && make install
    popd
    popd

    exec_cmd "Cleaning the working directory" rm -rf $workdir
}

function install {
    update
    dep_install
    install_protobuf_c_rpc
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
