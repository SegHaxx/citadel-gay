// This module sits directly above the HTTP layer.  By the time we get here,
// an HTTP request has been fully received and parsed.  Control is passed up
// to this layer to actually perform the request.  We then fill in the response
// and pass control back down to the HTTP layer to output the response back to
// the client.
//
// Copyright (c) 1996-2023 by the citadel.org team
//
// This program is open source software.  Use, duplication, or
// disclosure are subject to the GNU General Public License v3.

#include "webcit.h"


// Not found!  Wowzers.
void do_404(struct http_transaction *h) {
	h->response_code = 404;
	h->response_string = strdup("NOT FOUND");
	add_response_header(h, strdup("Content-type"), strdup("text/plain"));
	h->response_body = strdup("404 NOT FOUND\r\n");
	h->response_body_length = strlen(h->response_body);
}


// Precondition failed (such as if-match)
void do_412(struct http_transaction *h) {
	h->response_code = 412;
	h->response_string = strdup("PRECONDITION FAILED");
}


// Succeed with no output
void do_204(struct http_transaction *h) {
	h->response_code = 204;
	h->response_string = strdup("No content");
}


// We throw an HTTP error "502 bad gateway" when we need to connect to Citadel, but can't.
void do_502(struct http_transaction *h) {
	h->response_code = 502;
	h->response_string = strdup("bad gateway");
	add_response_header(h, strdup("Content-type"), strdup("text/plain"));
	h->response_body = strdup(_("This program was unable to connect or stay connected to the Citadel server.  Please report this problem to your system administrator."));
	h->response_body_length = strlen(h->response_body);
}


// Tell the client to authenticate using HTTP-AUTH (RFC 2617)
void request_http_authenticate(struct http_transaction *h) {
	h->response_code = 401;
	h->response_string = strdup("Unauthorized");
	add_response_header(h, strdup("WWW-Authenticate"), strdup("Basic realm=\"Citadel Server\""));
}


// Easy and fun utility function to throw a redirect.
void http_redirect(struct http_transaction *h, char *to_where) {
	syslog(LOG_DEBUG, "Redirecting to: %s", to_where);
	h->response_code = 302;
	h->response_string = strdup("Moved Temporarily");
	add_response_header(h, strdup("Location"), strdup(to_where));
	add_response_header(h, strdup("Content-type"), strdup("text/plain"));
	h->response_body = strdup(to_where);
	h->response_body_length = strlen(h->response_body);
}


// perform_request() is the entry point for *every* HTTP transaction.
void perform_request(struct http_transaction *h) {
	struct ctdlsession *c;

	// Determine which code path to take based on the beginning of the URL.
	// This is implemented as a series of strncasecmp() calls rather than a
	// lookup table in order to make the code more readable.

	if (IsEmptyStr(h->url)) {	// Sanity check
		do_404(h);
		return;
	}

	// Right about here is where we should try to handle anything that doesn't start
	// with the /ctdl/ prefix.
	// Root (/) ...

	if ((!strcmp(h->url, "/")) && (!strcasecmp(h->method, "GET"))) {
		http_redirect(h, "/ctdl/s/index.html");
		return;
	}

	// Legacy URL patterns (/readnew?gotoroom=xxx&start_reading_at=yyy) ...
	// Direct room name (/my%20blog) ...

	// HTTP-01 challenge [RFC5785 section 3, RFC8555 section 9.2]
	if (!strncasecmp(h->url, HKEY("/.well-known"))) {	// Static content
		output_static(h);
		return;
	}

	if (!strcasecmp(h->url, "/favicon.ico")) {
		output_static(h);
		return;
	}

	// Everything below this line is strictly REST URL patterns.

	if (strncasecmp(h->url, HKEY("/ctdl/"))) {		// Reject non-REST
		do_404(h);
		return;
	}

	if (!strncasecmp(h->url, HKEY("/ctdl/s/"))) {		// Static content
		output_static(h);
		return;
	}

	if (h->url[7] != '/') {
		do_404(h);
		return;
	}

	// Anything below this line:
	//      1. Is in the format of /ctdl/?/*
	//      2. Requires a connection to a Citadel server.

	c = connect_to_citadel(h);
	if (c == NULL) {
		do_502(h);
		return;
	}

	// WebDAV methods like OPTIONS and PROPFIND *require* a logged-in session,
	// even if the Citadel server allows anonymous access.
	if (IsEmptyStr(c->auth)) {
		if (	   (!strcasecmp(h->method, "OPTIONS"))
			|| (!strcasecmp(h->method, "PROPFIND"))
			|| (!strcasecmp(h->method, "REPORT"))
			|| (!strcasecmp(h->method, "DELETE"))
			|| (!strcasecmp(h->method, "MOVE"))
			|| (!strcasecmp(h->method, "COPY"))
		) {
			request_http_authenticate(h);
			disconnect_from_citadel(c);
			return;
		}
	}

	// Break down the URL by path and send the request to the appropriate part of the program.
	switch (h->url[6]) {
	case 'a':		// /ctdl/a/ == RESTful path to admin functions
		ctdl_a(h, c);
		break;
	case 'c':		// /ctdl/c/ == misc Citadel server commands
		ctdl_c(h, c);
		break;
	case 'f':		// /ctdl/f/ == RESTful path to floors
		ctdl_f(h, c);
		break;
	case 'r':		// /ctdl/r/ == RESTful path to rooms
		ctdl_r(h, c);
		break;
	case 'u':		// /ctdl/u/ == RESTful path to users
		ctdl_u(h, c);
		break;
	default:
		do_404(h);
	}

	// Are we in an authenticated session?  If so, set a cookie so we stay logged in.
	if (!IsEmptyStr(c->auth)) {
		char koekje[AUTH_MAX];
		char *exp = http_datestring(time(NULL) + (86400 * 30));
		snprintf(koekje, AUTH_MAX, "wcauth=%s; path=/ctdl/; Expires=%s", c->auth, exp);	// warn
		free(exp);
		add_response_header(h, strdup("Set-Cookie"), strdup(koekje));
	}

	// Durlng development we are foiling the browser cache completely.  In production we'll be more selective.
	add_response_header(h, strdup("Cache-Control"), strdup("no-store, must-revalidate"));
	add_response_header(h, strdup("Pragma"), strdup("no-cache"));
	add_response_header(h, strdup("Expires"), strdup("0"));

	// Unbind from our Citadel server connection.
	disconnect_from_citadel(c);
}
