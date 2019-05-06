#!/usr/bin/env bash
#
# This script generates an LXC container and starts it up
# Once the script completes successfully, a container has become available for usage
# The container image to be used and the container name to be set, are part of variables 
# that must be present for the script to work
#
# Copyright: SPDX-License-Identifier: GPL-3.0-or-later
#
# Author  : Pavlos Emm. Katsoulakis (paul@netdata.cloud)
set -e

# If we are not in netdata git repo, at the top level directory, fail
TOP_LEVEL=$(basename "$(git rev-parse --show-toplevel)")
CWD=$(git rev-parse --show-cdup)
if [ ! -z $CWD ] || [ ! "${TOP_LEVEL}" == "netdata" ]; then
    echo "Run as .travis/package_management/$(basename "$0") from top level directory of netdata git repository"
    echo "Docker build process aborted"
    exit 1
fi

# Check for presence of mandatory environment variables
if [ -z "${BUILD_DISTRO}" ]; then
	echo "No Distribution was defined. Make sure BUILD_DISTRO is set on the environment before running this script"
	exit 1
fi

if [ -z "${BUILD_ARCH}" ]; then
	echo "No container arch was defined. Make sure BUILD_ARCH is set on the environment before running this script"
	exit 1
fi

if [ -z "${BUILDER_NAME}" ]; then
	echo "No builder account and container name defined. Make sure BUILDER_NAME is set on the environment before running this script"
	exit 1
fi

# We got at this point, get the container set up

# Create the container
echo "Creating the container.."
lxc-create -n "${BUILDER_NAME}" -t download -d "${BUILD_DISTRO}" -- -r "${BUILD_RELEASE}" -a "${BUILD_ARCH}"

# Start the container
echo "Starting the container..."
lxc-start --name "${BUILDER_NAME}"

# Create the user account and establish RPM structure
echo "Configuring the container...."
.travis/package_management/configure_lxc_environment.py

echo "..LXC creation complete!"
