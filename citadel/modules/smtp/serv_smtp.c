// This module is an SMTP and ESMTP server for the Citadel system.
// It is compliant with all of the following:
//
// RFC  821 - Simple Mail Transfer Protocol
// RFC  876 - Survey of SMTP Implementations
// RFC 1047 - Duplicate messages and SMTP
// RFC 1652 - 8 bit MIME
// RFC 1869 - Extended Simple Mail Transfer Protocol
// RFC 1870 - SMTP Service Extension for Message Size Declaration
// RFC 2033 - Local Mail Transfer Protocol
// RFC 2197 - SMTP Service Extension for Command Pipelining
// RFC 2476 - Message Submission
// RFC 2487 - SMTP Service Extension for Secure SMTP over TLS
// RFC 2554 - SMTP Service Extension for Authentication
// RFC 2821 - Simple Mail Transfer Protocol
// RFC 2822 - Internet Message Format
// RFC 2920 - SMTP Service Extension for Command Pipelining
//  
// The VRFY and EXPN commands have been removed from this implementation
// because nobody uses these commands anymore, except for spammers.
//
// Copyright (c) 1998-2022 by the citadel.org team
//
// This program is open source software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License version 3.
//  
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

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
#include <time.h>
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
#include "room_ops.h"
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

enum {				// Command states for login authentication
	smtp_command,
	smtp_user,
	smtp_password,
	smtp_plain
};

enum SMTP_FLAGS {
	HELO,
	EHLO,
	LHLO
};


// Here's where our SMTP session begins its happy day.
void smtp_greeting(int is_msa) {
	char message_to_spammer[1024];

	strcpy(CC->cs_clientname, "SMTP session");
	CC->internal_pgm = 1;
	CC->cs_flags |= CS_STEALTH;
	CC->session_specific_data = malloc(sizeof(struct citsmtp));
	memset(SMTP, 0, sizeof(struct citsmtp));
	SMTP->is_msa = is_msa;
	SMTP->Cmd = NewStrBufPlain(NULL, SIZ);
	SMTP->helo_node = NewStrBuf();
	SMTP->from = NewStrBufPlain(NULL, SIZ);
	SMTP->recipients = NewStrBufPlain(NULL, SIZ);
	SMTP->OneRcpt = NewStrBufPlain(NULL, SIZ);
	SMTP->preferred_sender_email = NULL;
	SMTP->preferred_sender_name = NULL;

	// If this config option is set, reject connections from problem
	// addresses immediately instead of after they execute a RCPT
	if ( (CtdlGetConfigInt("c_rbl_at_greeting")) && (SMTP->is_msa == 0) ) {
		if (rbl_check(CC->cs_addr, message_to_spammer)) {
			if (server_shutting_down)
				cprintf("421 %s\r\n", message_to_spammer);
			else
				cprintf("550 %s\r\n", message_to_spammer);
			CC->kill_me = KILLME_SPAMMER;
			/* no need to free_recipients(valid), it's not allocated yet */
			return;
		}
	}

	// Otherwise we're either clean or we check later.

	if (CC->nologin==1) {
		cprintf("451 Too many connections are already open; please try again later.\r\n");
		CC->kill_me = KILLME_MAX_SESSIONS_EXCEEDED;
		// no need to free_recipients(valid), it's not allocated yet
		return;
	}

	// Note: the FQDN *must* appear as the first thing after the 220 code.
	// Some clients (including citmail.c) depend on it being there.
	cprintf("220 %s ESMTP Citadel server ready.\r\n", CtdlGetConfigStr("c_fqdn"));
}


// SMTPS is just like SMTP, except it goes crypto right away.
void smtps_greeting(void) {
	CtdlModuleStartCryptoMsgs(NULL, NULL, NULL);
#ifdef HAVE_OPENSSL
	if (!CC->redirect_ssl) CC->kill_me = KILLME_NO_CRYPTO;		// kill session if no crypto
#endif
	smtp_greeting(0);
}


// SMTP MSA port requires authentication.
void smtp_msa_greeting(void) {
	smtp_greeting(1);
}


// LMTP is like SMTP but with some extra bonus footage added.
void lmtp_greeting(void) {

	smtp_greeting(0);
	SMTP->is_lmtp = 1;
}


// Generic SMTP MTA greeting
void smtp_mta_greeting(void) {
	smtp_greeting(0);
}


// We also have an unfiltered LMTP socket that bypasses spam filters.
void lmtp_unfiltered_greeting(void) {
	smtp_greeting(0);
	SMTP->is_lmtp = 1;
	SMTP->is_unfiltered = 1;
}


