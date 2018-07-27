/*
 * Client-side IPC functions
 *
 * Copyright (c) 1987-2018 by the citadel.org team
 *
 * This program is open source software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */


#include "textclient.h"


/* Note that some of these functions may not work with multiple instances. */

static void (*deathHook) (void) = NULL;
int (*error_printf) (char *s, ...) = (int (*)(char *, ...)) printf;

void setIPCDeathHook(void (*hook) (void))
{
	deathHook = hook;
}

void setIPCErrorPrintf(int (*func) (char *s, ...))
{
	error_printf = func;
}

void connection_died(CtdlIPC * ipc, int using_ssl)
{
	if (deathHook != NULL) {
		deathHook();
	}

	stty_ctdl(SB_RESTORE);
	fprintf(stderr, "\r\n\n\n");
	fprintf(stderr, "Your connection to %s is broken.\n", ipc->ServInfo.humannode);

#ifdef HAVE_OPENSSL
	if (using_ssl) {
		fprintf(stderr, "Last error: %s\n", ERR_reason_error_string(ERR_get_error()));
		SSL_free(ipc->ssl);
		ipc->ssl = NULL;
	} else
#endif
		fprintf(stderr, "Last error: %s\n", strerror(errno));

	fprintf(stderr, "Please re-connect and log in again.\n");
	fflush(stderr);
	fflush(stdout);
	shutdown(ipc->sock, 2);
	ipc->sock = -1;
	exit(1);
}
