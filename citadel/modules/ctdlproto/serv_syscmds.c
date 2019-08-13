
/* 
 * Main source module for the Citadel server
 *
 * Copyright (c) 1987-2018 by the citadel.org team
 *
 * This program is open source software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <stdio.h>
#include <libcitadel.h>

#include "serv_extensions.h"
#include "ctdl_module.h"


/*
 * Shut down the server
 */
void cmd_down(char *argbuf) {
	char *Reply ="%d Shutting down server.  Goodbye.\n";

	if (CtdlAccessCheck(ac_aide)) return;

	if (!IsEmptyStr(argbuf))
	{
		int state = CIT_OK;
		restart_server = extract_int(argbuf, 0);
		
		if (restart_server > 0) {
			Reply = "%d citserver will now shut down and automatically restart.\n";
		}
		cprintf(Reply, state);
	}
	else {
		cprintf(Reply, CIT_OK + SERVER_SHUTTING_DOWN); 
	}
	CC->kill_me = KILLME_SERVER_SHUTTING_DOWN;
	server_shutting_down = 1;
}


/*
 * Halt the server without exiting the server process.
 */
void cmd_halt(char *argbuf) {

	if (CtdlAccessCheck(ac_aide)) return;

	cprintf("%d Halting server.  Goodbye.\n", CIT_OK);
	server_shutting_down = 1;
	shutdown_and_halt = 1;
}


/*
 * Schedule or cancel a server shutdown
 */
void cmd_scdn(char *argbuf)
{
	int new_state;
	int state = CIT_OK;
	char *Reply = "%d %d\n";

	if (CtdlAccessCheck(ac_aide)) return;

	new_state = extract_int(argbuf, 0);
	if ((new_state == 2) || (new_state == 3)) {
		restart_server = 1;
		restart_server = extract_int(argbuf, 0);
		new_state -= 2;
	}
	if ((new_state == 0) || (new_state == 1)) {
		ScheduledShutdown = new_state;
	}
	cprintf(Reply, state, ScheduledShutdown);
}


/*****************************************************************************/
/*                      MODULE INITIALIZATION STUFF                          */
/*****************************************************************************/

CTDL_MODULE_INIT(syscmd)
{
	if (!threading) {
		CtdlRegisterProtoHook(cmd_down, "DOWN", "perform a server shutdown");
		CtdlRegisterProtoHook(cmd_halt, "HALT", "halt the server without exiting the server process");
		CtdlRegisterProtoHook(cmd_scdn, "SCDN", "schedule or cancel a server shutdown");
	}
        /* return our id for the Log */
	return "syscmd";
}
