/*
 * Message base functions
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


/*
 * Given an encoded UID, translate that to an unencoded Citadel EUID and
 * then search for it in the current room.  Return a message number or -1
 * if not found.
 *
 */
long locate_message_by_uid(struct ctdlsession *c, char *uid)
{
	char buf[1024];

	ctdl_printf(c, "EUID %s", uid);
	ctdl_readline(c, buf, sizeof buf);
	if (buf[0] == '2') {
		return (atol(&buf[4]));

	}

	/* Ugly hack to handle Mozilla Thunderbird, try stripping ".ics" if present */
	if (!strcasecmp(&uid[strlen(uid) - 4], ".ics")) {
		safestrncpy(buf, uid, sizeof buf);
		buf[strlen(buf) - 4] = 0;
		ctdl_printf(c, "EUID %s", buf);
		ctdl_readline(c, buf, sizeof buf);
		if (buf[0] == '2') {
			return (atol(&buf[4]));

		}
	}

	return (-1);
}


/*
 * DAV delete an object in a room.
 */
void dav_delete_message(struct http_transaction *h, struct ctdlsession *c, long msgnum)
{
	ctdl_delete_msgs(c, &msgnum, 1);
	h->response_code = 204;
	h->response_string = strdup("no content");
}


/*
 * GET method directly on a message in a room
 */
void dav_get_message(struct http_transaction *h, struct ctdlsession *c, long msgnum)
{
	char buf[1024];
	int in_body = 0;
	int encoding = 0;
	StrBuf *Body = NULL;

	ctdl_printf(c, "MSG2 %ld", msgnum);
	ctdl_readline(c, buf, sizeof buf);
	if (buf[0] != '1') {
		do_404(h);
		return;
	}

	char *etag = malloc(20);
	if (etag != NULL) {
		sprintf(etag, "%ld", msgnum);
		add_response_header(h, strdup("ETag"), etag);	// http_transaction now owns this memory
	}

	while (ctdl_readline(c, buf, sizeof buf), strcmp(buf, "000")) {
		if (IsEmptyStr(buf) && (in_body == 0)) {
			in_body = 1;
			Body = NewStrBuf();
		} else if (in_body == 0) {
			char *k = buf;
			char *v = strchr(buf, ':');
			if (v) {
				*v = 0;
				++v;
				striplt(v);	// we now have a key (k) and a value (v)
				if ((!strcasecmp(k, "content-type"))	// fields which can be passed from RFC822 to HTTP as-is
				    || (!strcasecmp(k, "date"))
				    ) {
					add_response_header(h, strdup(k), strdup(v));
				} else if (!strcasecmp(k, "content-transfer-encoding")) {
					if (!strcasecmp(v, "base64")) {
						encoding = 'b';
					} else if (!strcasecmp(v, "quoted-printable")) {
						encoding = 'q';
					}
				}
			}
		} else if ((in_body == 1) && (Body != NULL)) {
			StrBufAppendPrintf(Body, "%s\n", buf);
		}
	}

	h->response_code = 200;
	h->response_string = strdup("OK");

	if (Body != NULL) {
		if (encoding == 'q') {
			h->response_body = malloc(StrLength(Body));
			if (h->response_body != NULL) {
				h->response_body_length =
				    CtdlDecodeQuotedPrintable(h->response_body, (char *) ChrPtr(Body), StrLength(Body));
			}
			FreeStrBuf(&Body);
		} else if (encoding == 'b') {
			h->response_body = malloc(StrLength(Body));
			if (h->response_body != NULL) {
				h->response_body_length = CtdlDecodeBase64(h->response_body, ChrPtr(Body), StrLength(Body));
			}
			FreeStrBuf(&Body);
		} else {
			h->response_body_length = StrLength(Body);
			h->response_body = SmashStrBuf(&Body);
		}
	}
}


/*
 * PUT a message into a room
 */