// Login greeting common to all auth methods
void smtp_auth_greeting(void) {
	cprintf("235 Hello, %s\r\n", CC->user.fullname);
	syslog(LOG_INFO, "serv_smtp: SMTP authenticated %s", CC->user.fullname);
	CC->internal_pgm = 0;
	CC->cs_flags &= ~CS_STEALTH;
}


// Implement HELO and EHLO commands.
// which_command:  0=HELO, 1=EHLO, 2=LHLO
void smtp_hello(int which_command) {

	if (StrLength(SMTP->Cmd) >= 6) {
		FlushStrBuf(SMTP->helo_node);
		StrBufAppendBuf(SMTP->helo_node, SMTP->Cmd, 5);
	}

	if ( (which_command != LHLO) && (SMTP->is_lmtp) ) {
		cprintf("500 Only LHLO is allowed when running LMTP\r\n");
		return;
	}

	if ( (which_command == LHLO) && (SMTP->is_lmtp == 0) ) {
		cprintf("500 LHLO is only allowed when running LMTP\r\n");
		return;
	}

	if (which_command == HELO) {
		cprintf("250 Hello %s (%s [%s])\r\n",
			ChrPtr(SMTP->helo_node),
			CC->cs_host,
			CC->cs_addr
		);
	}
	else {
		if (which_command == EHLO) {
			cprintf("250-Hello %s (%s [%s])\r\n",
				ChrPtr(SMTP->helo_node),
				CC->cs_host,
				CC->cs_addr
			);
		}
		else {
			cprintf("250-Greetings and joyous salutations.\r\n");
		}
		cprintf("250-HELP\r\n");
		cprintf("250-SIZE %ld\r\n", CtdlGetConfigLong("c_maxmsglen"));

#ifdef HAVE_OPENSSL
		// Offer TLS, but only if TLS is not already active.
		// Furthermore, only offer TLS when running on
		// the SMTP-MSA port, not on the SMTP-MTA port, due to
		// questionable reliability of TLS in certain sending MTA's.
		if ( (!CC->redirect_ssl) && (SMTP->is_msa) ) {
			cprintf("250-STARTTLS\r\n");
		}
#endif

		cprintf("250-AUTH LOGIN PLAIN\r\n"
			"250-AUTH=LOGIN PLAIN\r\n"
			"250 8BITMIME\r\n"
		);
	}
}


// Backend function for smtp_webcit_preferences_hack().
// Look at a message and determine if it's the preferences file.
void smtp_webcit_preferences_hack_backend(long msgnum, void *userdata) {
	struct CtdlMessage *msg;
	char **webcit_conf = (char **) userdata;

	if (*webcit_conf) {
		return;		// already got it
	}

	msg = CtdlFetchMessage(msgnum, 1);
	if (msg == NULL) {
		return;
	}

	if ( !CM_IsEmpty(msg, eMsgSubject) && (!strcasecmp(msg->cm_fields[eMsgSubject], "__ WebCit Preferences __"))) {
		// This is it!  Change ownership of the message text so it doesn't get freed.
		*webcit_conf = (char *)msg->cm_fields[eMesageText];
		msg->cm_fields[eMesageText] = NULL;
	}
	CM_Free(msg);
}


// The configuration item for the user's preferred display name for outgoing email is, unfortunately,
// stored in the account's WebCit configuration.  We have to fetch it now.
void smtp_webcit_preferences_hack(void) {
	char config_roomname[ROOMNAMELEN];
	char *webcit_conf = NULL;

	snprintf(config_roomname, sizeof config_roomname, "%010ld.%s", CC->user.usernum, USERCONFIGROOM);
	if (CtdlGetRoom(&CC->room, config_roomname) != 0) {
		return;
	}

	// Find the WebCit configuration message
	CtdlForEachMessage(MSGS_ALL, 1, NULL, NULL, NULL, smtp_webcit_preferences_hack_backend, (void *)&webcit_conf);

	if (!webcit_conf) {
		return;
	}

	// Parse the webcit configuration and attempt to do something useful with it
	char *str = webcit_conf;
	char *saveptr = str;
	char *this_line = NULL;
	while (this_line = strtok_r(str, "\n", &saveptr), this_line != NULL) {
		str = NULL;
		if (!strncasecmp(this_line, "defaultfrom|", 12)) {
			SMTP->preferred_sender_email = NewStrBufPlain(&this_line[12], -1);
		}
		if (!strncasecmp(this_line, "defaultname|", 12)) {
			SMTP->preferred_sender_name = NewStrBufPlain(&this_line[12], -1);
		}
		if ((!strncasecmp(this_line, "defaultname|", 12)) && (SMTP->preferred_sender_name == NULL)) {
			SMTP->preferred_sender_name = NewStrBufPlain(&this_line[12], -1);
		}

	}
	free(webcit_conf);
}


