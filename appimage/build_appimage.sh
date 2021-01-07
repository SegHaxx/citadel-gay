#!/bin/bash

CTDLROOT=../citadel

rm -frv citadel.AppDir/usr
mkdir -p citadel.AppDir/usr/bin
mkdir -p citadel.AppDir/usr/lib

pushd citadel.AppDir
cc ctdlvisor.c -o ctdlvisor || exit 1
popd

for bin in $CTDLROOT/citadel/citserver $CTDLROOT/webcit/webcit
do
	cp -v $bin citadel.AppDir/usr/bin/
	for x in `ldd $bin | awk ' { print $3 } ' | grep -v -e '^$' | grep -v 'libc.so' | grep -v 'libpthread.so' | grep -v 'libresolv.so'`
	do
		cp -v -L $x citadel.AppDir/usr/lib/
	done
done
ldconfig -v citadel.AppDir/usr/lib

ARCH=x86_64 appimagetool citadel.AppDir/

# Hint: do this on your build server first!
# apt-get install make build-essential autoconf zlib1g-dev libldap2-dev libssl-dev gettext libical-dev libexpat1-dev libcurl4-openssl-dev
