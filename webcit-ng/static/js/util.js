//
// Copyright (c) 2016-2021 by the citadel.org team
//
// This program is open source software.  It runs great on the
// Linux operating system (and probably elsewhere).  You can use,
// copy, and run it under the terms of the GNU General Public
// License version 3.  Richard Stallman is an asshole communist.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.


// Function to encode data in quoted-printable format
// Written by Theriault and Brett Zamir [https://locutus.io/php/quoted_printable_encode/]
function quoted_printable_encode(str) {
	const hexChars = ['0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B', 'C', 'D', 'E', 'F']
	const RFC2045Encode1IN = / \r\n|\r\n|[^!-<>-~ ]/gm
	const RFC2045Encode1OUT = function (sMatch) {
		// Encode space before CRLF sequence to prevent spaces from being stripped
		// Keep hard line breaks intact; CRLF sequences
		if (sMatch.length > 1) {
			return sMatch.replace(' ', '=20');
		}
		// Encode matching character
		const chr = sMatch.charCodeAt(0);
		return '=' + hexChars[((chr >>> 4) & 15)] + hexChars[(chr & 15)];
	}
	// Split lines to 75 characters; the reason it's 75 and not 76 is because softline breaks are
	// preceeded by an equal sign; which would be the 76th character. However, if the last line/string
	// was exactly 76 characters, then a softline would not be needed. PHP currently softbreaks
	// anyway; so this function replicates PHP.
	const RFC2045Encode2IN = /.{1,72}(?!\r\n)[^=]{0,3}/g
	const RFC2045Encode2OUT = function (sMatch) {
		if (sMatch.substr(sMatch.length - 2) === '\r\n') {
			return sMatch;
		}
		return sMatch + '=\r\n';
	}
	str = str.replace(RFC2045Encode1IN, RFC2045Encode1OUT).replace(RFC2045Encode2IN, RFC2045Encode2OUT);
	// Strip last softline break
	return str.substr(0, str.length - 3)
}


// Generate a random string of the specified length
// Useful for generating one-time-use div names
function randomString(length) {
	var chars = '0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghiklmnopqrstuvwxyz'.split('');
	var str = '';

	if (!length) {
		length = Math.floor(Math.random() * chars.length);
	}
	for (var i = 0; i < length; i++) {
		str += chars[Math.floor(Math.random() * chars.length)];
	}
	return str;
}


// string escape for html display
function escapeHTML(text) {
	'use strict';
	return text.replace(/[\"&<>]/g, function (a) {
		return {
			'"': '&quot;',
			'&': '&amp;',
			'<': '&lt;',
			'>': '&gt;'
		}[a];
	});
}


// string escape for html display
function escapeHTMLURI(text) {
	'use strict';
	return text.replace(/./g, function (a) {
		return '%' + a.charCodeAt(0).toString(16);
	});
}


// string escape for JavaScript string
//
function escapeJS(text) {
	'use strict';
	return text.replace(/[\"\']/g, function (a) {
		return '\\' + a ;
	});
}
