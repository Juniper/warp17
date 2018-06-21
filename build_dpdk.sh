#!/usr/bin/env bash
# Dpdk builder and configuration
# Intake dpdk version as argument "xx.xx.x"

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

# skipping in case dpdk is already there
if [[ -d "$dest/$name/build" ]]; then

    echo dpdk $ver is already there
    ls $dest/$name
    exit

else

    rm -rf $dest/$name
    get $dest $tmp
    build "$dest/$name" x86_64-native-linuxapp-gcc
    install "$dest/$name"
    exit

fi
