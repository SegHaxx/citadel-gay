/*
 * Bring external RSS and/or Atom feeds into rooms.  This module implements a
 * very loose parser that scrapes both kinds of feeds and is not picky about
 * the standards compliance of the source data.
 *
 * Copyright (c) 2007-2020 by the citadel.org team
 *
 * This program is open source software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>

#if TIME_WITH_SYS_TIME
# include <sys/time.h>
# include <time.h>
#else
# if HAVE_SYS_TIME_H
#include <sys/time.h>
# else
#include <time.h>
# endif
#endif

#include <ctype.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <expat.h>
#include <curl/curl.h>
#include <libcitadel.h>
#include "citadel.h"
#include "server.h"
#include "citserver.h"
#include "support.h"
#include "config.h"
#include "threads.h"
#include "ctdl_module.h"
#include "msgbase.h"
#include "parsedate.h"
#include "database.h"
#include "citadel_dirs.h"
#include "context.h"
#include "internet_addressing.h"

struct rssroom {
	struct rssroom *next;
	char *room;
};

struct rssurl {
	struct rssurl *next;
	char *url;
	struct rssroom *rooms;
};

struct rssparser {
	StrBuf *CData;
	struct CtdlMessage *msg;
	char *link;
	char *description;
	char *item_id;
	struct rssroom *rooms;
};

time_t last_run = 0L;
struct rssurl *rsstodo = NULL;


// This handler is called whenever an XML tag opens.
//
void rss_start_element(void *data, const char *el, const char **attribute)
{
	struct rssparser *r = (struct rssparser *)data;
	int i;

	if (
		(!strcasecmp(el, "entry"))
		|| (!strcasecmp(el, "item"))
	) {
		// this is the start of a new item(rss) or entry(atom)
		if (r->msg != NULL) {
			CM_Free(r->msg);
			r->msg = NULL;
		}
		r->msg = malloc(sizeof(struct CtdlMessage));
		memset(r->msg, 0, sizeof(struct CtdlMessage));
		r->msg->cm_magic = CTDLMESSAGE_MAGIC;
		r->msg->cm_anon_type = MES_NORMAL;
		r->msg->cm_format_type = FMT_RFC822;
	}

	else if (!strcasecmp(el, "link")) {			// atom feeds have the link as an attribute
		for(i = 0; attribute[i]; i += 2) {
			if (!strcasecmp(attribute[i], "href")) {
				if (r->link != NULL) {
					free(r->link);
					r->link = NULL;
				}
				r->link = strdup(attribute[i+1]);
				striplt(r->link);
			}
		}
	}
}


// This handler is called whenever an XML tag closes.
//
void rss_end_element(void *data, const char *el)
{
	struct rssparser *r = (struct rssparser *)data;

	if (							// end of a new item(rss) or entry(atom)
		(!strcasecmp(el, "entry"))
		|| (!strcasecmp(el, "item"))
	) {
		if (r->msg != NULL) {				// Save the message to the rooms

			// use the link as an item id if nothing else is available
			if ((r->item_id == NULL) && (r->link != NULL)) {
				r->item_id = strdup(r->link);
			}

			// check the use table
			StrBuf *u = NewStrBuf();
			StrBufAppendPrintf(u, "rss/%s", r->item_id);
			int already_seen = CheckIfAlreadySeen(u);
			FreeStrBuf(&u);

			if (already_seen == 0) {

				// Compose the message text
				StrBuf *TheMessage = NewStrBuf();
				StrBufAppendPrintf(TheMessage,
					"Content-type: text/html\n\n"
					"\n\n"
					"<html><head></head><body>"
				);
		
				if (r->description != NULL) {
					StrBufAppendPrintf(TheMessage, "%s<br><br>\r\n", r->description);
					free(r->description);
					r->description = NULL;
				}
		
				if (r->link != NULL) {
					StrBufAppendPrintf(TheMessage, "<a href=\"%s\">%s</a>\r\n", r->link, r->link);
					free(r->link);
					r->link = NULL;
				}
	
				StrBufAppendPrintf(TheMessage, "</body></html>\r\n");
				CM_SetField(r->msg, eMesageText, ChrPtr(TheMessage), StrLength(TheMessage));
				FreeStrBuf(&TheMessage);
	
				if (CM_IsEmpty(r->msg, eAuthor)) {
					CM_SetField(r->msg, eAuthor, HKEY("rss"));
				}
	
				if (CM_IsEmpty(r->msg, eTimestamp)) {
					CM_SetFieldLONG(r->msg, eTimestamp, time(NULL));
				}
	
				// Save it to the room(s)
				struct rssroom *rr = NULL;
				long msgnum = (-1);
				for (rr=r->rooms; rr!=NULL; rr=rr->next) {
					if (rr == r->rooms) {
						msgnum = CtdlSubmitMsg(r->msg, NULL, rr->room, 0);	// in first room, save msg
					}
					else {
						CtdlSaveMsgPointerInRoom(rr->room, msgnum, 0, NULL);	// elsewhere, save a pointer
					}
					syslog(LOG_DEBUG, "rssclient: saved message %ld to %s", msgnum, rr->room);
				}
			}
			else {
				syslog(LOG_DEBUG, "rssclient: already seen %s", r->item_id);
			}
	
			CM_Free(r->msg);
			r->msg = NULL;
		}

		if (r->item_id != NULL) {
			free(r->item_id);
			r->item_id = NULL;
		}
	}

	else if (!strcasecmp(el, "title")) {			// item subject (rss and atom)
		if ((r->msg != NULL) && (CM_IsEmpty(r->msg, eMsgSubject))) {
			CM_SetField(r->msg, eMsgSubject, ChrPtr(r->CData), StrLength(r->CData));
			striplt(r->msg->cm_fields[eMsgSubject]);
		}
	}

	else if (!strcasecmp(el, "creator")) {			// <creator> can be used if <author> is not present
		if ((r->msg != NULL) && (CM_IsEmpty(r->msg, eAuthor))) {
			CM_SetField(r->msg, eAuthor, ChrPtr(r->CData), StrLength(r->CData));
			striplt(r->msg->cm_fields[eAuthor]);
		}
	}

	else if (!strcasecmp(el, "author")) {			// <author> supercedes <creator> if both are present
		if (r->msg != NULL) {
			CM_SetField(r->msg, eAuthor, ChrPtr(r->CData), StrLength(r->CData));	// CM_SetField will free() the previous value
			striplt(r->msg->cm_fields[eAuthor]);
		}
	}

	else if (!strcasecmp(el, "pubdate")) {			// date/time stamp (rss) Sat, 25 Feb 2017 14:28:01 EST
		if ((r->msg)&&(r->msg->cm_fields[eTimestamp]==NULL)) {
			CM_SetFieldLONG(r->msg, eTimestamp, parsedate(ChrPtr(r->CData)));
		}
	}

	else if (!strcasecmp(el, "updated")) {			// date/time stamp (atom) 2003-12-13T18:30:02Z
		if ((r->msg)&&(r->msg->cm_fields[eTimestamp]==NULL)) {
			struct tm t;
			char zulu;
			memset(&t, 0, sizeof t);
			sscanf(ChrPtr(r->CData), "%d-%d-%dT%d:%d:%d%c", &t.tm_year, &t.tm_mon, &t.tm_mday, &t.tm_hour, &t.tm_min, &t.tm_sec, &zulu);
			t.tm_year -= 1900;
			t.tm_mon -= 1;
			CM_SetFieldLONG(r->msg, eTimestamp, mktime(&t));
		}
	}

	else if (!strcasecmp(el, "link")) {			// link to story (rss)
		if (r->link != NULL) {
			free(r->link);
			r->link = NULL;
		}
		r->link = strdup(ChrPtr(r->CData));
		striplt(r->link);
	}

	else if (
		(!strcasecmp(el, "guid"))			// unique item id (rss)
		|| (!strcasecmp(el, "id"))			// unique item id (atom)
	) {
		if (r->item_id != NULL) {
			free(r->item_id);
			r->item_id = NULL;
		}
		r->item_id = strdup(ChrPtr(r->CData));
		striplt(r->item_id);
	}

	else if (
		(!strcasecmp(el, "description"))		// message text (rss)
		|| (!strcasecmp(el, "summary"))			// message text (atom)
		|| (!strcasecmp(el, "content"))			// message text (atom)
	) {
		if (r->description != NULL) {
			free(r->description);
			r->description = NULL;
		}
		r->description = strdup(ChrPtr(r->CData));
		striplt(r->description);
	}

	if (r->CData != NULL) {
		FreeStrBuf(&r->CData);
		r->CData = NULL;
	}
}


// This handler is called whenever data appears between opening and closing tags.
//
void rss_handle_data(void *data, const char *content, int length)
{
	struct rssparser *r = (struct rssparser *)data;

	if (r->CData == NULL) {
		r->CData = NewStrBuf();
	}

	StrBufAppendBufPlain(r->CData, content, length, 0);
}


// Feed has been downloaded, now parse it.
//
void rss_parse_feed(StrBuf *Feed, struct rssroom *rooms)
{
	struct rssparser r;

	memset(&r, 0, sizeof r);
	r.rooms = rooms;
	XML_Parser p = XML_ParserCreate("UTF-8");
	XML_SetElementHandler(p, rss_start_element, rss_end_element);
	XML_SetCharacterDataHandler(p, rss_handle_data);
	XML_SetUserData(p, (void *)&r);
	XML_Parse(p, ChrPtr(Feed), StrLength(Feed), XML_TRUE);
	XML_ParserFree(p);
}


// Add a feed/room pair into the todo list
//
void rssclient_push_todo(char *rssurl, char *roomname)
{
	struct rssurl *r = NULL;
	struct rssurl *thisone = NULL;
	struct rssroom *newroom = NULL;

	syslog(LOG_DEBUG, "rssclient: will fetch %s to %s", rssurl, roomname);

	for (r=rsstodo; r!=NULL; r=r->next) {
		if (!strcasecmp(r->url, rssurl)) {
			thisone = r;
		}
	}

	if (thisone == NULL) {
		thisone = malloc(sizeof(struct rssurl));
		thisone->url = strdup(rssurl);
		thisone->rooms = NULL;
		thisone->next = rsstodo;
		rsstodo = thisone;
	}

	newroom = malloc(sizeof(struct rssroom));
	newroom->room = strdup(roomname);
	newroom->next = thisone->rooms;
	thisone->rooms = newroom;
}


// pull one feed (possibly multiple rooms)
//
void rss_pull_one_feed(struct rssurl *url)
{
	CURL *curl;
	CURLcode res;
	StrBuf *Downloaded = NULL;

	syslog(LOG_DEBUG, "rssclient: fetching %s", url->url);

	curl = curl_easy_init();
	if (!curl) {
		return;
	}

	Downloaded = NewStrBuf();

	curl_easy_setopt(curl, CURLOPT_URL, url->url);
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);			// Follow redirects
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, CurlFillStrBuf_callback);	// What to do with downloaded data
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, Downloaded);			// Give it our StrBuf to work with
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 20L);				// Time out after 20 seconds
	res = curl_easy_perform(curl);						// Perform the request
	if (res != CURLE_OK) {
		syslog(LOG_WARNING, "rssclient: failed to load feed: %s", curl_easy_strerror(res));
	}
	curl_easy_cleanup(curl);

	rss_parse_feed(Downloaded, url->rooms);					// parse the feed
	FreeStrBuf(&Downloaded);						// free the downloaded feed data
}


// We have a list, now download the feeds
//
void rss_pull_feeds(void)
{
	struct rssurl *r;
	struct rssroom *rr;

	while (rsstodo != NULL) {
		rss_pull_one_feed(rsstodo);
		r = rsstodo;
		rsstodo = rsstodo->next;
		while (r->rooms != NULL) {
			rr = r->rooms;
			r->rooms = r->rooms->next;
			free(rr->room);
			free(rr);
		}
		free(r->url);
		free(r);
	}
}


// Scan a room's netconfig looking for RSS feed parsing requests
//
void rssclient_scan_room(struct ctdlroom *qrbuf, void *data)
{
	char *serialized_config = NULL;
	int num_configs = 0;
	char cfgline[SIZ];
	int i = 0;

        serialized_config = LoadRoomNetConfigFile(qrbuf->QRnumber);
        if (!serialized_config) {
		return;
	}

	num_configs = num_tokens(serialized_config, '\n');
	for (i=0; i<num_configs; ++i) {
		extract_token(cfgline, serialized_config, i, '\n', sizeof cfgline);
		if (!strncasecmp(cfgline, HKEY("rssclient|"))) {
			strcpy(cfgline, &cfgline[10]);
			char *vbar = strchr(cfgline, '|');
			if (vbar != NULL) {
				*vbar = 0;
			}
			rssclient_push_todo(cfgline, qrbuf->QRname);
		}
	}

	free(serialized_config);
}


/*
 * Scan for rooms that have RSS client requests configured
 */
void rssclient_scan(void) {
	time_t now = time(NULL);

	/* Run no more than once every 15 minutes. */
	if ((now - last_run) < 900) {
		syslog(LOG_DEBUG,
			"rssclient: polling interval not yet reached; last run was %ldm%lds ago",
			((now - last_run) / 60),
			((now - last_run) % 60)
		);
		return;
	}

	syslog(LOG_DEBUG, "rssclient: started");
	CtdlForEachRoom(rssclient_scan_room, NULL);
	rss_pull_feeds();
	syslog(LOG_DEBUG, "rssclient: ended");
	last_run = time(NULL);
	return;
}


CTDL_MODULE_INIT(rssclient)
{
	if (!threading)
	{
		syslog(LOG_INFO, "rssclient: using %s", curl_version());
		CtdlRegisterSessionHook(rssclient_scan, EVT_TIMER, PRIO_AGGR + 300);
	}
	return "rssclient";
}

