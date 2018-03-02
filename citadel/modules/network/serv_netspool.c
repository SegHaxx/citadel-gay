/*
 * This module handles shared rooms, inter-Citadel mail, and outbound
 * mailing list processing.
 *
 * Copyright (c) 2000-2018 by the citadel.org team
 *
 * This program is open source software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * ** NOTE **   A word on the S_NETCONFIGS semaphore:
 * This is a fairly high-level type of critical section.  It ensures that no
 * two threads work on the netconfigs files at the same time.  Since we do
 * so many things inside these, here are the rules:
 *  1. begin_critical_section(S_NETCONFIGS) *before* begin_ any others.
 *  2. Do *not* perform any I/O with the client during these sections.
 *
 */

/*
 * Duration of time (in seconds) after which pending list subscribe/unsubscribe
 * requests that have not been confirmed will be deleted.
 */
#define EXP	259200	/* three days */

#include "sysdep.h"
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <ctype.h>
#include <signal.h>
#include <pwd.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
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
#ifdef HAVE_SYSCALL_H
# include <syscall.h>
#else 
# if HAVE_SYS_SYSCALL_H
#  include <sys/syscall.h>
# endif
#endif

#include <sys/wait.h>
#include <string.h>
#include <limits.h>
#include <assert.h>
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
#include "serv_network.h"
#include "clientsocket.h"
#include "citadel_dirs.h"
#include "threads.h"
#include "context.h"
#include "ctdl_module.h"
#include "netspool.h"
#include "netmail.h"


void ParseLastSent(const CfgLineType *ThisOne, StrBuf *Line, const char *LinePos, OneRoomNetCfg *OneRNCFG)
{
	RoomNetCfgLine *nptr;
	nptr = (RoomNetCfgLine *) malloc(sizeof(RoomNetCfgLine));
	memset(nptr, 0, sizeof(RoomNetCfgLine));
	OneRNCFG->lastsent = extract_long(LinePos, 0);
	OneRNCFG->NetConfigs[ThisOne->C] = nptr;
}

void ParseRoomAlias(const CfgLineType *ThisOne, StrBuf *Line, const char *LinePos, OneRoomNetCfg *rncfg)
{
	if (rncfg->Sender != NULL) {
		return;
	}

	ParseGeneric(ThisOne, Line, LinePos, rncfg);
	rncfg->Sender = NewStrBufDup(rncfg->NetConfigs[roommailalias]->Value[0]);
}

void ParseSubPendingLine(const CfgLineType *ThisOne, StrBuf *Line, const char *LinePos, OneRoomNetCfg *OneRNCFG)
{
	if (time(NULL) - extract_long(LinePos, 3) > EXP) 
		return; /* expired subscription... */

	ParseGeneric(ThisOne, Line, LinePos, OneRNCFG);
}
void ParseUnSubPendingLine(const CfgLineType *ThisOne, StrBuf *Line, const char *LinePos, OneRoomNetCfg *OneRNCFG)
{
	if (time(NULL) - extract_long(LinePos, 2) > EXP)
		return; /* expired subscription... */

	ParseGeneric(ThisOne, Line, LinePos, OneRNCFG);
}


void SerializeLastSent(const CfgLineType *ThisOne, StrBuf *OutputBuffer, OneRoomNetCfg *RNCfg, RoomNetCfgLine *data)
{
	StrBufAppendBufPlain(OutputBuffer, CKEY(ThisOne->Str), 0);
	StrBufAppendPrintf(OutputBuffer, "|%ld\n", RNCfg->lastsent);
}

void DeleteLastSent(const CfgLineType *ThisOne, RoomNetCfgLine **data)
{
	free(*data);
	*data = NULL;
}

static const RoomNetCfg SpoolCfgs [4] = {
	listrecp,
	digestrecp,
	participate,
};

static const long SpoolCfgsCopyN [4] = {
	1, 1, 1, 2
};

