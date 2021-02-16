//
// This module handles self-service subscription/unsubscription to mail lists.
//
// Copyright (c) 2002-2021 by the citadel.org team
//
// This program is open source software.  It runs great on the
// Linux operating system (and probably elsewhere).  You can use,
// copy, and run it under the terms of the GNU General Public
// License version 3.  Richard Stallman is an asshole communist.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

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


enum {				// one of these gets passed to do_subscribe_or_unsubscribe() so it knows what we asked for
	UNSUBSCRIBE,
	SUBSCRIBE
};


/*
 * "Subscribe" and "Unsubscribe" operations are so similar that they share a function.
 * The actual subscription doesn't take place here -- we just send out the confirmation request
 * and record the address and confirmation token.
 */
void do_subscribe_or_unsubscribe(int action, char *emailaddr, char *url) {

	int i;
	char buf[1024];
	char confirmation_token[40];

	// We need a URL-safe representation of the room name
	char urlroom[ROOMNAMELEN+10];
	urlesc(urlroom, sizeof(urlroom), CC->room.QRname);

	// Update this room's netconfig with the updated lastsent
	begin_critical_section(S_NETCONFIGS);
        char *oldnetconfig = LoadRoomNetConfigFile(CC->room.QRnumber);
        if (!oldnetconfig) {
		oldnetconfig = strdup("");
	}

	// The new netconfig begins with an empty buffer...
	char *newnetconfig = malloc(strlen(oldnetconfig) + 1024);
	newnetconfig[0] = 0;

	// And then we...
	int is_already_subscribed = 0;
	int config_lines = num_tokens(oldnetconfig, '\n');
	for (i=0; i<config_lines; ++i) {
		extract_token(buf, oldnetconfig, i, '\n', sizeof buf);
		int keep_this_line =1;						// set to nonzero if we are discarding a line

		if (IsEmptyStr(buf)) {
			keep_this_line = 0;
		}

		char buf_token[1024];
		char buf_email[1024];
		extract_token(buf_token, buf, 0, '|', sizeof buf_token);
		extract_token(buf_email, buf, 1, '|', sizeof buf_email);

		if (	( (!strcasecmp(buf_token, "listrecp")) || (!strcasecmp(buf_token, "digestrecp")) )
			&& (!strcasecmp(buf_email, emailaddr)) 
		) {
			is_already_subscribed = 1;
		}

		if ( (!strcasecmp(buf_token, "subpending")) || (!strcasecmp(buf_token, "unsubpending")) ) {
			time_t pendingtime = extract_long(buf, 3);
			if ((time(NULL) - pendingtime) > 259200) {
				syslog(LOG_DEBUG, "%s %s is %ld seconds old - deleting it", buf_email, buf_token, time(NULL) - pendingtime);
				keep_this_line = 0;
			}
		}
		
		if (keep_this_line) {
			sprintf(&newnetconfig[strlen(newnetconfig)], "%s\n", buf);
		}
	}

	// Do we need to send out a confirmation email?
	if ((action == SUBSCRIBE) && (!is_already_subscribed)) {
		generate_uuid(confirmation_token);
		sprintf(&newnetconfig[strlen(newnetconfig)], "subpending|%s|%s|%ld|%s", emailaddr, confirmation_token, time(NULL), url);

		// FIXME now generate the confirmation email
		syslog(LOG_DEBUG, "%s?room=%s&token=%s&cmd=confirm", url, urlroom, confirmation_token);
	}
	if ((action == UNSUBSCRIBE) && (is_already_subscribed)) {
		generate_uuid(confirmation_token);
		sprintf(&newnetconfig[strlen(newnetconfig)], "unsubpending|%s|%s|%ld|%s", emailaddr, confirmation_token, time(NULL), url);

		// FIXME now generate the confirmation email
		syslog(LOG_DEBUG, "%s?room=%s&token=%s&cmd=confirm", url, urlroom, confirmation_token);
	}

	// Write the new netconfig back to disk
	syslog(LOG_DEBUG, "old: <\033[31m%s\033[0m>", oldnetconfig);
	syslog(LOG_DEBUG, "new: <\033[32m%s\033[0m>", newnetconfig);
	SaveRoomNetConfigFile(CC->room.QRnumber, newnetconfig);
	end_critical_section(S_NETCONFIGS);
	free(newnetconfig);			// this was the new netconfig, free it because we're done with it
	free(oldnetconfig);			// this was the old netconfig, free it even if we didn't do anything

	// Tell the client what happened.
	if ((action == SUBSCRIBE) && (is_already_subscribed)) {
		cprintf("%d This email is already subscribed.\n", ERROR + ALREADY_EXISTS);
	}
	else if ((action == SUBSCRIBE) && (!is_already_subscribed)) {
		cprintf("%d Confirmation email sent.\n", CIT_OK);
	}
	else if ((action == UNSUBSCRIBE) && (!is_already_subscribed)) {
		cprintf("%d This email is not subscribed.\n", ERROR + NO_SUCH_USER);
	}
	else if ((action == UNSUBSCRIBE) && (is_already_subscribed)) {
		cprintf("%d Confirmation email sent.\n", CIT_OK);
	}
	else {
		cprintf("%d FIXME tell the client what we did\n", ERROR);
	}
}


