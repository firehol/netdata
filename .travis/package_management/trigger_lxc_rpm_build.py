#!/usr/bin/python3
#
# This script is responsible for running the RPM build on the running container
#
#
# Copyright: SPDX-License-Identifier: GPL-3.0-or-later
#
# Author  : Pavlos Emm. Katsoulakis (paul@netdata.cloud)

import os
import sys
import lxc

print (sys.argv)
if len(sys.argv) != 2:
    print ('You need to provide a container name to get things started')
    sys.exit(1)
container_name=sys.argv[1]

# Load the container, break if its not there
container = lxc.Container(container_name)
if not c.defined:
    raise Exception("Container %s does not exist!" % container_name)

# Check if the container is running, attempt to start it up in case its not running
if not container.running or not container.state == "RUNNING":
    print ('Container %s is not running, attempt to start it up' % container_name)

    # Start the container
    if not container.start():
        raise Exception("Failed to start the container")

    if not container.running or not container.state == "RUNNING":
        raise Exception('Container %s is not running, configuration process aborted ' % container_name)

# Wait for connectivity
if not container.get_ips(timeout=30):
    raise Exception("Timeout while waiting for container")

# Run the build process on the container
container.attach_wait(lxc.attach_run_command, ["sudo", "-u", os.environ['BUILDER_NAME'], "rpmbuild", "-ba", "--rebuild", "rpmbuild/SPECS/netdata.spec"])

print ('Done!')
