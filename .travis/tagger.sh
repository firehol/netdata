#!/bin/bash
# SPDX-License-Identifier: MIT
# Copyright (C) 2018 Pawel Krupa (@paulfantom) - All Rights Reserved
# Permission to copy and modify is granted under the MIT license
#
# Original script is available at https://github.com/paulfantom/travis-helper/blob/master/releasing/releaser.sh
#
# Tags are generated by searching for a keyword in last commit message. Keywords are:
#  - [patch] or [fix] to bump patch number
#  - [minor], [feature] or [feat] to bump minor number
#  - [major] or [breaking change] to bump major number
# All keywords MUST be surrounded with square braces.
#
# Requirements:
#   - GITHUB_TOKEN variable set with GitHub token. Access level: repo.public_repo
#   - git-semver python package (pip install git-semver)

# exported variables are needed by releaser.sh

set -e

if [ ! -f .gitignore ]; then
	echo "Run as ./travis/$(basename "$0") from top level directory of git repository"
	exit 1
fi

# Figure out what will be new release candidate tag based only on previous ones.
# This assumes that RELEASES are in format of "v0.1.2" and prereleases (RCs) are using "v0.1.2-rc0"
function release_candidate() {
	LAST_TAG=$(git semver)
	if [[ $LAST_TAG =~ -rc* ]]; then
		VERSION=$(echo "$LAST_TAG" | cut -d'-' -f 1)
		LAST_RC=$(echo "$LAST_TAG" | cut -d'c' -f 2)
		RC=$((LAST_RC + 1))
	else
		VERSION="$(git semver --next-minor)"
		RC=0
	fi
	GIT_TAG="v$VERSION-rc$RC"
	export GIT_TAG
	export RC
}

# Check if current commit is tagged or not
GIT_TAG=$(git tag --points-at)
if [ -z "${GIT_TAG}" ]; then
	git semver
	# Figure out next tag based on commit message
	echo "Last commit message: $TRAVIS_COMMIT_MESSAGE"
	case "${TRAVIS_COMMIT_MESSAGE}" in
	*"[netdata patch release]"*) GIT_TAG="v$(git semver --next-patch)" ;;
	*"[netdata minor release]"*) GIT_TAG="v$(git semver --next-minor)" ;;
	*"[netdata major release]"*) GIT_TAG="v$(git semver --next-major)" ;;
	*"[netdata release candidate]"*) release_candidate ;;
	*)
		echo "Keyword not detected. Exiting..."
		exit 0
		;;
	esac
fi
export GIT_TAG
