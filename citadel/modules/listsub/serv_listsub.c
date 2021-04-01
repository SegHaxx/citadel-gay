//
// This module handles self-service subscription/unsubscription to mail lists.
//
// Copyright (c) 2002-2021 by the citadel.org team
//
// This program is open source software.  It runs great on the
// Linux operating system (and probably elsewhere).  You can use,
// copy, and run it under the terms of the GNU General Public
// License version 3.
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
 * This generates an email with a link the user clicks to confirm a list subscription.
 */
void send_subscribe_confirmation_email(char *roomname, char *emailaddr, char *url, char *confirmation_token) {
	// We need a URL-safe representation of the room name
	char urlroom[ROOMNAMELEN+10];
	urlesc(urlroom, sizeof(urlroom), roomname);

	char from_address[1024];
	snprintf(from_address, sizeof from_address, "noreply@%s", CtdlGetConfigStr("c_fqdn"));

	char emailtext[SIZ];
	snprintf(emailtext, sizeof emailtext,
		"MIME-Version: 1.0\n"
		"Content-Type: multipart/alternative; boundary=\"__ctdlmultipart__\"\n"
		"\n"
		"This is a multipart message in MIME format.\n"
		"\n"
		"--__ctdlmultipart__\n"
		"Content-type: text/plain\n"
		"\n"
		"Someone (probably you) has submitted a request to subscribe\n"
		"<%s> to the <%s> mailing list.\n"
		"\n"
		"Please go here to confirm this request:\n"
		"%s?room=%s&token=%s&cmd=confirm\n"
		"\n"
		"If this request has been submitted in error and you do not\n"
		"wish to receive the <%s> mailing list, simply do nothing,\n"
		"and you will not receive any further mailings.\n"
		"\n"
		"--__ctdlmultipart__\n"
		"Content-type: text/html\n"
		"\n"
		"<html><body><p>Someone (probably you) has submitted a request to subscribe "
		"<strong>%s</strong> to the <strong>%s</strong> mailing list.</p>"
		"<p>Please go here to confirm this request:</p>"
		"<p><a href=\"%s?room=%s&token=%s&cmd=confirm\">"
		"%s?room=%s&token=%s&cmd=confirm</a></p>"
		"<p>If this request has been submitted in error and you do not "
		"wish to receive the <strong>%s<strong> mailing list, simply do nothing, "
		"and you will not receive any further mailings.</p>"
		"</body></html>\n"
		"\n"
		"--__ctdlmultipart__--\n"
		,
		emailaddr, roomname,
		url, urlroom, confirmation_token,
		roomname
		,
		emailaddr, roomname,
		url, urlroom, confirmation_token,
		url, urlroom, confirmation_token,
		roomname
	);

	quickie_message("Citadel", from_address, emailaddr, NULL, emailtext, FMT_RFC822, "Please confirm your list subscription");
}


/*
 * This generates an email with a link the user clicks to confirm a list unsubscription.
 */
void send_unsubscribe_confirmation_email(char *roomname, char *emailaddr, char *url, char *confirmation_token) {
	// We need a URL-safe representation of the room name
	char urlroom[ROOMNAMELEN+10];
	urlesc(urlroom, sizeof(urlroom), roomname);

	char from_address[1024];
	snprintf(from_address, sizeof from_address, "noreply@%s", CtdlGetConfigStr("c_fqdn"));

	char emailtext[SIZ];
	snprintf(emailtext, sizeof emailtext,
		"MIME-Version: 1.0\n"
		"Content-Type: multipart/alternative; boundary=\"__ctdlmultipart__\"\n"
		"\n"
		"This is a multipart message in MIME format.\n"
		"\n"
		"--__ctdlmultipart__\n"
		"Content-type: text/plain\n"
		"\n"
		"Someone (probably you) has submitted a request to unsubscribe\n"
		"<%s> from the <%s> mailing list.\n"
		"\n"
		"Please go here to confirm this request:\n"
		"%s?room=%s&token=%s&cmd=confirm\n"
		"\n"
		"If this request has been submitted in error and you still\n"
		"wish to receive the <%s> mailing list, simply do nothing,\n"
		"and you will remain subscribed.\n"
		"\n"
		"--__ctdlmultipart__\n"
		"Content-type: text/html\n"
		"\n"
		"<html><body><p>Someone (probably you) has submitted a request to unsubscribe "
		"<strong>%s</strong> from the <strong>%s</strong> mailing list.</p>"
		"<p>Please go here to confirm this request:</p>"
		"<p><a href=\"%s?room=%s&token=%s&cmd=confirm\">"
		"%s?room=%s&token=%s&cmd=confirm</a></p>"
		"<p>If this request has been submitted in error and you still "
		"wish to receive the <strong>%s<strong> mailing list, simply do nothing, "
		"and you will remain subscribed.</p>"
		"</body></html>\n"
		"\n"
		"--__ctdlmultipart__--\n"
		,
		emailaddr, roomname,
		url, urlroom, confirmation_token,
		roomname
		,
		emailaddr, roomname,
		url, urlroom, confirmation_token,
		url, urlroom, confirmation_token,
		roomname
	);

	quickie_message("Citadel", from_address, emailaddr, NULL, emailtext, FMT_RFC822, "Please confirm your list unsubscription");
}