// Implement HELP command.
void smtp_help(void) {
	cprintf("214 RTFM http://www.ietf.org/rfc/rfc2821.txt\r\n");
}


void smtp_get_user(int offset) {
	char buf[SIZ];

	StrBuf *UserName = NewStrBufDup(SMTP->Cmd);
	StrBufCutLeft(UserName, offset);
	StrBufDecodeBase64(UserName);

	if (CtdlLoginExistingUser(ChrPtr(UserName)) == login_ok) {
		size_t len = CtdlEncodeBase64(buf, "Password:", 9, 0);

		if (buf[len - 1] == '\n') {
			buf[len - 1] = '\0';
		}
		cprintf("334 %s\r\n", buf);
		SMTP->command_state = smtp_password;
	}
	else {
		cprintf("500 No such user.\r\n");
		SMTP->command_state = smtp_command;
	}
	FreeStrBuf(&UserName);
}


void smtp_get_pass(void) {
	char password[SIZ];

	memset(password, 0, sizeof(password));
	StrBufDecodeBase64(SMTP->Cmd);
	syslog(LOG_DEBUG, "serv_smtp: trying <%s>", password);
	if (CtdlTryPassword(SKEY(SMTP->Cmd)) == pass_ok) {
		smtp_auth_greeting();
	}
	else {
		cprintf("535 Authentication failed.\r\n");
	}
	SMTP->command_state = smtp_command;
}


// Back end for PLAIN auth method (either inline or multistate)
void smtp_try_plain(void) {
	const char*decoded_authstring;
	char ident[256] = "";
	char user[256] = "";
	char pass[256] = "";
	int result;

	long decoded_len;
	long len = 0;
	long plen = 0;

	memset(pass, 0, sizeof(pass));
	decoded_len = StrBufDecodeBase64(SMTP->Cmd);

	if (decoded_len > 0) {
		decoded_authstring = ChrPtr(SMTP->Cmd);

		len = safestrncpy(ident, decoded_authstring, sizeof ident);

		decoded_len -= len - 1;
		decoded_authstring += len + 1;

		if (decoded_len > 0) {
			len = safestrncpy(user, decoded_authstring, sizeof user);

			decoded_authstring += len + 1;
			decoded_len -= len - 1;
		}

		if (decoded_len > 0) {
			plen = safestrncpy(pass, decoded_authstring, sizeof pass);

			if (plen < 0)
				plen = sizeof(pass) - 1;
		}
	}

	SMTP->command_state = smtp_command;

	if (!IsEmptyStr(ident)) {
		result = CtdlLoginExistingUser(ident);
	}
	else {
		result = CtdlLoginExistingUser(user);
	}

	if (result == login_ok) {
		if (CtdlTryPassword(pass, plen) == pass_ok) {
			smtp_webcit_preferences_hack();
			smtp_auth_greeting();
			return;
		}
	}
	cprintf("504 Authentication failed.\r\n");
}


// Attempt to perform authenticated SMTP
void smtp_auth(void) {
	char username_prompt[64];
	char method[64];
	char encoded_authstring[1024];

	if (CC->logged_in) {
		cprintf("504 Already logged in.\r\n");
		return;
	}

	if (StrLength(SMTP->Cmd) < 6) {
		cprintf("501 Syntax error\r\n");
		return;
	}

	extract_token(method, ChrPtr(SMTP->Cmd) + 5, 0, ' ', sizeof method);

	if (!strncasecmp(method, "login", 5) ) {
		if (StrLength(SMTP->Cmd) >= 12) {
			syslog(LOG_DEBUG, "serv_smtp: username <%s> supplied inline", ChrPtr(SMTP->Cmd)+11);
			smtp_get_user(11);
		}
		else {
			size_t len = CtdlEncodeBase64(username_prompt, "Username:", 9, 0);
			if (username_prompt[len - 1] == '\n') {
				username_prompt[len - 1] = '\0';
			}
			cprintf("334 %s\r\n", username_prompt);
			SMTP->command_state = smtp_user;
		}
		return;
	}

	if (!strncasecmp(method, "plain", 5) ) {
		long len;
		if (num_tokens(ChrPtr(SMTP->Cmd) + 5, ' ') < 2) {
			cprintf("334 \r\n");
			SMTP->command_state = smtp_plain;
			return;
		}

		len = extract_token(encoded_authstring, 
				    ChrPtr(SMTP->Cmd) + 5,
				    1, ' ',
				    sizeof encoded_authstring);
		StrBufPlain(SMTP->Cmd, encoded_authstring, len);
		smtp_try_plain();
		return;
	}

	cprintf("504 Unknown authentication method.\r\n");
	return;
}


