/*
 * This file contains functions which handle all of the CalDAV "REPORT" queries
 * specified in RFC4791 section 7.
 *
 * Copyright (c) 2018 by the citadel.org team
 *
 * This program is open source software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include "webcit.h"


/*
 * A CalDAV REPORT can only be one type.  This is stored in the report_type member.
 */
enum cr_type {
	cr_calendar_query,
	cr_calendar_multiget,
	cr_freebusy_query
};


/*
 * Data type for CalDAV Report Parameters.
 * As we slog our way through the XML we learn what the client is asking for
 * and build up the contents of this data type.
 */
struct cr_parms {
	int tag_nesting_level;		// not needed, just kept for pretty-printing
	enum cr_type report_type;	// which RFC4791 section 7 REPORT are we generating
	StrBuf *Chardata;		// XML chardata in between tags is built up here
	StrBuf *Hrefs;			// list of items requested by a calendar-multiget report
};


/*
 * XML parser callback
 */
void caldav_xml_start(void *data, const char *el, const char **attr)
{
	struct cr_parms *crp = (struct cr_parms *) data;
	int i;

	// syslog(LOG_DEBUG, "CALDAV ELEMENT START: <%s> %d", el, crp->tag_nesting_level);

	for (i = 0; attr[i] != NULL; i += 2) {
		syslog(LOG_DEBUG, "                    Attribute '%s' = '%s'", attr[i], attr[i + 1]);
	}

	if (!strcasecmp(el, "urn:ietf:params:xml:ns:caldav:calendar-multiget")) {
		crp->report_type = cr_calendar_multiget;
	}

	else if (!strcasecmp(el, "urn:ietf:params:xml:ns:caldav:calendar-query")) {
		crp->report_type = cr_calendar_query;
	}

	else if (!strcasecmp(el, "urn:ietf:params:xml:ns:caldav:free-busy-query")) {
		crp->report_type = cr_freebusy_query;
	}

	++crp->tag_nesting_level;
}


/*
 * XML parser callback
 */
void caldav_xml_end(void *data, const char *el)
{
	struct cr_parms *crp = (struct cr_parms *) data;
	--crp->tag_nesting_level;

	if (crp->Chardata != NULL) {
		// syslog(LOG_DEBUG, "CALDAV CHARDATA     : %s", ChrPtr(crp->Chardata));
	}
	// syslog(LOG_DEBUG, "CALDAV ELEMENT END  : <%s> %d", el, crp->tag_nesting_level);

	if ((!strcasecmp(el, "DAV::href")) || (!strcasecmp(el, "DAV:href"))) {
		if (crp->Hrefs == NULL) {	// append crp->Chardata to crp->Hrefs
			crp->Hrefs = NewStrBuf();
		} else {
			StrBufAppendBufPlain(crp->Hrefs, HKEY("|"), 0);
		}
		StrBufAppendBuf(crp->Hrefs, crp->Chardata, 0);
	}

	if (crp->Chardata != NULL) {		// Tag is closed; chardata is now out of scope.
		FreeStrBuf(&crp->Chardata);	// Free the buffer.
		crp->Chardata = NULL;
	}
}


/*
 * XML parser callback
 */
void caldav_xml_chardata(void *data, const XML_Char * s, int len)
{
	struct cr_parms *crp = (struct cr_parms *) data;

	if (crp->Chardata == NULL) {
		crp->Chardata = NewStrBuf();
	}

	StrBufAppendBufPlain(crp->Chardata, s, len, 0);

	return;
}


/*
 * Called by caldav_response() to fetch a message (by number) in the current room,
 * and return only the icalendar data as a StrBuf.  Returns NULL if not found.
 *
 * NOTE: this function expects that "MSGP text/calendar" was issued at the beginning
 * of a REPORT operation to set our preferred MIME type to calendar data.
 */
StrBuf *fetch_ical(struct ctdlsession * c, long msgnum)
{
	char buf[1024];
	StrBuf *Buf = NULL;

	ctdl_printf(c, "MSG4 %ld", msgnum);
	ctdl_readline(c, buf, sizeof(buf));
	if (buf[0] != '1') {
		return NULL;
	}

	while (ctdl_readline(c, buf, sizeof(buf)), strcmp(buf, "000")) {
		if (Buf != NULL) {	// already in body
			StrBufAppendPrintf(Buf, "%s\n", buf);
		} else if (IsEmptyStr(buf)) {	// beginning of body
			Buf = NewStrBuf();
		}
	}

	return Buf;

// webcit[13039]: msgn=53CE87AF-00392161@uncensored.citadel.org
// webcit[13039]: path=IGnatius T Foobar
// webcit[13039]: time=1208008800
// webcit[13039]: from=IGnatius T Foobar
// webcit[13039]: room=0000000001.Calendar
// webcit[13039]: node=uncnsrd
// webcit[13039]: hnod=Uncensored
// webcit[13039]: exti=040000008200E00074C5B7101A82E0080000000080C728F83E84C801000000000000000010000000E857E0DC57F53947ADF0BB91EE3A502F
// webcit[13039]: subj==?UTF-8?B?V2VzbGV5J3MgYmlydGhkYXkgcGFydHk=
// webcit[13039]: ?=
// webcit[13039]: part=||1||text/calendar|1127||
// webcit[13039]: text
// webcit[13039]: Content-type: text/calendar
// webcit[13039]: Content-length: 1127
// webcit[13039]: Content-transfer-encoding: 7bit
// webcit[13039]: X-Citadel-MSG4-Partnum: 1
// webcit[13039]:
// webcit[13039]: BEGIN:VCALENDAR
}


