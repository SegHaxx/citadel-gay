/*
 * User functions
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
 * Fetch a user photo (avatar)
 */
void fetch_user_photo(struct http_transaction *h, struct ctdlsession *c)
{
	char username[1024];

	extract_token(username, h->uri, 3, '/', sizeof username);

	do_404(h);	// FIXME finish this
}


/*
 * Fetch a user bio (profile)
 */
void fetch_user_bio(struct http_transaction *h, struct ctdlsession *c)
{
	char username[1024];

	extract_token(username, h->uri, 3, '/', sizeof username);

	do_404(h);	// FIXME finish this
}


/*
 * Client requested an object related to a user.
 */
void object_in_user(struct http_transaction *h, struct ctdlsession *c)
{
	char buf[1024];
	long msgnum = (-1);
	char unescaped_euid[1024];

	extract_token(buf, h->uri, 4, '/', sizeof buf);

	if (!strcasecmp(buf, "userpic")) {		// user photo (avatar)
		fetch_user_photo(h, c);
		return;
	}

	if (!strcasecmp(buf, "bio")) {			// user bio (profile)
		fetch_user_bio(h, c);
		return;
	}

	do_404(h);					// unknown object
	return;
}


/*
 * Handle REST/DAV requests for the user itself (such as /ctdl/u/username
 * or /ctdl/i/username/ but *not* specific properties of the user)
 */
void the_user_itself(struct http_transaction *h, struct ctdlsession *c)
{
	do_404(h);
}


/*
 * Dispatcher for "/ctdl/u" and "/ctdl/u/" for the user list
 */
void user_list(struct http_transaction *h, struct ctdlsession *c)
{
	do_404(h);
}


/*
 * Dispatcher for paths starting with /ctdl/u/
 */
void ctdl_u(struct http_transaction *h, struct ctdlsession *c)
{
	char requested_username[128];
	char buf[1024];

	// All user-related functions require accessing the user in question.
	extract_token(requested_username, h->uri, 3, '/', sizeof requested_username);
	unescape_input(requested_username);

	if (IsEmptyStr(requested_username)) {	//      /ctdl/u/
		user_list(h, c);
		return;
	}

	// Try to access the user...
	if (strcasecmp(requested_username, c->room)) {
		do_404(h);
	} else {
		do_404(h);
		return;
	}

	// At this point we have accessed the requested user account.
	if (num_tokens(h->uri, '/') == 4) {	//      /ctdl/u/username
		the_user_itself(h, c);
		return;
	}

	extract_token(buf, h->uri, 4, '/', sizeof buf);
	if (num_tokens(h->uri, '/') == 5) {
		if (IsEmptyStr(buf)) {
			the_user_itself(h, c);	//      /ctdl/u/username/       ( same as /ctdl/u/username )
		} else {
			object_in_user(h, c);	//      /ctdl/u/username/object
		}
		return;
	}

	if (num_tokens(h->uri, '/') == 6) {
		object_in_user(h, c);	//      /ctdl/u/username/object/ or possibly /ctdl/u/username/object/component
		return;
	}

	// If we get to this point, the client specified a valid user but requested an action we don't know how to perform.
	do_404(h);
}