// Implements the RSET (reset state) command.
// Currently this just zeroes out the state buffer.  If pointers to data
// allocated with malloc() are ever placed in the state buffer, we have to
// be sure to free() them first!
//
// Set do_response to nonzero to output the SMTP RSET response code.
void smtp_rset(int do_response) {
	FlushStrBuf(SMTP->Cmd);
	FlushStrBuf(SMTP->helo_node);
	FlushStrBuf(SMTP->from);
	FlushStrBuf(SMTP->recipients);
	FlushStrBuf(SMTP->OneRcpt);

	SMTP->command_state = 0;
	SMTP->number_of_recipients = 0;
	SMTP->delivery_mode = 0;
	SMTP->message_originated_locally = 0;
	SMTP->is_msa = 0;
	// is_lmtp and is_unfiltered should not be cleared. 

	if (do_response) {
		cprintf("250 Zap!\r\n");
	}
}


// Clear out the portions of the state buffer that need to be cleared out
// after the DATA command finishes.
void smtp_data_clear(void) {
	FlushStrBuf(SMTP->from);
	FlushStrBuf(SMTP->recipients);
	FlushStrBuf(SMTP->OneRcpt);
	SMTP->number_of_recipients = 0;
	SMTP->delivery_mode = 0;
	SMTP->message_originated_locally = 0;
}


// Implements the "MAIL FROM:" command
void smtp_mail(void) {
	char user[SIZ];
	char node[SIZ];
	char name[SIZ];

	if (StrLength(SMTP->from) > 0) {
		cprintf("503 Only one sender permitted\r\n");
		return;
	}

	if (StrLength(SMTP->Cmd) < 6) {
		cprintf("501 Syntax error\r\n");
		return;
	}

	if (strncasecmp(ChrPtr(SMTP->Cmd) + 5, "From:", 5)) {
		cprintf("501 Syntax error\r\n");
		return;
	}

	StrBufAppendBuf(SMTP->from, SMTP->Cmd, 5);
	StrBufTrim(SMTP->from);
	if (strchr(ChrPtr(SMTP->from), '<') != NULL) {
		StrBufStripAllBut(SMTP->from, '<', '>');
	}

	// We used to reject empty sender names, until it was brought to our
	// attention that RFC1123 5.2.9 requires that this be allowed.  So now
	// we allow it, but replace the empty string with a fake
	// address so we don't have to contend with the empty string causing
	// other code to fail when it's expecting something there.
	if (StrLength(SMTP->from) == 0) {
		StrBufPlain(SMTP->from, HKEY("someone@example.com"));
	}

	// If this SMTP connection is from a logged-in user, force the 'from'
	// to be the user's Internet e-mail address as Citadel knows it.
	if (CC->logged_in) {
		StrBufPlain(SMTP->from, CC->cs_inet_email, -1);
		cprintf("250 Sender ok <%s>\r\n", ChrPtr(SMTP->from));
		SMTP->message_originated_locally = 1;
		return;
	}

	else if (SMTP->is_lmtp) {
		// Bypass forgery checking for LMTP
	}

	// Otherwise, make sure outsiders aren't trying to forge mail from
	// this system (unless, of course, c_allow_spoofing is enabled)
	else if (CtdlGetConfigInt("c_allow_spoofing") == 0) {
		process_rfc822_addr(ChrPtr(SMTP->from), user, node, name);
		syslog(LOG_DEBUG, "serv_smtp: claimed envelope sender is '%s' == '%s' @ '%s' ('%s')",
			ChrPtr(SMTP->from), user, node, name
		);
		if (CtdlHostAlias(node) != hostalias_nomatch) {
			cprintf("550 You must log in to send mail from %s\r\n", node);
			FlushStrBuf(SMTP->from);
			syslog(LOG_DEBUG, "serv_smtp: rejecting unauthenticated mail from %s", node);
			return;
		}
	}

	cprintf("250 Sender ok\r\n");
}


