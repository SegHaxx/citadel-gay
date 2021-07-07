#!/bin/bash

# Work from the directory this script is in
APPIMAGE_BUILD_DIR=$(dirname $(readlink -f $0))
cd $APPIMAGE_BUILD_DIR || exit 28
echo APPIMAGE_BUILD_DIR is `pwd`

# Remove old versions
rm -vf citadel-*appimage

export CITADEL_BUILD_DIR=/tmp/citadel-build-$$
export WEBCIT_BUILD_DIR=/tmp/webcit-build-$$

# libcitadel has to be built in a "real" library directory
pushd ../libcitadel || exit 1
make distclean 2>/dev/null
./bootstrap || exit 2
./configure || exit 3
make || exit 4
make install || exit 5
popd

# Build the text mode client
pushd ../textclient || exit 22
make distclean 2>/dev/null
./bootstrap || exit 23
./configure --prefix=`pwd` || exit 24
make || exit 25
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
for bin in \
	$CITADEL_BUILD_DIR/citserver \
	$WEBCIT_BUILD_DIR/webcit \
	$CITADEL_BUILD_DIR/ctdlmigrate \
	../textclient/citadel \
	`which gdb`
do
	ldd $bin
done | sort | while read libname junk libpath
do
	if [ ! -e ${libpath} 2>/dev/null ] ; then
		echo -e \\033[31m ${libname} was not found and will not be packaged. \\033[0m
	elif grep ^${libname}$ excludelist >/dev/null 2>/dev/null ; then
		echo -e \\033[33m ${libname} is in the exclude list and will not be packaged. \\033[0m
	else
		echo -e \\033[32m ${libname} will be packaged. \\033[0m
		cp -L ${libpath} citadel.AppDir/usr/lib/ 2>/dev/null
	fi
done
ldconfig -v citadel.AppDir/usr/lib

# Copy over some utilities
for bin in db_dump db_load db_recover gdb
do
	cp `which $bin` citadel.AppDir/usr/bin/	|| exit 16
done

# Copy over the client
cp ../textclient/citadel citadel.AppDir/usr/bin/ || exit 26
cp ../textclient/citadel.rc citadel.AppDir/ || exit 27

# Install the Citadel Server application tree
mkdir -p citadel.AppDir/usr/local/citadel || exit 17
rsync -va $CITADEL_BUILD_DIR/ ./citadel.AppDir/usr/local/citadel/ || exit 18

# Install the WebCit application tree
mkdir -p citadel.AppDir/usr/local/webcit || exit 19
rsync -va $WEBCIT_BUILD_DIR/ ./citadel.AppDir/usr/local/webcit/ || exit 20

# Remove the build directories
rm -fr $CITADEL_BUILD_DIR $WEBCIT_BUILD_DIR

cc ctdlvisor.c -o citadel.AppDir/usr/bin/ctdlvisor || exit 21

CPU=`uname -m`
basefilename=citadel-`cat ../release_version.txt`
if [ $CPU == x86_64 ] ; then
	export ARCH=x86_64
elif [ $CPU == armv7l ] ; then
	export ARCH=armhf
elif [ $CPU == aarch64 ] ; then
	export ARCH=aarch64
fi
echo ARCH: $ARCH
echo CPU: $CPU
appimagetool citadel.AppDir/ ${basefilename}-${CPU}.appimage
