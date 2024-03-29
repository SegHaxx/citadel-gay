// A basic toolset containing miscellaneous functions for string manipluation,
// encoding/decoding, and a bunch of other stuff.
//
// Copyright (c) 1987-2022 by the citadel.org team
//
// This program is open source software.  Use, duplication, or disclosure
// is subject to the terms of the GNU General Public License, version 3.

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

#if TIME_WITH_SYS_TIME
# include <sys/time.h>
# include <time.h>
#else
# if HAVE_SYS_TIME_H
#  include <sys/time.h>
# else
#  include <time.h>
# endif
#endif

#include "libcitadel.h"


#define TRUE  1
#define FALSE 0

typedef unsigned char byte;	      // Byte type

// copy a string into a buffer of a known size. abort if we exceed the limits
//
// dest	the targetbuffer
// src	the source string
// n	the size od dest
//
// returns the number of characters copied if dest is big enough, -n if not.
int safestrncpy(char *dest, const char *src, size_t n) {
	int i = 0;

	if (dest == NULL || src == NULL)
	{
		fprintf(stderr, "safestrncpy: NULL argument\n");
		abort();
	}

	do {
		dest[i] = src[i];
		if (dest[i] == 0) return i;
		++i;
	} while (i<n);
	dest[n - 1] = 0;
	return -i;
}


// num_tokens()  -  discover number of parameters/tokens in a string
int num_tokens(const char *source, char tok) {
	int count = 1;
	const char *ptr = source;

	if (source == NULL) {
		return (0);
	}

	while (*ptr != '\0') {
		if (*ptr++ == tok) {
			++count;
		}
	}
	
	return (count);
}


// extract_token() - a string tokenizer
// returns -1 if not found, or length of token.
long extract_token(char *dest, const char *source, int parmnum, char separator, int maxlen) {
	const char *s;			// source
	int len = 0;			// running total length of extracted string
	int current_token = 0;		// token currently being processed

	s = source;

	if (dest == NULL) {
		return(-1);
	}

	dest[0] = 0;

	if (s == NULL) {
		return(-1);
	}
	
	maxlen--;

	while (*s) {
		if (*s == separator) {
			++current_token;
		}
		if ( (current_token == parmnum) && (*s != separator) && (len < maxlen) ) {
			dest[len] = *s;
			++len;
		}
		else if ((current_token > parmnum) || (len >= maxlen)) {
			break;
		}
		++s;
	}

	dest[len] = '\0';
	if (current_token < parmnum) {
		return(-1);
	}
	return(len);
}


// remove_token() - a tokenizer that kills, maims, and destroys
void remove_token(char *source, int parmnum, char separator) {
	char *d, *s;		// dest, source
	int count = 0;

	// Find desired parameter
	d = source;
	while (count < parmnum) {
		// End of string, bail!
		if (!*d) {
			d = NULL;
			break;
		}
		if (*d == separator) {
			count++;
		}
		d++;
	}
	if (!d) return;		// Parameter not found

	// Find next parameter
	s = d;
	while (*s && *s != separator) {
		s++;
	}

	// Hack and slash
	if (*s)
		strcpy(d, ++s);
	else if (d == source)
		*d = 0;
	else
		*--d = 0;
}


// extract_int()  -  extract an int parm without supplying a buffer
int extract_int(const char *source, int parmnum) {
	char buf[32];
	
	if (extract_token(buf, source, parmnum, '|', sizeof buf) > 0)
		return(atoi(buf));
	else
		return 0;
}


// extract_long()  -  extract an long parm without supplying a buffer
long extract_long(const char *source, int parmnum) {
	char buf[32];
	
	if (extract_token(buf, source, parmnum, '|', sizeof buf) > 0)
		return(atol(buf));
	else
		return 0;
}


// extract_unsigned_long() - extract an unsigned long parm
unsigned long extract_unsigned_long(const char *source, int parmnum) {
	char buf[32];

	if (extract_token(buf, source, parmnum, '|', sizeof buf) > 0)
		return strtoul(buf, NULL, 10);
	else 
		return 0;
}


