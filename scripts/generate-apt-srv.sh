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
cp "${1}/"*.deb "${STAGING_DIR}/${RELEASE}"

pushd "${STAGING_DIR}"

mecho "Generating Packages file..."
apt-ftparchive packages testing > testing/Packages
gzip -k testing/Packages

mecho "Generating Release file..."
# don't write Release to testing/ until the end or else Release contains it's
# own hases!
apt-ftparchive release testing > Release
mv Release testing/Release

mecho "Signing Release (please enter passphrase)..."
gpg --default-key C8CC48892A8D0B59F08B40D80C374E742AE862B4 -abs -o testing/Release.gpg testing/Release

popd

mecho "All tasks completed successfully."
