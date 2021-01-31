/*
 * This module handles network mail and mailing list processing.
 *
 * Copyright (c) 2000-2021 by the citadel.org team
 *
 * This program is open source software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * ** NOTE **   A word on the S_NETCONFIGS semaphore:
 * This is a fairly high-level type of critical section.  It ensures that no
 * two threads work on the netconfigs files at the same time.  Since we do
 * so many things inside these, here are the rules:
 *  1. begin_critical_section(S_NETCONFIGS) *before* begin_ any others.
 *  2. Do *not* perform any I/O with the client during these sections.
 */

/*
 * Duration of time (in seconds) after which pending list subscribe/unsubscribe
 * requests that have not been confirmed will be deleted.
 */
#define EXP	259200	/* three days */

#include "sysdep.h"
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <ctype.h>
#include <signal.h>
#include <pwd.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <time.h>

#ifdef HAVE_SYSCALL_H
# include <syscall.h>
#else 
# if HAVE_SYS_SYSCALL_H
#  include <sys/syscall.h>
# endif
#endif

#include <sys/wait.h>
#include <string.h>
#include <limits.h>
#include <libcitadel.h>
#include "citadel.h"
#include "server.h"
#include "citserver.h"
#include "support.h"
#include "config.h"
#include "user_ops.h"
#include "database.h"
#include "msgbase.h"
#include "internet_addressing.h"
#include "serv_network.h"
#include "clientsocket.h"
#include "citadel_dirs.h"
#include "threads.h"
#include "context.h"
#include "ctdl_module.h"
#include "netspool.h"
#include "netmail.h"

/* comes from lookup3.c from libcitadel... */
extern uint32_t hashlittle( const void *key, size_t length, uint32_t initval);

/*
 * network_do_queue()
 * 
 * Run through the rooms doing various types of network stuff.
 */
void network_do_queue(void) {
	static time_t last_run = 0L;
	int full_processing = 1;

	/*
	 * Run the full set of processing tasks no more frequently
	 * than once every n seconds
	 */
	if ( (time(NULL) - last_run) < CtdlGetConfigLong("c_net_freq") ) {
		full_processing = 0;
		syslog(LOG_DEBUG, "network: full processing in %ld seconds.", CtdlGetConfigLong("c_net_freq") - (time(NULL)- last_run));
	}

	begin_critical_section(S_RPLIST);
	end_critical_section(S_RPLIST);

	/* 
	 * Go ahead and run the queue
	 */
	if (full_processing && !server_shutting_down) {
		syslog(LOG_DEBUG, "network: loading outbound queue");
		//CtdlForEachNetCfgRoom(network_queue_interesting_rooms, &RL);
		// FIXME do the outbound crapola AJC 2021
	}
	syslog(LOG_DEBUG, "network: queue run completed");

	if (full_processing) {
		last_run = time(NULL);
	}
}


/*
 * Module entry point
 */
CTDL_MODULE_INIT(network)
{
	if (!threading)
	{
		CtdlRegisterSessionHook(network_do_queue, EVT_TIMER, PRIO_QUEUE + 10);
	}
	return "network";
}
