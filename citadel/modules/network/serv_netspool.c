/*
 * This module handles shared rooms, inter-Citadel mail, and outbound
 * mailing list processing.
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
 *
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
#include <assert.h>
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


/*
 * Module entry point
 */
CTDL_MODULE_INIT(network_spool)
{
	if (!threading)
	{
		//CtdlREGISTERRoomCfgType(subpending,       ParseSubPendingLine,   0, 5, SerializeGeneric,  DeleteGenericCfgLine);
		//CtdlREGISTERRoomCfgType(unsubpending,     ParseUnSubPendingLine, 0, 4, SerializeGeneric,  DeleteGenericCfgLine);
		//CtdlREGISTERRoomCfgType(lastsent,         ParseLastSent,         1, 1, SerializeLastSent, DeleteLastSent);
		//CtdlREGISTERRoomCfgType(listrecp,         ParseGeneric,          0, 1, SerializeGeneric,  DeleteGenericCfgLine);
		//CtdlREGISTERRoomCfgType(digestrecp,       ParseGeneric,          0, 1, SerializeGeneric,  DeleteGenericCfgLine);
		//CtdlREGISTERRoomCfgType(participate,      ParseGeneric,          0, 1, SerializeGeneric,  DeleteGenericCfgLine);
		//CtdlREGISTERRoomCfgType(roommailalias,    ParseRoomAlias,        0, 1, SerializeGeneric,  DeleteGenericCfgLine);
	}
	return "network_spool";
}
