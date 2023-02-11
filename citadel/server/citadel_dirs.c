// citadel_dirs.c : calculate pathnames for various files used in the Citadel system
//
// Copyright (c) 1987-2021 by the citadel.org team
//
// This program is open source software.  Use, duplication, or disclosure
// is subject to the terms of the GNU General Public License, version 3.

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
#include "server.h"
#include "citadel_dirs.h"

// Generate an associated file name for a room
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
		syslog(LOG_ERR, "failed to set permissions for directory %s: %s", which, strerror(errno));
		return rv;
	}
	rv = chown(which, UID, GID);
	if (rv == -1) {
		syslog(LOG_ERR, "failed to set owner for directory %s: %s", which, strerror(errno));
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
