/*
 * Transmit outbound SMTP mail to the big wide world of the Internet
 *
 * This is the new, exciting, clever version that makes libcurl do all the work  :)
 *
 * Copyright (c) 1997-2018 by the citadel.org team
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
#include <sys/time.h>
#include <time.h>
#else
#if HAVE_SYS_TIME_H
#include <sys/time.h>
#else
#include <time.h>
#endif
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
#include "domain.h"
#include "internet_addressing.h"
#include "citadel_dirs.h"
#include "modules/smtp/smtp_util.h"

struct smtpmsgsrc {		// Data passed in and out of libcurl for message upload
	StrBuf *TheMessage;
	int bytes_total;
	int bytes_sent;
};

static int doing_smtpclient = 0;
long *smtpq = NULL;		// array of msgnums containing queue instructions
int smtpq_count = 0;		// number of queue messages in smtpq
int smtpq_alloc = 0;		// current allocation size for smtpq


/*
 * Initialize the SMTP outbound queue
 */
void smtp_init_spoolout(void)
{
	struct ctdlroom qrbuf;

	/*
	 * Create the room.  This will silently fail if the room already
	 * exists, and that's perfectly ok, because we want it to exist.
	 */
	CtdlCreateRoom(SMTP_SPOOLOUT_ROOM, 3, "", 0, 1, 0, VIEW_QUEUE);

	/*
	 * Make sure it's set to be a "system room" so it doesn't show up
	 * in the <K>nown rooms list for administrators.
	 */
	if (CtdlGetRoomLock(&qrbuf, SMTP_SPOOLOUT_ROOM) == 0) {
		qrbuf.QRflags2 |= QR2_SYSTEM;
		CtdlPutRoomLock(&qrbuf);
	}
}


/* For internet mail, generate delivery instructions.
 * Yes, this is recursive.  Deal with it.  Infinite recursion does
 * not happen because the delivery instructions message does not
 * contain a recipient.
 */
int smtp_aftersave(struct CtdlMessage *msg, recptypes * recps)
{
	if ((recps != NULL) && (recps->num_internet > 0)) {
		struct CtdlMessage *imsg = NULL;
		char recipient[SIZ];
		StrBuf *SpoolMsg = NewStrBuf();
		long nTokens;
		int i;

		syslog(LOG_DEBUG, "smtpclient: generating delivery instructions");

		StrBufPrintf(SpoolMsg,
			     "Content-type: " SPOOLMIME "\n"
			     "\n"
			     "msgid|%s\n"
			     "submitted|%ld\n" "bounceto|%s\n", msg->cm_fields[eVltMsgNum], (long) time(NULL), recps->bounce_to);

		if (recps->envelope_from != NULL) {
			StrBufAppendBufPlain(SpoolMsg, HKEY("envelope_from|"), 0);
			StrBufAppendBufPlain(SpoolMsg, recps->envelope_from, -1, 0);
			StrBufAppendBufPlain(SpoolMsg, HKEY("\n"), 0);
		}
		if (recps->sending_room != NULL) {
			StrBufAppendBufPlain(SpoolMsg, HKEY("source_room|"), 0);
			StrBufAppendBufPlain(SpoolMsg, recps->sending_room, -1, 0);
			StrBufAppendBufPlain(SpoolMsg, HKEY("\n"), 0);
		}

		nTokens = num_tokens(recps->recp_internet, '|');
		for (i = 0; i < nTokens; i++) {
			long len;
			len = extract_token(recipient, recps->recp_internet, i, '|', sizeof recipient);
			if (len > 0) {
				StrBufAppendBufPlain(SpoolMsg, HKEY("remote|"), 0);
				StrBufAppendBufPlain(SpoolMsg, recipient, len, 0);
				StrBufAppendBufPlain(SpoolMsg, HKEY("|0||\n"), 0);
			}
		}

		imsg = malloc(sizeof(struct CtdlMessage));
		memset(imsg, 0, sizeof(struct CtdlMessage));
		imsg->cm_magic = CTDLMESSAGE_MAGIC;
		imsg->cm_anon_type = MES_NORMAL;
		imsg->cm_format_type = FMT_RFC822;
		CM_SetField(imsg, eMsgSubject, HKEY("QMSG"));
		CM_SetField(imsg, eAuthor, HKEY("Citadel"));
		CM_SetField(imsg, eJournal, HKEY("do not journal"));
		CM_SetAsFieldSB(imsg, eMesageText, &SpoolMsg);
		CtdlSubmitMsg(imsg, NULL, SMTP_SPOOLOUT_ROOM, QP_EADDR);
		CM_Free(imsg);
	}
	return 0;
}


