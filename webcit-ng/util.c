/*
 * Utility functions
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
 * remove escaped strings from i.e. the url string (like %20 for blanks)
 */
int unescape_input(char *buf)
{
	unsigned int a, b;
	char hex[3];
	long buflen;
	long len;

	buflen = strlen(buf);

	while ((buflen > 0) && (isspace(buf[buflen - 1]))) {
		buf[buflen - 1] = 0;
		buflen--;
	}

	a = 0;
	while (a < buflen) {
		if (buf[a] == '+')
			buf[a] = ' ';
		if (buf[a] == '%') {
			/* don't let % chars through, rather truncate the input. */
			if (a + 2 > buflen) {
				buf[a] = '\0';
				buflen = a;
			} else {
				hex[0] = buf[a + 1];
				hex[1] = buf[a + 2];
				hex[2] = 0;
				b = 0;
				b = decode_hex(hex);
				buf[a] = (char) b;
				len = buflen - a - 2;
				if (len > 0)
					memmove(&buf[a + 1], &buf[a + 3], len);

				buflen -= 2;
			}
		}
		a++;
	}
	return a;
}


/*
 * Supplied with a unix timestamp, generate a textual time/date stamp.
 * Caller owns the returned memory.
 */
char *http_datestring(time_t xtime)
{

	/* HTTP Months - do not translate - these are not for human consumption */
	static char *httpdate_months[] = {
		"Jan", "Feb", "Mar", "Apr", "May", "Jun",
		"Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
	};

	/* HTTP Weekdays - do not translate - these are not for human consumption */
	static char *httpdate_weekdays[] = {
		"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
	};

	struct tm t;
	long offset;
	char offsign;
	int n = 40;
	char *buf = malloc(n);
	if (!buf)
		return (NULL);

	localtime_r(&xtime, &t);

	/* Convert "seconds west of GMT" to "hours/minutes offset" */
	offset = t.tm_gmtoff;
	if (offset > 0) {
		offsign = '+';
	} else {
		offset = 0L - offset;
		offsign = '-';
	}
	offset = ((offset / 3600) * 100) + (offset % 60);

	snprintf(buf, n, "%s, %02d %s %04d %02d:%02d:%02d %c%04ld",
		 httpdate_weekdays[t.tm_wday],
		 t.tm_mday, httpdate_months[t.tm_mon], t.tm_year + 1900, t.tm_hour, t.tm_min, t.tm_sec, offsign, offset);
	return (buf);
}