void dav_put_message(struct http_transaction *h, struct ctdlsession *c, char *euid, long old_msgnum)
{
	char buf[1024];
	char *content_type = NULL;
	int n;
	long new_msgnum;
	char new_euid[1024];
	char response_string[1024];

	if ((h->request_body == NULL) || (h->request_body_length < 1)) {
		do_404(h);	// Refuse to post a null message
		return;
	}

	ctdl_printf(c, "ENT0 1|||4|||1|");	// This protocol syntax will give us metadata back after upload
	ctdl_readline(c, buf, sizeof buf);
	if (buf[0] != '8') {
		h->response_code = 502;
		h->response_string = strdup("bad gateway");
		add_response_header(h, strdup("Content-type"), strdup("text/plain"));
		h->response_body = strdup(buf);
		h->response_body_length = strlen(h->response_body);
		return;
	}

	content_type = header_val(h, "Content-type");
	ctdl_printf(c, "Content-type: %s\r\n", (content_type ? content_type : "application/octet-stream"));
	ctdl_printf(c, "\r\n");
	ctdl_write(c, h->request_body, h->request_body_length);
	if (h->request_body[h->request_body_length] != '\n') {
		ctdl_printf(c, "\r\n");
	}
	ctdl_printf(c, "000");

	/*
	 * Now handle the response from the Citadel server.
	 */

	n = 0;
	new_msgnum = 0;
	strcpy(new_euid, "");
	strcpy(response_string, "");

	while (ctdl_readline(c, buf, sizeof buf), strcmp(buf, "000"))
		switch (n++) {
		case 0:
			new_msgnum = atol(buf);
			break;
		case 1:
			safestrncpy(response_string, buf, sizeof response_string);
			syslog(LOG_DEBUG, "new_msgnum=%ld (%s)\n", new_msgnum, buf);
			break;
		case 2:
			safestrncpy(new_euid, buf, sizeof new_euid);
			break;
		default:
			break;
		}

	/* Tell the client what happened. */

	/* Citadel failed in some way? */
	char *new_location = malloc(1024);
	if ((new_msgnum < 0L) || (new_location == NULL)) {
		h->response_code = 502;
		h->response_string = strdup("bad gateway");
		add_response_header(h, strdup("Content-type"), strdup("text/plain"));
		h->response_body = strdup(response_string);
		h->response_body_length = strlen(h->response_body);
		return;
	}

	char *etag = malloc(20);
	if (etag != NULL) {
		sprintf(etag, "%ld", new_msgnum);
		add_response_header(h, strdup("ETag"), etag);	// http_transaction now owns this memory
	}

	char esc_room[1024];
	char esc_euid[1024];
	urlesc(esc_room, sizeof esc_room, c->room);
	urlesc(esc_euid, sizeof esc_euid, new_euid);
	snprintf(new_location, 1024, "/ctdl/r/%s/%s", esc_room, esc_euid);
	add_response_header(h, strdup("Location"), new_location);	// http_transaction now owns this memory

	if (old_msgnum <= 0) {
		h->response_code = 201;	// We created this item for the first time.
		h->response_string = strdup("created");
	} else {
		h->response_code = 204;	// We modified an existing item.
		h->response_string = strdup("no content");

		/* The item we replaced has probably already been deleted by
		 * the Citadel server, but we'll do this anyway, just in case.
		 */
		ctdl_delete_msgs(c, &old_msgnum, 1);
	}

}


/*
 * Download a single component of a MIME-encoded message
 */
void download_mime_component(struct http_transaction *h, struct ctdlsession *c, long msgnum, char *partnum)
{
	char buf[1024];
	char content_type[1024];

	ctdl_printf(c, "DLAT %ld|%s", msgnum, partnum);
	ctdl_readline(c, buf, sizeof buf);
	if (buf[0] != '6') {
		do_404(h);	// too bad, so sad, go away
	}
	// Server response is going to be: 6XX length|-1|filename|content-type|charset
	h->response_body_length = extract_int(&buf[4], 0);
	extract_token(content_type, buf, 3, '|', sizeof content_type);

	h->response_body = malloc(h->response_body_length + 1);
	int bytes = 0;
	int thisblock;
	do {
		thisblock = read(c->sock, &h->response_body[bytes], (h->response_body_length - bytes));
		bytes += thisblock;
		syslog(LOG_DEBUG, "Bytes read: %d of %d", (int) bytes, (int) h->response_body_length);
	} while ((bytes < h->response_body_length) && (thisblock >= 0));
	h->response_body[h->response_body_length] = 0;	// null terminate it just for good measure
	syslog(LOG_DEBUG, "content type: %s", content_type);

	add_response_header(h, strdup("Content-type"), strdup(content_type));
	h->response_code = 200;
	h->response_string = strdup("OK");
}
