/*
 * Consolidate mail from remote POP3 accounts.
 *
 * Copyright (c) 2007-2017 by the citadel.org team
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


struct CitContext pop3_client_CC;
static int doing_pop3client = 0;


//	This is how we form a USETABLE record for pop3 client
//
//	StrBufPrintf(RecvMsg->CurrMsg->MsgUID,
//		     "pop3/%s/%s:%s@%s",
//		     ChrPtr(RecvMsg->RoomName),
//		     ChrPtr(RecvMsg->CurrMsg->MsgUIDL),
//		     RecvMsg->IO.ConnectMe->User,
//		     RecvMsg->IO.ConnectMe->Host
//	);


/*
 * Process one mailbox.
 */
void pop3client_one_mailbox(struct ctdlroom *qrbuf, const char *host, const char *user, const char *pass, int keep, long interval)
{
		syslog(LOG_DEBUG, "\033[33mpop3client: room=<%s> host=<%s> user=<%s> pass=<%s> keep=<%d> interval=<%ld>\033[0m",
			qrbuf->QRname, host, user, pass, keep, interval
		);
}


/*
 * Scan a room's netconfig to determine whether it requires POP3 aggregation
 */
void pop3client_scan_room(struct ctdlroom *qrbuf, void *data, OneRoomNetCfg *OneRNCFG)
{
	const RoomNetCfgLine *pLine;

	if (server_shutting_down) return;

	pLine = OneRNCFG->NetConfigs[pop3client];

	while (pLine != NULL)
	{
		pop3client_one_mailbox(qrbuf, 
			ChrPtr(pLine->Value[0]),
			ChrPtr(pLine->Value[1]),
			ChrPtr(pLine->Value[2]),
			atoi(ChrPtr(pLine->Value[3])),
			atol(ChrPtr(pLine->Value[4]))
		);
		pLine = pLine->next;

	}
}


void pop3client_scan(void) {
	static time_t last_run = 0L;
	time_t fastest_scan;

	become_session(&pop3_client_CC);

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

	syslog(LOG_DEBUG, "pop3client started");
	CtdlForEachNetCfgRoom(pop3client_scan_room, NULL);
	syslog(LOG_DEBUG, "pop3client ended");
	last_run = time(NULL);
	doing_pop3client = 0;
}


CTDL_MODULE_INIT(pop3client)
{
	if (!threading)
	{
		CtdlFillSystemContext(&pop3_client_CC, "POP3aggr");
		CtdlREGISTERRoomCfgType(pop3client, ParseGeneric, 0, 5, SerializeGeneric, DeleteGenericCfgLine);
		CtdlRegisterSessionHook(pop3client_scan, EVT_TIMER, PRIO_AGGR + 50);
	}

	/* return our module id for the log */
 	return "pop3client";
}
