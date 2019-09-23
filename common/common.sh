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
#     common.sh
#
# Description:
#     Library for common bash functions
#
# Author:
#     Matteo Triggiani
#
# Initial Created:
#     06/22/2018
#
# Notes:
#

# Check for root permissions
function check_root {
    if [ "$EUID" -ne 0 ]
      then echo "Please run as root"
      exit
    fi
}

# This function ask for confirmation
function confirm {
    echo
    echo "$1"
    echo "do you want to run this command? [y/N]"
    read ans
    if [[ $ans == "y" ]]; then
        return 0
    else
        die "Leaving"
    fi
}

# This function takes a comment to print as first argument and than eval the
#   other arguments
function exec_cmd {
    echo
    echo "$1"; shift
    echo "$@"
    if [[ -z $dry_run ]]; then
        eval "$@"
        if [[ $? != 0 ]]; then
            die "Exit code:_ $?"
        else
            return 0
        fi
    fi
}

# This function prints an exit message in stderr and exit
function die {
    echo $@ >&2
    exit 1
}

# This function prints the usage message and exit
function usage {
    die ${usage}
    exit 1
}

# Update ubuntu
function update {
    if [[ -z $interactive ]]; then
        confirm "Do you want to upgrade you packages?"
    fi
    apt update
    apt upgrade -y
}

# Debug print function that adds a counter
#   You can also provide your message at the end
function count_debug_print() {
    let i=i+1
    echo -e "I'm $i \t $@"
}
