#!/bin/sh
#
#  Script to install kbuild header
#
#  Copyright 2006-2009 Sony Corporation.
#
set -e # stop on error

WORK=`pwd`/.work_install_kbuild_headers
HEADER=${WORK}/kernel_header.tar
KBUILD=${WORK}/kbuild_tree.tar

# COPY_FILES should be synced with make_kernel_headers.sh.
COPY_FILES=".config .arch_name .cross_compile .target_name"


usage() {
    echo "$0 [--top=DIR] <kbuild_headers_archive>" > /dev/tty
    exit 1
}

fatal () {
    echo "FATAL: $*" > /dev/tty
    rm -rf ${WORK}
    exit 1
}

while [ $# != 0 ]; do
    case $1 in
      --top | -top)
	shift
	TOP=$1
	shift
	;;
      --top=* | -top=*)
	TOP=`expr "x$1" : 'x[^=]*=\(.*\)'`
	shift
	;;
      --help)
        usage
        ;;
      *)
        break
    esac
done

test "$#" -ne 1 && usage
FILE=$1
test -f ${FILE} || usage
test "_`id -u`" == "_0" || fatal "Must be superuser"

rm -rf ${WORK}
mkdir -p ${WORK}

#
# extract archive files
#
tar xfz ${FILE} -C ${WORK}
if [[ ! -f ${HEADER} || ! -f ${KBUILD} ]]; then
    rm -r ${WORK}
    fatal "Invalid file"
fi

for f in ${COPY_FILES} ; do
    tar xf ${KBUILD} -C ${WORK} ./${f}
done

#
# create install path
#
if [ -z "${srctree}" ]; then
    SCR_PATH=`dirname $0`
else
    SCR_PATH=${srctree}/scripts
fi

ARCH=`${SCR_PATH}/install_headers_util.sh -a ${WORK}`
CROSS_COMPILE=`${SCR_PATH}/install_headers_util.sh -c ${WORK}`

if [ -z "$TOP" ]; then
    # no --top.  use old style dir
    INSTDIR=`${SCR_PATH}/install_headers_util.sh -k ${WORK}`
else
    # --top specified.  use $TOP/<ARCH> where ARCH is taken from tripplet
    sub=`expr "x$CROSS_COMPILE" : 'x\(.*\)-sony-linux.*'`
    INSTDIR="$TOP/$sub"
fi

KBUILDDIR=${INSTDIR}/kbuild
INSTDIR_PATH=`${SCR_PATH}/install_headers_util.sh -p ${WORK}` 

echo -n "Installing kbuild headers for ${ARCH} to ${INSTDIR_PATH}..." 

#
# install headers for kbuild
#
rm -rf ${KBUILDDIR}
#mkdir -p ${KBUILDDIR}/include

# Install kernel headers for each target board rather overwrite
#
mkdir -p ${INSTDIR}
SOFT_LINK=`${SCR_PATH}/install_headers_util.sh -l ${WORK}`
[ -L "$SOFT_LINK" ] && (ln -s $SOFT_LINK $KBUILDDIR)

tar xf ${KBUILD} -C ${KBUILDDIR}
tar xf ${HEADER} -C ${KBUILDDIR}

# chmod files
chown -R root:root ${KBUILDDIR}

rm -r ${WORK}

echo "done"

exit 0
