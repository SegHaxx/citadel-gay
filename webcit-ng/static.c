/*
 * Output static content
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
 * Called from perform_request() to handle the /ctdl/s/ prefix -- always static content.
 */
void output_static(struct http_transaction *h)
{
	char filename[PATH_MAX];
	struct stat statbuf;

	snprintf(filename, sizeof filename, "static/%s", &h->uri[8]);

	if (strstr(filename, "../")) {	// 100% guaranteed attacker.
		do_404(h);	// Die in a car fire.
		return;
	}

	FILE *fp = fopen(filename, "r");	// Try to open the requested file.
	if (fp == NULL) {
		do_404(h);
		return;
	}

	fstat(fileno(fp), &statbuf);
	h->response_body_length = statbuf.st_size;
	h->response_body = malloc(h->response_body_length);
	if (h->response_body != NULL) {
		fread(h->response_body, h->response_body_length, 1, fp);
	} else {
		h->response_body_length = 0;
	}
	fclose(fp);		// Content is now in memory.

	h->response_code = 200;
	h->response_string = strdup("OK");
	add_response_header(h, strdup("Content-type"), strdup(GuessMimeByFilename(filename, strlen(filename))));

	char *datestring = http_datestring(statbuf.st_mtime);
	if (datestring) {
		add_response_header(h, strdup("Last-Modified"), datestring);
	}
}
