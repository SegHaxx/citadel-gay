/*
 * This module handles network mail and mailing list processing.
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

void network_deliver_list(struct CtdlMessage *msg, SpoolControl *sc, const char *RoomName);

void aggregate_recipients(StrBuf **recps, RoomNetCfg Which, OneRoomNetCfg *OneRNCfg, long nSegments)
{
	int i;
	size_t recps_len = 0;
	RoomNetCfgLine *nptr;

	*recps = NULL;
	/*
	 * Figure out how big a buffer we need to allocate
	 */
	for (nptr = OneRNCfg->NetConfigs[Which]; nptr != NULL; nptr = nptr->next) {
		recps_len = recps_len + StrLength(nptr->Value[0]) + 2;
	}

	/* Nothing todo... */
	if (recps_len == 0)
		return;

	*recps = NewStrBufPlain(NULL, recps_len);

	if (*recps == NULL) {
		syslog(LOG_ERR, "netmail: cannot allocate %ld bytes for recps", (long)recps_len);
		abort();
	}

	/* Each recipient */
	for (nptr = OneRNCfg->NetConfigs[Which]; nptr != NULL; nptr = nptr->next) {
		if (nptr != OneRNCfg->NetConfigs[Which]) {
			for (i = 0; i < nSegments; i++)
				StrBufAppendBufPlain(*recps, HKEY(","), i);
		}
		StrBufAppendBuf(*recps, nptr->Value[0], 0);
		if (Which == ignet_push_share)
		{
			StrBufAppendBufPlain(*recps, HKEY(","), 0);
			StrBufAppendBuf(*recps, nptr->Value[1], 0);

		}
	}
}

static void ListCalculateSubject(struct CtdlMessage *msg)
{
	struct CitContext *CCC = CC;
	StrBuf *Subject, *FlatSubject;
	int rlen;
	char *pCh;

	if (CM_IsEmpty(msg, eMsgSubject)) {
		Subject = NewStrBufPlain(HKEY("(no subject)"));
	}
	else {
		Subject = NewStrBufPlain(CM_KEY(msg, eMsgSubject));
	}
	FlatSubject = NewStrBufPlain(NULL, StrLength(Subject));
	StrBuf_RFC822_to_Utf8(FlatSubject, Subject, NULL, NULL);

	rlen = strlen(CCC->room.QRname);
	pCh  = strstr(ChrPtr(FlatSubject), CCC->room.QRname);
	if ((pCh == NULL) ||
	    (*(pCh + rlen) != ']') ||
	    (pCh == ChrPtr(FlatSubject)) ||
	    (*(pCh - 1) != '[')
		)
	{
		StrBuf *tmp;
		StrBufPlain(Subject, HKEY("["));
		StrBufAppendBufPlain(Subject,
				     CCC->room.QRname,
				     rlen, 0);
		StrBufAppendBufPlain(Subject, HKEY("] "), 0);
		StrBufAppendBuf(Subject, FlatSubject, 0);
		/* so we can free the right one swap them */
		tmp = Subject;
		Subject = FlatSubject;
		FlatSubject = tmp;
		StrBufRFC2047encode(&Subject, FlatSubject);
	}

	CM_SetAsFieldSB(msg, eMsgSubject, &Subject);

	FreeStrBuf(&FlatSubject);
}

/*
 * Deliver digest messages
 */
