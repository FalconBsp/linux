#!/bin/sh

# This script is a subroutine called from config-modifier scripts.
# Implements common pattern for .cm that supports
# target-board-dependent amendments.
#
# To use this script,
#
#  1. Simply call this script from config-modifier script, with its
#     name and arguments passed by its caller.
#
#      EX) sample.cm:
#        scripts/config-modifiers/cm_targetdep_support.sh sample "$@"
#
#  2. Create NAME.common.cm, which specifies target-independent settings.
#
#      EX) sample.common.cm:
#        scripts/setconfig.py CONFIG_SAMPLE=y
#
#  3. Create NAME.TARGET.cm if necessary, which specifies
#     target-dependent settings.
#
#      EX) sample.osk.cm:
#        scripts/setconfig.py CONFIG_SAMPLE_OSK=y
#
# Then, 'setup-osk sample' will executes sample.common.cm and
# sample.osk.cm in this order.

set -e # stop on error
set -u # stop on unset variable

name=$1; shift
build_dir=$1; shift
defconfig=$1; shift

if [ -f "$defconfig.$name" ]; then
    # legacy style 'setup-BOARD patch-test' support.
    cp "$defconfig.$name" "$build_dir/.config"
else
    target=$(cat "$build_dir/.target_name")
    if [ -f "scripts/config-modifiers/$name.common.cm" ]; then
	"scripts/config-modifiers/$name.common.cm" "$build_dir" "$defconfig" "$@"
    fi
    if [ -f "scripts/config-modifiers/$name.$target.cm" ]; then
	"scripts/config-modifiers/$name.$target.cm" "$build_dir" "$defconfig" "$@"
    fi
fi
