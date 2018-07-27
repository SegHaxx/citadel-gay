/*
 * Admin functions
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
 * /ctdl/a/login is called when a user is trying to log in
 */
void try_login(struct http_transaction *h, struct ctdlsession *c)
{
	char buf[1024];
	char auth[AUTH_MAX];
	char username[256];
	char password[256];
	int login_success = 0;

	extract_token(username, h->request_body, 0, '|', sizeof username);
	extract_token(password, h->request_body, 1, '|', sizeof password);

	snprintf(buf, sizeof buf, "%s:%s", username, password);
	CtdlEncodeBase64(auth, buf, strlen(buf), 0);

	syslog(LOG_DEBUG, "try_login(username='%s',password=(%d bytes))", username, (int) strlen(password));

	ctdl_printf(c, "LOUT");			// log out, in case we were logged in
	ctdl_readline(c, buf, sizeof(buf));	// ignore the result
	memset(c->auth, 0, AUTH_MAX);		// if this connection had auth, it doesn't now.
	memset(c->whoami, 0, 64);		// if this connection had auth, it doesn't now.

	login_success = login_to_citadel(c, auth, buf);	// Now try logging in to Citadel

	h->response_code = 200;			// 'buf' will contain the relevant response
	h->response_string = strdup("OK");
	add_response_header(h, strdup("Content-type"), strdup("text/plain"));
	h->response_body = strdup(buf);
	h->response_body_length = strlen(h->response_body);
}


/*
 * /ctdl/a/logout is called when a user is trying to log out.   Don't use this as an ajax.
 */
void logout(struct http_transaction *h, struct ctdlsession *c)
{
	char buf[1024];
	char auth[AUTH_MAX];
	char username[256];
	char password[256];
	int login_success = 0;

	ctdl_printf(c, "LOUT");	// log out
	ctdl_readline(c, buf, sizeof(buf));	// ignore the result
	strcpy(c->auth, "x");
	//memset(c->auth, 0, AUTH_MAX);		// if this connection had auth, it doesn't now.
	memset(c->whoami, 0, 64);		// if this connection had auth, it doesn't now.

	http_redirect(h, "/ctdl/s/index.html");	// go back where we started :)
}


/*
 * /ctdl/a/whoami returns the name of the currently logged in user, or an empty string if not logged in
 */
void whoami(struct http_transaction *h, struct ctdlsession *c)
{
	h->response_code = 200;
	h->response_string = strdup("OK");
	add_response_header(h, strdup("Content-type"), strdup("text/plain"));
	h->response_body = strdup(c->whoami);
	h->response_body_length = strlen(h->response_body);
}


/*
 * Dispatcher for paths starting with /ctdl/a/
 */
void ctdl_a(struct http_transaction *h, struct ctdlsession *c)
{
	if (!strcasecmp(h->uri, "/ctdl/a/login")) {	// log in
		try_login(h, c);
		return;
	}

	if (!strcasecmp(h->uri, "/ctdl/a/logout")) {	// log out
		logout(h, c);
		return;
	}

	if (!strcasecmp(h->uri, "/ctdl/a/whoami")) {	// return display name of user
		whoami(h, c);
		return;
	}

	do_404(h);					// unknown
}
