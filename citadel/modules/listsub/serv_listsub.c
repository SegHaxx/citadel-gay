/*
 * This module handles self-service subscription/unsubscription to mail lists.
 *
 * Copyright (c) 2002-2016 by the citadel.org team
 *
 * This program is open source software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3.
 *  
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include "sysdep.h"
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <ctype.h>
#include <signal.h>
#include <pwd.h>
#include <errno.h>
#include <sys/types.h>
#include <dirent.h>
#include <time.h>
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
#include "clientsocket.h"
#include "ctdl_module.h"



// FIXME rewrite the subscribe-o-matic AJC 2021


/* 
 * process subscribe/unsubscribe requests and confirmations
 */
void cmd_subs(char *cmdbuf)
{
	cprintf("%d Invalid command\n", ERROR + ILLEGAL_VALUE);
}


/*
 * Module entry point
 */
CTDL_MODULE_INIT(listsub)
{
	if (!threading)
	{
		CtdlRegisterProtoHook(cmd_subs, "SUBS", "List subscribe/unsubscribe");
	}
	
	/* return our module name for the log */
	return "listsub";
}
