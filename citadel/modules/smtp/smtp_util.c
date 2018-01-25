/*
 * Utility functions for the Citadel SMTP implementation
 *
 * Copyright (c) 1998-2018 by the citadel.org team
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
#include <termios.h>
#include <fcntl.h>
#include <signal.h>
#include <pwd.h>
#include <errno.h>
#include <sys/types.h>
#include <syslog.h>

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

#include <sys/wait.h>
#include <ctype.h>
#include <string.h>
#include <limits.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <libcitadel.h>
#include "citadel.h"
#include "server.h"
#include "citserver.h"
#include "support.h"
#include "config.h"
#include "control.h"
#include "user_ops.h"
#include "database.h"
#include "msgbase.h"
#include "internet_addressing.h"
#include "genstamp.h"
#include "domain.h"
#include "clientsocket.h"
#include "locate_host.h"
#include "citadel_dirs.h"
#include "ctdl_module.h"
#include "smtp_util.h"

const char *smtp_get_Recipients(void)
{
	citsmtp *sSMTP = SMTP;

	if (sSMTP == NULL)
		return NULL;
	else return ChrPtr(sSMTP->from);
}


/*
 * smtp_do_bounce() is caled by smtp_process_one_msg() to scan a set of delivery
 * instructions for errors and produce/deliver a "bounce" message (delivery
 * status notification).
 *
 * is_final should be set to:
 *	SDB_BOUNCE_FATALS	Advise the sender of all 5XX (permanent failures)
 *	SDB_BOUNCE_ALL		Advise the sender that all deliveries have failed and will not be retried
 *	SDB_WARN		Warn the sender about all 4XX transient delays
 */
