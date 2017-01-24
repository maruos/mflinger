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

readonly STAGING_DIR="apt-srv-root"
readonly RELEASE="testing"

help () {
    cat <<EOF
Automated script for publishing mflinger apt packages

Usage: $(basename "$0") [PATH TO PACKAGE BUILD DIR]
EOF
}

mecho () {
    echo "[*] ${1}"
}

if [ ! -d "$1" ] ; then
    mecho >&2 "Please specify package build directory!"
    help
    exit 2
fi

mecho "Creating server staging area..."
mkdir -p "${STAGING_DIR}/${RELEASE}"
cp "${1}/"* "${STAGING_DIR}/${RELEASE}"

mecho "Generating Packages.gz..."
pushd "${STAGING_DIR}"
dpkg-scanpackages -m testing /dev/null | gzip -9c > testing/Packages.gz
popd

mecho "All tasks completed successfully."
