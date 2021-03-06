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
	char *ch;
	char bounce_to[256];
	int i = 0;

	ld->msgnum = msgnum;
	if (msgnum <= 0) return;

	struct CtdlMessage *TheMessage = CtdlFetchMessage(msgnum, 1);
	if (!TheMessage) return;

	// If the subject line does not contain the name of the room, add it now.
	if (!bmstrcasestr(TheMessage->cm_fields[eMsgSubject], CC->room.QRname)) {
		snprintf(buf, sizeof buf, "[%s] %s", CC->room.QRname, TheMessage->cm_fields[eMsgSubject]);
		CM_SetField(TheMessage, eMsgSubject, buf, strlen(buf));
	}

	// Reply-to: should be set so that replies come to the list.
	snprintf(buf, sizeof buf, "room_%s@%s", CC->room.QRname, CtdlGetConfigStr("c_fqdn"));
	for (ch=buf; *ch; ++ch) {
		if (isspace(*ch)) *ch = '_';
	}
	CM_SetField(TheMessage, eReplyTo, buf, strlen(buf));

	// With that out of the way, let's figure out who this message needs to be sent to.
	char *recipients = malloc(strlen(ld->netconf));
	if (recipients) {
		recipients[0] = 0;

		int config_lines = num_tokens(ld->netconf, '\n');
		for (i=0; i<config_lines; ++i) {
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

		// Where do we want bounces and other noise to be sent?  Certainly not to the list members!
		snprintf(bounce_to, sizeof bounce_to, "room_aide@%s", CtdlGetConfigStr("c_fqdn"));

		// Now submit the message
		struct recptypes *valid = validate_recipients(recipients, NULL, 0);
		if (valid) {
			valid->bounce_to = strdup(bounce_to);
			valid->envelope_from = strdup(bounce_to);
			CtdlSubmitMsg(TheMessage, valid, "");
			free_recipients(valid);
		}
	}
	CM_Free(TheMessage);
}


/*
 * Sweep through one room looking for mailing list deliveries to do
 */
void listdeliver_sweep_room(char *roomname) {
	char *netconfig = NULL;
	char *newnetconfig = NULL;
	long lastsent = 0;
	char buf[SIZ];
	int config_lines;
	int i;
	int number_of_messages_processed = 0;
	int number_of_recipients = 0;
	struct lddata ld;

	if (CtdlGetRoom(&CC->room, roomname)) {
		syslog(LOG_DEBUG, "listdeliver: no room <%s>", roomname);
		return;
	}

        netconfig = LoadRoomNetConfigFile(CC->room.QRnumber);
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
		syslog(LOG_DEBUG, "listdeliver: processing new messages in <%s> for <%d> recipients", CC->room.QRname, number_of_recipients);
		ld.netconf = netconfig;
		number_of_messages_processed = CtdlForEachMessage(MSGS_GT, lastsent, NULL, NULL, NULL, listdeliver_do_msg, &ld);
		syslog(LOG_INFO, "listdeliver: processed <%d> messages in <%s> for <%d> recipients", number_of_messages_processed, CC->room.QRname, number_of_recipients);
	
		if (number_of_messages_processed > 0) {
			syslog(LOG_DEBUG, "listdeliver: new lastsent is %ld", ld.msgnum);

			// Update this room's netconfig with the updated lastsent
			begin_critical_section(S_NETCONFIGS);
        		netconfig = LoadRoomNetConfigFile(CC->room.QRnumber);
        		if (!netconfig) {
				netconfig = strdup("");
			}

			// The new netconfig begins with the new lastsent directive
			newnetconfig = malloc(strlen(netconfig) + 1024);
			sprintf(newnetconfig, "lastsent|%ld\n", ld.msgnum);

			// And then we append all of the old netconfig, minus the old lastsent.  Also omit blank lines.
			config_lines = num_tokens(netconfig, '\n');
			for (i=0; i<config_lines; ++i) {
				extract_token(buf, netconfig, i, '\n', sizeof buf);
				if ( (!IsEmptyStr(buf)) && (strncasecmp(buf, "lastsent|", 9)) ) {
					sprintf(&newnetconfig[strlen(newnetconfig)], "%s\n", buf);
				}
			}

			// Write the new netconfig back to disk
			SaveRoomNetConfigFile(CC->room.QRnumber, newnetconfig);
			end_critical_section(S_NETCONFIGS);
			free(newnetconfig);	// this was the new netconfig, free it because we're done with it
		}
	}
	free(netconfig);			// this was the old netconfig, free it even if we didn't do anything
}


/*
 * Callback for listdeliver_sweep()
 * Adds one room to the queue
 */
void listdeliver_queue_room(struct ctdlroom *qrbuf, void *data) {
	Array *roomlistarr = (Array *)data;
	array_append(roomlistarr, qrbuf->QRname);
}


/*
 * Queue up the list of rooms so we can sweep them for mailing list delivery instructions
 */
void listdeliver_sweep(void) {
	static time_t last_run = 0L;
	int i = 0;

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

	Array *roomlistarr = array_new(ROOMNAMELEN);			// we have to queue them
	CtdlForEachRoom(listdeliver_queue_room, roomlistarr);		// otherwise we get multiple cursors in progress

	for (i=0; i<array_len(roomlistarr); ++i) {
		listdeliver_sweep_room((char *)array_get_element_at(roomlistarr, i));
	}

	array_free(roomlistarr);
	syslog(LOG_DEBUG, "listdeliver: ended");
	last_run = time(NULL);
	doing_listdeliver = 0;
}


/*
 * Module entry point
 */
CTDL_MODULE_INIT(listdeliver)
{
	if (!threading) {
		CtdlRegisterSessionHook(listdeliver_sweep, EVT_TIMER, PRIO_AGGR + 50);
	}
	
	/* return our module name for the log */
	return "listsub";
}