int HaveSpoolConfig(OneRoomNetCfg* RNCfg)
{
	int i;
	int interested = 0;
	for (i=0; i < 4; i++) if (RNCfg->NetConfigs[SpoolCfgs[i]] == NULL) interested = 1;
	return interested;
}


void InspectQueuedRoom(SpoolControl **pSC,
		       RoomProcList *room_to_spool,     
		       HashList *working_ignetcfg,
		       HashList *the_netmap
) {
	SpoolControl *sc;
	int i = 0;

	syslog(LOG_INFO, "netspool: InspectQueuedRoom(%s)", room_to_spool->name);

	sc = (SpoolControl*)malloc(sizeof(SpoolControl));
	memset(sc, 0, sizeof(SpoolControl));
	sc->working_ignetcfg = working_ignetcfg;
	sc->the_netmap = the_netmap;

	/*
	 * If the room doesn't exist, don't try to perform its networking tasks.
	 * Normally this should never happen, but once in a while maybe a room gets
	 * queued for networking and then deleted before it can happen.
	 */
	if (CtdlGetRoom(&sc->room, room_to_spool->name) != 0) {
		syslog(LOG_INFO, "netspool: ERROR; cannot load <%s>", room_to_spool->name);
		free(sc);
		return;
	}

	assert(sc->RNCfg == NULL);	// checking to make sure we cleared it from last time

	sc->RNCfg = CtdlGetNetCfgForRoom(sc->room.QRnumber);

	syslog(LOG_DEBUG, "netspool: room <%s> highest=%ld lastsent=%ld", room_to_spool->name, sc->room.QRhighest, sc->RNCfg->lastsent);
	if ( (!HaveSpoolConfig(sc->RNCfg)) || (sc->room.QRhighest <= sc->RNCfg->lastsent) ) 
	{
		// There is nothing to send from this room.
		syslog(LOG_DEBUG, "netspool: nothing to do for <%s>", room_to_spool->name);
		FreeRoomNetworkStruct(&sc->RNCfg);
		sc->RNCfg = NULL;
		free(sc);
		return;
	}

	sc->lastsent = sc->RNCfg->lastsent;
	room_to_spool->lastsent = sc->lastsent;

	/* Now lets remember whats needed for the actual work... */

	for (i=0; i < 4; i++)
	{
		aggregate_recipients(&sc->Users[SpoolCfgs[i]], SpoolCfgs[i], sc->RNCfg, SpoolCfgsCopyN[i]);
	}
	
	if (StrLength(sc->RNCfg->Sender) > 0) {
		sc->Users[roommailalias] = NewStrBufDup(sc->RNCfg->Sender);
	}

	sc->next = *pSC;
	*pSC = sc;

	FreeRoomNetworkStruct(&sc->RNCfg);	// done with this for now, we'll grab it again next time
	sc->RNCfg = NULL;
}


