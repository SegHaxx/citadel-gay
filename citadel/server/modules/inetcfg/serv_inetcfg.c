/*
 * This module handles the loading/saving and maintenance of the
 * system's Internet configuration.  It's not an optional component; I
 * wrote it as a module merely to keep things as clean and loosely coupled
 * as possible.
 *
 * Copyright (c) 1987-2022 by the citadel.org team
 *
 * This program is open source software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include "../../sysdep.h"
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <signal.h>
#include <pwd.h>
#include <errno.h>
#include <sys/types.h>
#include <time.h>
#include <sys/wait.h>
#include <string.h>
#include <limits.h>
#include <libcitadel.h>
#include "../../citadel_defs.h"
#include "../../server.h"
#include "../../citserver.h"
#include "../../support.h"
#include "../../config.h"
#include "../../user_ops.h"
#include "../../database.h"
#include "../../msgbase.h"
#include "../../internet_addressing.h"
#include "../../genstamp.h"
#include "../../domain.h"
#include "../../ctdl_module.h"


void inetcfg_setTo(struct CtdlMessage *msg) {
	char *conf;
	char buf[SIZ];
	
	if (CM_IsEmpty(msg, eMesageText)) return;
	conf = strdup(msg->cm_fields[eMesageText]);

	if (conf != NULL) {
		do {
			extract_token(buf, conf, 0, '\n', sizeof buf);
			strcpy(conf, &conf[strlen(buf)+1]);
		} while ( (!IsEmptyStr(conf)) && (!IsEmptyStr(buf)) );

		if (inetcfg != NULL) free(inetcfg);
		inetcfg = conf;
	}
}


/*
 * This handler detects changes being made to the system's Internet
 * configuration.
 */
int inetcfg_aftersave(struct CtdlMessage *msg, struct recptypes *recp) {
	char *ptr;
	int linelen;

	/* If this isn't the configuration room, or if this isn't a MIME
	 * message, don't bother.
	 */
	if ((msg->cm_fields[eOriginalRoom]) && (strcasecmp(msg->cm_fields[eOriginalRoom], SYSCONFIGROOM))) {
		return(0);
	}
	if (msg->cm_format_type != 4) {
		return(0);
	}

	ptr = msg->cm_fields[eMesageText];
	while (ptr != NULL) {
	
		linelen = strcspn(ptr, "\n");
		if (linelen == 0) {
			return(0);	/* end of headers */	
		}
		
		if (!strncasecmp(ptr, "Content-type: ", 14)) {
			if (!strncasecmp(&ptr[14], INTERNETCFG, strlen(INTERNETCFG))) {
				inetcfg_setTo(msg);	/* changing configs */
			}
		}

		ptr = strchr((char *)ptr, '\n');
		if (ptr != NULL) ++ptr;
	}

	return(0);
}


void inetcfg_init_backend(long msgnum, void *userdata) {
	struct CtdlMessage *msg;

       	msg = CtdlFetchMessage(msgnum, 1);
	syslog(LOG_DEBUG, "inetcfg: config msg %ld is %s", msgnum, msg ? "not null" : "null");
       	if (msg != NULL) {
		inetcfg_setTo(msg);
               	CM_Free(msg);
	}
}


void inetcfg_init(void) {
	syslog(LOG_DEBUG, "inetcfg: inetcfg_init() started");
	if (CtdlGetRoom(&CC->room, SYSCONFIGROOM) != 0) {
		syslog(LOG_WARNING, "inetcfg: could not find <%s>", SYSCONFIGROOM);
		return;
	}
	CtdlForEachMessage(MSGS_LAST, 1, NULL, INTERNETCFG, NULL, inetcfg_init_backend, NULL);
	syslog(LOG_DEBUG, "inetcfg: inetcfg_init() completed");
}


// Initialization function, called from modules_init.c
char *ctdl_module_init_inetcfg(void) {
	if (!threading) {
		CtdlRegisterMessageHook(inetcfg_aftersave, EVT_AFTERSAVE);
		inetcfg_init();
	}
	
	/* return our module name for the log */
	return "inetcfg";
}
