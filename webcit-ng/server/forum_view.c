// The code in here feeds messages out as JSON to the client browser.  It is currently being used
// for the forum view, but as we implement other views we'll probably reuse a lot of what's here.
//
// Copyright (c) 1996-2022 by the citadel.org team
//
// This program is open source software.  Use, duplication, or
// disclosure are subject to the GNU General Public License v3.

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


// Convenience function for json_render_one_message()
// Converts a string of comma-separated recipients into a JSON Array
JsonValue *json_tokenize_recipients(const char *Key, long keylen, char *recp) {
	char tokbuf[1024];

	JsonValue *j = NewJsonArray(Key, keylen);
	int num_recp = num_tokens(recp, ',');
	for (int i=0; i<num_recp; ++i) {
		extract_token(tokbuf, recp, i, ',', sizeof(tokbuf));
		string_trim(tokbuf);
		JsonArrayAppend(j, NewJsonPlainString(HKEY("r"), tokbuf, strlen(tokbuf)));
	}
	return(j);
}


// Fetch a single message and return it in JSON format for client-side rendering
void json_render_one_message(struct http_transaction *h, struct ctdlsession *c, long msgnum) {
	StrBuf *raw_msg = NULL;
	StrBuf *sanitized_msg = NULL;
	char buf[1024];
	char content_transfer_encoding[1024] = { 0 };
	char content_type[1024] = { 0 };
	char datetime[128] = { 0 };
	int message_originated_locally = 0;

	setup_for_forum_view(c);

	ctdl_printf(c, "MSG4 %ld", msgnum);
	ctdl_readline(c, buf, sizeof(buf));
	if (buf[0] != '1') {
		do_404(h);
		return;
	}

	JsonValue *j = NewJsonObject(HKEY("message"));
	JsonObjectAppend(j, NewJsonNumber(HKEY("msgnum"), msgnum));

	while ((ctdl_readline(c, buf, sizeof(buf)) >= 0) && (strcmp(buf, "text")) && (strcmp(buf, "000"))) {
		utf8ify_rfc822_string(&buf[5]);

		// citadel header parsing here
		if (!strncasecmp(buf, "from=", 5)) {
			JsonObjectAppend(j, NewJsonPlainString(HKEY("from"), &buf[5], -1));
		}
		else if (!strncasecmp(buf, "rfca=", 5)) {
			JsonObjectAppend(j, NewJsonPlainString(HKEY("rfca"), &buf[5], -1));
		}
		else if (!strncasecmp(buf, "time=", 5)) {
			JsonObjectAppend(j, NewJsonNumber(HKEY("time"), atol(&buf[5])));
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
		else if (!strncasecmp(buf, "rcpt=", 5)) {
			JsonObjectAppend(j, json_tokenize_recipients(HKEY("rcpt"), &buf[5]));
		}
		else if (!strncasecmp(buf, "cccc=", 5)) {
			JsonObjectAppend(j, json_tokenize_recipients(HKEY("cccc"), &buf[5]));
		}
	}
	JsonObjectAppend(j, NewJsonNumber(HKEY("locl"), message_originated_locally));

	if (!strcmp(buf, "text")) {
		while ((ctdl_readline(c, buf, sizeof(buf)) >= 0) && (strcmp(buf, "")) && (strcmp(buf, "000"))) {
			// rfc822 header parsing here
			if (!strncasecmp(buf, "Content-transfer-encoding:", 26)) {
				strcpy(content_transfer_encoding, &buf[26]);
				string_trim(content_transfer_encoding);
			}
			if (!strncasecmp(buf, "Content-type:", 13)) {
				strcpy(content_type, &buf[13]);
				string_trim(content_type);
			}
		}
		if (!strcmp(buf, "000")) {		// if we have an empty message, don't try to read further
			raw_msg = NULL;
		}
		else {
			raw_msg = ctdl_readtextmsg(c);
		}
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
