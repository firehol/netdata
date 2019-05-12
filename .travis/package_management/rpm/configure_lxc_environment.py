#!/usr/bin/env python3
#
# Prepare the build environment within the container
# The script attaches to the running container and does the following:
# 1) Create the container
# 2) Start the container up
# 3) Create the builder user
# 4) Prepare the environment for RPM build
#
# Copyright: SPDX-License-Identifier: GPL-3.0-or-later
#
# Author  : Pavlos Emm. Katsoulakis (paul@netdata.cloud)

import os
import sys
import lxc

def run_command(command):
    print ("Running command: %s" % command)
    command_result = container.attach_wait(lxc.attach_run_command, command)

    if command_result != 0:
        raise Exception("Command failed with exit code %d" % command_result)

if len(sys.argv) != 2:
    print ('You need to provide a container name to get things started')
    sys.exit(1)
container_name=sys.argv[1]

# Setup the container object
print ("Defining container %s" % container_name)
container = lxc.Container(container_name)
if container.defined:
    raise Exception("Container %s already exists" % container_name)

# Create the container rootfs
print ("Creating container with parameters: %s, %s, %s " % (os.environ["BUILD_DISTRO"], os.environ["BUILD_RELEASE"], os.environ["BUILD_ARCH"]))
if not container.create("ubuntu", lxc.LXC_CREATE_QUIET, {"dist": os.environ["BUILD_DISTRO"],
                                                           "release": os.environ["BUILD_RELEASE"],
                                                           "arch": os.environ["BUILD_ARCH"]}):
    raise Exception("Failed to create the container rootfs")
print ("Container %s was successfully created, starting it up" % container_name)

# Start the container
if not container.start():
    raise Exception("Failed to start the container")

if not container.running or not container.state == "RUNNING":
    raise Exception('Container %s is not running, configuration process aborted ' % container_name)

# Wait for connectivity
print ("Waiting for container connectivity to start configuration sequence")
if not container.get_ips(timeout=30):
    raise Exception("Timeout while waiting for container")

# Run the required activities now
# Create the builder user
print ("1. Adding user %s" % os.environ['BUILDER_NAME'])
run_command(["useradd", os.environ['BUILDER_NAME']])

# Fetch wget, sudo and rpm-build within the container
print ("2. Installing package dependencies within LXC container")
run_command(["yum", "install", "-y", "wget"])
run_command(["yum", "install", "-y", "sudo"])
run_command(["yum", "install", "-y", "rpm-build"])

print ("3. Setting up macros")
run_command(["sudo", "-u", os.environ['BUILDER_NAME'], "/bin/echo", "'%_topdir %(echo /home/" + os.environ['BUILDER_NAME'] + ")/rpmbuild' > /home/" + os.environ['BUILDER_NAME'] + "/.rpmmacros"])

print ("4. Create rpmbuild directory")
run_command(["sudo", "-u", os.environ['BUILDER_NAME'], "mkdir", "-p", "/home/" + os.environ['BUILDER_NAME'] + "/rpmbuild/BUILD"])
run_command(["sudo", "-u", os.environ['BUILDER_NAME'], "mkdir", "-p", "/home/" + os.environ['BUILDER_NAME'] + "/rpmbuild/RPMS"])
run_command(["sudo", "-u", os.environ['BUILDER_NAME'], "mkdir", "-p", "/home/" + os.environ['BUILDER_NAME'] + "/rpmbuild/SOURCES"])
run_command(["sudo", "-u", os.environ['BUILDER_NAME'], "mkdir", "-p", "/home/" + os.environ['BUILDER_NAME'] + "/rpmbuild/SPECS"])
run_command(["sudo", "-u", os.environ['BUILDER_NAME'], "mkdir", "-p", "/home/" + os.environ['BUILDER_NAME'] + "/rpmbuild/SRPMS"])
run_command(["sudo", "-u", os.environ['BUILDER_NAME'], "ls", "-ltrR", "/home/" + os.environ['BUILDER_NAME'] + "/rpmbuild"])

# Download the source
dest_archive="/home/%s/rpmbuild/SOURCES/netdata-%s.tar.gz" % (os.environ['BUILDER_NAME'],os.environ['BUILD_VERSION'])
release_url="https://github.com/netdata/netdata/releases/download/%s/netdata-%s.tar.gz" % (os.environ['BUILD_VERSION'], os.environ['BUILD_VERSION'])
print ("5. Fetch netdata source into the repo structure(%s -> %s)" % (release_url, dest_archive))
run_command(["sudo", "-u", os.environ['BUILDER_NAME'], "wget", "--output-document=" + dest_archive, release_url])

# Extract the spec file in place
print ("6. Extract spec file from the source")
spec_file="/home/%s/rpmbuild/SPECS/netdata.spec" % os.environ['BUILDER_NAME']
run_command(["sudo", "-u", os.environ['BUILDER_NAME'], "tar", "--to-command=cat > %s" % spec_file, "-xvf", dest_archive, "netdata-%s/netdata.spec" % os.environ['BUILD_VERSION']])

print ('Done!')
