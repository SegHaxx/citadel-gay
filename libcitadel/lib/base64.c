// This is a simple implementation of a Base64 encoder/decoder as specified
// in RFC 2045 section 6.8.   In the past, someone tried to make this "elegant"
// and in the process they made it broken when certain conditions exist.  If
// you are reading this and it isn't broken, don't try to improve it.  It works
// and I don't want to fix it again.  I don't care how many nanoseconds you think
// you can shave off the execution time.  Don't fucking touch it.
//
// Copyright (c) 1987-2022 by the citadel.org team
//
// This program is open source software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA

#define _GNU_SOURCE
#include "sysdep.h"
#include <ctype.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <sys/select.h>
#include <fcntl.h>
#include <sys/types.h>
#include "libcitadel.h"


// Encode raw binary data into base64
// dest		Destination buffer supplied by the caller
// source	Source binary data to be encoded
// sourcelen	Stop after reading this many bytes
// linebreaks	If nonzero, insert CRLF after every 76 bytes
// return value	The length of the encoded data, not including the null terminator
size_t CtdlEncodeBase64(char *dest, const char *source, size_t sourcelen, int linebreaks) {
	static char *alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	size_t bytes_processed = 0;
	char *ptr = (char *)source;
	char encodebuf[3];
	size_t bytes_output = 0;

	while (bytes_processed < sourcelen) {
		size_t remain = sourcelen - bytes_processed;
		if (remain >= 3) {
			memcpy(encodebuf, ptr, 3);
			ptr += 3;
			bytes_processed += 3;
			sprintf(&dest[bytes_output], "%c%c%c%c",
				alphabet[((encodebuf[0] & 0xFC) >> 2)],
				alphabet[( ((encodebuf[0] & 0x03) << 4) | ((encodebuf[1] & 0xF0) >> 4) )],
				alphabet[( ((encodebuf[1] & 0x0F) << 2) | ((encodebuf[2] & 0xC0) >> 6) )],
				alphabet[(encodebuf[2] & 0x3F)] 
			);
			bytes_output += 4;
				
		}
		else if (remain == 2) {
			memcpy(encodebuf, ptr, 2);
			encodebuf[2] = 0;
			ptr += 2;
			bytes_processed += 2;
			sprintf(&dest[bytes_output], "%c%c%c=",
				alphabet[((encodebuf[0] & 0xFC) >> 2)],
				alphabet[( ((encodebuf[0] & 0x03) << 4) | ((encodebuf[1] & 0xF0) >> 4) )],
				alphabet[( ((encodebuf[1] & 0x0F) << 2) )]
			);
			bytes_output += 4;
		}
		else if (remain == 1) {
			memcpy(encodebuf, ptr, 1);
			encodebuf[1] = 0;
			encodebuf[2] = 0;
			ptr += 1;
			bytes_processed += 1;
			sprintf(&dest[bytes_output], "%c%c==",
				alphabet[((encodebuf[0] & 0xFC) >> 2)],
				alphabet[( ((encodebuf[0] & 0x03) << 4) )]
			);
			bytes_output += 4;
		}
		if ( ((bytes_processed % 57) == 0) || (bytes_processed >= sourcelen) ) {
			if (linebreaks) {
				sprintf(&dest[bytes_output], "\r\n");
				bytes_output += 2;
			}
		}

	}

	return bytes_output;
}


// convert base64 alphabet characters to 6-bit decimal values
char unalphabet(char ch) {
	if (isupper(ch)) {
		return(ch - 'A');
	}
	else if (islower(ch)) {
		return(ch - 'a' + 26);
	}
	else if (isdigit(ch)) {
		return(ch - '0' + 52);
	}
	else if (ch == '+') {
		return(62);
	}
	else if (ch == '/') {
		return(63);
	}
	else if (ch == '=') {			// this character marks the end of an encoded block
		return(64);
	}
	else {					// anything else is an invalid character
		return(65);
	}
}


// Decode base64 back to binary
// dest		Destination buffer
// source	Source base64-encoded buffer
// source_len	Stop after parsing this many bytes
// return value	Decoded length
size_t CtdlDecodeBase64(char *dest, const char *source, size_t source_len) {
	size_t bytes_read = 0;
	size_t bytes_decoded = 0;
	int decodepos = 0;
	char decodebuf[4];

	while (bytes_read < source_len) {

		char ch = unalphabet(source[bytes_read++]);
		if (ch < 65) {
			decodebuf[decodepos++] = ch;
		}
		if (decodepos == 4) {			// we have a quartet of encoded bytes now
			decodepos = 0;
			int increment = 0;		// number of decoded bytes found in this encoded quartet

			ch = decodebuf[0];
			if (ch != 64) {
				dest[bytes_decoded] = ch << 2;
			}

			ch = decodebuf[1];
			if (ch != 64) {
				dest[bytes_decoded] |= ((ch & 0x30) >> 4);
				dest[bytes_decoded+1] = ((ch & 0x0F) << 4);
				increment = 1;
			}

			ch = decodebuf[2];
			if (ch != 64) {
				dest[bytes_decoded+1] |= ((ch & 0x3C) >> 2);
				dest[bytes_decoded+2] = ((ch & 0x03) << 6);
				increment = 2;
			}

			ch = decodebuf[3];
			if (ch != 64) {
				dest[bytes_decoded+2] |= (ch & 0x3F);
				increment = 3;
			}

			bytes_decoded += increment;
		}
	}

	return bytes_decoded;
}
