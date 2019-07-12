#!/usr/bin/env python3
#
# This script is responsible for running the RPM build on the running container
#
# Copyright: SPDX-License-Identifier: GPL-3.0-or-later
#
# Author   : Pavlos Emm. Katsoulakis <paul@netdata.cloud>

import common
import os
import sys
import lxc

if len(sys.argv) != 2:
    print('You need to provide a container name to get things started')
    sys.exit(1)
container_name=sys.argv[1]

# Load the container, break if its not there
print("Starting up container %s" % container_name)
container = lxc.Container(container_name)
if not container.defined:
    raise Exception("Container %s does not exist!" % container_name)

# Check if the container is running, attempt to start it up in case its not running
if not container.running or not container.state == "RUNNING":
    print('Container %s is not running, attempt to start it up' % container_name)

    # Start the container
    if not container.start():
        raise Exception("Failed to start the container")

    if not container.running or not container.state == "RUNNING":
        raise Exception('Container %s is not running, configuration process aborted ' % container_name)

# Wait for connectivity
if not container.get_ips(timeout=30):
    raise Exception("Timeout while waiting for container")

print("Setting up EMAIL and DEBFULLNAME variables required by the build tools")
os.environ["EMAIL"] = "bot@netdata.cloud"
os.environ["DEBFULLNAME"] = "Netdata builder"

# Run the build process on the container
new_version = os.environ["BUILD_VERSION"].replace('v', '').replace('.latest', '')
print("Starting DEB build process for version %s" % new_version)

netdata_tarball = "/home/%s/netdata-%s.tar.gz" % (os.environ['BUILDER_NAME'], new_version)
unpacked_netdata = netdata_tarball.replace(".tar.gz", "")

print("Extracting tarball %s" % netdata_tarball)
common.run_command(container, ["sudo", "-u", os.environ['BUILDER_NAME'], "tar", "xf", netdata_tarball, "-C", "/home/%s/" % os.environ['BUILDER_NAME']])

print("Fixing changelog tags")
changelog = "%s/contrib/debian/changelog" % unpacked_netdata
common.run_command(container, ["sudo", "-u", os.environ['BUILDER_NAME'], 'sed', '-i', 's/PREVIOUS_PACKAGE_VERSION/%s/g' % os.environ["LATEST_RELEASE_VERSION"], changelog])
common.run_command(container, ["sudo", "-u", os.environ['BUILDER_NAME'], 'sed', '-i', 's/PREVIOUS_PACKAGE_DATE/%s/g' % os.environ["LATEST_RELEASE_DATE"], changelog])

print("Generating changelog since %s" % os.environ["LATEST_RELEASE_VERSION"])
common.run_command(container, ["sudo", "-u", os.environ['BUILDER_NAME'], "(cd %s && gbp dch --release --spawn-editor=snapshot --since=%s)" % (unpacked_netdata, os.environ["LATEST_RELEASE_VERSION"])])

print("Building the package")
common.run_command(container, ["sudo", "-u", os.environ['BUILDER_NAME'], "(cd %s && dpkg-buildpackage --host-arch amd64 --target-arch amd64 --post-clean --pre-clean --build=binary)" % netdata_tarball.replace(".tar.gz", "")])

print('Done!')