/*
 * Callback for smtp_attempt_delivery() to supply libcurl with upload data.
 */
static size_t upload_source(void *ptr, size_t size, size_t nmemb, void *userp)
{
	struct smtpmsgsrc *s = (struct smtpmsgsrc *) userp;
	int sendbytes = 0;
	const char *send_this = NULL;

	sendbytes = (size * nmemb);

	if (s->bytes_sent >= s->bytes_total) {
		return (0);	// no data remaining; we are done
	}

	if (sendbytes > (s->bytes_total - s->bytes_sent)) {
		sendbytes = s->bytes_total - s->bytes_sent;	// can't send more than we have
	}

	send_this = ChrPtr(s->TheMessage);
	send_this += s->bytes_sent;	// start where we last left off

	memcpy(ptr, send_this, sendbytes);
	s->bytes_sent += sendbytes;
	return (sendbytes);	// return the number of bytes _actually_ copied
}


/*
 * The libcurl API doesn't provide a way to capture the actual SMTP result message returned
 * by the remote server.  This is an ugly way to extract it, by capturing debug data from
 * the library and filtering on the lines we want.
 */
int ctdl_libcurl_smtp_debug_callback(CURL *handle, curl_infotype type, char *data, size_t size, void *userptr)
{
	if (type != CURLINFO_HEADER_IN)
		return 0;
	if (!userptr)
		return 0;
	char *debugbuf = (char *) userptr;

	int len = strlen(debugbuf);
	if (len + size > SIZ)
		return 0;

	memcpy(&debugbuf[len], data, size);
	debugbuf[len + size] = 0;
	return 0;
}


/*
 * Go through the debug output of an SMTP transaction, and boil it down to just the final success or error response message.
 */
void trim_response(long response_code, char *response)
{
	if ((response_code < 100) || (response_code > 999) || (IsEmptyStr(response))) {
		return;
	}

	char *t = malloc(strlen(response));
	if (!t) {
		return;
	}
	t[0] = 0;

	char *p;
	for (p = response; *p != 0; ++p) {
		if ( (*p != '\n') && (!isprint(*p)) ) {		// expunge any nonprintables except for newlines
			*p = ' ';
		}
	}

	char response_code_str[4];
	snprintf(response_code_str, sizeof response_code_str, "%ld", response_code);
	char *respstart = strstr(response, response_code_str);
	if (respstart == NULL) {
		strcpy(response, smtpstatus(response_code));
		return;
	}
	strcpy(response, respstart);

	p = strstr(response, "\n");
	if (p != NULL) {
		*p = 0;
	}
}


/*
 * Attempt a delivery to one recipient.
 * Returns a three-digit SMTP status code.
 */
