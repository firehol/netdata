#!/usr/bin/env bash
# Coverity installation script
#
# Copyright: SPDX-License-Identifier: GPL-3.0-or-later
#
# Author: Pavlos Emm. Katsoulakis (paul@netdata.cloud)

token="${COVERITY_SCAN_TOKEN}"
([ -z "${token}" ] && [ -f .coverity-token ]) && token="$(<.coverity-token)"
if [ -z "${token}" ]; then
	echo >&2 "Save the coverity token to .coverity-token or export it as COVERITY_SCAN_TOKEN."
	exit 1
fi

covbuild="$(which cov-build 2>/dev/null || command -v cov-build 2>/dev/null)"
([ -z "${covbuild}" ] && [ -f .coverity-build ]) && covbuild="$(<.coverity-build)"
if [ ! -z "${covbuild}" ]; then
	echo >&2 "Coverity already installed, nothing to do!"
	exit 0
fi

echo >&2 "Installing coverity..."
WORKDIR=$(mktemp -d)

curl -SL --data "token=${token}&project=netdata%2Fnetdata" https://scan.coverity.com/download/linux64 > "${WORKDIR}/coverity_tool.tar.gz"
tar -x -C "${WORKDIR}/coverity-install" -f "${WORKDIR}/coverity_tool.tar.gz"
sudo mv "${WORKDIR}/coverity-install/cov-analysis-linux64-2017.07" /opt/coverity
export PATH=${PATH}:/opt/coverity/bin/

covbuild="$(which cov-build 2>/dev/null || command -v cov-build 2>/dev/null)"

echo >&2 "Coverity scan installed!"
