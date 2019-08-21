/*
 * This module handles network mail and mailing list processing.
 *
 * Copyright (c) 2000-2019 by the citadel.org team
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

typedef struct __roomlists {
	RoomProcList *rplist;
} roomlists;


/*
 * When we do network processing, it's accomplished in two passes; one to
 * gather a list of rooms and one to actually do them.  It's ok that rplist
 * is global; we have a mutex that keeps it safe.
 */
struct RoomProcList *rplist = NULL;

RoomProcList *CreateRoomProcListEntry(struct ctdlroom *qrbuf, OneRoomNetCfg *OneRNCFG)
{
	int i;
	struct RoomProcList *ptr;

	ptr = (struct RoomProcList *) malloc(sizeof (struct RoomProcList));
	if (ptr == NULL) {
		return NULL;
	}

	ptr->namelen = strlen(qrbuf->QRname);
	if (ptr->namelen > ROOMNAMELEN) {
		ptr->namelen = ROOMNAMELEN - 1;
	}

	memcpy (ptr->name, qrbuf->QRname, ptr->namelen);
	ptr->name[ptr->namelen] = '\0';
	ptr->QRNum = qrbuf->QRnumber;

	for (i = 0; i < ptr->namelen; i++)
	{
		ptr->lcname[i] = tolower(ptr->name[i]);
	}

	ptr->lcname[ptr->namelen] = '\0';
	ptr->key = hashlittle(ptr->lcname, ptr->namelen, 9872345);
	return ptr;
}

/*
 * Batch up and send all outbound traffic from the current room
 */
void network_queue_interesting_rooms(struct ctdlroom *qrbuf, void *data, OneRoomNetCfg *OneRNCfg)
{
	struct RoomProcList *ptr;
	roomlists *RP = (roomlists*) data;

	if (!HaveSpoolConfig(OneRNCfg)) {
		return;
	}

	ptr = CreateRoomProcListEntry(qrbuf, OneRNCfg);

	if (ptr != NULL)
	{
		ptr->next = RP->rplist;
		RP->rplist = ptr;
	}
}

/*
 * Batch up and send all outbound traffic from the current room
 */
int network_room_handler(struct ctdlroom *qrbuf)
{
	struct RoomProcList *ptr;
	OneRoomNetCfg *RNCfg;

	if (qrbuf->QRdefaultview == VIEW_QUEUE) {
		return 1;
	}

	RNCfg = CtdlGetNetCfgForRoom(qrbuf->QRnumber);
	if (RNCfg == NULL) {
		return 1;
	}

	if (!HaveSpoolConfig(RNCfg)) {
		FreeRoomNetworkStruct(&RNCfg);
		return 1;
	}

	ptr = CreateRoomProcListEntry(qrbuf, RNCfg);
	if (ptr == NULL) {
		FreeRoomNetworkStruct(&RNCfg);
		return 1;
	}

	begin_critical_section(S_RPLIST);
	ptr->next = rplist;
	rplist = ptr;
	end_critical_section(S_RPLIST);
	FreeRoomNetworkStruct(&RNCfg);
	return 1;
}

void destroy_network_queue_room(RoomProcList *rplist)
{
	struct RoomProcList *cur, *p;

	cur = rplist;
	while (cur != NULL)
	{
		p = cur->next;
		free (cur);
		cur = p;		
	}
}

void destroy_network_queue_room_locked (void)
{
	begin_critical_section(S_RPLIST);
	destroy_network_queue_room(rplist);
	end_critical_section(S_RPLIST);
}


/*
 * network_do_queue()
 * 
 * Run through the rooms doing various types of network stuff.
 */
void network_do_queue(void)
{
	static time_t last_run = 0L;
	int full_processing = 1;
	HashList *working_ignetcfg;
	HashList *the_netmap = NULL;
	int netmap_changed = 0;
	roomlists RL;
	SpoolControl *sc = NULL;
	SpoolControl *pSC;

	/*
	 * Run the full set of processing tasks no more frequently
	 * than once every n seconds
	 */
	if ( (time(NULL) - last_run) < CtdlGetConfigLong("c_net_freq") )
	{
		full_processing = 0;
		syslog(LOG_DEBUG, "network: full processing in %ld seconds.", CtdlGetConfigLong("c_net_freq") - (time(NULL)- last_run));
	}

	begin_critical_section(S_RPLIST);
	RL.rplist = rplist;
	rplist = NULL;
	end_critical_section(S_RPLIST);

	// TODO hm, check whether we have a config at all here?
	/* Load the IGnet Configuration into memory */
	working_ignetcfg = CtdlLoadIgNetCfg();

	/*
	 * Load the network map and filter list into memory.
	 */
	if (!server_shutting_down) {
		the_netmap = CtdlReadNetworkMap();
	}

	/* 
	 * Go ahead and run the queue
	 */
	if (full_processing && !server_shutting_down) {
		syslog(LOG_DEBUG, "network: loading outbound queue");
		CtdlForEachNetCfgRoom(network_queue_interesting_rooms, &RL);
	}

	if ((RL.rplist != NULL) && (!server_shutting_down)) {
		RoomProcList *ptr, *cmp;
		ptr = RL.rplist;
		syslog(LOG_DEBUG, "network: running outbound queue");
		while (ptr != NULL && !server_shutting_down) {
			
			cmp = ptr->next;
			/* filter duplicates from the list... */
			while (cmp != NULL) {
				if ((cmp->namelen > 0) &&
				    (cmp->key == ptr->key) &&
				    (cmp->namelen == ptr->namelen) &&
				    (strcmp(cmp->lcname, ptr->lcname) == 0))
				{
					cmp->namelen = 0;
				}
				cmp = cmp->next;
			}

			if (ptr->namelen > 0) {
				InspectQueuedRoom(&sc, ptr, working_ignetcfg, the_netmap);
			}
			ptr = ptr->next;
		}
	}


	pSC = sc;
	while (pSC != NULL)
	{
		network_spoolout_room(pSC);
		pSC = pSC->next;
	}

	pSC = sc;
	while (pSC != NULL)
	{
		sc = pSC->next;
		free_spoolcontrol_struct(&pSC);
		pSC = sc;
	}

	/* Save the network map back to disk */
	if (netmap_changed) {
		StrBuf *MapStr = CtdlSerializeNetworkMap(the_netmap);
		char *pMapStr = SmashStrBuf(&MapStr);
		CtdlPutSysConfig(IGNETMAP, pMapStr);
		free(pMapStr);
	}

	/* shut down. */

	DeleteHash(&the_netmap);

	DeleteHash(&working_ignetcfg);

	syslog(LOG_DEBUG, "network: queue run completed");

	if (full_processing) {
		last_run = time(NULL);
	}
	destroy_network_queue_room(RL.rplist);
}


/*
 * Module entry point
 */
CTDL_MODULE_INIT(network)
{
	if (!threading)
	{
		CtdlRegisterRoomHook(network_room_handler);
		CtdlRegisterCleanupHook(destroy_network_queue_room_locked);
		CtdlRegisterSessionHook(network_do_queue, EVT_TIMER, PRIO_QUEUE + 10);
	}
	return "network";
}
