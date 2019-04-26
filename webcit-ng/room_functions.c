/*
 * Room functions
 *
 * Copyright (c) 1996-2019 by the citadel.org team
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
 * Return a "zero-terminated" array of message numbers in the current room.
 * Caller owns the memory and must free it.  Returns NULL if any problems.
 */
long *get_msglist(struct ctdlsession *c, char *which_msgs)
{
	char buf[1024];
	long *msglist = NULL;
	int num_msgs = 0;
	int num_alloc = 0;

	ctdl_printf(c, "MSGS %s", which_msgs);
	ctdl_readline(c, buf, sizeof(buf));
	if (buf[0] == '1') {
		do {
			if (num_msgs >= num_alloc) {
				if (num_alloc == 0) {
					num_alloc = 1024;
					msglist = malloc(num_alloc * sizeof(long));
				} else {
					num_alloc *= 2;
					msglist = realloc(msglist, num_alloc * sizeof(long));
				}
			}
			ctdl_readline(c, buf, sizeof(buf));
			msglist[num_msgs++] = atol(buf);
		} while (strcmp(buf, "000"));	// this makes the last element a "0" terminator
	}
	return msglist;
}


/*
 * Supplied with a list of potential matches from an If-Match: or If-None-Match: header, and
 * a message number (which we always use as the entity tag in Citadel), return nonzero if the
 * message number matches any of the supplied tags in the string.
 */
int match_etags(char *taglist, long msgnum)
{
	int num_tags = num_tokens(taglist, ',');
	int i = 0;
	char tag[1024];

	if (msgnum <= 0) {					// no msgnum?  no match.
		return (0);
	}

	for (i = 0; i < num_tags; ++i) {
		extract_token(tag, taglist, i, ',', sizeof tag);
		striplt(tag);
		char *lq = (strchr(tag, '"'));
		char *rq = (strrchr(tag, '"'));
		if (lq < rq) {					// has two double quotes
			strcpy(rq, "");
			strcpy(tag, ++lq);
		}
		striplt(tag);
		if (!strcmp(tag, "*")) {			// wildcard match
			return (1);
		}
		long tagmsgnum = atol(tag);
		if ((tagmsgnum > 0) && (tagmsgnum == msgnum)) {	// match
			return (1);
		}
	}

	return (0);						// no match
}


/*
 * Client is requesting a message list
 */
void json_msglist(struct http_transaction *h, struct ctdlsession *c, char *which)
{
	int i = 0;
	long *msglist = get_msglist(c, which);
	JsonValue *j = NewJsonArray(HKEY("msgs"));

	if (msglist != NULL) {
		for (i = 0; msglist[i] > 0; ++i) {
			JsonArrayAppend(j, NewJsonNumber(HKEY("m"), msglist[i]));
		}
		free(msglist);
	}

	StrBuf *sj = NewStrBuf();
	SerializeJson(sj, j, 1);	// '1' == free the source array

	add_response_header(h, strdup("Content-type"), strdup("application/json"));
	h->response_code = 200;
	h->response_string = strdup("OK");
	h->response_body_length = StrLength(sj);
	h->response_body = SmashStrBuf(&sj);
	return;
}


/*
 * Client requested an object in a room.
 */
