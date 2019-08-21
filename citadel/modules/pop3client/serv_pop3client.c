/*
 * Consolidate mail from remote POP3 accounts.
 *
 * Copyright (c) 2007-2019 by the citadel.org team
 *
 * This program is open source software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as published
 * by the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <sysconfig.h>

#if TIME_WITH_SYS_TIME
# include <sys/time.h>
# include <time.h>
#else
# if HAVE_SYS_TIME_H
#  include <sys/time.h>
# else
#  include <time.h>
# endif
#endif

#include <ctype.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <libcitadel.h>
#include <curl/curl.h>
#include "citadel.h"
#include "server.h"
#include "citserver.h"
#include "support.h"
#include "config.h"
#include "ctdl_module.h"
#include "clientsocket.h"
#include "msgbase.h"
#include "internet_addressing.h"
#include "database.h"
#include "citadel_dirs.h"

struct p3cq {				// module-local queue of pop3 client work that needs processing
	struct p3cq *next;
	char *room;
	char *host;
	char *user;
	char *pass;
	int keep;
	long interval;
};

static int doing_pop3client = 0;
struct p3cq *p3cq = NULL;

/*
 * Process one mailbox.
 */
void pop3client_one_mailbox(char *room, const char *host, const char *user, const char *pass, int keep, long interval)
{
	syslog(LOG_DEBUG, "pop3client: room=<%s> host=<%s> user=<%s> keep=<%d> interval=<%ld>", room, host, user, keep, interval);

	char url[SIZ];
	CURL *curl;
	CURLcode res = CURLE_OK;
	StrBuf *Uidls = NULL;
	int i;
	char cmd[1024];

	curl = curl_easy_init();
	if (!curl) {
		return;
	}

	Uidls = NewStrBuf();

	curl_easy_setopt(curl, CURLOPT_USERNAME, user);
	curl_easy_setopt(curl, CURLOPT_PASSWORD, pass);
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, CurlFillStrBuf_callback);	// What to do with downloaded data
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, Uidls);			// Give it our StrBuf to work with
	curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "UIDL");

	/* Try POP3S (SSL encrypted) first */
	snprintf(url, sizeof url, "pop3s://%s", host);
	curl_easy_setopt(curl, CURLOPT_URL, url);
	res = curl_easy_perform(curl);
	if (res == CURLE_OK) {
	} else {
		syslog(LOG_DEBUG, "pop3client: POP3S connection failed: %s , trying POP3 next", curl_easy_strerror(res));
		snprintf(url, sizeof url, "pop3://%s", host);			// try unencrypted next
		curl_easy_setopt(curl, CURLOPT_URL, url);
		FlushStrBuf(Uidls);
		res = curl_easy_perform(curl);
	}

	if (res != CURLE_OK) {
		syslog(LOG_DEBUG, "pop3client: POP3 connection failed: %s", curl_easy_strerror(res));
		curl_easy_cleanup(curl);
		FreeStrBuf(&Uidls);
		return;
	}

	// If we got this far, a connection was established, we know whether it's pop3s or pop3, and UIDL is supported.
	// Now go through the UIDL list and look for messages.

	int num_msgs = num_tokens(ChrPtr(Uidls), '\n');
	syslog(LOG_DEBUG, "pop3client: there are %d messages", num_msgs);
	for (i=0; i<num_msgs; ++i) {
		char oneuidl[1024];
		extract_token(oneuidl, ChrPtr(Uidls), i, '\n', sizeof oneuidl);
		if (strlen(oneuidl) > 2) {
			if (oneuidl[strlen(oneuidl)-1] == '\r') {
				oneuidl[strlen(oneuidl)-1] = 0;
			}
			int this_msg = atoi(oneuidl);
			char *c = strchr(oneuidl, ' ');
			if (c) strcpy(oneuidl, ++c);

			// Make up the Use Table record so we can check if we've already seen this message.
			StrBuf *UT = NewStrBuf();
			StrBufPrintf(UT, "pop3/%s/%s:%s@%s", room, oneuidl, user, host);
			int already_seen = CheckIfAlreadySeen(UT);
			FreeStrBuf(&UT);

			// Only fetch the message if we haven't seen it before.
			if (already_seen == 0) {
				StrBuf *TheMsg = NewStrBuf();
				snprintf(cmd, sizeof cmd, "RETR %d", this_msg);
				curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, cmd);
				curl_easy_setopt(curl, CURLOPT_WRITEDATA, TheMsg);
				res = curl_easy_perform(curl);
				if (res == CURLE_OK) {
					struct CtdlMessage *msg = convert_internet_message_buf(&TheMsg);
					CtdlSubmitMsg(msg, NULL, room, 0);
					CM_Free(msg);
				}
				else {
					FreeStrBuf(&TheMsg);
				}

				// Unless the configuration says to keep the message on the server, delete it.
				if (keep == 0) {
					snprintf(cmd, sizeof cmd, "DELE %d", this_msg);
					curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, cmd);
					res = curl_easy_perform(curl);
				}
			}
			else {
				syslog(LOG_DEBUG, "pop3client: %s has already been retrieved", oneuidl);
			}
		}
	}

	curl_easy_cleanup(curl);
	FreeStrBuf(&Uidls);
	return;
}