// Implements the "RCPT To:" command
void smtp_rcpt(void) {
	char message_to_spammer[SIZ];
	struct recptypes *valid = NULL;

	if (StrLength(SMTP->from) == 0) {
		cprintf("503 Need MAIL before RCPT\r\n");
		return;
	}
	
	if (StrLength(SMTP->Cmd) < 6) {
		cprintf("501 Syntax error\r\n");
		return;
	}

	if (strncasecmp(ChrPtr(SMTP->Cmd) + 5, "To:", 3)) {
		cprintf("501 Syntax error\r\n");
		return;
	}

	if ( (SMTP->is_msa) && (!CC->logged_in) ) {
		cprintf("550 You must log in to send mail on this port.\r\n");
		FlushStrBuf(SMTP->from);
		return;
	}

	FlushStrBuf(SMTP->OneRcpt);
	StrBufAppendBuf(SMTP->OneRcpt, SMTP->Cmd, 8);
	StrBufTrim(SMTP->OneRcpt);
	StrBufStripAllBut(SMTP->OneRcpt, '<', '>');

	if ( (StrLength(SMTP->OneRcpt) + StrLength(SMTP->recipients)) >= SIZ) {
		cprintf("452 Too many recipients\r\n");
		return;
	}

	// RBL check
	if (
		(!CC->logged_in)						// Don't RBL authenticated users
		&& (!SMTP->is_lmtp)						// Don't RBL LMTP clients
		&& (CtdlGetConfigInt("c_rbl_at_greeting") == 0)			// Don't RBL if we did it at connection time
		&& (rbl_check(CC->cs_addr, message_to_spammer))
	) {
		cprintf("550 %s\r\n", message_to_spammer);
		return;								// no need to free_recipients(valid)
	}									// because it hasn't been allocated yet

	// This is a *preliminary* call to validate_recipients() to evaluate one recipient.
	valid = validate_recipients(
		(char *)ChrPtr(SMTP->OneRcpt), 
		smtp_get_Recipients(),
		(SMTP->is_lmtp)? POST_LMTP: (CC->logged_in)? POST_LOGGED_IN: POST_EXTERNAL
	);

	// Any type of error thrown by validate_recipients() will make the SMTP transaction fail at this point.
	if (valid->num_error != 0) {
		cprintf("550 %s\r\n", valid->errormsg);
		free_recipients(valid);
		return;
	}

	if (
		(valid->num_internet > 0)					// If it's outbound Internet mail...
		&& (CC->logged_in)						// ...and we're a logged-in user...
		&& (CtdlCheckInternetMailPermission(&CC->user)==0)		// ...who does not have Internet mail rights...
	) {
		cprintf("551 <%s> - you do not have permission to send Internet mail\r\n", ChrPtr(SMTP->OneRcpt));
		free_recipients(valid);
		return;
	}

	if (
		(valid->num_internet > 0)					// If it's outbound Internet mail...
		&& (SMTP->message_originated_locally == 0)			// ...and also inbound Internet mail...
		&& (SMTP->is_lmtp == 0)						/// ...and didn't arrive via LMTP...
	) {
		cprintf("551 <%s> - relaying denied\r\n", ChrPtr(SMTP->OneRcpt));
		free_recipients(valid);
		return;
	}

	if (
		(valid->num_room > 0)						// If it's mail to a room (mailing list)...
		&& (SMTP->message_originated_locally == 0)			// ...and also inbound Internet mail...
		&& (is_email_subscribed_to_list((char *)ChrPtr(SMTP->from), valid->recp_room) == 0)	// ...and not a subscriber
	) {
		cprintf("551 <%s> - The message is not from a list member\r\n", ChrPtr(SMTP->OneRcpt));
		free_recipients(valid);
		return;
	}

	cprintf("250 RCPT ok <%s>\r\n", ChrPtr(SMTP->OneRcpt));
	if (StrLength(SMTP->recipients) > 0) {
		StrBufAppendBufPlain(SMTP->recipients, HKEY(","), 0);
	}
	StrBufAppendBuf(SMTP->recipients, SMTP->OneRcpt, 0);
	SMTP->number_of_recipients ++;
	if (valid != NULL)  {
		free_recipients(valid);
	}
}


