// Output static content
//
// Copyright (c) 1996-2022 by the citadel.org team
//
// This program is open source software.  Use, duplication, or
// disclosure are subject to the GNU General Public License v3.

#include "webcit.h"


// Called from perform_request() to handle static content.
void output_static(struct http_transaction *h) {
	char filename[PATH_MAX];
	struct stat statbuf;

	syslog(LOG_DEBUG, "static: %s", h->url);

	if (!strncasecmp(h->url, "/ctdl/s/", 8)) {
		snprintf(filename, sizeof filename, "static/%s", &h->url[8]);
	}
	else if (!strncasecmp(h->url, "/.well-known/", 13)) {
		snprintf(filename, sizeof filename, "static/.well-known/%s", &h->url[13]);
	}
	else if (!strcasecmp(h->url, "/favicon.ico")) {
		snprintf(filename, sizeof filename, "static/images/favicon.ico");
	}
	else {
		do_404(h);
		return;
	}

	if (strstr(filename, "../")) {		// 100% guaranteed attacker.
		do_404(h);			// Die in a car fire.
		return;
	}

	FILE *fp = fopen(filename, "r");	// Try to open the requested file.
	if (fp == NULL) {
		syslog(LOG_DEBUG, "%s: %s", filename, strerror(errno));
		do_404(h);
		return;
	}

	fstat(fileno(fp), &statbuf);
	h->response_body_length = statbuf.st_size;
	h->response_body = malloc(h->response_body_length);
	if (h->response_body != NULL) {
		fread(h->response_body, h->response_body_length, 1, fp);
	}
	else {
		h->response_body_length = 0;
	}
	fclose(fp);				// Content is now in memory.

	h->response_code = 200;
	h->response_string = strdup("OK");
	add_response_header(h, strdup("Content-type"), strdup(GuessMimeByFilename(filename, strlen(filename))));

	char *datestring = http_datestring(statbuf.st_mtime);
	if (datestring) {
		add_response_header(h, strdup("Last-Modified"), datestring);
	}
}
