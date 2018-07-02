#!/usr/bin/env bash
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
#     release.sh
#
# Description:
#     Bumps the version number and tags the release
#
# Author:
#     Dumitru Ceara, Eelco Chaudron
#
# Initial Created:
#     02/17/2016
#
# Notes:
#     Poor man's solution for now..
#

vfile=VERSION
usage="$1 -t major|minor -o <remote_name> -d \"<version_description>\" -p <perf_result_log_path>"

source common/common.sh

function build_version {
    major=$1
    minor=$2
    ver=$major.$minor
}

function check_master {
    [ "$(git name-rev --name-only HEAD)" != "master" ] &&
        die "Please run only on the master branch!"
}

function check_no_commits {
    local local_rev=$(git rev-parse @)
    local remote_rev=$(git rev-parse @{u})
    local base_rev=$(git merge-base @ @{u})

    [ $local_rev = $remote_rev ] && return 1
    [ $local_rev = $base_rev ] && die "Please first pull all remote changes!"
    [ $remote_rev = $base_rev ] && die "Please first push all local changes!"
    die "Branch Diverged!"
}

function check_perf_file_version {
    local perf_file=$1
    local major=$2
    local minor=$3

    # stores the version in $ver
    build_version $major $minor

    local count=$(grep -c "warp17 v$ver" $perf_file)
    [ $count != 1 ] &&
    die "Expecting version $ver! Please provide results from the current version!"
}

function check_release_notes {
    local rnfile=$1
    local ver=$2

    local count=$(head -n 1 $rnfile | grep -c "v$ver:\$")
    [ $count != 1 ] &&
    die "Missing release notes for version $ver!"
}

function inc_version {
    type=$1
    major=$2
    minor=$3
    case $type in
    "major")
        minor=0
        major=$(($major + 1))
        ;;
    "minor")
        minor=$(($minor + 1))
        ;;
    *)
        usage $0
        ;;
    esac

    # stores the version in $ver
    build_version $major $minor
}

type=
remote=
descr=
perf_file=

# Parse args
while getopts "t:o:d:p:n" opt; do
    case $opt in
    t)
        type=$OPTARG
        ;;
    o)
        remote=$OPTARG
        ;;
    d)
        descr=$OPTARG
        ;;
    p)
        perf_file=$OPTARG
        ;;
    n)
        dry_run=1
        ;;
    \?)
        usage $0
        ;;
    esac
done

# Check mandatory args
([[ -z $type ]] || [[ -z $descr ]] || [[ -z $perf_file ]] || [[ -z $remote ]]) &&
usage $0

# Check that we're on the master branch and that there are no pending commits
# or changes.
check_master
check_no_commits

major=$(cat $vfile | cut -d '.' -f 1)
minor=$(cat $vfile | cut -d '.' -f 2)

# Check that the supplied performance results are from the current version.
check_perf_file_version $perf_file $major $minor

# Stores the version in $ver
inc_version $type $major $minor

rnfile=RELEASE_NOTES

# Check that the relase notes are up to date (matching the new version).
check_release_notes $rnfile $ver

tag_name=v$ver
perf_results=$(grep -E 'test_|Average' $perf_file)
commit_msg=$(printf "Bump version to $ver\n%s\n" "$perf_results")

bmark_out="./benchmarks"
bmark_file="$bmark_out/benchmark.csv"
bmark_plot_gp=$bmark_out/benchmark_plot.gp
bmark_tcp_cnt=$(grep 'TCP' $bmark_file | wc -l)
bmark_udp_cnt=$(grep 'UDP' $bmark_file | wc -l)
bmark_http_cnt=$(grep 'HTTP' $bmark_file | wc -l)

echo "THIS WILL BUMP THE VERSION TO $ver!"
echo "A NEW TAG ($tag_name) WILL BE CREATED!"
read -p "Are you sure you wish to continue? [Y/N]" -n 1 yn
echo
case $yn in
[Yy]* )
    ;;
* )
    echo "Exiting";
    exit 1
    ;;
esac

exec_cmd "Updating version to: $ver" echo $ver > $vfile

# Generate the .png charts to be used by the release documentation.
exec_cmd "Generating .png perf charts" gnuplot -e filename=$bmark_file -e \
    tcp=$bmark_tcp_cnt -e udp=$bmark_udp_cnt -e http=$bmark_http_cnt \
    -e out_dir=$bmark_out $bmark_plot_gp

# Now commit everything and tag the release
exec_cmd "Add the generated .png perf charts" git add $bmark_out/*.png
exec_cmd "Commit the new version" git commit -m $commit_msg -- \
    $vfile $bmark_out/*.png
exec_cmd "Tag the new release" git tag -a $tag_name -m $ver - $type - $descr
exec_cmd "Push release to master" git push $remote master
exec_cmd "Push tags" git push --tags

