#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later

. $(dirname "${0}")/../functions.sh "${@}" || exit 1

fetch "netdata-ioping-43d15a5" "https://github.com/netdata/ioping/tarball/master"

export CFLAGS="-static"

run make clean
run make -j$(find_processors)
run mkdir -p ${NETDATA_INSTALL_PATH}/bin/
run install -m 0755 ioping ${NETDATA_INSTALL_PATH}/bin/

if [ ${NETDATA_BUILD_WITH_DEBUG} -eq 0 ]
then
    run strip ${NETDATA_INSTALL_PATH}/bin/ioping
fi
