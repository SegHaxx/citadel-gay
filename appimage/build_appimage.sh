#!/bin/bash

export CITADEL_BUILD_DIR=/tmp/citadel-build-$$
export WEBCIT_BUILD_DIR=/tmp/webcit-build-$$
rm -fr $CITADEL_BUILD_DIR $WEBCIT_BUILD_DIR

# libcitadel has to be built in a "real" library directory
pushd ../libcitadel || exit 1
./bootstrap || exit 1
./configure || exit 1
make || exit 1
make install || exit 1
popd

# Build the Citadel server
pushd ../citadel || exit 1
./bootstrap || exit 1
./configure --prefix=$CITADEL_BUILD_DIR || exit 1
make || exit 1
make install || exit 1
popd

# Build WebCit
pushd ../webcit || exit 1
./bootstrap || exit 1
./configure --prefix=$WEBCIT_BUILD_DIR || exit 1
make || exit 1
make install || exit 1
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

# Install the Citadel Server application tree
mkdir -p citadel.AppDir/usr/local/citadel
rsync -va $CITADEL_BUILD_DIR/ ./citadel.AppDir/usr/local/citadel/

# Install the WebCit application tree
mkdir -p citadel.AppDir/usr/local/webcit
rsync -va $WEBCIT_BUILD_DIR/ ./citadel.AppDir/usr/local/webcit/

# Remove the build directories
rm -fr $CITADEL_BUILD_DIR $WEBCIT_BUILD_DIR

cc ctdlvisor.c -o citadel.AppDir/usr/bin/ctdlvisor || exit 1

cpu=`uname -p`
if [ $cpu == x86_64 ] ; then
	ARCH=x86_64 appimagetool citadel.AppDir/
	md5sum Citadel-x86_64.AppImage | awk ' { print $1 } ' >Citadel-x86_64.AppImage.md5
else
	ARCH=ARM appimagetool citadel.AppDir/
	md5sum Citadel-armhf.AppImage | awk ' { print $1 } ' >Citadel-armhf.AppImage.md5
fi
