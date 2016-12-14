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

    -i, --import    Import the latest source before building.
    -a, --arch      Architecture to produce package for.
                    Defaults to armhf.
EOF
}

mecho () {
    echo "[ * ] ${1}"
}

import_source () {
    mecho "creating latest source tarball..."
    git checkout master
    make dist

    mecho "importing latest source into gbp..."

    # gbp import-orig can't read git-describe style versions so we explicitly
    # save it and pipe it to the prompt
    version="$(git describe | cut -c 2-)"

    git checkout debian
    echo "${version}" | gbp import-orig \
        --import-msg="gbp: imported upstream version %(version)s" \
        --upstream-vcs-tag="$(git rev-parse master)" \
        out/*.tar.xz
}

OPT_IMPORT=false
OPT_ARCH="armhf"

ARGS="$(getopt -o ia:h --long import,arch:,help -n "$0" -- "$@")"
if [ $? != 0 ] ; then
    mecho >&2 "Error parsing options!"
    exit 2
fi

eval set -- "$ARGS"

while true; do
    case "$1" in
        -i|--import) OPT_IMPORT="true"; shift ;;
        -a|--arch) OPT_ARCH="$2"; shift 2 ;;
        -h|--help) help; exit 0 ;;
        --) shift; break ;;
    esac
done

if [ "$OPT_IMPORT" = true ] ; then
    import_source
fi

mecho "building package..."
git checkout debian

# libxi-dev multiarch conflicts in jessie workaround
mecho "running workaround for libxi-dev multiarch conflicts..."
sudo apt-get update && sudo apt-get install -y "libxi-dev:${OPT_ARCH}"

gbp buildpackage \
    --git-ignore-new \
    --git-pristine-tar \
    --git-builder="debuild -us -uc -a ${OPT_ARCH}"

mecho "All tasks completed successfully."