/* 
 * process subscribe/unsubscribe requests and confirmations
 */
void cmd_subs(char *cmdbuf) {
	char cmd[20];
	char roomname[ROOMNAMELEN];
	char emailaddr[1024];
	char options[256];
	char url[1024];
	char token[128];

	extract_token(cmd, cmdbuf, 0, '|', sizeof cmd);				// token 0 is the sub-command being sent
	extract_token(roomname, cmdbuf, 1, '|', sizeof roomname);		// token 1 is always a room name

	// First confirm that the caller is referencing a room that actually exists.
	if (CtdlGetRoom(&CC->room, roomname) != 0) {
		cprintf("%d There is no list called '%s'\n", ERROR + ROOM_NOT_FOUND, roomname);
		return;
	}

	if ((CC->room.QRflags2 & QR2_SELFLIST) == 0) {
		cprintf("%d '%s' does not accept subscribe/unsubscribe requests.\n", ERROR + ROOM_NOT_FOUND, roomname);
		return;
	}

	// Room confirmed, now parse the command.

	if (!strcasecmp(cmd, "subscribe")) {
		extract_token(emailaddr, cmdbuf, 2, '|', sizeof emailaddr);	// token 2 is the subscriber's email address
		extract_token(options, cmdbuf, 3, '|', sizeof options);		// there are no options ... ignore this token
		extract_token(url, cmdbuf, 4, '|', sizeof url);			// token 3 is the URL at which we subscribed
		do_subscribe_or_unsubscribe(SUBSCRIBE, emailaddr, url);
	}

	else if (!strcasecmp(cmd, "unsubscribe")) {
		extract_token(emailaddr, cmdbuf, 2, '|', sizeof emailaddr);	// token 2 is the subscriber's email address
		extract_token(options, cmdbuf, 3, '|', sizeof options);		// there are no options ... ignore this token
		extract_token(url, cmdbuf, 4, '|', sizeof url);			// token 3 is the URL at which we subscribed
		do_subscribe_or_unsubscribe(UNSUBSCRIBE, emailaddr, url);
	}

	else if (!strcasecmp(cmd, "confirm")) {
		extract_token(token, cmdbuf, 2, '|', sizeof token);		// token 2 is the confirmation token
		cprintf("%d not implemented\n", ERROR);
	}

	else {									// sorry man, I can't deal with that
		cprintf("%d Invalid command '%s'\n", ERROR + ILLEGAL_VALUE, cmd);
	}
}


/*
 * Module entry point
 */
CTDL_MODULE_INIT(listsub)
{
	if (!threading)
	{
		CtdlRegisterProtoHook(cmd_subs, "SUBS", "List subscribe/unsubscribe");
	}
	
	/* return our module name for the log */
	return "listsub";
}