// if we send out non ascii subjects, we encode it this way.
char *rfc2047encode(const char *line, long length) {
	const char *AlreadyEncoded;
	char *result;
	long end;
#define UTF8_HEADER "=?UTF-8?B?"

	// check if we're already done
	AlreadyEncoded = strstr(line, "=?");
	if ((AlreadyEncoded != NULL) && ((strstr(AlreadyEncoded, "?B?") != NULL)|| (strstr(AlreadyEncoded, "?Q?") != NULL))) {
		return strdup(line);
	}

	result = (char*) malloc(sizeof(UTF8_HEADER) + 4 + length * 2);
	strncpy (result, UTF8_HEADER, strlen (UTF8_HEADER));
	CtdlEncodeBase64(result + strlen(UTF8_HEADER), line, length, BASE64_NO_LINEBREAKS);
	end = strlen (result);
        result[end]='?';
	result[end+1]='=';
	result[end+2]='\0';
	return result;
}

// removes double slashes from pathnames
// allows / disallows trailing slashes
void StripSlashes(char *Dir, int TrailingSlash) {
	char *a, *b;

	a = b = Dir;

	while (!IsEmptyStr(a)) {
		if (*a == '/') {
			while (*a == '/')
				a++;
			*b = '/';
			b++;
		}
		else {
			*b = *a;
			b++; a++;
		}
	}
	if ((TrailingSlash) && (*(b - 1) != '/')){
		*b = '/';
		b++;
	}
	*b = '\0';

}


// Trim leading and trailing whitespace from a string
size_t string_trim(char *buf) {
	char *first_nonspace = NULL;
	char *last_nonspace = NULL;
	char *ptr;
	size_t new_len = 0;

	if ((buf == NULL) || (*buf == '\0')) {
		return 0;
	}

	for (ptr=buf; *ptr!=0; ++ptr) {
		if (!isspace(*ptr)) {
			if (!first_nonspace) {
				first_nonspace = ptr;
			}
			last_nonspace = ptr;
		}
	}

	if ((!first_nonspace) || (!last_nonspace)) {
		buf[0] = 0;
		return 0;
	}

	new_len = last_nonspace - first_nonspace + 1;
	memmove(buf, first_nonspace, new_len);
	buf[new_len] = 0;
	return new_len;
}


// check for the presence of a character within a string (returns count)
// st	the string to examine
// ch	the char to search
// returns the number of times ch appears in st
int haschar(const char *st, int ch) {
	const char *ptr;
	int b;
	b = 0;
	ptr = st;
	while (!IsEmptyStr(ptr))
	{
		if (*ptr == ch)
			++b;
		ptr ++;
	}
	return (b);
}


// Determine whether the specified message number is contained within the specified sequence set.
int is_msg_in_sequence_set(const char *mset, long msgnum) {
	int num_sets;
	int s;
	char setstr[128], lostr[128], histr[128];
	long lo, hi;

	num_sets = num_tokens(mset, ',');
	for (s=0; s<num_sets; ++s) {
		extract_token(setstr, mset, s, ',', sizeof setstr);

		extract_token(lostr, setstr, 0, ':', sizeof lostr);
		if (num_tokens(setstr, ':') >= 2) {
			extract_token(histr, setstr, 1, ':', sizeof histr);
			if (!strcmp(histr, "*")) {
				snprintf(histr, sizeof histr, "%ld", LONG_MAX);
			}
		} 
		else {
			strcpy(histr, lostr);
		}
		lo = atol(lostr);
		hi = atol(histr);

		if ((msgnum >= lo) && (msgnum <= hi)) return(1);
	}

	return(0);
}

// Utility function to "readline" from memory
// start	Location in memory from which we are reading.
// buf		the buffer to place the string in.
// maxlen	Size of string buffer
// returns pointer to the source memory right after we stopped reading.
char *memreadline(char *start, char *buf, int maxlen) {
	char ch;
	char *ptr;
	int len = 0;		// tally our own length to avoid strlen() delays

	ptr = start;

	while (1) {
		ch = *ptr++;
		if ((len + 1 < (maxlen)) && (ch != 13) && (ch != 10)) {
			buf[len++] = ch;
		}
		if ((ch == 10) || (ch == 0)) {
			buf[len] = 0;
			return ptr;
		}
	}
}


// Utility function to "readline" from memory
// start	Location in memory from which we are reading.
// buf		the buffer to place the string in.
// maxlen	Size of string buffer
// retlen	the length of the returned string
// returns a pointer to the source memory right after we stopped reading.
char *memreadlinelen(char *start, char *buf, int maxlen, int *retlen) {
	char ch;
	char *ptr;
	int len = 0;		// tally our own length to avoid strlen() delays

	ptr = start;

	while (1) {
		ch = *ptr++;
		if ((len + 1 < (maxlen)) && (ch != 13) && (ch != 10)) {
			buf[len++] = ch;
		}
		if ((ch == 10) || (ch == 0)) {
			buf[len] = 0;
			*retlen = len;
			return ptr;
		}
	}
}


