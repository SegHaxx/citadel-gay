// Copyright (c) 2016-2023 by the citadel.org team
//
// This program is open source software.  Use, duplication, or
// disclosure are subject to the GNU General Public License v3.


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


// generate a random string -- mainly used for generating one-time-use div names
function randomString() {
	return Math.random().toString(36).replace('0.','ctdl_' || '');
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


// string escape for html display FIXME can this be replaced with encodeURI() from the standard library?
function escapeHTMLURI(text) {
	'use strict';
	return text.replace(/./g, function (a) {
		return '%' + a.charCodeAt(0).toString(16);
	});
}


// string escape for JavaScript string
function escapeJS(text) {
	'use strict';
	return text.replace(/[\"\']/g, function (a) {
		return '\\' + a ;
	});
}


// Convert a UNIX timestamp to the browser's local time
// See also: https://timestamp.online/article/how-to-convert-timestamp-to-datetime-in-javascript
// format should be: 0=full (date+time), 1=brief (date if not today, time if today)
function string_timestamp(timestamp, format) {
	var ts = new Date(timestamp * 1000);
	if (format == 1) {
		var now_ts = new Date(Date.now());
		if (ts.toLocaleDateString() == now_ts.toLocaleDateString()) {
			return(ts.toLocaleTimeString());
		}
		else {
			return(ts.toLocaleDateString());
		}
	}
	else {
		return(ts.toLocaleString());
	}
}


// An old version of string_timestamp() did it the hard way.
// It used https://gist.github.com/kmaida/6045266 as a reference.
// check git history prior to 2022-jul-03 if you want to see it.


// Get the value of a cookie from the HTTP session
// Shamelessly swiped from https://stackoverflow.com/questions/5639346/what-is-the-shortest-function-for-reading-a-cookie-by-name-in-javascript
const getCookieValue = (name) => (
	document.cookie.match('(^|;)\\s*' + name + '\\s*=\\s*([^;]+)')?.pop() || ''
)


