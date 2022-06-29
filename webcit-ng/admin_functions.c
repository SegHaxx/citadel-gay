// Admin functions
//
// Copyright (c) 1996-2022 by the citadel.org team
//
// This program is open source software.  Use, duplication, or
// disclosure are subject to the GNU General Public License v3.

#include "webcit.h"


// /ctdl/a/login is called when a user is trying to log in
void try_login(struct http_transaction *h, struct ctdlsession *c) {
	char buf[1024];
	char auth[AUTH_MAX];
	char username[256];
	char password[256];
	int login_success = 0;

	extract_token(username, h->request_body, 0, '|', sizeof username);
	extract_token(password, h->request_body, 1, '|', sizeof password);

	snprintf(buf, sizeof buf, "%s:%s", username, password);
	CtdlEncodeBase64(auth, buf, strlen(buf), BASE64_NO_LINEBREAKS);

	syslog(LOG_DEBUG, "try_login(username='%s',password=(%d bytes))", username, (int) strlen(password));

	ctdl_printf(c, "LOUT");							// log out, in case we were logged in
	ctdl_readline(c, buf, sizeof(buf));					// ignore the result
	memset(c->auth, 0, AUTH_MAX);						// if this connection had auth, it doesn't now.
	memset(c->whoami, 0, 64);						// if this connection had auth, it doesn't now.
	login_success = login_to_citadel(c, auth, buf);				// Now try logging in to Citadel

	JsonValue *j = NewJsonObject(HKEY("login"));				// Compose a JSON object with the results
	if (buf[0] == '2') {
		JsonObjectAppend(j, NewJsonBool(HKEY("result"), 1));
		JsonObjectAppend(j, NewJsonPlainString(HKEY("message"), "logged in", -1));
		extract_token(username, &buf[4], 0, '|', sizeof username);	// This will have the proper capitalization etc.
		JsonObjectAppend(j, NewJsonPlainString(HKEY("fullname"), username, -1));
		JsonObjectAppend(j, NewJsonNumber(HKEY("axlevel"), extract_int(&buf[4], 1) ));
		JsonObjectAppend(j, NewJsonNumber(HKEY("timescalled"), extract_long(&buf[4], 2) ));
		JsonObjectAppend(j, NewJsonNumber(HKEY("posted"), extract_long(&buf[4], 3) ));
		JsonObjectAppend(j, NewJsonNumber(HKEY("usernum"), extract_long(&buf[4], 5) ));
		JsonObjectAppend(j, NewJsonNumber(HKEY("previous_login"), extract_long(&buf[4], 6) ));
	}
	else {
		JsonObjectAppend(j, NewJsonBool(HKEY("result"), 0));
		JsonObjectAppend(j, NewJsonPlainString(HKEY("message"), &buf[4], -1));
	}
	StrBuf *sj = NewStrBuf();
	SerializeJson(sj, j, 1);						// '1' == free the source object

	add_response_header(h, strdup("Content-type"), strdup("application/json"));
	h->response_code = 200;
	h->response_string = strdup("OK");
	h->response_body_length = StrLength(sj);
	h->response_body = SmashStrBuf(&sj);
}


// /ctdl/a/logout is called when a user is trying to log out.   Don't use this as an ajax.
void logout(struct http_transaction *h, struct ctdlsession *c) {
	char buf[1024];
	char auth[AUTH_MAX];
	char username[256];
	char password[256];
	int login_success = 0;

	ctdl_printf(c, "LOUT");	// log out
	ctdl_readline(c, buf, sizeof(buf));	// ignore the result
	strcpy(c->auth, "x");
	memset(c->whoami, 0, 64);		// if this connection had auth, it doesn't now.

	http_redirect(h, "/ctdl/s/index.html");	// go back where we started :)
}


// /ctdl/a/whoami returns the name of the currently logged in user, or an empty string if not logged in
void whoami(struct http_transaction *h, struct ctdlsession *c) {
	h->response_code = 200;
	h->response_string = strdup("OK");
	add_response_header(h, strdup("Content-type"), strdup("text/plain"));
	h->response_body = strdup(c->whoami);
	h->response_body_length = strlen(h->response_body);
}


// Dispatcher for paths starting with /ctdl/a/
void ctdl_a(struct http_transaction *h, struct ctdlsession *c) {
	if (!strcasecmp(h->url, "/ctdl/a/login")) {	// log in
		try_login(h, c);
		return;
	}

	if (!strcasecmp(h->url, "/ctdl/a/logout")) {	// log out
		logout(h, c);
		return;
	}

	if (!strcasecmp(h->url, "/ctdl/a/whoami")) {	// return display name of user
		whoami(h, c);
		return;
	}

	do_404(h);					// unknown
}