void smtp_do_bounce(const char *instr, int is_final)
{
	int i;
	int lines;
	int status;
	char buf[1024];
	char key[1024];
	char addr[1024];
	char dsn[1024];
	char bounceto[1024];
	StrBuf *boundary;
	int num_bounces = 0;
	int bounce_this = 0;
	struct CtdlMessage *bmsg = NULL;
	recptypes *valid;
	int successful_bounce = 0;
	static int seq = 0;
	StrBuf *BounceMB;
	long omsgid = (-1);

	syslog(LOG_DEBUG, "smtp_do_bounce() called");
	strcpy(bounceto, "");
	boundary = NewStrBufPlain(HKEY("=_Citadel_Multipart_"));

	StrBufAppendPrintf(boundary, "%s_%04x%04x", CtdlGetConfigStr("c_fqdn"), getpid(), ++seq);


	/* Start building our bounce message */

	bmsg = (struct CtdlMessage *) malloc(sizeof(struct CtdlMessage));
	if (bmsg == NULL) return;
	memset(bmsg, 0, sizeof(struct CtdlMessage));
	BounceMB = NewStrBufPlain(NULL, 1024);

	bmsg->cm_magic = CTDLMESSAGE_MAGIC;
	bmsg->cm_anon_type = MES_NORMAL;
	bmsg->cm_format_type = FMT_RFC822;
	CM_SetField(bmsg, eAuthor, HKEY("Citadel"));
	CM_SetField(bmsg, eOriginalRoom, HKEY(MAILROOM));
	CM_SetField(bmsg, eMsgSubject, HKEY("Delivery Status Notification (Failure)"));
	StrBufAppendBufPlain(BounceMB, HKEY("Content-type: multipart/mixed; boundary=\""), 0);
	StrBufAppendBuf(BounceMB, boundary, 0);
	StrBufAppendBufPlain(BounceMB, HKEY("\"\r\n"), 0);
	StrBufAppendBufPlain(BounceMB, HKEY("MIME-Version: 1.0\r\n"), 0);
	StrBufAppendBufPlain(BounceMB, HKEY("X-Mailer: " CITADEL "\r\n"), 0);
	StrBufAppendBufPlain(BounceMB, HKEY("\r\nThis is a multipart message in MIME format.\r\n\r\n"), 0);
	StrBufAppendBufPlain(BounceMB, HKEY("--"), 0);
	StrBufAppendBuf(BounceMB, boundary, 0);
	StrBufAppendBufPlain(BounceMB, HKEY("\r\n"), 0);
	StrBufAppendBufPlain(BounceMB, HKEY("Content-type: text/plain\r\n\r\n"), 0);

	if (is_final == SDB_BOUNCE_ALL)
	{
		StrBufAppendBufPlain(
			BounceMB,
			HKEY(	"A message you sent could not be delivered "
				"to some or all of its recipients\ndue to "
				"prolonged unavailability of its destination(s).\n"
				"Giving up on the following addresses:\n\n"),
			0);
	}
	else if (is_final == SDB_BOUNCE_FATALS)
	{
		StrBufAppendBufPlain(
			BounceMB,
			HKEY(	"A message you sent could not be delivered "
				"to some or all of its recipients.\n"
				"The following addresses were undeliverable:\n\n"),
			0);
	}
	else if (is_final == SDB_WARN)
	{
		StrBufAppendBufPlain(
			BounceMB,
			HKEY("A message you sent has not been delivered "
				"to some or all of its recipients.\n"
				"Citadel will continue attempting delivery for five days.\n"
				"The following addresses were undeliverable:\n\n"),
			0);
	}
	else	// should never get here
	{
		StrBufAppendBufPlain(BounceMB, HKEY("This message should never occur.\n\n"), 0);
	}

	/*
	 * Now go through the instructions checking for stuff.
	 */
	lines = num_tokens(instr, '\n');
	for (i=0; i<lines; ++i) {
		long addrlen;
		long dsnlen;
		extract_token(buf, instr, i, '\n', sizeof buf);
		extract_token(key, buf, 0, '|', sizeof key);
		addrlen = extract_token(addr, buf, 1, '|', sizeof addr);
		status = extract_int(buf, 2);
		dsnlen = extract_token(dsn, buf, 3, '|', sizeof dsn);
		bounce_this = 0;

		syslog(LOG_DEBUG, "key=<%s> addr=<%s> status=%d dsn=<%s>", key, addr, status, dsn);

		if (!strcasecmp(key, "bounceto")) {
			strcpy(bounceto, addr);
		}

		if (!strcasecmp(key, "msgid")) {
			omsgid = atol(addr);
		}

		if (!strcasecmp(key, "remote")) {
			if ((is_final == SDB_BOUNCE_FATALS) && (status == 5)) bounce_this = 1;
			if ((is_final == SDB_BOUNCE_ALL) && (status != 2)) bounce_this = 1;
			if ((is_final == SDB_WARN) && (status == 4)) bounce_this = 1;
		}

		if (bounce_this) {
			++num_bounces;
			StrBufAppendBufPlain(BounceMB, addr, addrlen, 0);
			StrBufAppendBufPlain(BounceMB, HKEY(": "), 0);
			StrBufAppendBufPlain(BounceMB, dsn, dsnlen, 0);
			StrBufAppendBufPlain(BounceMB, HKEY("\r\n"), 0);
		}
	}

	/* Attach the original message */
	if (omsgid >= 0) {
		StrBufAppendBufPlain(BounceMB, HKEY("--"), 0);
		StrBufAppendBuf(BounceMB, boundary, 0);
		StrBufAppendBufPlain(BounceMB, HKEY("\r\n"), 0);
		StrBufAppendBufPlain(BounceMB, HKEY("Content-type: message/rfc822\r\n"), 0);
		StrBufAppendBufPlain(BounceMB, HKEY("Content-Transfer-Encoding: 7bit\r\n"), 0);
		StrBufAppendBufPlain(BounceMB, HKEY("Content-Disposition: inline\r\n"), 0);
		StrBufAppendBufPlain(BounceMB, HKEY("\r\n"), 0);

		CC->redirect_buffer = NewStrBufPlain(NULL, SIZ);
		CtdlOutputMsg(omsgid,
			MT_RFC822,
			HEADERS_ALL,
			0, 1, NULL, 0,
			NULL, NULL, NULL
		);
		StrBufAppendBuf(BounceMB, CC->redirect_buffer, 0);
		FreeStrBuf(&CC->redirect_buffer);
	}

	/* Close the multipart MIME scope */
	StrBufAppendBufPlain(BounceMB, HKEY("--"), 0);
	StrBufAppendBuf(BounceMB, boundary, 0);
	StrBufAppendBufPlain(BounceMB, HKEY("--\r\n"), 0);
	CM_SetAsFieldSB(bmsg, eMesageText, &BounceMB);

	/* Deliver the bounce if there's anything worth mentioning */
	syslog(LOG_DEBUG, "num_bounces = %d", num_bounces);
	if (num_bounces > 0) {

		/* First try the user who sent the message */
		if (IsEmptyStr(bounceto)) {
			syslog(LOG_ERR, "No bounce address specified");
		}
		else {
			syslog(LOG_DEBUG, "bounce to user <%s>", bounceto);
		}
		/* Can we deliver the bounce to the original sender? */
		valid = validate_recipients(bounceto, smtp_get_Recipients (), 0);
		if (valid != NULL) {
			if (valid->num_error == 0) {
				CtdlSubmitMsg(bmsg, valid, "", QP_EADDR);
				successful_bounce = 1;
			}
		}

		/* If not, post it in the Aide> room */
		if (successful_bounce == 0) {
			CtdlSubmitMsg(bmsg, NULL, CtdlGetConfigStr("c_aideroom"), QP_EADDR);
		}

		/* Free up the memory we used */
		if (valid != NULL) {
			free_recipients(valid);
		}
	}
	FreeStrBuf(&boundary);
	CM_Free(bmsg);
	syslog(LOG_DEBUG, "Done processing bounces");
}





