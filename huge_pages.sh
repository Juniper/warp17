#!/usr/bin/env bash
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
#     huge_pages.sh
#
# Description:
#     Hugepage creator
#
# Author:
#     Matteo Triggiani
#
# Initial Created:
#     07/29/2019
#
# Notes:
#     

# Parse args
source common/common.sh

grub_file="/etc/default/grub"
usage="$0 -n number hugepage -d dest -g grub location"
dest="/mnt/huge_1GB"

n_hugepages=1
while getopts "n:d:g:c" opt; do
    case $opt in
    n)
        n_hugepages=$OPTARG
        ;;
    d)
        dest=$OPTARG
        ;;
    g)
        grub_file=$OPTARG
        ;;
    c)
        dry_run=1
        ;;
    \?)
        usage $0
        ;;
    esac
done

function check_hugepages_mount() {
    if [[ -d $dest ]]; then
        if [[ -n $(mount | grep $dest) ]]; then
            die "$dest folder is already mounted"
        fi
        if [[ -n $(cat /etc/fstab | grep $dest) ]]; then
            die "$dest folder is already in fstab"
        fi
        die "$dest folder is already there"
    fi
    # Checking if we already have the hugepages configured
    check=$(grep HugePages_Total /proc/meminfo | cut -d " " -f 8)
    if [ "$check" != "$n_hugepages" ]; then
        # Checking if we already enforce the paramenter in grub
        grub_file_content=$(grep "hugepages" $grub_file | tr ' ' '\n')
        for line in ${grub_file_content// /$'\n'}; do
            configured=$(echo $line | grep "hugepages=")
            if [[ -n $configured ]]; then
                configured=$(echo $line | grep "hugepages=")
                configured=${configured//\"}
                configured=$(echo $configured | cut -d "=" -f 2)
            fi
        done
        if [[ "$configured" == "$n_hugepages" ]]; then
            die "ATTENTION: hugepages are already configured in grub"
        else
            return $configured
        fi
    else
        die "Hugepages are already mounted"
    fi
}

function set_grub()
{
    if [[ -n $1 ]]; then
        let n_hugepages+=$1
        confirm "You had $1 hugepages, I'm going to configure $n_hugepages"
    fi
    exec_cmd "Backup current grub def config" "cp $grub_file $grub_file.bk"
    dest_cmd_line="GRUB_CMDLINE_LINUX=\\\"default_hugepagesz=1G hugepagesz=1G hugepages=$n_hugepages\\\""
    exec_cmd "Modifing grub def config" "echo $dest_cmd_line >> $grub_file"
    exec_cmd "Rebuilding grub bootloader config" "grub-mkconfig -o /boot/grub/grub.cfg"
}

function set_mounting_point()
{
    fstab_str="nodev $dest hugetlbfs pagesize=1GB 0 0"
    
    exec_cmd "Creating dest folder" "mkdir $dest"
    exec_cmd "Giving fstab configuration" "echo $fstab_str >> /etc/fstab"
    exec_cmd "Temporary mounting dest folder" "mount $dest"
}

check_root
check_hugepages_mount
set_grub $?
set_mounting_point

exit