int smtp_attempt_delivery(long msgid, char *recp, char *envelope_from, char *response)
{
	struct smtpmsgsrc s;
	char *fromaddr = NULL;
	CURL *curl;
	CURLcode res = CURLE_OK;
	struct curl_slist *recipients = NULL;
	long response_code = 421;
	int num_mx = 0;
	char mxes[SIZ];
	char user[1024];
	char node[1024];
	char name[1024];
	char try_this_mx[256];
	char smtp_url[512];
	int i;

	syslog(LOG_DEBUG, "smtpclient: smtp_attempt_delivery(%ld, %s)", msgid, recp);

	process_rfc822_addr(recp, user, node, name);	// split recipient address into username, hostname, displayname
	num_mx = getmx(mxes, node);
	if (num_mx < 1) {
		return (421);
	}

	CC->redirect_buffer = NewStrBufPlain(NULL, SIZ);
	CtdlOutputMsg(msgid, MT_RFC822, HEADERS_ALL, 0, 1, NULL, 0, NULL, &fromaddr, NULL);
	s.TheMessage = CC->redirect_buffer;
	s.bytes_total = StrLength(CC->redirect_buffer);
	s.bytes_sent = 0;
	CC->redirect_buffer = NULL;
	response_code = 421;

	for (i = 0; ((i < num_mx) && ((response_code / 100) == 4)); ++i) {	// keep trying MXes until one works or we run out
		response_code = 421;	// default 421 makes non-protocol errors transient
		s.bytes_sent = 0;	// rewind our buffer in case we try multiple MXes

		curl = curl_easy_init();
		if (curl) {
			response[0] = 0;

			if (!IsEmptyStr(envelope_from)) {
				curl_easy_setopt(curl, CURLOPT_MAIL_FROM, envelope_from);
			} else {
				curl_easy_setopt(curl, CURLOPT_MAIL_FROM, fromaddr);
			}

			recipients = curl_slist_append(recipients, recp);
			curl_easy_setopt(curl, CURLOPT_MAIL_RCPT, recipients);
			curl_easy_setopt(curl, CURLOPT_READFUNCTION, upload_source);
			curl_easy_setopt(curl, CURLOPT_READDATA, &s);
			curl_easy_setopt(curl, CURLOPT_UPLOAD, 1);	// tell libcurl we are uploading
			curl_easy_setopt(curl, CURLOPT_TIMEOUT, 20L);	// Time out after 20 seconds
			if (CtdlGetConfigInt("c_smtpclient_disable_starttls") == 0) {
				curl_easy_setopt(curl, CURLOPT_USE_SSL, CURLUSESSL_TRY);	// Attempt STARTTLS if offered
			}
			curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
			curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
			curl_easy_setopt(curl, CURLOPT_DEBUGFUNCTION, ctdl_libcurl_smtp_debug_callback);
			curl_easy_setopt(curl, CURLOPT_DEBUGDATA, (void *) response);
			curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);

			// Construct an SMTP URL in the form of:
			//      smtp[s]://target_host/source_host
			// This looks weird but libcurl uses that last part to set our name for EHLO or HELO.
			// We check for "smtp://" and "smtps://" because the admin may have put those prefixes in a smart-host entry.
			// If there is no prefix we add "smtp://"
			extract_token(try_this_mx, mxes, i, '|', (sizeof try_this_mx - 7));
			snprintf(smtp_url, sizeof smtp_url, "%s%s/%s", (((!strncasecmp(try_this_mx, HKEY("smtp://")))
									 ||
									 (!strncasecmp
									  (try_this_mx,
									   HKEY("smtps://")))) ? "" : "smtp://"),
				 try_this_mx, CtdlGetConfigStr("c_fqdn")
			    );
			curl_easy_setopt(curl, CURLOPT_URL, smtp_url);
			syslog(LOG_DEBUG, "smtpclient: trying %s", smtp_url);	// send the message
			res = curl_easy_perform(curl);
			curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
			syslog(LOG_DEBUG,
			       "smtpclient: libcurl returned %d (%s) , SMTP response %ld",
			       res, curl_easy_strerror(res), response_code);

			if ((res != CURLE_OK) && (response_code == 0)) {	// check for errors
				response_code = 421;
			}

			curl_slist_free_all(recipients);
			recipients = NULL;	// this gets reused; avoid double-free
			curl_easy_cleanup(curl);
			curl = NULL;	// this gets reused; avoid double-free

			/* Trim the error message buffer down to just the actual message */
			trim_response(response_code, response);
		}
	}

	FreeStrBuf(&s.TheMessage);
	if (fromaddr)
		free(fromaddr);
	return ((int) response_code);
}


/*
 * Process one outbound message.
 */
