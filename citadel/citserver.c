/* 
 * Main source module for the Citadel server
 *
 * Copyright (c) 1987-2019 by the citadel.org team
 *
 * This program is open source software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/stat.h>
#include "sysdep.h"
#include <time.h>
#include <libcitadel.h>

#include "ctdl_module.h"
#include "housekeeping.h"
#include "locate_host.h"
#include "citserver.h"
#include "user_ops.h"
#include "control.h"
#include "config.h"

char *unique_session_numbers;
int ScheduledShutdown = 0;
time_t server_startup_time;
int panic_fd;
int openid_level_supported = 0;


/*
 * Various things that need to be initialized at startup
 */
void master_startup(void)
{
	struct timeval tv;
	unsigned int seed;
	FILE *urandom;
	struct ctdlroom qrbuf;
	int rv;
	struct passwd *pw;
	gid_t gid;

	syslog(LOG_DEBUG, "master_startup() started");
	time(&server_startup_time);

	syslog(LOG_INFO, "Checking directory access");
	if ((pw = getpwuid(ctdluid)) == NULL) {
		gid = getgid();
	} else {
		gid = pw->pw_gid;
	}

	if (create_run_directories(CTDLUID, gid) != 0) {
		syslog(LOG_EMERG, "failed to access & create directories");
		exit(1);
	}
	syslog(LOG_INFO, "Opening databases");
	open_databases();

	/* Load site-specific configuration */
	syslog(LOG_INFO, "Initializing configuration system");
	initialize_config_system();
	validate_config();
	migrate_legacy_control_record();

	// If we have an existing database that is older than version 928, reindex the user records.
	// Unfortunately we cannot do this in serv_upgrade.c because it needs to happen VERY early during startup.
	int existing_db = CtdlGetConfigInt("MM_hosted_upgrade_level");
	if ( (existing_db > 0) && (existing_db < 928) ) {
		ForEachUser(reindex_user_928, NULL);
	}

	/* Check floor reference counts */
	check_ref_counts();

	syslog(LOG_INFO, "Creating base rooms (if necessary)");
	CtdlCreateRoom(CtdlGetConfigStr("c_baseroom"), 0, "", 0, 1, 0, VIEW_BBS);
	CtdlCreateRoom(AIDEROOM, 3, "", 0, 1, 0, VIEW_BBS);
	CtdlCreateRoom(SYSCONFIGROOM, 3, "", 0, 1, 0, VIEW_BBS);
	CtdlCreateRoom(CtdlGetConfigStr("c_twitroom"), 0, "", 0, 1, 0, VIEW_BBS);

	/* The "Local System Configuration" room doesn't need to be visible */
	if (CtdlGetRoomLock(&qrbuf, SYSCONFIGROOM) == 0) {
		qrbuf.QRflags2 |= QR2_SYSTEM;
		CtdlPutRoomLock(&qrbuf);
	}

	/* Aide needs to be public postable, else we're not RFC conformant. */
	if (CtdlGetRoomLock(&qrbuf, AIDEROOM) == 0) {
		qrbuf.QRflags2 |= QR2_SMTP_PUBLIC;
		CtdlPutRoomLock(&qrbuf);
	}

	syslog(LOG_INFO, "Seeding the pseudo-random number generator...");
	urandom = fopen("/dev/urandom", "r");
	if (urandom != NULL) {
		rv = fread(&seed, sizeof seed, 1, urandom);
		if (rv == -1) {
			syslog(LOG_ERR, "citserver: failed to read random seed: %m");
		}
		fclose(urandom);
	} else {
		gettimeofday(&tv, NULL);
		seed = tv.tv_usec;
	}
	srand(seed);
	srandom(seed);

	syslog(LOG_DEBUG, "master_startup() finished");
}


/*
 * Cleanup routine to be called when the server is shutting down.  Returns the needed exit code.
 */
int master_cleanup(int exitcode)
{
	struct CleanupFunctionHook *fcn;
	static int already_cleaning_up = 0;

	if (already_cleaning_up) {
		while (1) {
			usleep(1000000);
		}
	}
	already_cleaning_up = 1;

	/* Run any cleanup routines registered by loadable modules */
	for (fcn = CleanupHookTable; fcn != NULL; fcn = fcn->next) {
		(*fcn->h_function_pointer) ();
	}

	/* Do system-dependent stuff */
	sysdep_master_cleanup();

	/* Close the configuration system */
	shutdown_config_system();

	/* Close databases */
	syslog(LOG_INFO, "Closing databases\n");
	close_databases();

	/* If the operator requested a halt but not an exit, halt here. */
	if (shutdown_and_halt) {
		syslog(LOG_ERR, "citserver: Halting server without exiting.");
		fflush(stdout);
		fflush(stderr);
		while (1) {
			sleep(32767);
		}
	}

	/* Now go away. */
	syslog(LOG_ERR, "citserver: Exiting with status %d", exitcode);
	fflush(stdout);
	fflush(stderr);

	if (restart_server != 0) {
		exitcode = 1;
	} else if ((running_as_daemon != 0) && ((exitcode == 0))) {
		exitcode = CTDLEXIT_SHUTDOWN;
	}
	return (exitcode);
}


/*
 * returns an asterisk if there are any instant messages waiting,
 * space otherwise.
 */
char CtdlCheckExpress(void)
{
	if (CC->FirstExpressMessage == NULL) {
		return (' ');
	} else {
		return ('*');
	}
}


void citproto_begin_session()
{
	if (CC->nologin == 1) {
		cprintf("%d %s: Too many users are already online (maximum is %d)\n",
			ERROR + MAX_SESSIONS_EXCEEDED, CtdlGetConfigStr("c_nodename"), CtdlGetConfigInt("c_maxsessions")
		    );
		CC->kill_me = KILLME_MAX_SESSIONS_EXCEEDED;
	} else {
		cprintf("%d %s Citadel server ready.\n", CIT_OK, CtdlGetConfigStr("c_nodename"));
		CC->can_receive_im = 1;
	}
}


void citproto_begin_admin_session()
{
	CC->internal_pgm = 1;
	cprintf("%d %s Citadel server ADMIN CONNECTION ready.\n", CIT_OK, CtdlGetConfigStr("c_nodename"));
}


/*
 * This loop performs all asynchronous functions.
 */
void do_async_loop(void)
{
	PerformSessionHooks(EVT_ASYNC);
}