void object_in_room(struct http_transaction *h, struct ctdlsession *c)
{
	char buf[1024];
	long msgnum = (-1);
	char unescaped_euid[1024];

	extract_token(buf, h->uri, 4, '/', sizeof buf);

	if (!strncasecmp(buf, "msgs.", 5))	// Client is requesting a list of message numbers
	{
		json_msglist(h, c, &buf[5]);
		return;
	}
#if 0
	if (!strncasecmp(buf, "threads", 5))	// Client is requesting a threaded view (still kind of fuzzy here)
	{
		threaded_view(h, c, &buf[5]);
		return;
	}

	if (!strncasecmp(buf, "flat", 5))	// Client is requesting a flat view (still kind of fuzzy here)
	{
		flat_view(h, c, &buf[5]);
		return;
	}
#endif

	if ((c->room_default_view == VIEW_CALENDAR)	// room types where objects are referenced by EUID
	    || (c->room_default_view == VIEW_TASKS)
	    || (c->room_default_view == VIEW_ADDRESSBOOK)
	    ) {
		safestrncpy(unescaped_euid, buf, sizeof unescaped_euid);
		unescape_input(unescaped_euid);
		msgnum = locate_message_by_uid(c, unescaped_euid);
	} else {
		msgnum = atol(buf);
	}

	/*
	 * All methods except PUT require the message to already exist
	 */
	if ((msgnum <= 0) && (strcasecmp(h->method, "PUT"))) {
		do_404(h);
	}

	/*
	 * If we get to this point we have a valid message number in an accessible room.
	 */
	syslog(LOG_DEBUG, "msgnum is %ld, method is %s", msgnum, h->method);

	/*
	 * A sixth component in the URL can be one of two things:
	 * (1) a MIME part specifier, in which case the client wants to download that component within the message
	 * (2) a content-type, in which ase the client wants us to try to render it a certain way
	 */
	if (num_tokens(h->uri, '/') == 6) {
		extract_token(buf, h->uri, 5, '/', sizeof buf);
		if (!IsEmptyStr(buf)) {
			if (!strcasecmp(buf, "json")) {
				json_render_one_message(h, c, msgnum);
			} else {
				download_mime_component(h, c, msgnum, buf);
			}
			return;
		}
	}

	/*
	 * Ok, we want a full message, but first let's check for the if[-none]-match headers.
	 */
	char *if_match = header_val(h, "If-Match");
	if ((if_match != NULL) && (!match_etags(if_match, msgnum))) {
		do_412(h);
		return;
	}

	char *if_none_match = header_val(h, "If-None-Match");
	if ((if_none_match != NULL) && (match_etags(if_none_match, msgnum))) {
		do_412(h);
		return;
	}

	/*
	 * DOOOOOO ITTTTT!!!
	 */

	if (!strcasecmp(h->method, "DELETE")) {
		dav_delete_message(h, c, msgnum);
	} else if (!strcasecmp(h->method, "GET")) {
		dav_get_message(h, c, msgnum);
	} else if (!strcasecmp(h->method, "PUT")) {
		dav_put_message(h, c, unescaped_euid, msgnum);
	} else {
		do_404(h);	// Got this far but the method made no sense?  Bummer.
	}

}


/*
 * Called by the_room_itself() when the HTTP method is REPORT
 */
void report_the_room_itself(struct http_transaction *h, struct ctdlsession *c)
{
	if (c->room_default_view == VIEW_CALENDAR) {
		caldav_report(h, c);	// CalDAV REPORTs ... fmgwac
		return;
	}

	do_404(h);		// future implementations like CardDAV will require code paths here
}


/*
 * Called by the_room_itself() when the HTTP method is OPTIONS
 */
void options_the_room_itself(struct http_transaction *h, struct ctdlsession *c)
{
	h->response_code = 200;
	h->response_string = strdup("OK");
	if (c->room_default_view == VIEW_CALENDAR) {
		add_response_header(h, strdup("DAV"), strdup("1, calendar-access"));	// offer CalDAV
	} else if (c->room_default_view == VIEW_ADDRESSBOOK) {
		add_response_header(h, strdup("DAV"), strdup("1, addressbook"));	// offer CardDAV
	} else {
		add_response_header(h, strdup("DAV"), strdup("1"));	// ordinary WebDAV for all other room types
	}
	add_response_header(h, strdup("Allow"), strdup("OPTIONS, PROPFIND, GET, PUT, REPORT, DELETE"));
}


/*
 * Called by the_room_itself() when the HTTP method is PROPFIND
 */