void smtp_process_one_msg(long qmsgnum)
{
	struct CtdlMessage *msg = NULL;
	char *instr = NULL;
	int i;
	int num_success = 0;
	int num_fail = 0;
	int num_delayed = 0;
	long deletes[2];
	int delete_this_queue = 0;
	char server_response[SIZ];

	msg = CtdlFetchMessage(qmsgnum, 1, 1);
	if (msg == NULL) {
		syslog(LOG_WARNING, "smtpclient: %ld does not exist", qmsgnum);
		return;
	}

	instr = msg->cm_fields[eMesageText];
	msg->cm_fields[eMesageText] = NULL;
	CM_Free(msg);

	// if the queue message has any CRLF's convert them to LF's
	char *crlf = NULL;
	while (crlf = strstr(instr, "\r\n"), crlf != NULL) {
		strcpy(crlf, crlf + 1);
	}

	// Strip out the headers and we are now left with just the instructions.
	char *soi = strstr(instr, "\n\n");
	if (soi) {
		strcpy(instr, soi + 2);
	}

	long msgid = 0;
	time_t submitted = time(NULL);
	time_t attempted = 0;
	char *bounceto = NULL;
	char *envelope_from = NULL;

	char cfgline[SIZ];
	for (i = 0; i < num_tokens(instr, '\n'); ++i) {
		extract_token(cfgline, instr, i, '\n', sizeof cfgline);
		if (!strncasecmp(cfgline, HKEY("msgid|")))
			msgid = atol(&cfgline[6]);
		if (!strncasecmp(cfgline, HKEY("submitted|")))
			submitted = atol(&cfgline[10]);
		if (!strncasecmp(cfgline, HKEY("attempted|")))
			attempted = atol(&cfgline[10]);
		if (!strncasecmp(cfgline, HKEY("bounceto|")))
			bounceto = strdup(&cfgline[9]);
		if (!strncasecmp(cfgline, HKEY("envelope_from|")))
			envelope_from = strdup(&cfgline[14]);
	}

	int should_try_now = 0;
	if (attempted < submitted) {	// If no attempts have been made yet, try now
		should_try_now = 1;
	} else if ((attempted - submitted) <= 14400) {
		if ((time(NULL) - attempted) > 1800) {	// First four hours, retry every 30 minutes
			should_try_now = 1;
		}
	} else {
		if ((time(NULL) - attempted) > 14400) {	// After that, retry once every 4 hours
			should_try_now = 1;
		}
	}

	if (should_try_now) {
		syslog(LOG_DEBUG, "smtpclient: %ld attempting delivery now", qmsgnum);
		StrBuf *NewInstr = NewStrBuf();
		StrBufAppendPrintf(NewInstr, "Content-type: " SPOOLMIME "\n\n");
		StrBufAppendPrintf(NewInstr, "msgid|%ld\n", msgid);
		StrBufAppendPrintf(NewInstr, "submitted|%ld\n", submitted);
		if (bounceto)
			StrBufAppendPrintf(NewInstr, "bounceto|%s\n", bounceto);
		if (envelope_from)
			StrBufAppendPrintf(NewInstr, "envelope_from|%s\n", envelope_from);

		for (i = 0; i < num_tokens(instr, '\n'); ++i) {
			extract_token(cfgline, instr, i, '\n', sizeof cfgline);
			if (!strncasecmp(cfgline, HKEY("remote|"))) {
				char recp[SIZ];
				int previous_result = extract_int(cfgline, 2);
				if ((previous_result == 0)
				    || (previous_result == 4)) {
					int new_result = 421;
					extract_token(recp, cfgline, 1, '|', sizeof recp);
					new_result = smtp_attempt_delivery(msgid, recp, envelope_from, server_response);
					syslog(LOG_DEBUG,
					       "smtpclient: recp: <%s> , result: %d (%s)", recp, new_result, server_response);
					if ((new_result / 100) == 2) {
						++num_success;
					} else {
						if ((new_result / 100) == 5) {
							++num_fail;
						} else {
							++num_delayed;
						}
						StrBufAppendPrintf
						    (NewInstr,
						     "remote|%s|%ld|%ld (%s)\n",
						     recp, (new_result / 100), new_result, server_response);
					}
				}
			}
		}

		StrBufAppendPrintf(NewInstr, "attempted|%ld\n", time(NULL));

		// All deliveries have now been attempted.  Now determine the disposition of this queue entry.

		time_t age = time(NULL) - submitted;
		syslog(LOG_DEBUG,
		       "smtpclient: submission age: %ldd%ldh%ldm%lds",
		       (age / 86400), ((age % 86400) / 3600), ((age % 3600) / 60), (age % 60));
		syslog(LOG_DEBUG, "smtpclient: num_success=%d , num_fail=%d , num_delayed=%d", num_success, num_fail, num_delayed);

		// If there are permanent fails on this attempt, deliver a bounce to the user.
		// The 5XX fails will be recorded in the rewritten queue, but they will be removed before the next attempt.
		if (num_fail > 0) {
			smtp_do_bounce(ChrPtr(NewInstr), SDB_BOUNCE_FATALS);
		}
		// If all deliveries have either succeeded or failed, we are finished with this queue entry.
		//
		if (num_delayed == 0) {
			delete_this_queue = 1;
		}
		// If it's been more than five days, give up and tell the sender that delivery failed
		//
		else if ((time(NULL) - submitted) > SMTP_DELIVER_FAIL) {
			smtp_do_bounce(ChrPtr(NewInstr), SDB_BOUNCE_ALL);
			delete_this_queue = 1;
		}
		// If it's been more than four hours but less than five days, warn the sender that delivery is delayed
		//
		else if (((attempted - submitted) < SMTP_DELIVER_WARN)
			 && ((time(NULL) - submitted) >= SMTP_DELIVER_WARN)) {
			smtp_do_bounce(ChrPtr(NewInstr), SDB_WARN);
		}

		if (delete_this_queue) {
			syslog(LOG_DEBUG, "smtpclient: %ld deleting", qmsgnum);
			deletes[0] = qmsgnum;
			deletes[1] = msgid;
			CtdlDeleteMessages(SMTP_SPOOLOUT_ROOM, deletes, 2, "");
			FreeStrBuf(&NewInstr);	// We have to free NewInstr here, no longer needed
		} else {
			// replace the old queue entry with the new one
			syslog(LOG_DEBUG, "smtpclient: %ld rewriting", qmsgnum);
			msg = convert_internet_message_buf(&NewInstr);	// This function will free NewInstr for us
			CtdlSubmitMsg(msg, NULL, SMTP_SPOOLOUT_ROOM, 0);
			CM_Free(msg);
			CtdlDeleteMessages(SMTP_SPOOLOUT_ROOM, &qmsgnum, 1, "");
		}
	} else {
		syslog(LOG_DEBUG, "smtpclient: %ld retry time not reached", qmsgnum);
	}

	if (bounceto != NULL)
		free(bounceto);
	if (envelope_from != NULL)
		free(envelope_from);
	free(instr);
}


