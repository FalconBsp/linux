#!/bin/sh
#
#  Script to copy kernel headers
#
#  Copyright 2008 Sony Corporation.
#
set -eu

SRC=$1
DST=$2

fatal() {
    echo "$0: $@" 1>&2
    exit 1
}

# check source directory
if [ ! -d ${SRC} ]; then
    fatal "no such directory: ${SRC}"
fi

# create destination directory if not exist
if [ ! -d ${DST} ]; then
    mkdir -p ${DST} || fatal "cannot create: ${DST}"
fi

# copy header files
(cd ${SRC}; find . -name "*.h" | cpio -o --format=newc --quiet) |
(cd ${DST}; cpio -iumd --quiet) || fatal "cannot copy: ${SRC}"
