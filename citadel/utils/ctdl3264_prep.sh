#!/bin/bash

SERVER_H=server/server.h

convert_struct() {
	start_line=$(cat ${SERVER_H} | egrep -n "^struct $1 {" | cut -d: -f1)
	tail +${start_line} ${SERVER_H} | sed '/};/q' \
	| sed s/"^struct $1 {"/"struct ${1}_32 {"/g \
	| sed s/"int "/"int32_t "/g \
	| sed s/"long "/"int32_t "/g \
	| sed s/"time_t "/"int32_t "/g

}

(
	convert_struct "ctdluser"
	convert_struct "ctdlroom"
	convert_struct "ExpirePolicy"
	convert_struct "floor"

) >utils/ctdl3264_structs.h
