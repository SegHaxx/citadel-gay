/*
 * Barebones SASL authentication service for XMPP (Jabber) clients.
 *
 * Note: RFC3920 says we "must" support DIGEST-MD5 but we only support PLAIN.
 *
 * Copyright (c) 2007-2019 by Art Cancro
 *
 * This program is open source software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include "../../sysdep.h"
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <signal.h>
#include <pwd.h>
#include <errno.h>
#include <sys/types.h>
#include <time.h>
#include <sys/wait.h>
#include <string.h>
#include <limits.h>
#include <ctype.h>
#include <expat.h>
#include <libcitadel.h>
#include "../../citadel_defs.h"
#include "../../server.h"
#include "../../citserver.h"
#include "../../support.h"
#include "../../config.h"
#include "../../user_ops.h"
#include "../../internet_addressing.h"
#include "../../ctdl_module.h"
#include "serv_xmpp.h"


/*
 * PLAIN authentication.  Returns zero on success, nonzero on failure.
 */
int xmpp_auth_plain(char *authstring)
{
	char decoded_authstring[1024];
	char ident[256];
	char user[256];
	char pass[256];
	int result;
	long len;

	/* Take apart the authentication string */
	memset(pass, 0, sizeof(pass));

	CtdlDecodeBase64(decoded_authstring, authstring, strlen(authstring));
	safestrncpy(ident, decoded_authstring, sizeof ident);
	safestrncpy(user, &decoded_authstring[strlen(ident) + 1], sizeof user);
	len = safestrncpy(pass, &decoded_authstring[strlen(ident) + strlen(user) + 2], sizeof pass);
	if (len < 0) {
		len = -len;
	}

	if (!IsEmptyStr(ident)) {
		result = CtdlLoginExistingUser(ident);
	}
	else {
		result = CtdlLoginExistingUser(user);
	}

	if (result == login_ok) {
		if (CtdlTryPassword(pass, len) == pass_ok) {
			return(0);				/* success */
		}
	}

	return(1);						/* failure */
}


/*
 * Output the list of SASL mechanisms offered by this stream.
 */
void xmpp_output_auth_mechs(void) {
	cprintf("<mechanisms xmlns=\"urn:ietf:params:xml:ns:xmpp-sasl\">");
	cprintf("<mechanism>PLAIN</mechanism>");
	cprintf("</mechanisms>");
}


/*
 * Here we go ... client is trying to authenticate.
 */
void xmpp_sasl_auth(char *sasl_auth_mech, char *authstring) {

	if (strcasecmp(sasl_auth_mech, "PLAIN")) {
		cprintf("<failure xmlns=\"urn:ietf:params:xml:ns:xmpp-sasl\">");
		cprintf("<invalid-mechanism/>");
		cprintf("</failure>");
		return;
	}

        if (CC->logged_in) {
		CtdlUserLogout();  /* Client may try to log in twice.  Handle this. */
	}

	if (CC->nologin) {
		cprintf("<failure xmlns=\"urn:ietf:params:xml:ns:xmpp-sasl\">");
		cprintf("<system-shutdown/>");
		cprintf("</failure>");
	}

	else if (xmpp_auth_plain(authstring) == 0) {
		cprintf("<success xmlns=\"urn:ietf:params:xml:ns:xmpp-sasl\"/>");
	}

	else {
		cprintf("<failure xmlns=\"urn:ietf:params:xml:ns:xmpp-sasl\">");
		cprintf("<not-authorized/>");
		cprintf("</failure>");
	}
}


/*
 * Non-SASL authentication
 */
void xmpp_non_sasl_authenticate(char *iq_id, char *username, char *password) {
	int result;
	char xmlbuf[256];

        if (CC->logged_in) {
		CtdlUserLogout();  /* Client may try to log in twice.  Handle this. */
	}

	result = CtdlLoginExistingUser(username);
	if (result == login_ok) {
		result = CtdlTryPassword(password, strlen(password));
		if (result == pass_ok) {
			cprintf("<iq type=\"result\" id=\"%s\"></iq>", xmlesc(xmlbuf, iq_id, sizeof xmlbuf));	/* success */
			return;
		}
	}

	/* failure */
	cprintf("<iq type=\"error\" id=\"%s\">", xmlesc(xmlbuf, iq_id, sizeof xmlbuf));
	cprintf("<error code=\"401\" type=\"auth\">"
		"<not-authorized xmlns=\"urn:ietf:params:xml:ns:xmpp-stanzas\"/>"
		"</error>"
		"</iq>"
	);
}