// Utility function to "readline" from memory
// start Location in memory from which we are reading.
// buf the buffer to place the string in.
// maxlen Size of string buffer
// return Pointer to the source memory right after we stopped reading.
const char *cmemreadline(const char *start, char *buf, int maxlen) {
	char ch;
	const char *ptr;
	int len = 0;		// tally our own length to avoid strlen() delays

	ptr = start;

	while (1) {
		ch = *ptr++;
		if ((len + 1 < (maxlen)) && (ch != 13) && (ch != 10)) {
			buf[len++] = ch;
		}
		if ((ch == 10) || (ch == 0)) {
			buf[len] = 0;
			return ptr;
		}
	}
}


// Utility function to "readline" from memory
// start Location in memory from which we are reading.
// buf the buffer to place the string in.
// maxlen Size of string buffer
// retlen the length of the returned string
// return Pointer to the source memory right after we stopped reading.
const char *cmemreadlinelen(const char *start, char *buf, int maxlen, int *retlen) {
	char ch;
	const char *ptr;
	int len = 0;		// tally our own length to avoid strlen() delays

	ptr = start;

	while (1) {
		ch = *ptr++;
		if ((len + 1 < (maxlen)) && (ch != 13) && (ch != 10)) {
			buf[len++] = ch;
		}
		if ((ch == 10) || (ch == 0)) {
			buf[len] = 0;
			*retlen = len;
			return ptr;
		}
	}
}


// Strip a boundarized substring out of a string (for example, remove parentheses and anything inside them).
int stripout(char *str, char leftboundary, char rightboundary) {
	long lb = (-1);
	long rb = (-1);

	if (!str) {
		return 0;
	}

	for (int a = 0; str[a]; ++a) {
		if ((lb==-1) && (str[a] == leftboundary)) {
			lb = a;
		} else if (str[a] == rightboundary) {
			rb = a;
		}
	}

	if ((lb==-1) || (rb <= lb)) {
		return 0;
	}

	strcpy(str + lb, str + rb + 1);
	return 1;
}

// Reduce a string down to a boundarized substring (for example, remove
// parentheses and anything outside them).
long stripallbut(char *str, char leftboundary, char rightboundary) {
	long lb = (-1);
	long rb = (-1);
	long orig_len = 0;

	if (!str) {
		return 0;
	}

	while (str[orig_len]) {
		if ((lb==-1) && (str[orig_len] == leftboundary)) {
			lb = orig_len;
		} else if (str[orig_len] == rightboundary) {
			rb = orig_len;
		}
		orig_len++;
	}

	if ((lb==-1) || (rb <= lb)) {
		return orig_len;
	}

	fflush(stderr);

	long new_len = rb - lb - 1;
	memmove(str, str + lb + 1, new_len);
	str[new_len] = 0;
	return new_len;
}

char *myfgets(char *s, int size, FILE *stream) {
	char *ret = fgets(s, size, stream);
	char *nl;

	if (ret != NULL) {
		nl = strchr(s, '\n');

		if (nl != NULL)
			*nl = 0;
	}

	return ret;
}


// Escape a string for feeding out as a URL.
// outbuf the output buffer
// oblen the size of outbuf to sanitize
// strbuf the input buffer
void urlesc(char *outbuf, size_t oblen, char *strbuf) {
	int a, b, c, len, eclen, olen;
	char *ec = " +#&;`'|*?-~<>^()[]{}/$\"\\";

	*outbuf = '\0';
	len = strlen(strbuf);
	eclen = strlen(ec);
	olen = 0;
	for (a = 0; a < len; ++a) {
		c = 0;
		for (b = 0; b < eclen; ++b) {
			if (strbuf[a] == ec[b])
				c = 1;
		}
		if (c == 1) {
			snprintf(&outbuf[olen], oblen - olen, "%%%02x", strbuf[a]);
			olen += 3;
		}
		else 
			outbuf[olen ++] = strbuf[a];
	}
	outbuf[olen] = '\0';
}