char *smtpcodes[][2] = {
	{ "211 - System status / system help reply" },
	{ "214", "Help message" },
	{ "220", "Domain service ready" },
	{ "221", "Domain service closing transmission channel" },
	{ "250", "Requested mail action completed and OK" },
	{ "251", "Not Local User, forward email to forward path" },
	{ "252", "Cannot Verify user, will attempt delivery later" },
	{ "253", "Pending messages for node started" },
	{ "354", "Start mail input; end with ." },
	{ "355", "Octet-offset is the transaction offset" },
	{ "421", "Domain service not available, closing transmission channel" },
	{ "432", "Domain service not available, closing transmission channel" },
	{ "450", "Requested mail action not taken: mailbox unavailable. request refused" },
	{ "451", "Requested action aborted: local error in processing Request is unable to be processed, try again" },
	{ "452", "Requested action not taken: insufficient system storage" },
	{ "453", "No mail" },
	{ "454", "TLS not available due to temporary reason. Encryption required for requested authentication mechanism" },
	{ "458", "Unable to queue messages for node" },
	{ "459", "Node not allowed: reason" },
	{ "500", "Syntax error, command unrecognized" },
	{ "501", "Syntax error in parameters or arguments" },
	{ "502", "Command not implemented" },
	{ "503", "Bad sequence of commands" },
	{ "504", "Command parameter not implemented" },
	{ "510", "Check the recipient address" },
	{ "512", "Domain can not be found. Unknown host." },
	{ "515", "Destination mailbox address invalid" },
	{ "517", "Problem with senders mail attribute, check properties" },
	{ "521", "Domain does not accept mail" },
	{ "522", "Recipient has exceeded mailbox limit" },
	{ "523", "Server limit exceeded. Message too large" },
	{ "530", "Access Denied. Authentication required" },
	{ "531", "Mail system Full" },
	{ "533", "Remote server has insufficient disk space to hold email" },
	{ "534", "Authentication mechanism is too weak. Message too big" },
	{ "535", "Multiple servers using same IP. Required Authentication" },
	{ "538", "Encryption required for requested authentication mechanism" },
	{ "540", "Email address has no DNS Server" },
	{ "541", "No response from host" },
	{ "542", "Bad Connection" },
	{ "543", "Routing server failure. No available route" },
	{ "546", "Email looping" },
	{ "547", "Delivery time-out" },
	{ "550", "Requested action not taken: mailbox unavailable or relaying denied" },
	{ "551", "User not local; please try forward path" },
	{ "552", "Requested mail action aborted: exceeded storage allocation" },
	{ "553", "Requested action not taken: mailbox name not allowed" },
	{ "554", "Transaction failed" }
};



char *smtpstatus(int code) {
	int i;

	for (i=0; i<(sizeof(smtpcodes)/sizeof(char *)/2); ++i) {
		if (atoi(smtpcodes[i][0]) == code) {
			return(smtpcodes[i][1]);
		}
	}
	
	return("Unknown or other SMTP status");
}

