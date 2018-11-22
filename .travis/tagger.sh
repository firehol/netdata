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

set -e

if [ ! -f .gitignore ]; then
	echo "Run as ./travis/$(basename "$0") from top level directory of git repository"
	exit 1
fi

# Embed new version in files which need it.
# This wouldn't be needed if we could use `git tag` everywhere.
function embed_version {
	VERSION="$1"
	MAJOR=$(echo "$GIT_TAG" | cut -d . -f 1 | cut -d v -f 2)
	MINOR=$(echo "$GIT_TAG" | cut -d . -f 2)
	PATCH=$(echo "$GIT_TAG" | cut -d . -f 3)
	sed -i "s/\\[VERSION_MAJOR\\], \\[.*\\]/\\[VERSION_MAJOR\\], \\[$MAJOR\\]/" configure.ac
	sed -i "s/\\[VERSION_MINOR\\], \\[.*\\]/\\[VERSION_MINOR\\], \\[$MINOR\\]/" configure.ac
	sed -i "s/\\[VERSION_PATCH\\], \\[.*\\]/\\[VERSION_PATCH\\], \\[$PATCH\\]/" configure.ac
	git add configure.ac
}

# Figure out what will be new release candidate tag based only on previous ones.
# This assumes that RELEASES are in format of "v0.1.2" and prereleases (RCs) are using "v0.1.2-rc0"
function release_candidate {
	LAST_TAG=$(git semver)
	if [[ $LAST_TAG =~ -rc* ]]; then
		LAST_RELEASE=$(echo "$LAST_TAG" | cut -d'-' -f 1)
		LAST_RC=$(echo "$LAST_TAG" | cut -d'c' -f 2)
		RC=$((LAST_RC + 1))
	else
		LAST_RELEASE=$LAST_TAG
		RC=0
	fi
	GIT_TAG="v$LAST_RELEASE-rc$RC"
	export GIT_TAG
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
	*) echo "Keyword not detected. Exiting..."; exit 1;;
	esac
	# Tag it!
	if [ "$GIT_TAG" != "HEAD" ]; then
		echo "Assigning a new tag: $GIT_TAG"
		embed_version "$GIT_TAG"
		git commit -m "[ci skip] release $GIT_TAG"
		git tag "$GIT_TAG" -a -m "Automatic tag generation for travis build no. $TRAVIS_BUILD_NUMBER"
		git push "https://${GITHUB_TOKEN}:@$(git config --get remote.origin.url | sed -e 's/^https:\/\///')"
		git push "https://${GITHUB_TOKEN}:@$(git config --get remote.origin.url | sed -e 's/^https:\/\///')" --tags
	fi
else
	embed_version "$GIT_TAG"
	git commit -m "[ci skip] release $GIT_TAG"
	git push "https://${GITHUB_TOKEN}:@$(git config --get remote.origin.url | sed -e 's/^https:\/\///')"
fi
export GIT_TAG
