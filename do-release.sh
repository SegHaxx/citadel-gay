#!/bin/bash

# Identify the version numbers embedded in these components.
# This is not very useful on its own.  It will become part of a build system later.



echo -e '\033[2J\033[H'
echo -e '\033[34m\033[1m'
echo -e '╔═══════════════════════════════════════════════╗'
echo -e '║      UPDATE-O-VERSION-O-MATIC FOR CITADEL     ║'
echo -e '╚═══════════════════════════════════════════════╝'
echo -e '\033[0m'

libcitadel_version=`grep LIBCITADEL_VERSION_NUMBER  libcitadel/lib/libcitadel.h | sed s/"[^0-9.]"/""/g`
citserver_version=`grep REV_LEVEL citadel/citadel.h | sed s/"[^0-9.]"/""/g`
webcit_version=`grep CLIENT_VERSION webcit/webcit.h | sed s/"[^0-9.]"/""/g`

NEW_VERSION=${libcitadel_version}
if [ ${citserver_version} -gt ${NEW_VERSION} ] ; then
	NEW_VERSION=${citserver_version}
fi
if [ ${webcit_version} -gt ${NEW_VERSION} ] ; then
	NEW_VERSION=${webcit_version}
fi
NEW_VERSION=`expr ${NEW_VERSION} + 1`

echo -e '\033[33m\033[1mlibcitadel \033[32m was version     \033[33m'$libcitadel_version'\033[0m'
echo -e '\033[33m\033[1mcitserver  \033[32m was version     \033[33m'$citserver_version'\033[0m'
echo -e '\033[33m\033[1mwebcit     \033[32m was version     \033[33m'$webcit_version'\033[0m'
echo -e '\033[33m\033[1mnew release\033[32m will be version \033[33m'$NEW_VERSION'\033[0m'

echo -e ''
echo -e '\033[37m\033[1mUpdating header files to reflect the new version number\033[0m'
echo -e ''

# Edit libcitadel.h to make it the new version
sed \
	-i s/\#define.\*LIBCITADEL_VERSION_NUMBER.\*${libcitadel_version}/\#define\ LIBCITADEL_VERSION_NUMBER\ ${NEW_VERSION}/g \
	libcitadel/lib/libcitadel.h

# Edit citadel.h to make it the new version
sed \
	-i s/\#define.\*REV_LEVEL.\*${citserver_version}/\#define\ REV_LEVEL\ ${NEW_VERSION}/g \
	citadel/citadel.h

# Edit webcit.h to make it the new version
sed \
	-i s/\#define.\*CLIENT_VERSION.\*${webcit_version}/\#define\ CLIENT_VERSION\ ${NEW_VERSION}/g \
	webcit/webcit.h

libcitadel_version=`grep LIBCITADEL_VERSION_NUMBER  libcitadel/lib/libcitadel.h | sed s/"[^0-9.]"/""/g`
citserver_version=`grep REV_LEVEL citadel/citadel.h | sed s/"[^0-9.]"/""/g`
webcit_version=`grep CLIENT_VERSION webcit/webcit.h | sed s/"[^0-9.]"/""/g`

echo -e '\033[33m\033[1mlibcitadel \033[32m is now version  \033[33m'$libcitadel_version'\033[0m'
echo -e '\033[33m\033[1mcitserver  \033[32m is now version  \033[33m'$citserver_version'\033[0m'
echo -e '\033[33m\033[1mwebcit     \033[32m is now version  \033[33m'$webcit_version'\033[0m'

echo $NEW_VERSION >release_version.txt
git add release_version.txt
echo -e ''
echo -e '\033[37m\033[1mUpdating release_version.txt to indicate version '${NEW_VERSION}'\033[0m'
echo -e ''
