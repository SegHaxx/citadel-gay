#!/bin/bash
# Copyright (c) 2023 by the citadel.org team
# This program is open source software.  Use, duplication, or disclosure
# is subject to the terms of the GNU General Public License, version 3.

# Source our data structures from the real live working code
SERVER_H=server/server.h

tail_opt=
tail +1 ${SERVER_H} > /dev/null 2>&1
if [ $? != 0 ] ; then
	tail_opt=-n
fi

# Generate the "32-bit" versions of these structures.
# Note that this is specifically for converting "32-bit to 64-bit" -- NOT "any-to-any"
convert_struct() {
	start_line=$(cat ${SERVER_H} | egrep -n "^struct $1 {" | cut -d: -f1)
	tail ${tail_opt} +${start_line} ${SERVER_H} | sed '/};/q' \
	| sed s/"^struct $1 {"/"struct ${1}_32 {"/g \
	| sed s/"long "/"int32_t "/g \
	| sed s/"time_t "/"int32_t "/g \
	| sed s/"size_t "/"int32_t "/g \
	| sed s/"struct ExpirePolicy "/"struct ExpirePolicy_32 "/g
	echo ''

}

# Here we go.  Let's make this thing happen.
(

	echo '// This file was automatically generated by ctdl3264_prep.sh'
	echo '// Attempting to modify it would be an exercise in futility.'
	echo ''

	convert_struct "ctdluser"
	convert_struct "ExpirePolicy"
	convert_struct "ctdlroom"
	convert_struct "floor"
	convert_struct "visit"
	convert_struct "visit_index"
	convert_struct "MetaData"
	convert_struct "CtdlCompressHeader"
	convert_struct "UseTable"

) >utils/ctdl3264_structs.h