void propfind_the_room_itself(struct http_transaction *h, struct ctdlsession *c)
{
	char *e;
	long timestamp;
	int dav_depth = (header_val(h, "Depth") ? atoi(header_val(h, "Depth")) : INT_MAX);
	syslog(LOG_DEBUG, "Client PROPFIND requested depth: %d", dav_depth);
	StrBuf *Buf = NewStrBuf();

	StrBufAppendPrintf(Buf, "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
			   "<D:multistatus " "xmlns:D=\"DAV:\" " "xmlns:C=\"urn:ietf:params:xml:ns:caldav\"" ">");

	/* Transmit the collection resource */
	StrBufAppendPrintf(Buf, "<D:response>");
	StrBufAppendPrintf(Buf, "<D:href>");
	StrBufXMLEscAppend(Buf, NULL, h->site_prefix, strlen(h->site_prefix), 0);
	StrBufAppendPrintf(Buf, "/ctdl/r/");
	StrBufXMLEscAppend(Buf, NULL, c->room, strlen(c->room), 0);
	StrBufAppendPrintf(Buf, "</D:href>");

	StrBufAppendPrintf(Buf, "<D:propstat>");
	StrBufAppendPrintf(Buf, "<D:status>HTTP/1.1 200 OK</D:status>");
	StrBufAppendPrintf(Buf, "<D:prop>");
	StrBufAppendPrintf(Buf, "<D:displayname>");
	StrBufXMLEscAppend(Buf, NULL, c->room, strlen(c->room), 0);
	StrBufAppendPrintf(Buf, "</D:displayname>");

	StrBufAppendPrintf(Buf, "<D:owner />");	// empty owner ought to be legal; see rfc3744 section 5.1

	StrBufAppendPrintf(Buf, "<D:resourcetype><D:collection />");
	switch (c->room_default_view) {
	case VIEW_CALENDAR:
		StrBufAppendPrintf(Buf, "<C:calendar />");	// RFC4791 section 4.2
		break;
	}
	StrBufAppendPrintf(Buf, "</D:resourcetype>");

	int enumerate_by_euid = 0;	// nonzero if messages will be retrieved by euid instead of msgnum
	switch (c->room_default_view) {
	case VIEW_CALENDAR:	// RFC4791 section 5.2
		StrBufAppendPrintf(Buf,
				   "<C:supported-calendar-component-set><C:comp name=\"VEVENT\"/></C:supported-calendar-component-set>");
		StrBufAppendPrintf(Buf, "<C:supported-calendar-data>");
		StrBufAppendPrintf(Buf, "<C:calendar-data content-type=\"text/calendar\" version=\"2.0\"/>");
		StrBufAppendPrintf(Buf, "</C:supported-calendar-data>");
		enumerate_by_euid = 1;
		break;
	case VIEW_TASKS:	// RFC4791 section 5.2
		StrBufAppendPrintf(Buf,
				   "<C:supported-calendar-component-set><C:comp name=\"VTODO\"/></C:supported-calendar-component-set>");
		StrBufAppendPrintf(Buf, "<C:supported-calendar-data>");
		StrBufAppendPrintf(Buf, "<C:calendar-data content-type=\"text/calendar\" version=\"2.0\"/>");
		StrBufAppendPrintf(Buf, "</C:supported-calendar-data>");
		enumerate_by_euid = 1;
		break;
	case VIEW_ADDRESSBOOK:	// FIXME put some sort of CardDAV crapola here when we implement it
		enumerate_by_euid = 1;
		break;
	case VIEW_WIKI:	// FIXME invent "WikiDAV" ?
		enumerate_by_euid = 1;
		break;
	}


	/* FIXME get the mtime
	   StrBufAppendPrintf(Buf, "<D:getlastmodified>");
	   escputs(datestring);
	   StrBufAppendPrintf(Buf, "</D:getlastmodified>");
	 */

	StrBufAppendPrintf(Buf, "</D:prop>");
	StrBufAppendPrintf(Buf, "</D:propstat>");
	StrBufAppendPrintf(Buf, "</D:response>\n");

	// If a depth greater than zero was specified, transmit the collection listing
	// BEGIN COLLECTION
	if (dav_depth > 0) {
		long *msglist = get_msglist(c, "ALL");
		if (msglist) {
			int i;
			for (i = 0; (msglist[i] > 0); ++i) {
				if ((i % 10) == 0)
					syslog(LOG_DEBUG, "PROPFIND enumerated %d messages", i);
				e = NULL;	// EUID gets stored here
				timestamp = 0;

				char cbuf[1024];
				ctdl_printf(c, "MSG0 %ld|3", msglist[i]);
				ctdl_readline(c, cbuf, sizeof(cbuf));
				if (cbuf[0] == '1')
					while (ctdl_readline(c, cbuf, sizeof(cbuf)), strcmp(cbuf, "000")) {
						if ((enumerate_by_euid) && (!strncasecmp(cbuf, "exti=", 5))) {
							// e = strdup(&cbuf[5]);
							int elen = (2 * strlen(&cbuf[5]));
							e = malloc(elen);
							urlesc(e, elen, &cbuf[5]);
						}
						if (!strncasecmp(cbuf, "time=", 5)) {
							timestamp = atol(&cbuf[5]);
						}
					}
				if (e == NULL) {
					e = malloc(20);
					sprintf(e, "%ld", msglist[i]);
				}
				StrBufAppendPrintf(Buf, "<D:response>");

				// Generate the 'href' tag for this message
				StrBufAppendPrintf(Buf, "<D:href>");
				StrBufXMLEscAppend(Buf, NULL, h->site_prefix, strlen(h->site_prefix), 0);
				StrBufAppendPrintf(Buf, "/ctdl/r/");
				StrBufXMLEscAppend(Buf, NULL, c->room, strlen(c->room), 0);
				StrBufAppendPrintf(Buf, "/");
				StrBufXMLEscAppend(Buf, NULL, e, strlen(e), 0);
				StrBufAppendPrintf(Buf, "</D:href>");
				StrBufAppendPrintf(Buf, "<D:propstat>");
				StrBufAppendPrintf(Buf, "<D:status>HTTP/1.1 200 OK</D:status>");
				StrBufAppendPrintf(Buf, "<D:prop>");

				switch (c->room_default_view) {
				case VIEW_CALENDAR:
					StrBufAppendPrintf(Buf,
							   "<D:getcontenttype>text/calendar; component=vevent</D:getcontenttype>");
					break;
				case VIEW_TASKS:
					StrBufAppendPrintf(Buf,
							   "<D:getcontenttype>text/calendar; component=vtodo</D:getcontenttype>");
					break;
				case VIEW_ADDRESSBOOK:
					StrBufAppendPrintf(Buf, "<D:getcontenttype>text/x-vcard</D:getcontenttype>");
					break;
				}

				if (timestamp > 0) {
					char *datestring = http_datestring(timestamp);
					if (datestring) {
						StrBufAppendPrintf(Buf, "<D:getlastmodified>");
						StrBufXMLEscAppend(Buf, NULL, datestring, strlen(datestring), 0);
						StrBufAppendPrintf(Buf, "</D:getlastmodified>");
						free(datestring);
					}
					if (enumerate_by_euid)	// FIXME ajc 2017oct30 should this be inside the timestamp conditional?
					{
						StrBufAppendPrintf(Buf, "<D:getetag>\"%ld\"</D:getetag>", msglist[i]);
					}
				}
				StrBufAppendPrintf(Buf, "</D:prop></D:propstat></D:response>\n");
				free(e);
			}
			free(msglist);
		};
	}
	// END COLLECTION

	StrBufAppendPrintf(Buf, "</D:multistatus>\n");

	add_response_header(h, strdup("Content-type"), strdup("text/xml"));
	h->response_code = 207;
	h->response_string = strdup("Multi-Status");
	h->response_body_length = StrLength(Buf);
	h->response_body = SmashStrBuf(&Buf);
}

