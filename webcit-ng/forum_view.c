// Forum view (threaded/flat)
//
// Copyright (c) 1996-2022 by the citadel.org team
//
// This program is open source software.  It runs great on the
// Linux operating system (and probably elsewhere).  You can use,
// copy, and run it under the terms of the GNU General Public
// License version 3.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

#include "webcit.h"

// Commands we need to send to Citadel Server before we begin rendering forum view.
// These are common to flat and threaded views.
void setup_for_forum_view(struct ctdlsession *c) {
	char buf[1024];
	ctdl_printf(c, "MSGP text/html|text/plain");	// Declare the MIME types we know how to render
	ctdl_readline(c, buf, sizeof(buf));		// Ignore the response
	ctdl_printf(c, "MSGP dont_decode");		// Tell the server we will decode base64/etc client-side
	ctdl_readline(c, buf, sizeof(buf));		// Ignore the response
}


// Fetch a single message and return it in JSON format for client-side rendering
void json_render_one_message(struct http_transaction *h, struct ctdlsession *c, long msgnum) {
	StrBuf *raw_msg = NULL;
	StrBuf *sanitized_msg = NULL;
	char buf[1024];
	char content_transfer_encoding[1024] = { 0 };
	char content_type[1024] = { 0 };
	char datetime[128] = { 0 };
	char author[1024] = { 0 };
	char emailaddr[1024] = { 0 };
	int message_originated_locally = 0;

	setup_for_forum_view(c);

	ctdl_printf(c, "MSG4 %ld", msgnum);
	ctdl_readline(c, buf, sizeof(buf));
	if (buf[0] != '1') {
		do_404(h);
		return;
	}

	JsonValue *j = NewJsonObject(HKEY("message"));

	while ((ctdl_readline(c, buf, sizeof(buf)) >= 0) && (strcmp(buf, "text")) && (strcmp(buf, "000"))) {

		// citadel header parsing here
		if (!strncasecmp(buf, "from=", 5)) {
			safestrncpy(author, &buf[5], sizeof author);
		}
		else if (!strncasecmp(buf, "rfca=", 5)) {
			safestrncpy(emailaddr, &buf[5], sizeof emailaddr);
		}
		else if (!strncasecmp(buf, "time=", 5)) {
			time_t tt;
			struct tm tm;
			tt = atol(&buf[5]);
			localtime_r(&tt, &tm);
			strftime(datetime, sizeof datetime, "%c", &tm);
			JsonObjectAppend(j, NewJsonPlainString(HKEY("time"), datetime, -1));
		}
		else if (!strncasecmp(buf, "locl=", 5)) {
			message_originated_locally = 1;
		}
		else if (!strncasecmp(buf, "subj=", 5)) {
			JsonObjectAppend(j, NewJsonPlainString(HKEY("subj"), &buf[5], -1));
		}
		else if (!strncasecmp(buf, "msgn=", 5)) {
			JsonObjectAppend(j, NewJsonPlainString(HKEY("msgn"), &buf[5], -1));
		}
		else if (!strncasecmp(buf, "wefw=", 5)) {
			JsonObjectAppend(j, NewJsonPlainString(HKEY("wefw"), &buf[5], -1));
		}
	}

	if (message_originated_locally) {
		JsonObjectAppend(j, NewJsonPlainString(HKEY("from"), author, -1));
	}
	else {
		JsonObjectAppend(j, NewJsonPlainString(HKEY("from"), emailaddr, -1));		// FIXME do the compound address string
	}

	if (!strcmp(buf, "text")) {
		while ((ctdl_readline(c, buf, sizeof(buf)) >= 0) && (strcmp(buf, "")) && (strcmp(buf, "000"))) {
			// rfc822 header parsing here
			if (!strncasecmp(buf, "Content-transfer-encoding:", 26)) {
				strcpy(content_transfer_encoding, &buf[26]);
				striplt(content_transfer_encoding);
			}
			if (!strncasecmp(buf, "Content-type:", 13)) {
				strcpy(content_type, &buf[13]);
				striplt(content_type);
			}
		}
		raw_msg = ctdl_readtextmsg(c);
	}
	else {
		raw_msg = NULL;
	}

	if (raw_msg) {
		// These are the encodings we know how to handle.  Decode in-place.

		if (!strcasecmp(content_transfer_encoding, "base64")) {
			StrBufDecodeBase64(raw_msg);
		}
		if (!strcasecmp(content_transfer_encoding, "quoted-printable")) {
			StrBufDecodeQP(raw_msg);
		}

		// At this point, raw_msg contains the decoded message.
		// Now run through the renderers we have available.

		if (!strncasecmp(content_type, "text/html", 9)) {
			sanitized_msg = html2html("UTF-8", 0, c->room, msgnum, raw_msg);
		}
		else if (!strncasecmp(content_type, "text/plain", 10)) {
			sanitized_msg = text2html("UTF-8", 0, c->room, msgnum, raw_msg);
		}
		else if (!strncasecmp(content_type, "text/x-citadel-variformat", 25)) {
			sanitized_msg = variformat2html(raw_msg);
		}
		else {
			sanitized_msg = NewStrBufPlain(HKEY("<i>No renderer for this content type</i><br>"));
			syslog(LOG_WARNING, "forum_view: no renderer for content type %s", content_type);
		}
		FreeStrBuf(&raw_msg);

		// If sanitized_msg is not NULL, we have rendered the message and can output it.
		if (sanitized_msg) {
			JsonObjectAppend(j, NewJsonString(HKEY("text"), sanitized_msg, NEWJSONSTRING_SMASHBUF));
		}
		else {
			syslog(LOG_WARNING, "forum_view: message %ld of content type %s converted to NULL", msgnum, content_type);
		}
	}

	StrBuf *sj = NewStrBuf();
	SerializeJson(sj, j, 1);	// '1' == free the source object

	add_response_header(h, strdup("Content-type"), strdup("application/json"));
	h->response_code = 200;
	h->response_string = strdup("OK");
	h->response_body_length = StrLength(sj);
	h->response_body = SmashStrBuf(&sj);
}