/*
 * Called by caldav_report() to output a single item.
 * Our policy is to throw away the list of properties the client asked for, and just send everything.
 */
void caldav_response(struct http_transaction *h, struct ctdlsession *c, StrBuf * ReportOut, StrBuf * ThisHref)
{
	long msgnum;
	StrBuf *Caldata = NULL;
	char *euid;

	euid = strrchr(ChrPtr(ThisHref), '/');
	if (euid != NULL) {
		++euid;
	} else {
		euid = (char *) ChrPtr(ThisHref);
	}

	char *unescaped_euid = strdup(euid);
	if (!unescaped_euid)
		return;
	unescape_input(unescaped_euid);

	StrBufAppendPrintf(ReportOut, "<D:response>");
	StrBufAppendPrintf(ReportOut, "<D:href>");
	StrBufXMLEscAppend(ReportOut, ThisHref, NULL, 0, 0);
	StrBufAppendPrintf(ReportOut, "</D:href>");
	StrBufAppendPrintf(ReportOut, "<D:propstat>");

	msgnum = locate_message_by_uid(c, unescaped_euid);
	free(unescaped_euid);
	if (msgnum > 0) {
		Caldata = fetch_ical(c, msgnum);
	}

	if (Caldata != NULL) {
		// syslog(LOG_DEBUG, "caldav_response(%s) 200 OK", ChrPtr(ThisHref));
		StrBufAppendPrintf(ReportOut, "<D:status>");
		StrBufAppendPrintf(ReportOut, "HTTP/1.1 200 OK");
		StrBufAppendPrintf(ReportOut, "</D:status>");
		StrBufAppendPrintf(ReportOut, "<D:prop>");
		StrBufAppendPrintf(ReportOut, "<D:getetag>");
		StrBufAppendPrintf(ReportOut, "%ld", msgnum);
		StrBufAppendPrintf(ReportOut, "</D:getetag>");
		StrBufAppendPrintf(ReportOut, "<C:calendar-data>");
		StrBufXMLEscAppend(ReportOut, Caldata, NULL, 0, 0);
		StrBufAppendPrintf(ReportOut, "</C:calendar-data>");
		StrBufAppendPrintf(ReportOut, "</D:prop>");
		FreeStrBuf(&Caldata);
		Caldata = NULL;
	} else {
		// syslog(LOG_DEBUG, "caldav_response(%s) 404 not found", ChrPtr(ThisHref));
		StrBufAppendPrintf(ReportOut, "<D:status>");
		StrBufAppendPrintf(ReportOut, "HTTP/1.1 404 not found");
		StrBufAppendPrintf(ReportOut, "</D:status>");
	}

	StrBufAppendPrintf(ReportOut, "</D:propstat>");
	StrBufAppendPrintf(ReportOut, "</D:response>");
}


/*
 * Called by report_the_room_itself() in room_functions.c when a CalDAV REPORT method
 * is requested on a calendar room.  We fire up an XML Parser to decode the request and
 * hopefully produce the correct output.
 */
void caldav_report(struct http_transaction *h, struct ctdlsession *c)
{
	struct cr_parms crp;
	char buf[1024];

	memset(&crp, 0, sizeof(struct cr_parms));

	XML_Parser xp = XML_ParserCreateNS("UTF-8", ':');
	if (xp == NULL) {
		syslog(LOG_INFO, "Cannot create XML parser!");
		do_404(h);
		return;
	}

	XML_SetElementHandler(xp, caldav_xml_start, caldav_xml_end);
	XML_SetCharacterDataHandler(xp, caldav_xml_chardata);
	XML_SetUserData(xp, &crp);
	XML_SetDefaultHandler(xp, NULL);	// Disable internal entity expansion to prevent "billion laughs attack"
	XML_Parse(xp, h->request_body, h->request_body_length, 1);
	XML_ParserFree(xp);

	if (crp.Chardata != NULL) {	// Discard any trailing chardata ... normally nothing here
		FreeStrBuf(&crp.Chardata);
		crp.Chardata = NULL;
	}

	/*
	 * We're going to make a lot of MSG4 calls, and the preferred MIME type we want is "text/calendar".
	 * The iCalendar standard is mature now, and we are no longer interested in text/x-vcal or application/ics.
	 */
	ctdl_printf(c, "MSGP text/calendar");
	ctdl_readline(c, buf, sizeof buf);

	/*
	 * Now begin the REPORT.
	 */
	syslog(LOG_DEBUG, "CalDAV REPORT type is: %d", crp.report_type);
	StrBuf *ReportOut = NewStrBuf();
	StrBufAppendPrintf(ReportOut, "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
			   "<D:multistatus " "xmlns:D=\"DAV:\" " "xmlns:C=\"urn:ietf:params:xml:ns:caldav\"" ">");

	if (crp.Hrefs != NULL) {	// Output all qualifying calendar items!
		StrBuf *ThisHref = NewStrBuf();
		const char *pvset = NULL;
		while (StrBufExtract_NextToken(ThisHref, crp.Hrefs, &pvset, '|') >= 0) {
			caldav_response(h, c, ReportOut, ThisHref);
		}
		FreeStrBuf(&ThisHref);
		FreeStrBuf(&crp.Hrefs);
		crp.Hrefs = NULL;
	}

	StrBufAppendPrintf(ReportOut, "</D:multistatus>\n");	// End the REPORT.

	add_response_header(h, strdup("Content-type"), strdup("text/xml"));
	h->response_code = 207;
	h->response_string = strdup("Multi-Status");
	h->response_body_length = StrLength(ReportOut);
	h->response_body = SmashStrBuf(&ReportOut);
}
