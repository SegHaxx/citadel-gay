#!/bin/bash

rm -fr /usr/local/citadel /usr/local/webcit

# Build libcitadel
pushd ../libcitadel || exit 1
./bootstrap || exit 1
./configure || exit 1
make || exit 1
make install || exit 1
popd

# Build the Citadel server
pushd ../citadel || exit 1
./bootstrap || exit 1
./configure --prefix=/usr/local/citadel || exit 1
make || exit 1
make install || exit 1
popd

# Build WebCit
pushd ../webcit || exit 1
./bootstrap || exit 1
./configure --prefix=/usr/local/webcit || exit 1
make || exit 1
make install || exit 1
popd

# Build the appimage supervisor
cc ctdlvisor.c -o ctdlvisor || exit 1

# Clear out our build directories
rm -frv citadel.AppDir/usr
mkdir -p citadel.AppDir/usr/bin
mkdir -p citadel.AppDir/usr/lib

# Copy over all the libraries we used
for bin in /usr/local/citadel/citserver /usr/local/webcit/webcit
do
	for x in `ldd $bin | awk ' { print $3 } ' | grep -v -e '^$' | grep -v 'libc.so' | grep -v 'libpthread.so' | grep -v 'libresolv.so'`
	do
		cp -v -L $x citadel.AppDir/usr/lib/
	done
done
ldconfig -v citadel.AppDir/usr/lib

# Copy over our application trees
for x in citadel webcit
do
	mkdir -p citadel.AppDir/usr/local/$x
	rsync -va /usr/local/$x/ ./citadel.AppDir/usr/local/$x/
done

cp ctdlvisor citadel.AppDir/usr/bin/

ARCH=x86_64 appimagetool citadel.AppDir/
