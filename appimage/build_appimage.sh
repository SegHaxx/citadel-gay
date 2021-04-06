#!/bin/bash

export CITADEL_BUILD_DIR=/tmp/citadel-build-$$
export WEBCIT_BUILD_DIR=/tmp/webcit-build-$$
rm -fr $CITADEL_BUILD_DIR $WEBCIT_BUILD_DIR

# libcitadel has to be built in a "real" library directory
pushd ../libcitadel || exit 1
make distclean 2>/dev/null
./bootstrap || exit 2
./configure || exit 3
make || exit 4
make install || exit 5
popd

# Build the Citadel server
pushd ../citadel || exit 6
make distclean 2>/dev/null
./bootstrap || exit 7
./configure --prefix=$CITADEL_BUILD_DIR || exit 8
make || exit 9
make install || exit 10
popd

# Build WebCit
pushd ../webcit || exit 11
make distclean 2>/dev/null
./bootstrap || exit 12
./configure --prefix=$WEBCIT_BUILD_DIR || exit 13
make || exit 14
make install || exit 15
popd

# Clear out any old versions in the AppDir
rm -frv citadel.AppDir/usr
mkdir -p citadel.AppDir/usr/bin
mkdir -p citadel.AppDir/usr/lib

# Copy over all the libraries we used
for bin in $CITADEL_BUILD_DIR/citserver $WEBCIT_BUILD_DIR/webcit
do
	for x in `ldd $bin | awk ' { print $3 } ' | grep -v -e '^$' | grep -v 'libc.so' | grep -v 'libpthread.so' | grep -v 'libresolv.so'`
	do
		cp -v -L $x citadel.AppDir/usr/lib/
	done
done
ldconfig -v citadel.AppDir/usr/lib

# Copy over some utilities
for bin in db_dump db_load db_recover
do
	cp `which $bin` citadel.AppDir/usr/bin/	|| exit 16
done

# Install the Citadel Server application tree
mkdir -p citadel.AppDir/usr/local/citadel || exit 17
rsync -va $CITADEL_BUILD_DIR/ ./citadel.AppDir/usr/local/citadel/ || exit 18

# Install the WebCit application tree
mkdir -p citadel.AppDir/usr/local/webcit || exit 19
rsync -va $WEBCIT_BUILD_DIR/ ./citadel.AppDir/usr/local/webcit/ || exit 20

# Remove the build directories
rm -fr $CITADEL_BUILD_DIR $WEBCIT_BUILD_DIR

cc ctdlvisor.c -o citadel.AppDir/usr/bin/ctdlvisor || exit 21

cpu=`uname -p`
basefilename=citadel-`date +%s`
if [ $cpu == x86_64 ] ; then
	ARCH=x86_64 appimagetool citadel.AppDir/ ${basefilename}-x64.appimage
else
	ARCH=ARM appimagetool citadel.AppDir/ ${basefilename}-arm32.appimage
fi