/*
 * Scan a room's netconfig to determine whether it requires POP3 aggregation
 */
void pop3client_scan_room(struct ctdlroom *qrbuf, void *data, OneRoomNetCfg *OneRNCFG)
{
	const RoomNetCfgLine *pLine;
	struct p3cq *pptr = NULL;

	if (server_shutting_down) return;

	pLine = OneRNCFG->NetConfigs[pop3client];

	while (pLine != NULL)
	{
		pptr = malloc(sizeof(struct p3cq));
		pptr->next = p3cq;
		p3cq = pptr;
		pptr->room = strdup(qrbuf->QRname);
		pptr->host = strdup(ChrPtr(pLine->Value[0]));
		pptr->user = strdup(ChrPtr(pLine->Value[1]));
		pptr->pass = strdup(ChrPtr(pLine->Value[2]));
		pptr->keep = atoi(ChrPtr(pLine->Value[3]));
		pptr->interval = atol(ChrPtr(pLine->Value[4]));
	
		pLine = pLine->next;
	}
}


void pop3client_scan(void) {
	static time_t last_run = 0L;
	time_t fastest_scan;
	struct p3cq *pptr = NULL;

	if (CtdlGetConfigLong("c_pop3_fastest") < CtdlGetConfigLong("c_pop3_fetch")) {
		fastest_scan = CtdlGetConfigLong("c_pop3_fastest");
	}
	else {
		fastest_scan = CtdlGetConfigLong("c_pop3_fetch");
	}

	/*
	 * Run POP3 aggregation no more frequently than once every n seconds
	 */
	if ( (time(NULL) - last_run) < fastest_scan ) {
		return;
	}

	/*
	 * This is a simple concurrency check to make sure only one pop3client
	 * run is done at a time.  We could do this with a mutex, but since we
	 * don't really require extremely fine granularity here, we'll do it
	 * with a static variable instead.
	 */
	if (doing_pop3client) return;
	doing_pop3client = 1;

	syslog(LOG_DEBUG, "pop3client: scan started");
	CtdlForEachNetCfgRoom(pop3client_scan_room, NULL);

	/*
	 * We have to queue and process in separate phases, otherwise we leave a cursor open
	 */
	syslog(LOG_DEBUG, "pop3client: processing started");
	while (p3cq != NULL) {
		pptr = p3cq;
		p3cq = p3cq->next;

		pop3client_one_mailbox(pptr->room, pptr->host, pptr->user, pptr->pass, pptr->keep, pptr->interval);

		free(pptr->room);
		free(pptr->host);
		free(pptr->user);
		free(pptr->pass);
		free(pptr);
	}

	syslog(LOG_DEBUG, "pop3client: ended");
	last_run = time(NULL);
	doing_pop3client = 0;
}


CTDL_MODULE_INIT(pop3client)
{
	if (!threading)
	{
		CtdlREGISTERRoomCfgType(pop3client, ParseGeneric, 0, 5, SerializeGeneric, DeleteGenericCfgLine);
		CtdlRegisterSessionHook(pop3client_scan, EVT_TIMER, PRIO_AGGR + 50);
	}

	/* return our module id for the log */
 	return "pop3client";
}
