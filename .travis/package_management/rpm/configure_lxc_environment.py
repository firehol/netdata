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
# Author   : Pavlos Emm. Katsoulakis <paul@netdata.cloud>

import os
import sys
import lxc

def replace_tag(tag_name, spec, new_tag_content):
    print ("Fixing tag %s in %s" % (tag_name, spec))

    ifp = open(os.environ['LXC_CONTAINER_ROOT'] + spec, "r")
    config = ifp.readlines()
    config_str = ''.join(config)
    ifp.close()

    source_line = ""
    for line in config:
        if str(line).count(tag_name) > 0:
            source_line = line.replace('\n','')
            print ("Found line: %s" % source_line)
            break

    if len(source_line) > 0:
        print ("Replacing line %s with %s in spec file" %(source_line, new_tag_content))

        config_str.replace(source_line, "%s:\t%s" % (tag_name, new_tag_content))
        ofp = open(os.environ['LXC_CONTAINER_ROOT'] + spec, 'w')
        ofp.write(config_str)
        ofp.close()

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
if not container.defined:
    raise Exception("Container %s not defined!" % container_name)

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
run_command(["useradd", "-m", os.environ['BUILDER_NAME']])

# Fetch package dependencies for the build
print ("2. Installing package dependencies within LXC container")
if str(os.environ["REPO_TOOL"]).count("zypper") == 1:
    run_command([os.environ["REPO_TOOL"], "clean", "-a"])
    run_command([os.environ["REPO_TOOL"], "--no-gpg-checks", "update", "-y"])
else:
    run_command([os.environ["REPO_TOOL"], "update", "-y"])

run_command([os.environ["REPO_TOOL"], "install", "-y", "sudo"])
run_command([os.environ["REPO_TOOL"], "install", "-y", "wget"])
run_command([os.environ["REPO_TOOL"], "install", "-y", "bash"])
run_command(["wget", "-T", "15", "-O", "/home/%s/.install-required-packages.sh" % (os.environ['BUILDER_NAME']), "https://raw.githubusercontent.com/netdata/netdata-demo-site/master/install-required-packages.sh"])
run_command(["bash", "/home/%s/.install-required-packages.sh" % (os.environ['BUILDER_NAME']), "netdata", "--dont-wait", "--non-interactive"])

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
version_list=str(os.environ['BUILD_VERSION']).split('.')
rpm_friendly_version='.'.join(version_list[0:3]) + version_list[3]
dest_archive=""
download_url=""
# TODO: Checksum validations
if str(os.environ['BUILD_VERSION']).count(".latest") == 1:
    print ("Building latest nightly version of netdata..(%s)" % os.environ['BUILD_VERSION'])
    dest_archive="/home/%s/rpmbuild/SOURCES/netdata-%s.tar.gz" % (os.environ['BUILDER_NAME'], rpm_friendly_version)
    download_url="https://storage.googleapis.com/netdata-nightlies/netdata-latest.tar.gz"
else:
    print ("Building latest stable version of netdata.. (%s)" % os.environ['BUILD_VERSION'])
    dest_archive="/home/%s/rpmbuild/SOURCES/netdata-%s.tar.gz" % (os.environ['BUILDER_NAME'],os.environ['BUILD_VERSION'])
    download_url="https://github.com/netdata/netdata/releases/download/%s/netdata-%s.tar.gz" % (os.environ['BUILD_VERSION'], os.environ['BUILD_VERSION'])

print ("5. Fetch netdata source into the repo structure(%s -> %s)" % (download_url, dest_archive))
run_command(["sudo", "-u", os.environ['BUILDER_NAME'], "wget", "-T", "15", "--output-document=" + dest_archive, download_url])

# Extract the spec file in place
print ("6. Extract spec file from the source")

spec_file="/home/%s/rpmbuild/SPECS/netdata.spec" % os.environ['BUILDER_NAME']
run_command(["sudo", "-u", os.environ['BUILDER_NAME'], "tar", "--to-command=cat > %s" % spec_file, "-xvf", dest_archive, "netdata-*/netdata.spec.in"])

print ("7. Temporary hack: Adjust version string on the spec file to %s and Source0 to %s" % (rpm_friendly_version, download_url))
replace_tag("Version", spec_file, rpm_friendly_version)
replace_tag("Source0", spec_file, download_url)

print ('Done!')
