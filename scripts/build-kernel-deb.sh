#!/bin/sh

DATE=`date +%Y%m%d`
SCEL_VER=u.2.0.2.3
KERNEL_VER=3.10.67
BUILD_VER=r1

fakeroot debian/rules binary-indep full_build=true
fakeroot debian/rules binary-arch-headers
./setup-amd-x86_64
fakeroot make-kpkg -j 4  --initrd  --append_to_version=-$BUILD_VER --REVISION=$KERNEL_VER.$DATE.$SCEL_VER kernel-image kernel-headers