/*
 * Callback for smtp_do_queue()
 */
void smtp_add_msg(long msgnum, void *userdata)
{

	if (smtpq == NULL) {
		smtpq_count = 0;
		smtpq_alloc = 100;
		smtpq = malloc(smtpq_alloc * sizeof(long));
	}

	if (smtpq_alloc >= smtpq_count) {
		smtpq_alloc += 100;
		smtpq = realloc(smtpq, (smtpq_alloc * sizeof(long)));
	}

	smtpq[smtpq_count++] = msgnum;
}


/*
 * Run through the queue sending out messages.
 */
void smtp_do_queue(void)
{
	int i = 0;

	/*
	 * This is a simple concurrency check to make sure only one smtpclient
	 * run is done at a time.  We could do this with a mutex, but since we
	 * don't really require extremely fine granularity here, we'll do it
	 * with a static variable instead.
	 */
	if (doing_smtpclient)
		return;
	doing_smtpclient = 1;

	syslog(LOG_DEBUG, "smtpclient: start queue run");

	if (CtdlGetRoom(&CC->room, SMTP_SPOOLOUT_ROOM) != 0) {
		syslog(LOG_WARNING, "Cannot find room <%s>", SMTP_SPOOLOUT_ROOM);
		doing_smtpclient = 0;
		return;
	}
	// Put the queue in memory so we can close the db cursor
	CtdlForEachMessage(MSGS_ALL, 0L, NULL, SPOOLMIME, NULL, smtp_add_msg, NULL);

	// We are ready to run through the queue now.
	for (i = 0; i < smtpq_count; ++i) {
		smtp_process_one_msg(smtpq[i]);
	}

	smtpq_count = 0;	// don't free it, we will use this memory on the next run
	doing_smtpclient = 0;
	syslog(LOG_DEBUG, "smtpclient: end queue run");
}


/*
 * Module entry point
 */
CTDL_MODULE_INIT(smtpclient)
{
	if (!threading) {
		CtdlRegisterMessageHook(smtp_aftersave, EVT_AFTERSAVE);
		CtdlRegisterSessionHook(smtp_do_queue, EVT_TIMER, PRIO_AGGR + 50);
		smtp_init_spoolout();
	}

	/* return our module id for the log */
	return "smtpclient";
}