// some good examples here
// http://blogs.nologin.es/rickyepoderi/index.php?/archives/14-Introducing-CalDAV-Part-I.html


/*
 * Called by the_room_itself() when the HTTP method is PROPFIND
 */
void get_the_room_itself(struct http_transaction *h, struct ctdlsession *c)
{
	JsonValue *j = NewJsonObject(HKEY("gotoroom"));

	JsonObjectAppend(j, NewJsonPlainString(HKEY("name"), c->room, -1));
	JsonObjectAppend(j, NewJsonNumber(HKEY("current_view"), c->room_current_view));
	JsonObjectAppend(j, NewJsonNumber(HKEY("default_view"), c->room_default_view));
	JsonObjectAppend(j, NewJsonNumber(HKEY("new_messages"), c->new_messages));
	JsonObjectAppend(j, NewJsonNumber(HKEY("total_messages"), c->total_messages));
	JsonObjectAppend(j, NewJsonNumber(HKEY("last_seen"), c->last_seen));

	StrBuf *sj = NewStrBuf();
	SerializeJson(sj, j, 1);	// '1' == free the source array

	add_response_header(h, strdup("Content-type"), strdup("application/json"));
	h->response_code = 200;
	h->response_string = strdup("OK");
	h->response_body_length = StrLength(sj);
	h->response_body = SmashStrBuf(&sj);
	return;
}


