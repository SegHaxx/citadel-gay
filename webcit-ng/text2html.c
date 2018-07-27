/*
 * Convert text/plain to text/html
 *
 * Copyright (c) 2017-2018 by the citadel.org team
 *
 * This program is open source software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include "webcit.h"


/*
 * Convert a text/plain message to text/html
 */
StrBuf *text2html(const char *supplied_charset, int treat_as_wiki, char *roomname, long msgnum, StrBuf * Source)
{
	StrBuf *sj = NULL;

	sj = NewStrBuf();
	if (!sj) {
		return (sj);
	}

	StrBufAppendPrintf(sj, "<pre>");
	StrEscAppend(sj, Source, NULL, 0, 0);	// FIXME - add code here to activate links
	StrBufAppendPrintf(sj, "</pre>\n");

	return (sj);
}


/*
 * Convert a text/x-citadel-variformat message to text/html
 */
StrBuf *variformat2html(StrBuf * Source)
{
	StrBuf *Target = NULL;

	Target = NewStrBuf();
	if (!Target) {
		return (Target);
	}

	const char *ptr, *pte;
	const char *BufPtr = NULL;
	StrBuf *Line = NewStrBufPlain(NULL, SIZ);
	StrBuf *Line1 = NewStrBufPlain(NULL, SIZ);
	StrBuf *Line2 = NewStrBufPlain(NULL, SIZ);
	int bn = 0;
	int bq = 0;
	int i;
	long len;
	int intext = 0;

	if (StrLength(Source) > 0)
		do {
			StrBufSipLine(Line, Source, &BufPtr);
			bq = 0;
			i = 0;
			ptr = ChrPtr(Line);
			len = StrLength(Line);
			pte = ptr + len;

			if ((intext == 1) && (isspace(*ptr))) {
				StrBufAppendBufPlain(Target, HKEY("<br>"), 0);
			}
			intext = 1;
			if (isspace(*ptr)) {
				while ((ptr < pte) && ((*ptr == '>') || isspace(*ptr))) {
					if (*ptr == '>') {
						bq++;
					}
					ptr++;
					i++;
				}
			}

			/*
			 * Quoted text should be displayed in italics and in a
			 * different colour.  This code understands Citadel-style
			 * " >" quotes and will convert to <BLOCKQUOTE> tags.
			 */
			if (i > 0)
				StrBufCutLeft(Line, i);

			for (i = bn; i < bq; i++)
				StrBufAppendBufPlain(Target, HKEY("<blockquote>"), 0);
			for (i = bq; i < bn; i++)
				StrBufAppendBufPlain(Target, HKEY("</blockquote>"), 0);
			bn = bq;

			if (StrLength(Line) == 0)
				continue;

			/* Activate embedded URL's */
			UrlizeText(Line1, Line, Line2);

			StrEscAppend(Target, Line1, NULL, 0, 0);

			StrBufAppendBufPlain(Target, HKEY("\n"), 0);
		}
		while ((BufPtr != StrBufNOTNULL) && (BufPtr != NULL));

	for (i = 0; i < bn; i++) {
		StrBufAppendBufPlain(Target, HKEY("</blockquote>"), 0);
	}
	StrBufAppendBufPlain(Target, HKEY("<br>\n"), 0);
	FreeStrBuf(&Line);
	FreeStrBuf(&Line1);
	FreeStrBuf(&Line2);
	return (Target);
}
