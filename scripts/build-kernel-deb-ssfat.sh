#!/bin/sh

# This script will build 2 images
# with config modifier 'ssfat'
# with config modifier 'ssfat-manual'

DATE=`date +%Y%m%d`
SCEL_VER=u.1.0.2.1
KERNEL_VER=3.10.28

build () {
	./setup-amd-x86_64 $BUILD_VER
	yes ""|make oldconfig
	fakeroot make-kpkg -j 4  --initrd  --append_to_version=-$BUILD_VER --REVISION=$KERNEL_VER.$DATE.$SCEL_VER kernel-image kernel-headers
}

BUILD_VER=ssfat
build
BUILD_VER=ssfat-manual
build