// In our world, we want strcpy() to be able to work with overlapping strings.
#ifdef strcpy
#undef strcpy
#endif
char *strcpy(char *dest, const char *src) {
	memmove(dest, src, (strlen(src) + 1) );
	return(dest);
}


// Generate a new, globally unique UID parameter for a calendar etc. object
void generate_uuid(char *buf) {
	static int seq = (-1);
	static int no_kernel_uuid = 0;

	// If we are running on Linux then we have a kernelspace uuid generator available

	if (no_kernel_uuid == 0) {
		FILE *fp;
		fp = fopen("/proc/sys/kernel/random/uuid", "rb");
		if (fp) {
			int rv;
			rv = fread(buf, 36, 1, fp);
			fclose(fp);
			if (rv == 1) {
				buf[36] = 0;
				return;
			}
		}
	}

	// If the kernel didn't provide us with a uuid, we generate a pseudo-random one

	no_kernel_uuid = 1;

	if (seq == (-1)) {
		seq = (int)rand();
	}
	++seq;
	seq = (seq % 0x0FFF) ;

	sprintf(buf, "%08lx-%04lx-4%03x-a%03x-%012lx",
		(long)time(NULL),
		(long)getpid(),
		seq,
		seq,
		(long)rand()
	);
}


// bmstrcasestr() -- case-insensitive substring search
//
// This uses the Boyer-Moore search algorithm and is therefore quite fast.
// The code is roughly based on the strstr() replacement from 'tin' written
// by Urs Jannsen.
inline static char *_bmstrcasestr_len(char *text, size_t textlen, const char *pattern, size_t patlen) {

	register unsigned char *p, *t;
	register int i, j, *delta;
	register size_t p1;
	int deltaspace[256];

	if (!text) return(NULL);
	if (!pattern) return(NULL);

	// algorithm fails if pattern is empty
	if ((p1 = patlen) == 0)
		return (text);

	// code below fails (whenever i is unsigned) if pattern too long
	if (p1 > textlen)
		return (NULL);

	// set up deltas
	delta = deltaspace;
	for (i = 0; i <= 255; i++)
		delta[i] = p1;
	for (p = (unsigned char *) pattern, i = p1; --i > 0;)
		delta[tolower(*p++)] = i;

	// From now on, we want patlen - 1.
	// In the loop below, p points to the end of the pattern,
	// t points to the end of the text to be tested against the
	// pattern, and i counts the amount of text remaining, not
	// including the part to be tested.
	p1--;
	p = (unsigned char *) pattern + p1;
	t = (unsigned char *) text + p1;
	i = textlen - patlen;
	while(1) {
		if (tolower(p[0]) == tolower(t[0])) {
			if (strncasecmp ((const char *)(p - p1), (const char *)(t - p1), p1) == 0) {
				return ((char *)t - p1);
			}
		}
		j = delta[tolower(t[0])];
		if (i < j)
			break;
		i -= j;
		t += j;
	}
	return (NULL);
}


/*
 * bmstrcasestr() -- case-insensitive substring search
 *
 * This uses the Boyer-Moore search algorithm and is therefore quite fast.
 * The code is roughly based on the strstr() replacement from 'tin' written
 * by Urs Jannsen.
 */
char *bmstrcasestr(char *text, const char *pattern) {
	size_t textlen;
	size_t patlen;

	if (!text) return(NULL);
	if (!pattern) return(NULL);

	textlen = strlen (text);
	patlen = strlen (pattern);

	return _bmstrcasestr_len(text, textlen, pattern, patlen);
}

char *bmstrcasestr_len(char *text, size_t textlen, const char *pattern, size_t patlen) {
	return _bmstrcasestr_len(text, textlen, pattern, patlen);
}


/*
 * bmstrcasestr() -- case-insensitive substring search
 *
 * This uses the Boyer-Moore search algorithm and is therefore quite fast.
 * The code is roughly based on the strstr() replacement from 'tin' written
 * by Urs Jannsen.
 */
