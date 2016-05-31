#!/bin/sh

# Exit immediately on errors.
set -e

function chmod_x () {
    for f in "$@"; do
	test -f "$f" && chmod ug+x "$f"
    done
    return 0 # Discard result of last test, which may fail.
}

# For SS related works.
chmod_x setup-*
chmod_x scripts/setup_common.py
chmod_x scripts/config-modifiers/*

# For git to properly work with .pc/ files.
chmod -R ug+rw .pc/

# This script itself
chmod_x scripts/fix-filemodes.sh

# migration trace
chmod_x scripts/migrate_extract
chmod_x scripts/migrate_stats

# Kernel header scripts
chmod_x scripts/*.sh

# setconfig scripts
chmod_x scripts/setconfig.py

# Exception Monitor scripts
chmod_x scripts/em/emlogconv

#ubuntu
chmod_x debian/rules
chmod_x debian/scripts/config-check
chmod_x debian/scripts/misc/find-obsolete-firmware
chmod_x debian/scripts/misc/get-firmware
chmod_x debian/scripts/misc/getabis

# Looks at the current index and checks to see if merges or updates
# are needed by checking stat() information.
# This is needed for errornous cg-commit log listing all files
# modified by this file.
#
# git-update-index returns non-zero (indicating some files are changed).
# Ignore it by `|| true', so that the entire script exits with zero.
git update-index --refresh > /dev/null || true