void network_deliver_digest(SpoolControl *sc)
{
	struct CitContext *CCC = CC;
	long len;
	char buf[SIZ];
	char *pbuf;
	struct CtdlMessage *msg = NULL;
	long msglen;
	recptypes *valid;
	char bounce_to[256];

	if (sc->Users[digestrecp] == NULL)
		return;

	msg = malloc(sizeof(struct CtdlMessage));
	memset(msg, 0, sizeof(struct CtdlMessage));
	msg->cm_magic = CTDLMESSAGE_MAGIC;
	msg->cm_format_type = FMT_RFC822;
	msg->cm_anon_type = MES_NORMAL;

	CM_SetFieldLONG(msg, eTimestamp, time(NULL));
	CM_SetField(msg, eAuthor, CCC->room.QRname, strlen(CCC->room.QRname));
	len = snprintf(buf, sizeof buf, "[%s]", CCC->room.QRname);
	CM_SetField(msg, eMsgSubject, buf, len);

	CM_SetField(msg, erFc822Addr, SKEY(sc->Users[roommailalias]));
	CM_SetField(msg, eRecipient, SKEY(sc->Users[roommailalias]));

	/* Set the 'List-ID' header */
	CM_SetField(msg, eListID, SKEY(sc->ListID));

	/*
	 * Go fetch the contents of the digest
	 */
	fseek(sc->digestfp, 0L, SEEK_END);
	msglen = ftell(sc->digestfp);

	pbuf = malloc(msglen + 1);
	fseek(sc->digestfp, 0L, SEEK_SET);
	fread(pbuf, (size_t)msglen, 1, sc->digestfp);
	pbuf[msglen] = '\0';
	CM_SetAsField(msg, eMesageText, &pbuf, msglen);

	/* Now generate the delivery instructions */

	/* Where do we want bounces and other noise to be heard?
	 * Surely not the list members! */
	snprintf(bounce_to, sizeof bounce_to, "room_aide@%s", CtdlGetConfigStr("c_fqdn"));

	/* Now submit the message */
	valid = validate_recipients(ChrPtr(sc->Users[digestrecp]), NULL, 0);
	if (valid != NULL) {
		valid->bounce_to = strdup(bounce_to);
		valid->envelope_from = strdup(bounce_to);
		CtdlSubmitMsg(msg, valid, NULL, 0);
	}
	CM_Free(msg);
	free_recipients(valid);
}


void network_process_digest(SpoolControl *sc, struct CtdlMessage *omsg, long *delete_after_send)
{

	struct CtdlMessage *msg = NULL;

	if (sc->Users[digestrecp] == NULL)
		return;

	/* If there are digest recipients, we have to build a digest */
	if (sc->digestfp == NULL) {
		
		sc->digestfp = create_digest_file(&sc->room, 1);

		if (sc->digestfp == NULL)
			return;

		sc->haveDigest = ftell(sc->digestfp) > 0;
		if (!sc->haveDigest) {
			fprintf(sc->digestfp, "Content-type: text/plain\n\n");
		}
		sc->haveDigest = 1;
	}

	msg = CM_Duplicate(omsg);
	if (msg != NULL) {
		sc->haveDigest = 1;
		fprintf(sc->digestfp,
			" -----------------------------------"
			"------------------------------------"
			"-------\n");
		fprintf(sc->digestfp, "From: ");
		if (!CM_IsEmpty(msg, eAuthor)) {
			fprintf(sc->digestfp,
				"%s ",
				msg->cm_fields[eAuthor]);
		}
		if (!CM_IsEmpty(msg, erFc822Addr)) {
			fprintf(sc->digestfp,
				"<%s> ",
				msg->cm_fields[erFc822Addr]);
		}
		else if (!CM_IsEmpty(msg, eNodeName)) {
			fprintf(sc->digestfp,
				"@%s ",
				msg->cm_fields[eNodeName]);
		}
		fprintf(sc->digestfp, "\n");
		if (!CM_IsEmpty(msg, eMsgSubject)) {
			fprintf(sc->digestfp,
				"Subject: %s\n",
				msg->cm_fields[eMsgSubject]);
		}

		CC->redirect_buffer = NewStrBufPlain(NULL, SIZ);

		safestrncpy(CC->preferred_formats,
			    "text/plain",
			    sizeof CC->preferred_formats);

		CtdlOutputPreLoadedMsg(msg,
				       MT_CITADEL,
				       HEADERS_NONE,
				       0, 0, 0);

		StrBufTrim(CC->redirect_buffer);
		fwrite(HKEY("\n"), 1, sc->digestfp);
		fwrite(SKEY(CC->redirect_buffer), 1, sc->digestfp);
		fwrite(HKEY("\n"), 1, sc->digestfp);

		FreeStrBuf(&CC->redirect_buffer);

		sc->num_msgs_spooled += 1;
		CM_Free(msg);
	}
}