/*
 * "Subscribe" and "Unsubscribe" operations are so similar that they share a function.
 * The actual subscription doesn't take place here -- we just send out the confirmation request
 * and record the address and confirmation token.
 */
void do_subscribe_or_unsubscribe(int action, char *emailaddr, char *url) {

	int i;
	char buf[1024];
	char confirmation_token[40];

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

		char buf_directive[1024];
		char buf_email[1024];
		extract_token(buf_directive, buf, 0, '|', sizeof buf_directive);
		extract_token(buf_email, buf, 1, '|', sizeof buf_email);

		if (	( (!strcasecmp(buf_directive, "listrecp")) || (!strcasecmp(buf_directive, "digestrecp")) )
			&& (!strcasecmp(buf_email, emailaddr)) 
		) {
			is_already_subscribed = 1;
		}

		if ( (!strcasecmp(buf_directive, "subpending")) || (!strcasecmp(buf_directive, "unsubpending")) ) {
			time_t pendingtime = extract_long(buf, 3);
			if ((time(NULL) - pendingtime) > 259200) {
				syslog(LOG_DEBUG, "%s %s is %ld seconds old - deleting it", buf_email, buf_directive, time(NULL) - pendingtime);
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
		send_subscribe_confirmation_email(CC->room.QRname, emailaddr, url, confirmation_token);
	}
	if ((action == UNSUBSCRIBE) && (is_already_subscribed)) {
		generate_uuid(confirmation_token);
		sprintf(&newnetconfig[strlen(newnetconfig)], "unsubpending|%s|%s|%ld|%s", emailaddr, confirmation_token, time(NULL), url);
		send_unsubscribe_confirmation_email(CC->room.QRname, emailaddr, url, confirmation_token);
	}

	// Write the new netconfig back to disk
	SaveRoomNetConfigFile(CC->room.QRnumber, newnetconfig);
	end_critical_section(S_NETCONFIGS);
	free(newnetconfig);			// this was the new netconfig, free it because we're done with it
	free(oldnetconfig);			// this was the old netconfig, free it even if we didn't do anything

	// Tell the client what happened.
	if ((action == SUBSCRIBE) && (is_already_subscribed)) {
		cprintf("%d This email address is already subscribed.\n", ERROR + ALREADY_EXISTS);
	}
	else if ((action == SUBSCRIBE) && (!is_already_subscribed)) {
		cprintf("%d Subscription was requested, and a confirmation email was sent.\n", CIT_OK);
	}
	else if ((action == UNSUBSCRIBE) && (!is_already_subscribed)) {
		cprintf("%d This email address is not subscribed.\n", ERROR + NO_SUCH_USER);
	}
	else if ((action == UNSUBSCRIBE) && (is_already_subscribed)) {
		cprintf("%d Unsubscription was requested, and a confirmation email was sent.\n", CIT_OK);
	}
	else {
		cprintf("%d Nothing happens.\n", ERROR);
	}
}


/*
 * Confirm a list subscription or unsubscription
 */
void do_confirm(char *token) {
	int yes_subscribe = 0;				// Set to 1 if the confirmation to subscribe is validated.
	int yes_unsubscribe = 0;			// Set to 1 if the confirmation to unsubscribe is validated.
	int i;
	char buf[1024];
	int config_lines = 0;
	char pending_directive[128];
	char pending_email[256];
	char pending_token[128];

	// We will have to do this in two passes.  The first pass checks to see if we have a confirmation request matching the token.
        char *oldnetconfig = LoadRoomNetConfigFile(CC->room.QRnumber);
        if (!oldnetconfig) {
		cprintf("%d There are no pending requests.\n", ERROR + NO_SUCH_USER);
		return;
	}

	config_lines = num_tokens(oldnetconfig, '\n');
	for (i=0; i<config_lines; ++i) {
		extract_token(buf, oldnetconfig, i, '\n', sizeof buf);
		extract_token(pending_directive, buf, 0, '|', sizeof pending_directive);
		extract_token(pending_email, buf, 1, '|', sizeof pending_email);
		extract_token(pending_token, buf, 2, '|', sizeof pending_token);

		if (!strcasecmp(pending_token, token)) {
			if (!strcasecmp(pending_directive, "subpending")) {
				yes_subscribe = 1;
			}
			else if (!strcasecmp(pending_directive, "unsubpending")) {
				yes_unsubscribe = 1;
			}
		}
	}
	free(oldnetconfig);

	// We didn't find a pending subscribe or unsubscribe request with the supplied token.
	if ((!yes_subscribe) && (!yes_unsubscribe)) {
		cprintf("%d The request you are trying to confirm was not found.\n", ERROR + NO_SUCH_USER);
		return;
	}

	// The second pass performs the now confirmed operation.
	// We will have to do this in two passes.  The first pass checks to see if we have a confirmation request matching the token.
        oldnetconfig = LoadRoomNetConfigFile(CC->room.QRnumber);
        if (!oldnetconfig) {
		oldnetconfig = strdup("");
	}

	// The new netconfig begins with an empty buffer...
	begin_critical_section(S_NETCONFIGS);
	char *newnetconfig = malloc(strlen(oldnetconfig) + 1024);
	newnetconfig[0] = 0;

	config_lines = num_tokens(oldnetconfig, '\n');
	for (i=0; i<config_lines; ++i) {
		char buf_email[256];
		extract_token(buf, oldnetconfig, i, '\n', sizeof buf);
		extract_token(buf_email, buf, 1, '|', sizeof pending_email);
		if (strcasecmp(buf_email, pending_email)) {
			sprintf(&newnetconfig[strlen(newnetconfig)], "%s\n", buf);	// only keep lines that do not reference this subscriber
		}
	}

	// We have now removed all lines containing the subscriber's email address.  This deletes any pending requests.
	// If this was an unsubscribe operation, they're now gone from the list.
	// But if this was a subscribe operation, we now need to add them.
	if (yes_subscribe) {
		sprintf(&newnetconfig[strlen(newnetconfig)], "listrecp|%s\n", pending_email);
	}

	// FIXME write it back to disk
	SaveRoomNetConfigFile(CC->room.QRnumber, newnetconfig);
	end_critical_section(S_NETCONFIGS);
	free(oldnetconfig);
	free(newnetconfig);
	cprintf("%d The pending request was confirmed.\n", CIT_OK);
}


/* 
 * process subscribe/unsubscribe requests and confirmations
 */
void cmd_lsub(char *cmdbuf) {
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
		extract_token(url, cmdbuf, 3, '|', sizeof url);			// token 3 is the URL at which we subscribed
		do_subscribe_or_unsubscribe(SUBSCRIBE, emailaddr, url);
	}

	else if (!strcasecmp(cmd, "unsubscribe")) {
		extract_token(emailaddr, cmdbuf, 2, '|', sizeof emailaddr);	// token 2 is the subscriber's email address
		extract_token(url, cmdbuf, 3, '|', sizeof url);			// token 3 is the URL at which we subscribed
		do_subscribe_or_unsubscribe(UNSUBSCRIBE, emailaddr, url);
	}

	else if (!strcasecmp(cmd, "confirm")) {
		extract_token(token, cmdbuf, 2, '|', sizeof token);		// token 2 is the confirmation token
		do_confirm(token);
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
		CtdlRegisterProtoHook(cmd_lsub, "LSUB", "List subscribe/unsubscribe");
	}
	
	/* return our module name for the log */
	return "listsub";
}
