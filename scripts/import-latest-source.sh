#!/bin/bash

#
# Copyright 2017 The Maru OS Project
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
Automated script for importing the latest source from mflinger

Usage: $(basename "$0") [OPTIONS]

    -c, --changelog    Create a changelog entry with "gbp dch --snapshot --auto"
EOF
}

mecho () {
    echo "[*] ${1}"
}

OPT_CHANGELOG=false

ARGS="$(getopt -o ch --long changelog,help -n "$0" -- "$@")"
if [ $? != 0 ] ; then
    mecho >&2 "Error parsing options!"
    exit 2
fi

eval set -- "$ARGS"

while true; do
    case "$1" in
        -c|--changelog) OPT_IMPORT=true; shift ;;
        -h|--help) help; exit 0 ;;
        --) shift; break ;;
    esac
done

mecho "making sure all needed branches are checked out locally..."
git checkout gbp/upstream

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


if [ "$OPT_CHANGELOG" = true ] ; then
    mecho "creating temporary snapshot changelog entry..."
    gbp dch --snapshot --auto
fi

mecho "All tasks completed successfully."
