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


// data passed back and forth between listdeliver_do_msg() and listdeliver_sweep_room()
struct lddata {
	long msgnum;		// number of most recent message processed
	char *netconf;		// netconfig for this room (contains the recipients)
};



void listdeliver_do_msg(long msgnum, void *userdata) {
	struct lddata *ld = (struct lddata *) userdata;
	if (!ld) return;
	char buf[SIZ];

	ld->msgnum = msgnum;
	if (msgnum <= 0) return;

	struct CtdlMessage *TheMessage = CtdlFetchMessage(msgnum, 1);
	if (!TheMessage) return;

	char *recipients = malloc(strlen(ld->netconf));
	if (recipients) {
		recipients[0] = 0;

		int config_lines = num_tokens(ld->netconf, '\n');
		for (int i=0; i<config_lines; ++i) {
			extract_token(buf, ld->netconf, i, '\n', sizeof buf);
			if (!strncasecmp(buf, "listrecp|", 9)) {
				if (recipients[0] != 0) {
					strcat(recipients, ",");
				}
				strcat(recipients, &buf[9]);
			}
			if (!strncasecmp(buf, "digestrecp|", 11)) {
				if (recipients[0] != 0) {
					strcat(recipients, ",");
				}
				strcat(recipients, &buf[11]);
			}
		}
		syslog(LOG_DEBUG, "\033[33m%s\033[0m", recipients);
		free(recipients);
	}
	CM_Free(TheMessage);
}


void listdeliver_sweep_room(struct ctdlroom *qrbuf, void *data) {
	char *netconfig = NULL;
	long lastsent = 0;
	char buf[SIZ];
	int config_lines;
	int i;
	int number_of_messages_processed = 0;
	int number_of_recipients = 0;
	struct lddata ld;

	if (CtdlGetRoom(&CC->room, qrbuf->QRname)) {
		syslog(LOG_DEBUG, "listdeliver: no room <%s>", qrbuf->QRname);
		return;
	}

        netconfig = LoadRoomNetConfigFile(qrbuf->QRnumber);
        if (!netconfig) {
		return;				// no netconfig, no processing, no problem
	}

	config_lines = num_tokens(netconfig, '\n');
	for (i=0; i<config_lines; ++i) {
		extract_token(buf, netconfig, i, '\n', sizeof buf);

		if (!strncasecmp(buf, "lastsent|", 9)) {
			lastsent = atol(&buf[9]);
		}
		else if ( (!strncasecmp(buf, "listrecp|", 9)) || (!strncasecmp(buf, "digestrecp|", 11)) ) {
			++number_of_recipients;
		}
	}

	if (number_of_recipients > 0) {
		syslog(LOG_DEBUG, "listdeliver: processing new messages in <%s> for <%d> recipients", qrbuf->QRname, number_of_recipients);
		ld.netconf = netconfig;
		number_of_messages_processed = CtdlForEachMessage(MSGS_GT, lastsent, NULL, NULL, NULL, listdeliver_do_msg, &ld);
		syslog(LOG_DEBUG, "listdeliver: processed %d messages", number_of_messages_processed);
	
		if (number_of_messages_processed > 0) {
			syslog(LOG_DEBUG, "listdeliver: new lastsent is %ld", ld.msgnum);

			// FIXME write lastsent back to netconfig
			syslog(LOG_DEBUG, "\033[31mBEFORE:<%s>\033[0m", netconfig);
			syslog(LOG_DEBUG, "\033[32mAFTER:<%s>\033[0m", netconfig);





		}
	}

	free(netconfig);
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

	//exit(0);
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
