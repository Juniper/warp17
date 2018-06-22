#
# DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER
#
# Copyright (c) 2018, Juniper Networks, Inc. All rights reserved.
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
#     build_dpdk.sh
#
# Description:
#     Dpdk builder and configuration
#
# Author:
#     Matteo Triggiani
#
# Initial Created:
#     06/22/2018
#
# Notes:
#     Intake dpdk version as argument "xx.xx.x"

ver="$1"
dest="/opt"
tmp="/tmp"
name="dpdk-$ver"
file="$name.tar.xz"
url="http://fast.dpdk.org/rel/$file"
kernel=`uname -r`

# Getting dpdk from the repo
# $1 destination folder
# $2 temporary folder
function get {
    cd $2
    wget $url
    mkdir $name
    tar -xaf $file -C $name --strip-components=1
    mv -f $name $1
}

# Build dpdk
# $1 dpdk folder
# $2 compiler version
function build {
    cd $1
    make T=$2 install
}

# Install dpdk in the local machine
# $1 dpdk folder
function install {
    cp $1/x86_64-native-linuxapp-gcc/kmod/igb_uio.ko /lib/modules/$kernel/kernel/drivers/uio/
    if [ -d "/lib/modules/$kernel/kernel/drivers/uio/igb_uio.ko" ]; then
        return 2
    fi
    depmod -a
    modprobe uio
    modprobe igb_uio
}

# Skipping in case dpdk is already there
if [[ -d "$dest/$name/build" ]]; then

    echo dpdk-$ver is already there
    exit

else

    rm -rf $dest/$name
    get $dest $tmp
    build "$dest/$name" x86_64-native-linuxapp-gcc
    install "$dest/$name"
    exit

fi