void CalcListID(SpoolControl *sc)
{
	StrBuf *RoomName;
	struct CitContext *CCC = CC;
#define MAX_LISTIDLENGTH 150

	// Load the room banner as the list description
	struct CtdlMessage *msg = CtdlFetchMessage(sc->room.msgnum_info, 1, 1);
        if (msg != NULL) {
		CC->redirect_buffer = NewStrBufPlain(NULL, SIZ);
                CtdlOutputPreLoadedMsg(msg, MT_CITADEL, HEADERS_NONE, 0, 0, 0);
                CM_Free(msg);
		sc->RoomInfo = CC->redirect_buffer;
		CC->redirect_buffer = NULL;
        }
	else {
		sc->RoomInfo = NewStrBufPlain(NULL, SIZ);
	}

	// Calculate the List ID
	sc->ListID = NewStrBufPlain(NULL, 1024);
	if (StrLength(sc->RoomInfo) > 0)
	{
		const char *Pos = NULL;
		StrBufSipLine(sc->ListID, sc->RoomInfo, &Pos);

		if (StrLength(sc->ListID) > MAX_LISTIDLENGTH)
		{
			StrBufCutAt(sc->ListID, MAX_LISTIDLENGTH, NULL);
			StrBufAppendBufPlain(sc->ListID, HKEY("..."), 0);
		}
		StrBufAsciify(sc->ListID, ' ');
	}
	else
	{
		StrBufAppendBufPlain(sc->ListID, CCC->room.QRname, -1, 0);
	}

	StrBufAppendBufPlain(sc->ListID, HKEY("<"), 0);
	RoomName = NewStrBufPlain (sc->room.QRname, -1);
	StrBufAsciify(RoomName, '_');
	StrBufReplaceChars(RoomName, ' ', '_');

	if (StrLength(sc->Users[roommailalias]) > 0)
	{
		long Pos;
		const char *AtPos;

		Pos = StrLength(sc->ListID);
		StrBufAppendBuf(sc->ListID, sc->Users[roommailalias], 0);
		AtPos = strchr(ChrPtr(sc->ListID) + Pos, '@');

		if (AtPos != NULL)
		{
			StrBufPeek(sc->ListID, AtPos, 0, '.');
		}
	}
	else
	{
		StrBufAppendBufPlain(sc->ListID, HKEY("room_"), 0);
		StrBufAppendBuf(sc->ListID, RoomName, 0);
		StrBufAppendBufPlain(sc->ListID, HKEY("."), 0);
		StrBufAppendBufPlain(sc->ListID, CtdlGetConfigStr("c_fqdn"), -1, 0);
		/*
		 * this used to be:
		 * roomname <Room-Number.list-id.fqdn>
		 * according to rfc2919.txt it only has to be a unique identifier
		 * under the domain of the system; 
		 * in general MUAs use it to calculate the reply address nowadays.
		 */
	}
	StrBufAppendBufPlain(sc->ListID, HKEY(">"), 0);

	if (StrLength(sc->Users[roommailalias]) == 0)
	{
		sc->Users[roommailalias] = NewStrBuf();
		
		StrBufAppendBufPlain(sc->Users[roommailalias], HKEY("room_"), 0);
		StrBufAppendBuf(sc->Users[roommailalias], RoomName, 0);
		StrBufAppendBufPlain(sc->Users[roommailalias], HKEY("@"), 0);
		StrBufAppendBufPlain(sc->Users[roommailalias], CtdlGetConfigStr("c_fqdn"), -1, 0);

		StrBufLowerCase(sc->Users[roommailalias]);
	}

	FreeStrBuf(&RoomName);
}


/*
 * Batch up and send all outbound traffic from the current room (this is definitely used for mailing lists)
 */
void network_spoolout_room(SpoolControl *sc)
{
	struct CitContext *CCC = CC;
	char buf[SIZ];
	int i;
	long lastsent;

	/*
	 * If the room doesn't exist, don't try to perform its networking tasks.
	 * Normally this should never happen, but once in a while maybe a room gets
	 * queued for networking and then deleted before it can happen.
	 */
	memcpy (&CCC->room, &sc->room, sizeof(ctdlroom));

	syslog(LOG_INFO, "netspool: network_spoolout_room(room=%s, lastsent=%ld)", CCC->room.QRname, sc->lastsent);

	CalcListID(sc);

	/* remember where we started... */
	lastsent = sc->lastsent;

	/* Fetch the messages we ought to send & prepare them. */
	CtdlForEachMessage(MSGS_GT, sc->lastsent, NULL, NULL, NULL, network_spool_msg, sc);

	if (StrLength(sc->Users[roommailalias]) > 0)
	{
		long len;
		len = StrLength(sc->Users[roommailalias]);
		if (len + 1 > sizeof(buf))
			len = sizeof(buf) - 1;
		memcpy(buf, ChrPtr(sc->Users[roommailalias]), len);
		buf[len] = '\0';
	}
	else
	{
		snprintf(buf, sizeof buf, "room_%s@%s", CCC->room.QRname, CtdlGetConfigStr("c_fqdn"));
	}

	for (i=0; buf[i]; ++i) {
		buf[i] = tolower(buf[i]);
		if (isspace(buf[i])) buf[i] = '_';
	}

	/* If we wrote a digest, deliver it and then close it */
	if ( (sc->Users[digestrecp] != NULL) && (sc->digestfp != NULL) )
	{
		fprintf(sc->digestfp,
			" ------------------------------------------------------------------------------\n"
			"You are subscribed to the '%s' list.\n"
			"To post to the list: %s\n",
			CCC->room.QRname, buf
		);
		network_deliver_digest(sc);	/* deliver */
		fclose(sc->digestfp);
		sc->digestfp = NULL;
		remove_digest_file(&sc->room);
	}

	/* Now rewrite the netconfig */
	syslog(LOG_DEBUG, "netspool: lastsent was %ld , now it is %ld", lastsent, sc->lastsent);
	if (sc->lastsent != lastsent)
	{
		OneRoomNetCfg *r;

		begin_critical_section(S_NETCONFIGS);
		r = CtdlGetNetCfgForRoom(sc->room.QRnumber);
		r->lastsent = sc->lastsent;
		SaveRoomNetConfigFile(r, sc->room.QRnumber);
		FreeRoomNetworkStruct(&r);
		end_critical_section(S_NETCONFIGS);
	}
}


