// Copyright (c) 1996-2022 by the citadel.org team
//
// This program is open source software.  Use, duplication, or disclosure
// are subject to the terms of the GNU General Public License v3.


#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <signal.h>
#include <sys/types.h>
#include <ctype.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>
#include <limits.h>
#include <iconv.h>
#include "libcitadel.h"

#if TIME_WITH_SYS_TIME
# include <sys/time.h>
# include <time.h>
#endif


// This is the non-define version in case it is needed for debugging
#if 0
inline void FindNextEnd (char *bptr, char *end) {
	/* Find the next ?Q? */
	end = strchr(bptr + 2, '?');
	if (end == NULL) return NULL;
	if (((*(end + 1) == 'B') || (*(end + 1) == 'Q')) && 
	    (*(end + 2) == '?')) {
		/* skip on to the end of the cluster, the next ?= */
		end = strstr(end + 3, "?=");
	}
	else
		/* sort of half valid encoding, try to find an end. */
		end = strstr(bptr, "?=");
}
#endif

#define FindNextEnd(bptr, end) { \
	end = strchr(bptr + 2, '?'); \
	if (end != NULL) { \
		if (((*(end + 1) == 'B') || (*(end + 1) == 'Q')) && (*(end + 2) == '?')) { \
			end = strstr(end + 3, "?="); \
		} else end = strstr(bptr, "?="); \
	} \
}

// Handle subjects with RFC2047 encoding such as:
// =?koi8-r?B?78bP0s3Mxc7JxSDXz9rE1dvO2c3JINvB0sHNySDP?=
void utf8ify_rfc822_string(char *buf) {
	char *start, *end, *next, *nextend, *ptr;
	char newbuf[1024];
	char charset[128];
	char encoding[16];
	char istr[1024];
	iconv_t ic = (iconv_t)(-1) ;
	char *ibuf;			// Buffer of characters to be converted
	char *obuf;			// Buffer for converted characters
	size_t ibuflen;			// Length of input buffer
	size_t obuflen;			// Length of output buffer
	char *isav;			// Saved pointer to input buffer
	char *osav;			// Saved pointer to output buffer
	int passes = 0;
	int i, len, delta;
	int illegal_non_rfc2047_encoding = 0;

	// Sometimes, badly formed messages contain strings which were simply
	// written out directly in some foreign character set instead of
	// using RFC2047 encoding.  This is illegal but we will attempt to
	// handle it anyway by converting from a user-specified default
	// charset to UTF-8 if we see any nonprintable characters.
	len = strlen(buf);
	for (i=0; i<len; ++i) {
		if ((buf[i] < 32) || (buf[i] > 126)) {
			illegal_non_rfc2047_encoding = 1;
			i = len;	// take a shortcut, it won't be more than one.
		}
	}
	if (illegal_non_rfc2047_encoding) {
		const char *default_header_charset = "iso-8859-1";
		if ( (strcasecmp(default_header_charset, "UTF-8")) && (strcasecmp(default_header_charset, "us-ascii")) ) {
			ctdl_iconv_open("UTF-8", default_header_charset, &ic);
			if (ic != (iconv_t)(-1) ) {
				ibuf = malloc(1024);
				isav = ibuf;
				safestrncpy(ibuf, buf, 1024);
				ibuflen = strlen(ibuf);
				obuflen = 1024;
				obuf = (char *) malloc(obuflen);
				osav = obuf;
				iconv(ic, &ibuf, &ibuflen, &obuf, &obuflen);
				osav[1024-obuflen] = 0;
				strcpy(buf, osav);
				free(osav);
				iconv_close(ic);
				free(isav);
			}
		}
	}

	// pre evaluate the first pair
	nextend = end = NULL;
	len = strlen(buf);
	start = strstr(buf, "=?");
	if (start != NULL) 
		FindNextEnd (start, end);

	while ((start != NULL) && (end != NULL)) {
		next = strstr(end, "=?");
		if (next != NULL)
			FindNextEnd(next, nextend);
		if (nextend == NULL)
			next = NULL;

		// did we find two partitions
		if ((next != NULL) && ((next - end) > 2)) {
			ptr = end + 2;
			while ((ptr < next) && 
			       (isspace(*ptr) ||
				(*ptr == '\r') ||
				(*ptr == '\n') || 
				(*ptr == '\t')))
				ptr ++;
			// did we find a gab just filled with blanks?
			if (ptr == next) {
				memmove(end + 2, next, len - (next - start));

				// now terminate the gab at the end
				delta = (next - end) - 2;
				len -= delta;
				buf[len] = '\0';

				// move next to its new location.
				next -= delta;
				nextend -= delta;
			}
		}
		// our next-pair is our new first pair now.
		start = next;
		end = nextend;
	}

	// Now we handle foreign character sets properly encoded in RFC2047 format.
	start = strstr(buf, "=?");
	FindNextEnd((start != NULL)? start : buf, end);
	while (start != NULL && end != NULL && end > start) {
		extract_token(charset, start, 1, '?', sizeof charset);
		extract_token(encoding, start, 2, '?', sizeof encoding);
		extract_token(istr, start, 3, '?', sizeof istr);

		ibuf = malloc(1024);
		isav = ibuf;
		if (!strcasecmp(encoding, "B")) {	// base64
			ibuflen = CtdlDecodeBase64(ibuf, istr, strlen(istr));
		}
		else if (!strcasecmp(encoding, "Q")) {	// quoted-printable
			size_t len;
			unsigned long pos;
			
			len = strlen(istr);
			pos = 0;
			while (pos < len) {
				if (istr[pos] == '_') istr[pos] = ' ';
				pos++;
			}
			ibuflen = CtdlDecodeQuotedPrintable(ibuf, istr, len);
		}
		else {
			strcpy(ibuf, istr);		// unknown encoding
			ibuflen = strlen(istr);
		}

		ctdl_iconv_open("UTF-8", charset, &ic);
		if (ic != (iconv_t)(-1) ) {
			obuflen = 1024;
			obuf = (char *) malloc(obuflen);
			osav = obuf;
			iconv(ic, &ibuf, &ibuflen, &obuf, &obuflen);
			osav[1024-obuflen] = 0;

			end = start;
			end++;
			strcpy(start, "");
			remove_token(end, 0, '?');
			remove_token(end, 0, '?');
			remove_token(end, 0, '?');
			remove_token(end, 0, '?');
			strcpy(end, &end[1]);

			snprintf(newbuf, sizeof newbuf, "%s%s%s", buf, osav, end);
			strcpy(buf, newbuf);
			free(osav);
			iconv_close(ic);
		}
		else {
			end = start;
			end++;
			strcpy(start, "");
			remove_token(end, 0, '?');
			remove_token(end, 0, '?');
			remove_token(end, 0, '?');
			remove_token(end, 0, '?');
			strcpy(end, &end[1]);

			snprintf(newbuf, sizeof newbuf, "%s(unreadable)%s", buf, end);
			strcpy(buf, newbuf);
		}

		free(isav);

		// Since spammers will go to all sorts of absurd lengths to get their
		// messages through, there are LOTS of corrupt headers out there.
		// So, prevent a really badly formed RFC2047 header from throwing
		// this function into an infinite loop.
		++passes;
		if (passes > 20) return;

		start = strstr(buf, "=?");
		FindNextEnd((start != NULL)? start : buf, end);
	}

}
