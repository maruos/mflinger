#!/bin/bash

#
# Copyright 2016 The Maru OS Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#    http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

set -e

help () {
    cat <<EOF
Automated script for packaging mflinger

Usage: build-package.sh [OPTIONS]

    -a, --arch      Architecture to produce package for.
                    Defaults to armhf.
EOF
}

mecho () {
    echo "[*] ${1}"
}

OPT_ARCH="armhf"

ARGS="$(getopt -o a:h --long arch:,help -n "$0" -- "$@")"
if [ $? != 0 ] ; then
    mecho >&2 "Error parsing options!"
    exit 2
fi

eval set -- "$ARGS"

while true; do
    case "$1" in
        -a|--arch) OPT_ARCH="$2"; shift 2 ;;
        -h|--help) help; exit 0 ;;
        --) shift; break ;;
    esac
done

mecho "building package..."
git checkout debian

# libxi-dev multiarch conflicts in jessie workaround
mecho "running workaround for libxi-dev multiarch conflicts..."
sudo apt-get update && sudo apt-get install -y "libxi-dev:${OPT_ARCH}"

# TODO: move scripts/ somewhere else so source packaging works
gbp buildpackage \
    --git-ignore-new \
    --git-pristine-tar \
    --git-builder="debuild -us -uc -b -a ${OPT_ARCH}"

mecho "All tasks completed successfully."
