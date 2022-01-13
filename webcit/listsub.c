// Web forms for handling mailing list subscribe/unsubscribe requests.
//
// Copyright (c) 1996-2022 by the citadel.org team
//
// This program is open source software.  You can redistribute it and/or
// modify it under the terms of the GNU General Public License, version 3.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

#include "webcit.h"

// List subscription handling
int Conditional_LISTSUB_EXECUTE_SUBSCRIBE(StrBuf *Target, WCTemplputParams *TP) {
	int rc;
	StrBuf *Line;
	const char *ImpMsg;
	const StrBuf *Room, *Email;

	if (strcmp(bstr("cmd"), "subscribe")) {
		return 0;
	}

	Room = sbstr("room");
	if (Room == NULL) {
		ImpMsg = _("You need to specify the mailinglist to subscribe to.");
		AppendImportantMessage(ImpMsg, -1);
		return 0;
	}
	Email = sbstr("email");
	if (Email == NULL) {
		ImpMsg = _("You need to specify the email address you'd like to subscribe with.");
		AppendImportantMessage(ImpMsg, -1);
		return 0;
	}

	Line = NewStrBuf();
	serv_printf("LSUB subscribe|%s|%s|%s/listsub", ChrPtr(Room), ChrPtr(Email), ChrPtr(site_prefix));
	StrBuf_ServGetln(Line);
	rc = GetServerStatusMsg(Line, NULL, 1, 2);
	FreeStrBuf(&Line);
	if (rc == 2) {
		putbstr("__FAIL", NewStrBufPlain(HKEY("1")));
	}
	return rc == 2;
}


int Conditional_LISTSUB_EXECUTE_UNSUBSCRIBE(StrBuf *Target, WCTemplputParams *TP) {
	int rc;
	StrBuf *Line;
	const char *ImpMsg;
	const StrBuf *Room, *Email;

	if (strcmp(bstr("cmd"), "unsubscribe")) {
		return 0;
	}

	Room = sbstr("room");
	if (Room == NULL) {
		ImpMsg = _("You need to specify the mailinglist to subscribe to.");
		AppendImportantMessage(ImpMsg, -1);
		return 0;
	}
	Email = sbstr("email");
	if (Email == NULL) {
		ImpMsg = _("You need to specify the email address you'd like to subscribe with.");
		AppendImportantMessage(ImpMsg, -1);
		return 0;
	}

	serv_printf("LSUB unsubscribe|%s|%s|%s/listsub", ChrPtr(Room), ChrPtr(Email), ChrPtr(site_prefix));
	Line = NewStrBuf();
	StrBuf_ServGetln(Line);
	rc = GetServerStatusMsg(Line, NULL, 1, 2);
	FreeStrBuf(&Line);
	if (rc == 2) {
		putbstr("__FAIL", NewStrBufPlain(HKEY("1")));
	}
	return rc == 2;
}


int confirm_sub_or_unsub(char *cmd, StrBuf *Target, WCTemplputParams *TP) {
	int rc;
	StrBuf *Line;

	Line = NewStrBuf();
	serv_printf("LSUB %s|%s|%s|%s/listsub|%s", cmd, bstr("room"), bstr("email"), ChrPtr(site_prefix), bstr("token"));
	StrBuf_ServGetln(Line);
	rc = GetServerStatusMsg(Line, NULL, 1, 2);
	FreeStrBuf(&Line);
	if (rc == 2) {
		putbstr("__FAIL", NewStrBufPlain(HKEY("1")));
	}
	return rc == 2;
}

int Conditional_LISTSUB_EXECUTE_CONFIRMSUBSCRIBE(StrBuf *Target, WCTemplputParams *TP) {
	if (strcmp(bstr("cmd"), "confirm_subscribe")) {
		return 0;
	}
	return(confirm_sub_or_unsub("confirm_subscribe", Target, TP));
}


int Conditional_LISTSUB_EXECUTE_CONFIRMUNSUBSCRIBE(StrBuf *Target, WCTemplputParams *TP) {
	if (strcmp(bstr("cmd"), "confirm_unsubscribe")) {
		return 0;
	}
	return(confirm_sub_or_unsub("confirm_unsubscribe", Target, TP));
}


void do_listsub(void) {
	if (!havebstr("cmd")) {
		putbstr("cmd", NewStrBufPlain(HKEY("choose")));
	}
	output_headers(1, 0, 0, 0, 1, 0);
	do_template("listsub_display");
	end_burst();
}


void 
InitModule_LISTSUB
(void)
{
	RegisterConditional("COND:LISTSUB:EXECUTE:SUBSCRIBE", 0, Conditional_LISTSUB_EXECUTE_SUBSCRIBE,  CTX_NONE);
	RegisterConditional("COND:LISTSUB:EXECUTE:UNSUBSCRIBE", 0, Conditional_LISTSUB_EXECUTE_UNSUBSCRIBE,  CTX_NONE);
	RegisterConditional("COND:LISTSUB:EXECUTE:CONFIRMSUBSCRIBE", 0, Conditional_LISTSUB_EXECUTE_CONFIRMSUBSCRIBE, CTX_NONE);
	RegisterConditional("COND:LISTSUB:EXECUTE:CONFIRMUNSUBSCRIBE", 0, Conditional_LISTSUB_EXECUTE_CONFIRMUNSUBSCRIBE, CTX_NONE);
	WebcitAddUrlHandler(HKEY("listsub"), "", 0, do_listsub, ANONYMOUS|COOKIEUNNEEDED|FORCE_SESSIONCLOSE);
}
