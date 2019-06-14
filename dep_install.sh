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
url="https://storage.googleapis.com/drive-bulk-export-anonymous/20190614T094635Z/4133399871716478688/10ab227c-821c-4712-9c76-c2305cb0be1c/1/69dd0249-c12c-4d64-86ce-93c1a8f714c1?authuser"

# Warp17 and protobuf2 dependencies
function update {
    apt-get update
    apt-get install build-essential libnuma-dev python ncurses-dev zlib1g-dev python-pkg-resources
}

function get_packets {
    cd $workdir
    curl -G $url > $dep_file
    unzip $dep_file
}

function install {
    update
    exec_cmd "Preparing the working directory" mkdir -p $workdir
    get_packets

    cd Dependencies
    exec_cmd "Installing protobuf2 packets" dpkg -i *.deb
    exec_cmd "Cleaning the working directory" rm -rf $workdir

}

check_root
install
exit
