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

	char *netconfig, *newnetconfig;
	int config_lines, i;
	char buf[1024];

	// Update this room's netconfig with the updated lastsent
	begin_critical_section(S_NETCONFIGS);
        netconfig = LoadRoomNetConfigFile(CC->room.QRnumber);
        if (!netconfig) {
		netconfig = strdup("");
	}

	// The new netconfig begins with the new lastsent directive
	newnetconfig = malloc(strlen(netconfig) + 1024);
#if 0
	FIXME SYNTAX ERROR #$%$&%$^#%$ sprintf(newnetconfig, "lastsent|%ld\n", ld.msgnum);
#endif

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
	free(newnetconfig);			// this was the new netconfig, free it because we're done with it
	free(netconfig);			// this was the old netconfig, free it even if we didn't do anything

#if 0
	FIXME tell the client what we did
#endif


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
