#!/usr/bin/env bash
#
# Entry point script for netdata
#
# Copyright: SPDX-License-Identifier: GPL-3.0-or-later
#
# Author  : Pavlos Emm. Katsoulakis <paul@netdata.cloud>
set -e

if [ ! "${DO_NOT_TRACK:-0}" -eq 0 ] || [ -n "$DO_NOT_TRACK" ]; then
  touch /etc/netdata/.opt-out-from-anonymous-statistics
fi

echo "Netdata entrypoint script starting"
if [ ${RESCRAMBLE+x} ] && [ "$(uname -m)" == "x86_64" ]; then
  echo "Injecting packages from Polymorphic Linux"
  apk update && apk upgrade
  curl https://sh.polyverse.io | sh -s install gcxce5byVQbtRz0iwfGkozZwy support+netdata@polyverse.io
  # shellcheck disable=SC2181
  if [ $? -eq 0 ]; then
    apk update && apk upgrade --available --no-cache && sed -in 's/^#//g' /etc/apk/repositories
  fi
fi

if [ -n "${PGID}" ]; then
  echo "Creating docker group ${PGID}"
  addgroup -g "${PGID}" "docker" || echo >&2 "Could not add group docker with ID ${PGID}, its already there probably"
  echo "Assign netdata user to docker group ${PGID}"
  usermod -a -G "${PGID}" "${DOCKER_USR}" || echo >&2 "Could not add netdata user to group docker with ID ${PGID}"
fi

exec /usr/sbin/netdata -u "${DOCKER_USR}" -D -s /host -p "${NETDATA_PORT}" -W set web "web files group" root -W set web "web files owner" root "$@"

echo "Netdata entrypoint script, completed!"
