//
// Copyright (c) 1987-2022 by the citadel.org team
//
// This program is open source software.  Use, duplication, or disclosure
// is subject to the terms of the GNU General Public License, version 3.

#include "sysdep.h"
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <syslog.h>
#include <libical/ical.h>
#include <libcitadel.h>
#include "citadel_defs.h"
#include "server.h"
#include "citserver.h"
#include "sysdep_decls.h"
#include "support.h"
#include "config.h"
#include "default_timezone.h"
#include "ctdl_module.h"


// Figure out which time zone needs to be used for timestamps that are
// not UTC and do not have a time zone specified.
icaltimezone *get_default_icaltimezone(void) {

        icaltimezone *zone = NULL;
	char *default_zone_name = CtdlGetConfigStr("c_default_cal_zone");

        if (!zone) {
                zone = icaltimezone_get_builtin_timezone(default_zone_name);
        }
        if (!zone) {
		syslog(LOG_ERR, "ical: Unable to load '%s' time zone.  Defaulting to UTC.", default_zone_name);
                zone = icaltimezone_get_utc_timezone();
	}
	if (!zone) {
		syslog(LOG_ERR, "ical: unable to load UTC time zone!");
	}

        return zone;
}