// Implements the DATA command
void smtp_data(void) {
	StrBuf *body;
	StrBuf *defbody; 
	struct CtdlMessage *msg = NULL;
	long msgnum = (-1L);
	char nowstamp[SIZ];
	struct recptypes *valid;
	int scan_errors;
	int i;

	if (StrLength(SMTP->from) == 0) {
		cprintf("503 Need MAIL command first.\r\n");
		return;
	}

	if (SMTP->number_of_recipients < 1) {
		cprintf("503 Need RCPT command first.\r\n");
		return;
	}

	cprintf("354 Transmit message now - terminate with '.' by itself\r\n");
	
	datestring(nowstamp, sizeof nowstamp, time(NULL), DATESTRING_RFC822);
	defbody = NewStrBufPlain(NULL, SIZ);

	if (defbody != NULL) {
		if (SMTP->is_lmtp && (CC->cs_UDSclientUID != -1)) {
			StrBufPrintf(
				defbody,
				"Received: from %s (Citadel from userid %ld)\n"
				"	by %s; %s\n",
				ChrPtr(SMTP->helo_node),
				(long int) CC->cs_UDSclientUID,
				CtdlGetConfigStr("c_fqdn"),
				nowstamp);
		}
		else {
			StrBufPrintf(
				defbody,
				"Received: from %s (%s [%s])\n"
				"	by %s; %s\n",
				ChrPtr(SMTP->helo_node),
				CC->cs_host,
				CC->cs_addr,
				CtdlGetConfigStr("c_fqdn"),
				nowstamp);
		}
	}
	body = CtdlReadMessageBodyBuf(HKEY("."), CtdlGetConfigLong("c_maxmsglen"), defbody, 1);
	FreeStrBuf(&defbody);
	if (body == NULL) {
		cprintf("550 Unable to save message: internal error.\r\n");
		return;
	}

	syslog(LOG_DEBUG, "serv_smtp: converting message...");
	msg = convert_internet_message_buf(&body);

	// If the user is locally authenticated, FORCE the From: header to
	// show up as the real sender.  Yes, this violates the RFC standard,
	// but IT MAKES SENSE.  If you prefer strict RFC adherence over
	// common sense, you can disable this in the configuration.
	//
	// We also set the "message room name" ('O' field) to MAILROOM
	// (which is Mail> on most systems) to prevent it from getting set
	// to something ugly like "0000058008.Sent Items>" when the message
	// is read with a Citadel client.

	if ( (CC->logged_in) && (CtdlGetConfigInt("c_rfc822_strict_from") != CFG_SMTP_FROM_NOFILTER) ) {
		int validemail = 0;
		
		if (!CM_IsEmpty(msg, erFc822Addr)       &&
		    ((CtdlGetConfigInt("c_rfc822_strict_from") == CFG_SMTP_FROM_CORRECT) || 
		     (CtdlGetConfigInt("c_rfc822_strict_from") == CFG_SMTP_FROM_REJECT)    )  )
		{
			if (!IsEmptyStr(CC->cs_inet_email))
				validemail = strcmp(CC->cs_inet_email, msg->cm_fields[erFc822Addr]) == 0;
			if ((!validemail) && 
			    (!IsEmptyStr(CC->cs_inet_other_emails)))
			{
				int num_secondary_emails = 0;
				int i;
				num_secondary_emails = num_tokens(CC->cs_inet_other_emails, '|');
				for (i=0; i < num_secondary_emails && !validemail; ++i) {
					char buf[256];
					extract_token(buf, CC->cs_inet_other_emails,i,'|',sizeof CC->cs_inet_other_emails);
					validemail = strcmp(buf, msg->cm_fields[erFc822Addr]) == 0;
				}
			}
		}

		if (!validemail && (CtdlGetConfigInt("c_rfc822_strict_from") == CFG_SMTP_FROM_REJECT)) {
			syslog(LOG_ERR, "serv_smtp: invalid sender '%s' - rejecting this message", msg->cm_fields[erFc822Addr]);
			cprintf("550 Invalid sender '%s' - rejecting this message.\r\n", msg->cm_fields[erFc822Addr]);
			return;
		}

        	CM_SetField(msg, eOriginalRoom, HKEY(MAILROOM));
		if (SMTP->preferred_sender_name != NULL)
			CM_SetField(msg, eAuthor, SKEY(SMTP->preferred_sender_name));
		else 
			CM_SetField(msg, eAuthor, CC->user.fullname, strlen(CC->user.fullname));

		if (!validemail) {
			if (SMTP->preferred_sender_email != NULL) {
				CM_SetField(msg, erFc822Addr, SKEY(SMTP->preferred_sender_email));
			}
			else {
				CM_SetField(msg, erFc822Addr, CC->cs_inet_email, strlen(CC->cs_inet_email));
			}
		}
	}

	// Set the "envelope from" address
	CM_SetField(msg, eMessagePath, SKEY(SMTP->from));

	// Set the "envelope to" address
	CM_SetField(msg, eenVelopeTo, SKEY(SMTP->recipients));

	// Submit the message into the Citadel system.
	valid = validate_recipients(
		(char *)ChrPtr(SMTP->recipients),
		smtp_get_Recipients(),
		(SMTP->is_lmtp)? POST_LMTP: (CC->logged_in)? POST_LOGGED_IN: POST_EXTERNAL
	);

	// If there are modules that want to scan this message before final
	// submission (such as virus checkers or spam filters), call them now
	// and give them an opportunity to reject the message.
	if (SMTP->is_unfiltered) {
		scan_errors = 0;
	}
	else {
		scan_errors = PerformMessageHooks(msg, valid, EVT_SMTPSCAN);
	}

	if (scan_errors > 0) {	// We don't want this message!

		if (CM_IsEmpty(msg, eErrorMsg)) {
			CM_SetField(msg, eErrorMsg, HKEY("Message rejected by filter"));
		}

		StrBufPrintf(SMTP->OneRcpt, "550 %s\r\n", msg->cm_fields[eErrorMsg]);
	}
	
	else {			// Ok, we'll accept this message.
		msgnum = CtdlSubmitMsg(msg, valid, "");
		if (msgnum > 0L) {
			StrBufPrintf(SMTP->OneRcpt, "250 Message accepted.\r\n");
		}
		else {
			StrBufPrintf(SMTP->OneRcpt, "550 Internal delivery error\r\n");
		}
	}

	// For SMTP and ESMTP, just print the result message.  For LMTP, we
	// have to print one result message for each recipient.  Since there
	// is nothing in Citadel which would cause different recipients to
	// have different results, we can get away with just spitting out the
	// same message once for each recipient.
	if (SMTP->is_lmtp) {
		for (i=0; i<SMTP->number_of_recipients; ++i) {
			cputbuf(SMTP->OneRcpt);
		}
	}
	else {
		cputbuf(SMTP->OneRcpt);
	}

	// Write something to the syslog(which may or may not be where the
	// rest of the Citadel logs are going; some sysadmins want LOG_MAIL).
	syslog((LOG_MAIL | LOG_INFO),
		    "%ld: from=<%s>, nrcpts=%d, relay=%s [%s], stat=%s",
		    msgnum,
		    ChrPtr(SMTP->from),
		    SMTP->number_of_recipients,
		    CC->cs_host,
		    CC->cs_addr,
		    ChrPtr(SMTP->OneRcpt)
	);

	// Clean up
	CM_Free(msg);
	free_recipients(valid);
	smtp_data_clear();	// clear out the buffers now
}


