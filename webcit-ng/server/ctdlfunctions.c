//
// These utility functions loosely make up a Citadel protocol client library.
//
// Copyright (c) 2016-2022 by the citadel.org team
//
// This program is open source software.  Use, duplication, or
// disclosure are subject to the GNU General Public License v3.

#include "webcit.h"

// Delete one or more messages from the connected Citadel server.
// This function expects the session to already be "in" the room from which the messages will be deleted.
void ctdl_delete_msgs(struct ctdlsession *c, long *msgnums, int num_msgs) {
	int i = 0;
	char buf[1024];

	if ((c == NULL) || (msgnums == NULL) || (num_msgs < 1)) {
		return;
	}

	i = 0;
	strcpy(buf, "DELE ");
	do {
		sprintf(&buf[strlen(buf)], "%ld", msgnums[i]);
		if ((((i + 1) % 50) == 0) || (i == num_msgs - 1))	// delete up to 50 messages with one server command
		{
			syslog(LOG_DEBUG, "%s", buf);
			ctdl_printf(c, "%s", buf);
			ctdl_readline(c, buf, sizeof(buf));
			syslog(LOG_DEBUG, "%s", buf);
		}
		else {
			strcat(buf, ",");
		}
	} while (++i < num_msgs);
}
