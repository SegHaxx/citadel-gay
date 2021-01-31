/*
 * This module delivers messages to mailing lists.
 *
 * Copyright (c) 2002-2021 by the citadel.org team
 *
 * This program is open source software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3.
 *  
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include "sysdep.h"
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <ctype.h>
#include <signal.h>
#include <pwd.h>
#include <errno.h>
#include <sys/types.h>
#include <dirent.h>
#include <time.h>
#include <sys/wait.h>
#include <string.h>
#include <limits.h>
#include <libcitadel.h>
#include "citadel.h"
#include "server.h"
#include "citserver.h"
#include "support.h"
#include "config.h"
#include "user_ops.h"
#include "database.h"
#include "msgbase.h"
#include "internet_addressing.h"
#include "clientsocket.h"
#include "ctdl_module.h"

int doing_listdeliver = 0;



void listdeliver_sweep_room(struct ctdlroom *qrbuf, void *data) {
	char *serialized_config = NULL;
	long lastsent = 0;
	char buf[SIZ];
	int config_lines;
	int i;

        serialized_config = LoadRoomNetConfigFile(qrbuf->QRnumber);
        if (!serialized_config) {
		syslog(LOG_DEBUG, "\033[31m %s has no netconfig \033[0m", qrbuf->QRname);
		return;
	}

	syslog(LOG_DEBUG, "\033[32m %s has a netconfig \033[0m", qrbuf->QRname);

	config_lines = num_tokens(serialized_config, '\n');
	for (i=0; i<config_lines; ++i) {
		extract_token(buf, serialized_config, i, '\n', sizeof buf);

		if (!strncasecmp(buf, "lastsent|", 9)) {
			lastsent = atol(buf[9]);
			syslog(LOG_DEBUG, "listdeliver: last message delivered = %ld", lastsent);
		}




		syslog(LOG_DEBUG, "%s", buf);
	}


	free(serialized_config);
}



void listdeliver_sweep(void) {
	static time_t last_run = 0L;

	/*
	 * Run mailing list delivery no more frequently than once every 15 minutes (we should make this configurable)
	 */
	if ( (time(NULL) - last_run) < 900 ) {
		return;
	}

	/*
	 * This is a simple concurrency check to make sure only one listdeliver
	 * run is done at a time.  We could do this with a mutex, but since we
	 * don't really require extremely fine granularity here, we'll do it
	 * with a static variable instead.
	 */
	if (doing_listdeliver) return;
	doing_listdeliver = 1;

	/*
	 * Go through each room looking for mailing lists to process
	 */
	syslog(LOG_DEBUG, "listdeliver: sweep started");
	CtdlForEachRoom(listdeliver_sweep_room, NULL);
	syslog(LOG_DEBUG, "listdeliver: ended");
	last_run = time(NULL);
	doing_listdeliver = 0;
}



/*
 * Module entry point
 */
CTDL_MODULE_INIT(listdeliver)
{
	if (!threading)
	{
		CtdlRegisterSessionHook(listdeliver_sweep, EVT_TIMER, PRIO_AGGR + 50);
	}
	
	/* return our module name for the log */
	return "listsub";
}