// Implements the STARTTLS command
void smtp_starttls(void) {
	char ok_response[SIZ];
	char nosup_response[SIZ];
	char error_response[SIZ];

	sprintf(ok_response, "220 Begin TLS negotiation now\r\n");
	sprintf(nosup_response, "554 TLS not supported here\r\n");
	sprintf(error_response, "554 Internal error\r\n");
	CtdlModuleStartCryptoMsgs(ok_response, nosup_response, error_response);
	smtp_rset(0);
}


// Implements the NOOP (NO OPeration) command
void smtp_noop(void) {
	cprintf("250 NOOP\r\n");
}


// Implements the QUIT command
void smtp_quit(void) {
	cprintf("221 Goodbye...\r\n");
	CC->kill_me = KILLME_CLIENT_LOGGED_OUT;
}


// Main command loop for SMTP server sessions.
void smtp_command_loop(void) {
	static const ConstStr AuthPlainStr = {HKEY("AUTH PLAIN")};

	if (SMTP == NULL) {
		syslog(LOG_ERR, "serv_smtp: Session SMTP data is null.  WTF?  We will crash now.");
		abort();
	}

	time(&CC->lastcmd);
	if (CtdlClientGetLine(SMTP->Cmd) < 1) {
		syslog(LOG_INFO, "SMTP: client disconnected: ending session.");
		CC->kill_me = KILLME_CLIENT_DISCONNECTED;
		return;
	}

	if (SMTP->command_state == smtp_user) {
		if (!strncmp(ChrPtr(SMTP->Cmd), AuthPlainStr.Key, AuthPlainStr.len)) {
			smtp_try_plain();
		}
		else {
			smtp_get_user(0);
		}
		return;
	}

	else if (SMTP->command_state == smtp_password) {
		smtp_get_pass();
		return;
	}

	else if (SMTP->command_state == smtp_plain) {
		smtp_try_plain();
		return;
	}

	syslog(LOG_DEBUG, "serv_smtp: client sent command <%s>", ChrPtr(SMTP->Cmd));

	if (!strncasecmp(ChrPtr(SMTP->Cmd), "NOOP", 4)) {
		smtp_noop();
		return;
	}

	if (!strncasecmp(ChrPtr(SMTP->Cmd), "QUIT", 4)) {
		smtp_quit();
		return;
	}

	if (!strncasecmp(ChrPtr(SMTP->Cmd), "HELO", 4)) {
		smtp_hello(HELO);
		return;
	}

	if (!strncasecmp(ChrPtr(SMTP->Cmd), "EHLO", 4)) {
		smtp_hello(EHLO);
		return;
	}

	if (!strncasecmp(ChrPtr(SMTP->Cmd), "LHLO", 4)) {
		smtp_hello(LHLO);
		return;
	}

	if (!strncasecmp(ChrPtr(SMTP->Cmd), "RSET", 4)) {
		smtp_rset(1);
		return;
	}

	if (!strncasecmp(ChrPtr(SMTP->Cmd), "AUTH", 4)) {
		smtp_auth();
		return;
	}

	if (!strncasecmp(ChrPtr(SMTP->Cmd), "DATA", 4)) {
		smtp_data();
		return;
	}

	if (!strncasecmp(ChrPtr(SMTP->Cmd), "HELP", 4)) {
		smtp_help();
		return;
	}

	if (!strncasecmp(ChrPtr(SMTP->Cmd), "MAIL", 4)) {
		smtp_mail();
		return;
	}
	
	if (!strncasecmp(ChrPtr(SMTP->Cmd), "RCPT", 4)) {
		smtp_rcpt();
		return;
	}
#ifdef HAVE_OPENSSL
	if (!strncasecmp(ChrPtr(SMTP->Cmd), "STARTTLS", 8)) {
		smtp_starttls();
		return;
	}
#endif

	cprintf("502 I'm afraid I can't do that.\r\n");
}


