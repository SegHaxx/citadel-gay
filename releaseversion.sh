#!/bin/bash

if test "$1" = '?'; then

	echo 'no help for the lost.'
	exit
fi

if test "$1" = 'list'; then
	echo "showing current release version state: "
	echo "-------- libcitadel: --------"
	grep AC_INIT libcitadel/configure.in
	grep 'PACKAGE_VERSION=' libcitadel/configure
	echo "  - Header version:"
	grep LIBCITADEL_VERSION_NUMBER libcitadel/lib/libcitadel.h
	head -n 5 libcitadel/debian/changelog
	
	echo "-------- citserver: --------"
	grep 'PACKAGE_VERSION=' citadel/configure
	grep '#define REV_LEVEL' citadel/citadel.h
	grep '#define REV_MIN' citadel/citadel.h
	grep '#define EXPORT_REV_MIN' citadel/citadel.h
	grep '#define LIBCITADEL_MIN' citadel/citadel.h
	head -n 5 citadel/debian/changelog

	echo "-------- textclient: --------"
	grep 'PACKAGE_VERSION=' textclient/configure
	head -n 5 textclient/debian/changelog

	echo "-------- webcit: --------"
	grep 'PACKAGE_VERSION=' webcit/configure
	grep '#define CLIENT_VERSION' webcit/webcit.h
	grep '#define MINIMUM_CIT_VERSION' webcit/webcit.h
	grep '#define LIBCITADEL_MIN' webcit/webcit.h
	head -n 5 webcit/debian/changelog

	exit
fi


if test "$1" = "revert"; then 
	echo "reverting all changes for version files"
	git checkout \
	libcitadel/lib/libcitadel.h \
	libcitadel/configure.in \
	libcitadel/debian/changelog \
	\
	citadel/citadel.h \
	citadel/configure.ac \
	citadel/debian/changelog \
	\
	textclient/configure.ac \
	textclient/debian/changelog \
	\
	webcit/webcit.h \
	webcit/configure.ac \
	webcit/debian/changelog

	exit
fi 


NEW_VERSION=$1

if test -z "$NEW_VERSION" ; then
	echo Need new version number
	exit
fi

export LANG=C
RELEASEDATE=`date -R`


function DebChangeLog()
{
	FILE=$1
	PROJECT=$2
	(
	printf "${PROJECT} (${NEW_VERSION}-1) stable; urgency=low\n\n  * new release\n\n -- Wilfried Goesgens <w.goesgens@outgesourced.org>  ${RELEASEDATE}\n\n"
	cat ${FILE}
	) > /tmp/${PROJECT}_changelog
	rm -f ${FILE}
	mv /tmp/${PROJECT}_changelog ${FILE}
}


################################################################################
# libcitadel

DebChangeLog libcitadel/debian/changelog libcitadel

sed -i -e \
	"s;^#define LIBCITADEL_VERSION_NUMBER.*[0-9][0-9][0-9]\(.*\)$;#define LIBCITADEL_VERSION_NUMBER\t${NEW_VERSION}\1;g" \
	libcitadel/lib/libcitadel.h

OLD_VERSION=`grep AC_INIT libcitadel/configure.in  |sed "s;.*\(....\), http.*;\1;"`
echo libcitadel OLD_VERSION is $OLD_VERSION

sed -i -e "s;${OLD_VERSION};${NEW_VERSION};" \
	-e "s;^LIBREVISION=[0-9][0-9][0-9]\(.*\)$;LIBREVISION=${NEW_VERSION}\1;g" \
	libcitadel/configure.in



################################################################################
# citserver

DebChangeLog citadel/debian/changelog citadel

OLD_VERSION=`grep AC_INIT citadel/configure.ac  |sed "s;.*\[\(....\)\],.*;\1;"`
echo citserver OLD_VERSION is $OLD_VERSION

sed -i "s;${OLD_VERSION};${NEW_VERSION};" citadel/configure.ac

sed  -i -e "s;^#define REV_LEVEL.*[0-9][0-9][0-9]\(.*\)$;#define REV_LEVEL\t${NEW_VERSION}\1;g" \
	-e "s;^#define LIBCITADEL_MIN.*[0-9][0-9][0-9]\(.*\)$;#define LIBCITADEL_MIN\t${NEW_VERSION}\1;g" \
	citadel/citadel.h

################################################################################
# textclient

DebChangeLog textclient/debian/changelog textclient

sed -i "s;${OLD_VERSION};${NEW_VERSION};" textclient/configure.ac


################################################################################
# webcit

DebChangeLog webcit/debian/changelog webcit

sed -i "s;${OLD_VERSION};${NEW_VERSION};" webcit/configure.ac

sed  -i -e "s;^#define CLIENT_VERSION.*[0-9][0-9][0-9]\(.*\)$;#define CLIENT_VERSION\t\t${NEW_VERSION}\1;g" \
	-e "s;^#define MINIMUM_CIT_VERSION.*[0-9][0-9][0-9]\(.*\)$;#define MINIMUM_CIT_VERSION\t${NEW_VERSION}\1;g" \
	-e "s;^#define LIBCITADEL_MIN.*[0-9][0-9][0-9]\(.*\)$;#define LIBCITADEL_MIN\t${NEW_VERSION}\1;g" \
	webcit/webcit.h