/*
 * Handle REST/DAV requests for the room itself (such as /ctdl/r/roomname
 * or /ctdl/r/roomname/ but *not* specific objects within the room)
 */
void the_room_itself(struct http_transaction *h, struct ctdlsession *c)
{
	// OPTIONS method on the room itself usually is a DAV client assessing what's here.

	if (!strcasecmp(h->method, "OPTIONS")) {
		options_the_room_itself(h, c);
		return;
	}
	// PROPFIND method on the room itself could be looking for a directory

	if (!strcasecmp(h->method, "PROPFIND")) {
		propfind_the_room_itself(h, c);
		return;
	}
	// REPORT method on the room itself is probably the dreaded CalDAV tower-of-crapola

	if (!strcasecmp(h->method, "REPORT")) {
		report_the_room_itself(h, c);
		return;
	}
	// GET method on the room itself is an API call, possibly from our JavaScript front end

	if (!strcasecmp(h->method, "get")) {
		get_the_room_itself(h, c);
		return;
	}
	// we probably want a "go to this room" for interactive access
	do_404(h);
}


/*
 * Dispatcher for "/ctdl/r" and "/ctdl/r/" for the room list
 */
void room_list(struct http_transaction *h, struct ctdlsession *c)
{
	char buf[1024];
	char roomname[1024];

	ctdl_printf(c, "LKRA");
	ctdl_readline(c, buf, sizeof(buf));
	if (buf[0] != '1') {
		do_502(h);
		return;
	}

	JsonValue *j = NewJsonArray(HKEY("lkra"));
	while (ctdl_readline(c, buf, sizeof(buf)), strcmp(buf, "000")) {

		// name|QRflags|QRfloor|QRorder|QRflags2|ra|current_view|default_view|mtime
		JsonValue *jr = NewJsonObject(HKEY("room"));

		extract_token(roomname, buf, 0, '|', sizeof roomname);
		JsonObjectAppend(jr, NewJsonPlainString(HKEY("name"), roomname, -1));

		int ra = extract_int(buf, 5);
		JsonObjectAppend(jr, NewJsonBool(HKEY("known"), (ra & UA_KNOWN)));
		JsonObjectAppend(jr, NewJsonBool(HKEY("hasnewmsgs"), (ra & UA_HASNEWMSGS)));

		int floor = extract_int(buf, 2);
		JsonObjectAppend(jr, NewJsonNumber(HKEY("floor"), floor));

		int rorder = extract_int(buf, 3);
		JsonObjectAppend(jr, NewJsonNumber(HKEY("rorder"), rorder));

		JsonArrayAppend(j, jr);	// add the room to the array
	}

	StrBuf *sj = NewStrBuf();
	SerializeJson(sj, j, 1);	// '1' == free the source array

	add_response_header(h, strdup("Content-type"), strdup("application/json"));
	h->response_code = 200;
	h->response_string = strdup("OK");
	h->response_body_length = StrLength(sj);
	h->response_body = SmashStrBuf(&sj);
}


