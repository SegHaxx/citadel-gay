/*
 * citadel_dirs.c : calculate pathnames for various files used in the Citadel system
 *
 * Copyright (c) 1987-2021 by the citadel.org team
 *
 *  This program is open source software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 3.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 */
#include <unistd.h>
#include <stdio.h>
#include <sys/stat.h>
#include <ctype.h>
#include <string.h>
#include <limits.h>
#include <time.h>
#include <errno.h>
#include <syslog.h>
#include <libcitadel.h>
#include "citadel.h"
#include "citadel_dirs.h"

/* all our directories */
char *ctdl_home_directory = ".";
char *ctdl_db_dir = "data";
char *ctdl_file_dir = "files";
char *ctdl_shared_dir = ".";
char *ctdl_image_dir = "images";
char *ctdl_info_dir = "info";
char *ctdl_key_dir = "keys";
char *ctdl_message_dir = "messages";
char *ctdl_usrpic_dir = "userpics";
char *ctdl_autoetc_dir = ".";
char *ctdl_run_dir = ".";
char *ctdl_netcfg_dir = "netconfigs";
char *ctdl_bbsbase_dir = ".";
char *ctdl_sbin_dir = ".";
char *ctdl_bin_dir = ".";
char *ctdl_utilbin_dir = ".";

/* some of the frequently used files */
char *file_citadel_config = "citadel.config";
char *file_lmtp_socket = "lmtp.socket";
char *file_lmtp_unfiltered_socket = "lmtp-unfiltered.socket";
char *file_arcq = "refcount_adjustments.dat";
char *file_citadel_socket = "citadel.socket";
char *file_citadel_admin_socket = "citadel-admin.socket";
char *file_pid_file = "/var/run/citserver.pid";
char *file_pid_paniclog = "panic.log";
char *file_crpt_file_key = "keys/citadel.key";
char *file_crpt_file_csr = "keys/citadel.csr";
char *file_crpt_file_cer = "keys/citadel.cer";
char *file_chkpwd = "chkpwd";
char *file_guesstimezone = "guesstimezone.sh";


/*
 * Generate an associated file name for a room
 */
size_t assoc_file_name(char *buf, size_t n, struct ctdlroom *qrbuf, const char *prefix) {
	return snprintf(buf, n, "%s%ld", prefix, qrbuf->QRnumber);
}


int create_dir(char *which, long ACCESS, long UID, long GID) {
	int rv;
	rv = mkdir(which, ACCESS);
	if ((rv == -1) && (errno != EEXIST)) {
		syslog(LOG_ERR,
		       "failed to create directory %s: %s",
		       which,
		       strerror(errno));
		return rv;
	}
	rv = chmod(which, ACCESS);
	if (rv == -1) {
		syslog(LOG_ERR,
		       "failed to set permissions for directory %s: %s",
		       which,
		       strerror(errno));
		return rv;
	}
	rv = chown(which, UID, GID);
	if (rv == -1) {
		syslog(LOG_ERR,
		       "failed to set owner for directory %s: %s",
		       which,
		       strerror(errno));
		return rv;
	}
	return rv;
}


int create_run_directories(long UID, long GID) {
	int rv = 0;
	rv += create_dir(ctdl_message_dir   , S_IRUSR|S_IWUSR|S_IXUSR, UID, -1);
	rv += create_dir(ctdl_file_dir      , S_IRUSR|S_IWUSR|S_IXUSR, UID, -1);
	rv += create_dir(ctdl_key_dir       , S_IRUSR|S_IWUSR|S_IXUSR, UID, -1);
	rv += create_dir(ctdl_run_dir       , S_IRUSR|S_IWUSR|S_IXUSR|S_IRGRP|S_IXGRP|S_IROTH|S_IXOTH, UID, GID);
	return rv;
}