void free_spoolcontrol_struct(SpoolControl **sc)
{
	free_spoolcontrol_struct_members(*sc);
	free(*sc);
	*sc = NULL;
}


void free_spoolcontrol_struct_members(SpoolControl *sc)
{
	int i;
	FreeStrBuf(&sc->RoomInfo);
	FreeStrBuf(&sc->ListID);
	for (i = 0; i < maxRoomNetCfg; i++)
		FreeStrBuf(&sc->Users[i]);
}


/*
 * It's ok if these directories already exist.  Just fail silently.
 */
void create_spool_dirs(void) {
	if ((mkdir(ctdl_spool_dir, 0700) != 0) && (errno != EEXIST))
		syslog(LOG_EMERG, "netspool: unable to create directory [%s]: %s", ctdl_spool_dir, strerror(errno));
	if (chown(ctdl_spool_dir, CTDLUID, (-1)) != 0)
		syslog(LOG_EMERG, "netspool: unable to set the access rights for [%s]: %s", ctdl_spool_dir, strerror(errno));
	if ((mkdir(ctdl_nettmp_dir, 0700) != 0) && (errno != EEXIST))
		syslog(LOG_EMERG, "netspool: unable to create directory [%s]: %s", ctdl_nettmp_dir, strerror(errno));
	if (chown(ctdl_nettmp_dir, CTDLUID, (-1)) != 0)
		syslog(LOG_EMERG, "netspool: unable to set the access rights for [%s]: %s", ctdl_nettmp_dir, strerror(errno));
}


/*
 * Module entry point
 */
CTDL_MODULE_INIT(network_spool)
{
	if (!threading)
	{
		CtdlREGISTERRoomCfgType(subpending,       ParseSubPendingLine,   0, 5, SerializeGeneric,  DeleteGenericCfgLine);
		CtdlREGISTERRoomCfgType(unsubpending,     ParseUnSubPendingLine, 0, 4, SerializeGeneric,  DeleteGenericCfgLine);
		CtdlREGISTERRoomCfgType(lastsent,         ParseLastSent,         1, 1, SerializeLastSent, DeleteLastSent);
		CtdlREGISTERRoomCfgType(listrecp,         ParseGeneric,          0, 1, SerializeGeneric,  DeleteGenericCfgLine);
		CtdlREGISTERRoomCfgType(digestrecp,       ParseGeneric,          0, 1, SerializeGeneric,  DeleteGenericCfgLine);
		CtdlREGISTERRoomCfgType(participate,      ParseGeneric,          0, 1, SerializeGeneric,  DeleteGenericCfgLine);
		CtdlREGISTERRoomCfgType(roommailalias,    ParseRoomAlias,        0, 1, SerializeGeneric,  DeleteGenericCfgLine);
		create_spool_dirs();
	}
	return "network_spool";
}
