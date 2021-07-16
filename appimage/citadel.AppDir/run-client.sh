#!/bin/bash

# This script attempts to find the Citadel Server running in an AppImage on
# the same host, and connects to the text mode client.

CLIENT_PATH=`ls /tmp/.*/usr/bin/citadel || exit 1` || exit 1
CLIENT_DIR=`dirname ${CLIENT_PATH}`
export PATH=${CLIENT_DIR}:$PATH
export APPDIR=`echo ${CLIENT_DIR} | sed s/'\/usr\/bin'//g`
export LD_LIBRARY_PATH=${APPDIR}/usr/lib:$LD_LIBRARY_PATH
export CTDL_DIR=$(dirname $(readlink -f $0))
ldd $CLIENT_PATH
exec $CLIENT_PATH