/*****************************************************************************/
/*                      MODULE INITIALIZATION STUFF                          */
/*****************************************************************************/
/*
 * This cleanup function blows away the temporary memory used by
 * the SMTP server.
 */
void smtp_cleanup_function(void)
{
	/* Don't do this stuff if this is not an SMTP session! */
	if (CC->h_command_function != smtp_command_loop) return;

	syslog(LOG_DEBUG, "Performing SMTP cleanup hook");

	FreeStrBuf(&SMTP->Cmd);
	FreeStrBuf(&SMTP->helo_node);
	FreeStrBuf(&SMTP->from);
	FreeStrBuf(&SMTP->recipients);
	FreeStrBuf(&SMTP->OneRcpt);
	FreeStrBuf(&SMTP->preferred_sender_email);
	FreeStrBuf(&SMTP->preferred_sender_name);

	free(SMTP);
}

const char *CitadelServiceSMTP_MTA="SMTP-MTA";
const char *CitadelServiceSMTPS_MTA="SMTPs-MTA";
const char *CitadelServiceSMTP_MSA="SMTP-MSA";
const char *CitadelServiceSMTP_LMTP="LMTP";
const char *CitadelServiceSMTP_LMTP_UNF="LMTP-UnF";


CTDL_MODULE_INIT(smtp)
{
	if (!threading) {
		CtdlRegisterServiceHook(CtdlGetConfigInt("c_smtp_port"),	/* SMTP MTA */
					NULL,
					smtp_mta_greeting,
					smtp_command_loop,
					NULL, 
					CitadelServiceSMTP_MTA);

#ifdef HAVE_OPENSSL
		CtdlRegisterServiceHook(CtdlGetConfigInt("c_smtps_port"),	/* SMTPS MTA */
					NULL,
					smtps_greeting,
					smtp_command_loop,
					NULL,
					CitadelServiceSMTPS_MTA);
#endif

		CtdlRegisterServiceHook(CtdlGetConfigInt("c_msa_port"),		/* SMTP MSA */
					NULL,
					smtp_msa_greeting,
					smtp_command_loop,
					NULL,
					CitadelServiceSMTP_MSA);

		CtdlRegisterServiceHook(0,			/* local LMTP */
					file_lmtp_socket,
					lmtp_greeting,
					smtp_command_loop,
					NULL,
					CitadelServiceSMTP_LMTP);

		CtdlRegisterServiceHook(0,			/* local LMTP */
					file_lmtp_unfiltered_socket,
					lmtp_unfiltered_greeting,
					smtp_command_loop,
					NULL,
					CitadelServiceSMTP_LMTP_UNF);

		CtdlRegisterSessionHook(smtp_cleanup_function, EVT_STOP, PRIO_STOP + 250);
	}
	
	/* return our module name for the log */
	return "smtp";
}
