// a setuid helper program for machines which use shadow passwords
//
// Copyright (c) 1987-2022 by Nathan Bryant and the citadel.org team
//
// This program is open source software.  Use, duplication, or disclosure
// is subject to the terms of the GNU General Public License, version 3.

#include <pwd.h>
#include <stdio.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>
#include <sys/types.h>
#include <libcitadel.h>
#include "auth.h"
#include "../server/config.h"
#include "../server/citadel_dirs.h"
#include "../server/citadel.h"


int main(void) {
	uid_t uid;
	char buf[SIZ];

	while (1) {
		buf[0] = '\0';
		read(0, &uid, sizeof(uid_t));	// uid
		read(0, buf, 256);		// password

		if (buf[0] == '\0') {
			return (0);
		}
		if (validate_password(uid, buf)) {
			write(1, "PASS", 4);
		}
		else {
			write(1, "FAIL", 4);
		}
	}

	return(0);
}