inline static const char *_cbmstrcasestr_len(const char *text, size_t textlen, const char *pattern, size_t patlen) {

	register unsigned char *p, *t;
	register int i, j, *delta;
	register size_t p1;
	int deltaspace[256];

	if (!text) return(NULL);
	if (!pattern) return(NULL);

	/* algorithm fails if pattern is empty */
	if ((p1 = patlen) == 0)
		return (text);

	/* code below fails (whenever i is unsigned) if pattern too long */
	if (p1 > textlen)
		return (NULL);

	/* set up deltas */
	delta = deltaspace;
	for (i = 0; i <= 255; i++)
		delta[i] = p1;
	for (p = (unsigned char *) pattern, i = p1; --i > 0;)
		delta[tolower(*p++)] = i;

	/*
	 * From now on, we want patlen - 1.
	 * In the loop below, p points to the end of the pattern,
	 * t points to the end of the text to be tested against the
	 * pattern, and i counts the amount of text remaining, not
	 * including the part to be tested.
	 */
	p1--;
	p = (unsigned char *) pattern + p1;
	t = (unsigned char *) text + p1;
	i = textlen - patlen;
	while(1) {
		if (tolower(p[0]) == tolower(t[0])) {
			if (strncasecmp ((const char *)(p - p1), (const char *)(t - p1), p1) == 0) {
				return ((char *)t - p1);
			}
		}
		j = delta[tolower(t[0])];
		if (i < j)
			break;
		i -= j;
		t += j;
	}
	return (NULL);
}


/*
 * bmstrcasestr() -- case-insensitive substring search
 *
 * This uses the Boyer-Moore search algorithm and is therefore quite fast.
 * The code is roughly based on the strstr() replacement from 'tin' written
 * by Urs Jannsen.
 */
const char *cbmstrcasestr(const char *text, const char *pattern) {
	size_t textlen;
	size_t patlen;

	if (!text) return(NULL);
	if (!pattern) return(NULL);

	textlen = strlen (text);
	patlen = strlen (pattern);

	return _cbmstrcasestr_len(text, textlen, pattern, patlen);
}


const char *cbmstrcasestr_len(const char *text, size_t textlen, const char *pattern, size_t patlen) {
	return _cbmstrcasestr_len(text, textlen, pattern, patlen);
}


/*
 * Local replacement for controversial C library function that generates
 * names for temporary files.  Included to shut up compiler warnings.
 */
void CtdlMakeTempFileName(char *name, int len) {
	int i = 0;

	while (i++, i < 100) {
		snprintf(name, len, "/tmp/ctdl.%04lx.%04x",
			(long)getpid(),
			rand()
		);
		if (!access(name, F_OK)) {
			return;
		}
	}
}


/*
 * Determine whether the specified message number is contained within the specified set.
 * Returns nonzero if the specified message number is in the specified message set string.
 */
int is_msg_in_mset(const char *mset, long msgnum) {
	int num_sets;
	int s;
	char setstr[SIZ], lostr[SIZ], histr[SIZ];
	long lo, hi;

	// Now set it for all specified messages.
	num_sets = num_tokens(mset, ',');
	for (s=0; s<num_sets; ++s) {
		extract_token(setstr, mset, s, ',', sizeof setstr);

		extract_token(lostr, setstr, 0, ':', sizeof lostr);
		if (num_tokens(setstr, ':') >= 2) {
			extract_token(histr, setstr, 1, ':', sizeof histr);
			if (!strcmp(histr, "*")) {
				snprintf(histr, sizeof histr, "%ld", LONG_MAX);
			}
		}
		else {
			strcpy(histr, lostr);
		}
		lo = atol(lostr);
		hi = atol(histr);

		if ((msgnum >= lo) && (msgnum <= hi)) return(1);
	}

	return(0);
}


// searches for a pattern within a search string
// returns position in string
int pattern2(char *search, char *patn) {
	int a;
	int len, plen;
	len = strlen (search);
	plen = strlen (patn);
	for (a = 0; a < len; ++a) {
		if (!strncasecmp(&search[a], patn, plen))
			return (a);
	}
	return (-1);
}


/*
 * Convert all whitespace characters in a supplied string to underscores
 */
void convert_spaces_to_underscores(char *str) {
	int len;
	int i;

	if (!str) return;

	len = strlen(str);
	for (i=0; i<len; ++i) {
		if (isspace(str[i])) {
			str[i] = '_';
		}
	}
}


/*
 * check whether the provided string needs to be qp encoded or not
 */
int CheckEncode(const char *pch, long len, const char *pche) {
	if (pche == NULL)
		pche = pch + len;
	while (pch < pche) {
		if (((unsigned char) *pch < 32) || 
		    ((unsigned char) *pch > 126)) {
			return 1;
		}
		pch++;
	}
	return 0;
}

