#!/bin/bash
#
# Script to automatically do a couple of things:
#   - generate a new tag according to semver (https://semver.org/)
#   - generate CHANGELOG.md by using https://github.com/skywinder/github-changelog-generator
#
# Tags are generated by searching for a keyword in last commit message. Keywords are:
#  - [patch] or [fix] to bump patch number
#  - [minor], [feature] or [feat] to bump minor number
#  - [major] or [breaking change] to bump major number
# All keywords MUST be surrounded with square braces.
#
# Script uses git mechanisms for locking, so it can be used in parallel builds
#
# Requirements:
#   - GITHUB_TOKEN variable set with GitHub token. Access level: repo.public_repo
#   - docker
#
# This is a modified version of:
# https://github.com/paulfantom/travis-helper/blob/master/releasing/releaser.sh
#
# Copyright: SPDX-License-Identifier: GPL-3.0-or-later
#
# Author: Pavlos Emm. Katsoulakis <paul@netdata.cloud>
# Author: Pawel Krupa (@paulfantom)
set -e

if [ ! -f .gitignore ]; then
	echo "Run as ./travis/$(basename "$0") from top level directory of git repository"
	exit 1
fi

echo "--- Changelog generator script starting ---"
# If we dont have a produced TAG there is nothing to do, so bail out happy
if [ -z "${GIT_TAG}" ]; then
	echo "GIT_TAG is empty, that is not suppose to happen (Value: $GIT_TAG)"
	exit 1
fi

if [ ! "${TRAVIS_REPO_SLUG}" == "netdata/netdata" ]; then
	echo "Beta mode on ${TRAVIS_REPO_SLUG}, nothing to do on the changelog generator and tagging script for (${GIT_TAG}), bye"
	exit 0
fi

echo "--- Initialize git configuration ---"
export GIT_MAIL="bot@netdata.cloud"
export GIT_USER="netdatabot"
git checkout master
git pull

echo "---- UPDATE VERSION FILE ----"
echo "$GIT_TAG" >packaging/version
git add packaging/version

echo "---- Create CHANGELOG -----"
./.travis/create_changelog.sh
git add CHANGELOG.md

echo "---- COMMIT AND PUSH CHANGES ----"
git commit -m "[ci skip] release $GIT_TAG" --author "${GIT_USER} <${GIT_MAIL}>"
git tag "$GIT_TAG" -a -m "Automatic tag generation for travis build no. $TRAVIS_BUILD_NUMBER"
git push "https://${GITHUB_TOKEN}:@$(git config --get remote.origin.url | sed -e 's/^https:\/\///')"
git push "https://${GITHUB_TOKEN}:@$(git config --get remote.origin.url | sed -e 's/^https:\/\///')" --tags
# After those operations output of command `git describe` should be identical with a value of GIT_TAG
