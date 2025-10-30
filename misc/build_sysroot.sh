#!/bin/sh

[ -z "$1" ] && echo "jinx executable path not supplied" && exit 1
[ -z "$2" ] && echo "jinx directory path not supplied" && exit 1
[ -z "$3" ] && echo "jinx architecture path not supplied" && exit 1
[ -z "$4" ] && echo "jinx build directory path not supplied" && exit 1
[ -z "$5" ] && echo "sysroot path not supplied" && exit 1

JINX="$1"
JINX_DIR="$2"
JINX_ARCH="$3"
BUILD_DIR="$4"
SYSROOT_DIR="$5"

mkdir -p ${BUILD_DIR}
cd ${BUILD_DIR}

if ! [ -f .jinx-parameters ]; then
    echo " => jinx: initialising build directory"
    ARCH=${JINX_ARCH} ${JINX} init ${JINX_DIR}
fi

echo " => jinx: updating base"
${JINX} update base

rm -rf ${SYSROOT_DIR}/*

echo " => jinx: installing sysroot"
${JINX} install ${SYSROOT_DIR} base