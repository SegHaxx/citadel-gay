#!/bin/bash

# Identify the version numbers embedded in these components.
# This is not very useful on its own.  It will become part of a build system later.

libcitadel_version=`grep LIBCITADEL_VERSION_NUMBER  libcitadel/lib/libcitadel.h | sed s/"[^0-9.]"/""/g`
citserver_version=`grep REV_LEVEL citadel/citadel.h | sed s/"[^0-9.]"/""/g`
webcit_version=`grep CLIENT_VERSION webcit/webcit.h | sed s/"[^0-9.]"/""/g`

echo libcitadel $libcitadel_version , citserver $citserver_version , webcit $webcit_version
