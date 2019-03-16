/*
 * This module implements server commands related to the display and
 * manipulation of the "Who's online" list.
 *
 * Copyright (c) 1987-2019 by the citadel.org team
 *
 * This program is open source software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include "sysdep.h"
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <signal.h>
#include <pwd.h>
#include <errno.h>
#include <sys/types.h>

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

#include <sys/wait.h>
#include <string.h>
#include <limits.h>
#include <libcitadel.h>
#include "citadel.h"
#include "server.h"
#include "citserver.h"
#include "support.h"
#include "config.h"
#include "control.h"
#include "user_ops.h"
#include "database.h"
#include "msgbase.h"


#include "ctdl_module.h"

/* Don't show the names of private rooms unless the viewing
 * user also knows the rooms.
 */
void GenerateRoomDisplay(char *real_room,
			CitContext *viewed,
			CitContext *viewer) {

	int ra;

	strcpy(real_room, viewed->room.QRname);
	if (viewed->room.QRflags & QR_MAILBOX) {
		strcpy(real_room, &real_room[11]);
	}
	if (viewed->room.QRflags & QR_PRIVATE) {
		CtdlRoomAccess(&viewed->room, &viewer->user, &ra, NULL);
		if ( (ra & UA_KNOWN) == 0) {
			strcpy(real_room, " ");
		}
	}

	if (viewed->cs_flags & CS_CHAT) {
		while (strlen(real_room) < 14) {
			strcat(real_room, " ");
		}
		strcpy(&real_room[14], "<chat>");
	}

}



/*
 * display who's online
 */
void cmd_rwho(char *argbuf) {
	struct CitContext *nptr;
	int nContexts, i;
	int spoofed = 0;
	int aide;
	char room[ROOMNAMELEN];
	char flags[5];
	
	/* So that we don't keep the context list locked for a long time
	 * we create a copy of it first
	 */
	nptr = CtdlGetContextArray(&nContexts) ;
	if (!nptr)
	{
		/* Couldn't malloc so we have to bail but stick to the protocol */
		cprintf("%d%c \n", LISTING_FOLLOWS, CtdlCheckExpress() );
		cprintf("000\n");
		return;
	}
	
	aide = ( (CC->user.axlevel >= AxAideU) || (CC->internal_pgm) ) ;
	cprintf("%d%c \n", LISTING_FOLLOWS, CtdlCheckExpress() );
	
	for (i=0; i<nContexts; i++)  {
		flags[0] = '\0';
		spoofed = 0;
		
		if (!aide && nptr[i].state == CON_SYS)
			continue;

		if (!aide && nptr[i].kill_me != 0)
			continue;

		if (nptr[i].cs_flags & CS_POSTING) {
			strcat(flags, "*");
		}
		else {
			strcat(flags, ".");
		}
		   
		GenerateRoomDisplay(room, &nptr[i], CC);

                if ((aide) && (spoofed)) {
                	strcat(flags, "+");
		}
		
		if ((nptr[i].cs_flags & CS_STEALTH) && (aide)) {
			strcat(flags, "-");
		}
		
		if (((nptr[i].cs_flags&CS_STEALTH)==0) || (aide)) {

			cprintf("%d|%s|%s|%s|%s|%ld|%s|%s|",
				nptr[i].cs_pid, nptr[i].curr_user, room,
				nptr[i].cs_host, nptr[i].cs_clientname,
				(long)(nptr[i].lastidle),
				nptr[i].lastcmdname, flags
			);

			cprintf("|");	// no spoofed user names anymore
			cprintf("|");	// no spoofed room names anymore
			cprintf("|");	// no spoofed host names anymore
	
			cprintf("%d\n", nptr[i].logged_in);
		}
	}
	
	/* release our copy of the context list */
	free(nptr);

	/* Now it's magic time.  Before we finish, call any EVT_RWHO hooks
	 * so that external paging modules such as serv_icq can add more
	 * content to the Wholist.
	 */
	PerformSessionHooks(EVT_RWHO);
	cprintf("000\n");
}


/*
 * enter or exit "stealth mode"
 */
void cmd_stel(char *cmdbuf)
{
	int requested_mode;

	requested_mode = extract_int(cmdbuf,0);

	if (CtdlAccessCheck(ac_logged_in)) return;

	if (requested_mode == 1) {
		CC->cs_flags = CC->cs_flags | CS_STEALTH;
		PerformSessionHooks(EVT_STEALTH);
	}
	if (requested_mode == 0) {
		CC->cs_flags = CC->cs_flags & ~CS_STEALTH;
		PerformSessionHooks(EVT_UNSTEALTH);
	}

	cprintf("%d %d\n", CIT_OK,
		((CC->cs_flags & CS_STEALTH) ? 1 : 0) );
}


CTDL_MODULE_INIT(rwho)
{
	if(!threading)
	{
	        CtdlRegisterProtoHook(cmd_rwho, "RWHO", "Display who is online");
	        CtdlRegisterProtoHook(cmd_stel, "STEL", "Enter/exit stealth mode");
		//CtdlRegisterSessionHook(dead_io_check, EVT_TIMER, PRIO_QUEUE + 50);

	}
	
	/* return our module name for the log */
        return "rwho";
}