/*
 * Dispatcher for paths starting with /ctdl/r/
 */
void ctdl_r(struct http_transaction *h, struct ctdlsession *c)
{
	char requested_roomname[128];
	char buf[1024];

	// All room-related functions require being "in" the room specified.  Are we in that room already?
	extract_token(requested_roomname, h->uri, 3, '/', sizeof requested_roomname);
	unescape_input(requested_roomname);

	if (IsEmptyStr(requested_roomname))	//      /ctdl/r/
	{
		room_list(h, c);
		return;
	}
	// If not, try to go there.
	if (strcasecmp(requested_roomname, c->room)) {
		ctdl_printf(c, "GOTO %s", requested_roomname);
		ctdl_readline(c, buf, sizeof(buf));
		if (buf[0] == '2') {
			// buf[3] will indicate whether any instant messages are waiting
			extract_token(c->room, &buf[4], 0, '|', sizeof c->room);
			c->new_messages = extract_int(&buf[4], 1);
			c->total_messages = extract_int(&buf[4], 2);
			//      3       (int)info                       Info flag: set to nonzero if the user needs to read this room's info file
			//      4       (int)CCC->room.QRflags          Various flags associated with this room.
			//      5       (long)CCC->room.QRhighest       The highest message number present in this room
			c->last_seen = extract_long(&buf[4], 6);	// The highest message number the user has read in this room
			//      7       (int)rmailflag                  Boolean flag: 1 if this is a Mail> room, 0 otherwise.
			//      8       (int)raideflag                  Nonzero if user is either Aide or a Room Aide in this room
			//      9       (int)newmailcount               The number of new Mail messages the user has
			//      10      (int)CCC->room.QRfloor          The floor number this room resides on
			c->room_current_view = extract_int(&buf[4], 11);
			c->room_default_view = extract_int(&buf[4], 12);
			//      13      (int)is_trash                   Boolean flag: 1 if this is the user's Trash folder, 0 otherwise.
			//      14      (int)CCC->room.QRflags2         More flags associated with this room
			//      15      (long)CCC->room.QRmtime         Timestamp of the last write activity in this room
		} else {
			do_404(h);
			return;
		}
	}
	// At this point our Citadel client session is "in" the specified room.

	if (num_tokens(h->uri, '/') == 4)	//      /ctdl/r/roomname
	{
		the_room_itself(h, c);
		return;
	}

	extract_token(buf, h->uri, 4, '/', sizeof buf);
	if (num_tokens(h->uri, '/') == 5) {
		if (IsEmptyStr(buf)) {
			the_room_itself(h, c);	//      /ctdl/r/roomname/       ( same as /ctdl/r/roomname )
		} else {
			object_in_room(h, c);	//      /ctdl/r/roomname/object
		}
		return;
	}
	if (num_tokens(h->uri, '/') == 6) {
		object_in_room(h, c);	//      /ctdl/r/roomname/object/ or possibly /ctdl/r/roomname/object/component
		return;
	}
	// If we get to this point, the client specified a valid room but requested an action we don't know how to perform.
	do_404(h);
}