void network_process_list(SpoolControl *sc, struct CtdlMessage *omsg, long *delete_after_send)
{
	struct CtdlMessage *msg = NULL;

	/*
	 * Process mailing list recipients
	 */
	if (sc->Users[listrecp] == NULL)
		return;

	/* create our own copy of the message.
	 *  We're going to need to modify it
	 * in order to insert the [list name] in it, etc.
	 */

	msg = CM_Duplicate(omsg);


	CM_SetField(msg, eReplyTo, SKEY(sc->Users[roommailalias]));

	/* if there is no other recipient, Set the recipient
	 * of the list message to the email address of the
	 * room itself.
	 */
	if (CM_IsEmpty(msg, eRecipient))
	{
		CM_SetField(msg, eRecipient, SKEY(sc->Users[roommailalias]));
	}

	/* Set the 'List-ID' header */
	CM_SetField(msg, eListID, SKEY(sc->ListID));


	/* Prepend "[List name]" to the subject */
	ListCalculateSubject(msg);

	/* Handle delivery */
	network_deliver_list(msg, sc, CC->room.QRname);
	CM_Free(msg);
}

/*
 * Deliver list messages to everyone on the list ... efficiently
 */
void network_deliver_list(struct CtdlMessage *msg, SpoolControl *sc, const char *RoomName)
{
	recptypes *valid;
	char bounce_to[256];

	/* Don't do this if there were no recipients! */
	if (sc->Users[listrecp] == NULL)
		return;

	/* Now generate the delivery instructions */

	/* Where do we want bounces and other noise to be heard?
	 *  Surely not the list members! */
	snprintf(bounce_to, sizeof bounce_to, "room_aide@%s", CtdlGetConfigStr("c_fqdn"));

	/* Now submit the message */
	valid = validate_recipients(ChrPtr(sc->Users[listrecp]), NULL, 0);
	if (valid != NULL) {
		valid->bounce_to = strdup(bounce_to);
		valid->envelope_from = strdup(bounce_to);
		valid->sending_room = strdup(RoomName);
		CtdlSubmitMsg(msg, valid, NULL, 0);
		free_recipients(valid);
	}
	/* Do not call CM_Free(msg) here; the caller will free it. */
}


void network_process_participate(SpoolControl *sc, struct CtdlMessage *omsg, long *delete_after_send)
{
	struct CtdlMessage *msg = NULL;
	int ok_to_participate = 0;
	recptypes *valid;

	/*
	 * Process client-side list participations for this room
	 */
	if (sc->Users[participate] == NULL)
		return;

	msg = CM_Duplicate(omsg);

	/* Only send messages which originated on our own
	 * Citadel network, otherwise we'll end up sending the
	 * remote mailing list's messages back to it, which
	 * is rude...
	 */
	ok_to_participate = 0;
	if (!CM_IsEmpty(msg, eNodeName)) {
		if (!strcasecmp(msg->cm_fields[eNodeName], CtdlGetConfigStr("c_nodename")))
		{
			ok_to_participate = 1;
		}
	}
	if (ok_to_participate)
	{
		/* Replace the Internet email address of the
		 * actual author with the email address of the
		 * room itself, so the remote listserv doesn't
		 * reject us.
		 */
		CM_SetField(msg, erFc822Addr, SKEY(sc->Users[roommailalias]));

		valid = validate_recipients(ChrPtr(sc->Users[participate]) , NULL, 0);

		CM_SetField(msg, eRecipient, SKEY(sc->Users[roommailalias]));
		CtdlSubmitMsg(msg, valid, "", 0);
		free_recipients(valid);
	}
	CM_Free(msg);
}


/*
 * Spools out one message from the list.
 */
void network_spool_msg(long msgnum, void *userdata)
{
	struct CtdlMessage *msg = NULL;
	long delete_after_send = 0;	/* Set to 1 to delete after spooling */
	SpoolControl *sc;

	sc = (SpoolControl *)userdata;
	msg = CtdlFetchMessage(msgnum, 1, 1);

	if (msg == NULL)
	{
		syslog(LOG_ERR, "netmail: failed to load Message <%ld> from disk", msgnum);
		return;
	}
	network_process_list(sc, msg, &delete_after_send);
	network_process_digest(sc, msg, &delete_after_send);
	network_process_participate(sc, msg, &delete_after_send);
	
	CM_Free(msg);

	/* update lastsent */
	sc->lastsent = msgnum;

	/* Delete this message if delete-after-send is set */
	if (delete_after_send) {
		CtdlDeleteMessages(CC->room.QRname, &msgnum, 1, "");
	}
}
