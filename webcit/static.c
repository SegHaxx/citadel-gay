// The functions in this file handle static pages and objects -- a basic web server.
//
// Copyright (c) 1996-2021 by the citadel.org team
//
// This program is open source software.  You can redistribute it and/or
// modify it under the terms of the GNU General Public License, version 3.

#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#include <unistd.h>
#include <stdio.h>
#include <stdarg.h>
#include <stddef.h>
#include "webcit.h"
#include "webserver.h"

unsigned char OnePixelGif[37] = {
		0x47, 0x49, 0x46, 0x38, 0x37, 0x61, 0x01, 0x00,
		0x01, 0x00, 0x80, 0x00, 0x00, 0xff, 0xff, 0xff,
		0xff, 0xff, 0xff, 0x2c, 0x00, 0x00, 0x00, 0x00,
		0x01, 0x00, 0x01, 0x00, 0x00, 0x02, 0x02, 0x44,
		0x01, 0x00, 0x3b 
};


void output_error_pic(const char *ErrMsg1, const char *ErrMsg2) {
	hprintf("HTTP/1.1 200 %s\r\n", ErrMsg1);
	hprintf("Content-Type: image/gif\r\n");
	hprintf("x-webcit-errormessage: %s\r\n", ErrMsg2);
	begin_burst();
	StrBufPlain(WC->WBuf, (const char *)OnePixelGif, sizeof(OnePixelGif));
	end_burst();
}

/*
 * dump out static pages from disk
 */
void output_static(char *prefix) {
	int fd;
	struct stat statbuf;
	off_t bytes;
	const char *content_type;
	int len;
	const char *Err;
	char what[SIZ];
	snprintf(what, sizeof what, "./%s/%s", prefix, (char *)ChrPtr(WC->Hdr->HR.ReqLine));

	syslog(LOG_DEBUG, "output_static(%s)", what);
	len = strlen (what);
	content_type = GuessMimeByFilename(what, len);
	fd = open(what, O_RDONLY);
	if (fd <= 0) {
		syslog(LOG_INFO, "output_static('%s') [%s] : %s", what, ChrPtr(WC->Hdr->this_page), strerror(errno));
		if (strstr(content_type, "image/") != NULL) {
			output_error_pic("the file you requsted is gone.", strerror(errno));
		}
		else {
			hprintf("HTTP/1.1 404 %s\r\n", strerror(errno));
			hprintf("Content-Type: text/plain\r\n");
			begin_burst();
			wc_printf("Cannot open %s: %s\r\n", what, strerror(errno));
			end_burst();
		}
	}
	else {
		if (fstat(fd, &statbuf) == -1) {
			syslog(LOG_INFO, "output_static('%s') : %s", what, strerror(errno));
			if (strstr(content_type, "image/") != NULL) {
				output_error_pic("Stat failed!", strerror(errno));
			}
			else {
				hprintf("HTTP/1.1 404 %s\r\n", strerror(errno));
				hprintf("Content-Type: text/plain\r\n");
				begin_burst();
				wc_printf("Cannot fstat %s: %s\n", what, strerror(errno));
				end_burst();
			}
			if (fd > 0) close(fd);
			return;
		}

		bytes = statbuf.st_size;

		if (StrBufReadBLOB(WC->WBuf, &fd, 1, bytes, &Err) < 0) {
			if (fd > 0) close(fd);
			syslog(LOG_INFO, "output_static('%s')  -- FREAD FAILED (%s) --\n", what, strerror(errno));
				hprintf("HTTP/1.1 500 internal server error \r\n");
				hprintf("Content-Type: text/plain\r\n");
				end_burst();
				return;
		}

		close(fd);
		http_transmit_thing(content_type, 2);
	}
	if (yesbstr("force_close_session")) {
		end_webcit_session();
	}
}


/*
 * robots.txt
 */
void robots_txt(void) {
	output_headers(0, 0, 0, 0, 0, 0);

	hprintf("Content-type: text/plain\r\n"
		"Server: %s\r\n"
		"Connection: close\r\n",
		PACKAGE_STRING);
	begin_burst();

	wc_printf("User-agent: *\r\n"
		"Disallow: /printmsg\r\n"
		"Disallow: /msgheaders\r\n"
		"Disallow: /groupdav\r\n"
		"Disallow: /do_template\r\n"
		"Disallow: /static\r\n"
		"Disallow: /display_page\r\n"
		"Disallow: /readnew\r\n"
		"Disallow: /display_enter\r\n"
		"Disallow: /skip\r\n"
		"Disallow: /ungoto\r\n"
		"Sitemap: %s/sitemap.xml\r\n"
		"\r\n"
		,
		ChrPtr(site_prefix)
	);

	wDumpContent(0);
}


// These are the various prefixes we can use to fetch static pages.
void output_static_root(void)		{	output_static(".");		}
void output_static_static(void)		{	output_static("static");	}
void output_static_tinymce(void)	{	output_static("tiny_mce");	}
void output_static_acme(void)		{	output_static(".well-known");	}


void 
ServerStartModule_STATIC
(void)
{
}


void 
ServerShutdownModule_STATIC
(void)
{
}

void 
InitModule_STATIC
(void)
{
	WebcitAddUrlHandler(HKEY("robots.txt"), "", 0, robots_txt, ANONYMOUS|COOKIEUNNEEDED|ISSTATIC|LOGCHATTY);
	WebcitAddUrlHandler(HKEY("favicon.ico"), "", 0, output_static_root, ANONYMOUS|COOKIEUNNEEDED|ISSTATIC|LOGCHATTY);
	WebcitAddUrlHandler(HKEY("static"), "", 0, output_static_static, ANONYMOUS|COOKIEUNNEEDED|ISSTATIC|LOGCHATTY);
	WebcitAddUrlHandler(HKEY("tinymce"), "", 0, output_static_tinymce, ANONYMOUS|COOKIEUNNEEDED|ISSTATIC|LOGCHATTY);
	WebcitAddUrlHandler(HKEY("tiny_mce"), "", 0, output_static_tinymce, ANONYMOUS|COOKIEUNNEEDED|ISSTATIC|LOGCHATTY);
	WebcitAddUrlHandler(HKEY(".well-known"), "", 0, output_static_acme, ANONYMOUS|COOKIEUNNEEDED|ISSTATIC|LOGCHATTY);
}
