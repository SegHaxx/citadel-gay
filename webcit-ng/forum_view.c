/*
 * Forum view (threaded/flat)
 *
 * Copyright (c) 1996-2018 by the citadel.org team
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

struct mthread {
	long msgnum;
	time_t datetime;
	int threadhash;
	int refhashes[10];
	char from[64];
	int parent;
};


// Commands we need to send to Citadel Server before we begin rendering forum view.
// These are common to flat and threaded views.
//
void setup_for_forum_view(struct ctdlsession *c)
{
	char buf[1024];
	ctdl_printf(c, "MSGP text/html|text/plain");	// Declare the MIME types we know how to render
	ctdl_readline(c, buf, sizeof(buf));		// Ignore the response
	ctdl_printf(c, "MSGP dont_decode");		// Tell the server we will decode base64/etc client-side
	ctdl_readline(c, buf, sizeof(buf));		// Ignore the response
}


#if 0
// Renderer for one message in the threaded view
// (This will probably work for the flat view too.)
//
void forum_render_one_message(struct ctdlsession *c, StrBuf * sj, long msgnum)
{
	StrBuf *raw_msg = NULL;
	StrBuf *sanitized_msg = NULL;
	char buf[1024];
	char content_transfer_encoding[1024] = { 0 };
	char content_type[1024] = { 0 };
	char author[128] = { 0 };
	char datetime[128] = { 0 };

	ctdl_printf(c, "MSG4 %ld", msgnum);
	ctdl_readline(c, buf, sizeof(buf));
	if (buf[0] != '1') {
		StrBufAppendPrintf(sj, "<div>ERROR CONDITION FIXME WRITE A BOX</div>");
		return;
	}

	while ((ctdl_readline(c, buf, sizeof(buf)) >= 0) && (strcmp(buf, "text")) && (strcmp(buf, "000"))) {
		// citadel header parsing here
		if (!strncasecmp(buf, "from=", 5)) {
			safestrncpy(author, &buf[5], sizeof author);
		}
		if (!strncasecmp(buf, "time=", 5)) {
			time_t tt;
			struct tm tm;
			tt = atol(&buf[5]);
			localtime_r(&tt, &tm);
			strftime(datetime, sizeof datetime, "%c", &tm);
		}
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
	} else {
		raw_msg = NULL;
	}

	// begin output

	StrBufAppendPrintf(sj, "<div>");	// begin message wrapper
	StrBufAppendPrintf(sj, "<div style=\"float:left;padding-right:2px\">");	// begin avatar FIXME move the style to a stylesheet
	StrBufAppendPrintf(sj, "<i class=\"fa fa-user-circle fa-2x\"></i> ");	// FIXME temporary avatar
	StrBufAppendPrintf(sj, "</div>");	// end avatar
	StrBufAppendPrintf(sj, "<div>");	// begin content
	StrBufAppendPrintf(sj, "<div>");	// begin header
	StrBufAppendPrintf(sj, "<span class=\"ctdl-username\"><a href=\"#\">%s</a></span> ", author);	// FIXME link to user profile or whatever
	StrBufAppendPrintf(sj, "<span class=\"ctdl-msgdate\">%s</span> ", datetime);
	StrBufAppendPrintf(sj, "</div>");	// end header
	StrBufAppendPrintf(sj, "<div>");	// begin body

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
		} else if (!strncasecmp(content_type, "text/plain", 10)) {
			sanitized_msg = text2html("UTF-8", 0, c->room, msgnum, raw_msg);
		} else if (!strncasecmp(content_type, "text/x-citadel-variformat", 25)) {
			sanitized_msg = variformat2html(raw_msg);
		} else {
			sanitized_msg = NewStrBufPlain(HKEY("<i>No renderer for this content type</i><br>"));
		}
		FreeStrBuf(&raw_msg);

		// If sanitized_msg is not NULL, we have rendered the message and can output it.

		if (sanitized_msg) {
			StrBufAppendBuf(sj, sanitized_msg, 0);
			FreeStrBuf(&sanitized_msg);
		}
	}

	StrBufAppendPrintf(sj, "</div>");	// end body
	StrBufAppendPrintf(sj, "</div>");	// end content
	StrBufAppendPrintf(sj, "</div>");	// end wrapper
}


// This code implements the thread display code.  The thread sorting algorithm is working nicely but we're trying
// not to do rendering in the C server of webcit.  Maybe move it into the server as "MSGS threaded" or something like that?

// Threaded view (recursive section)
//
void thread_o_print(struct ctdlsession *c, StrBuf * sj, struct mthread *m, int num_msgs, int where_parent_is, int nesting_level)
{
	int i = 0;
	int j = 0;
	int num_printed = 0;

	for (i = 0; i < num_msgs; ++i) {
		if (m[i].parent == where_parent_is) {

			if (++num_printed == 1) {
				StrBufAppendPrintf(sj, "<ul style=\"list-style-type: none;\">");
			}

			StrBufAppendPrintf(sj, "<li class=\"post\" id=\"post-%ld\">", m[i].msgnum);
			forum_render_one_message(c, sj, m[i].msgnum);
			StrBufAppendPrintf(sj, "</li>\r\n");
			if (i != 0) {
				thread_o_print(c, sj, m, num_msgs, i, nesting_level + 1);
			}
		}
	}

	if (num_printed > 0) {
		StrBufAppendPrintf(sj, "</ul>");
	}
}


// Threaded view (entry point)
//
void threaded_view(struct http_transaction *h, struct ctdlsession *c, char *which)
{
	int num_msgs = 0;
	int num_alloc = 0;
	struct mthread *m;
	char buf[1024];
	char refs[1024];
	int i, j, k;

	ctdl_printf(c, "MSGS ALL|||9");	// 9 == headers + thread references
	ctdl_readline(c, buf, sizeof(buf));
	if (buf[0] != '1') {
		do_404(h);
		return;
	}

	StrBuf *sj = NewStrBuf();
	StrBufAppendPrintf(sj, "<html><body>\r\n");

	while (ctdl_readline(c, buf, sizeof buf), strcmp(buf, "000")) {

		++num_msgs;
		if (num_msgs > num_alloc) {
			if (num_alloc == 0) {
				num_alloc = 100;
				m = malloc(num_alloc * sizeof(struct mthread));
			} else {
				num_alloc *= 2;
				m = realloc(m, (num_alloc * sizeof(struct mthread)));
			}
		}

		memset(&m[num_msgs - 1], 0, sizeof(struct mthread));
		m[num_msgs - 1].msgnum = extract_long(buf, 0);
		m[num_msgs - 1].datetime = extract_long(buf, 1);
		extract_token(m[num_msgs - 1].from, buf, 2, '|', sizeof m[num_msgs - 1].from);
		m[num_msgs - 1].threadhash = extract_int(buf, 6);
		extract_token(refs, buf, 7, '|', sizeof refs);

		char *t;
		char *r = refs;
		i = 0;
		while ((t = strtok_r(r, ",", &r))) {
			if (i == 0) {
				m[num_msgs - 1].refhashes[0] = atoi(t);	// always keep the first one
			} else {
				memcpy(&m[num_msgs - 1].refhashes[1], &m[num_msgs - 1].refhashes[2], sizeof(int) * 8);	// shift the rest
				m[num_msgs - 1].refhashes[9] = atoi(t);
			}
			++i;
		}

	}

	// Sort by thread.  I did read jwz's sorting algorithm and it looks pretty good, but jwz is a self-righteous asshole so we do it our way.
	for (i = 0; i < num_msgs; ++i) {
		for (j = 9; (j >= 0) && (m[i].parent == 0); --j) {
			for (k = 0; (k < num_msgs) && (m[i].parent == 0); ++k) {
				if (m[i].refhashes[j] == m[k].threadhash) {
					m[i].parent = k;
				}
			}
		}
	}

	// Now render it
	setup_for_forum_view(c);
	thread_o_print(c, sj, m, num_msgs, 0, 0);	// Render threads recursively and recursively

	// Garbage collection is for people who aren't smart enough to manage their own memory.
	if (num_msgs > 0) {
		free(m);
	}

	StrBufAppendPrintf(sj, "</body></html>\r\n");

	add_response_header(h, strdup("Content-type"), strdup("text/html; charset=utf-8"));
	h->response_code = 200;
	h->response_string = strdup("OK");
	h->response_body_length = StrLength(sj);
	h->response_body = SmashStrBuf(&sj);
	return;
}


// flat view (entry point)
//
void flat_view(struct http_transaction *h, struct ctdlsession *c, char *which)
{
	StrBuf *sj = NewStrBuf();
	StrBufAppendPrintf(sj, "<html><body>\r\n");

	setup_for_forum_view(c);
	long *msglist = get_msglist(c, "ALL");
	if (msglist) {
		int i;
		for (i = 0; (msglist[i] > 0); ++i) {
			forum_render_one_message(c, sj, msglist[i]);
		}
		free(msglist);
	}

	StrBufAppendPrintf(sj, "</body></html>\r\n");

	add_response_header(h, strdup("Content-type"), strdup("text/html; charset=utf-8"));
	h->response_code = 200;
	h->response_string = strdup("OK");
	h->response_body_length = StrLength(sj);
	h->response_body = SmashStrBuf(&sj);
	return;
}


// render one message (entire transaction)      FIXME EXTERMINATE
//
void html_render_one_message(struct http_transaction *h, struct ctdlsession *c, long msgnum)
{
	StrBuf *sj = NewStrBuf();
	StrBufAppendPrintf(sj, "<html><body>\r\n");
	setup_for_forum_view(c);	// FIXME way too inefficient to do this for every message !!!!!!!!!!!!!
	forum_render_one_message(c, sj, msgnum);
	StrBufAppendPrintf(sj, "</body></html>\r\n");
	add_response_header(h, strdup("Content-type"), strdup("text/html; charset=utf-8"));
	h->response_code = 200;
	h->response_string = strdup("OK");
	h->response_body_length = StrLength(sj);
	h->response_body = SmashStrBuf(&sj);
	return;
}

#endif

// Fetch a single message and return it in JSON format for client-side rendering
//
void json_render_one_message(struct http_transaction *h, struct ctdlsession *c, long msgnum)
{
	StrBuf *raw_msg = NULL;
	StrBuf *sanitized_msg = NULL;
	char buf[1024];
	char content_transfer_encoding[1024] = { 0 };
	char content_type[1024] = { 0 };
	char author[128] = { 0 };
	char datetime[128] = { 0 };

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
			JsonObjectAppend(j, NewJsonPlainString(HKEY("from"), &buf[5], -1));
		}
		if (!strncasecmp(buf, "rfca=", 5)) {
			JsonObjectAppend(j, NewJsonPlainString(HKEY("from"), &buf[5], -1));
		}
		if (!strncasecmp(buf, "time=", 5)) {
			time_t tt;
			struct tm tm;
			tt = atol(&buf[5]);
			localtime_r(&tt, &tm);
			strftime(datetime, sizeof datetime, "%c", &tm);
			JsonObjectAppend(j, NewJsonPlainString(HKEY("time"), datetime, -1));
		}
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
	} else {
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
		} else if (!strncasecmp(content_type, "text/plain", 10)) {
			sanitized_msg = text2html("UTF-8", 0, c->room, msgnum, raw_msg);
		} else if (!strncasecmp(content_type, "text/x-citadel-variformat", 25)) {
			sanitized_msg = variformat2html(raw_msg);
		} else {
			sanitized_msg = NewStrBufPlain(HKEY("<i>No renderer for this content type</i><br>"));
		}
		FreeStrBuf(&raw_msg);

		// If sanitized_msg is not NULL, we have rendered the message and can output it.

		if (sanitized_msg) {
			JsonObjectAppend(j, NewJsonString(HKEY("text"), sanitized_msg, NEWJSONSTRING_SMASHBUF));
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
