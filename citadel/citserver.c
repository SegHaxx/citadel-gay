/* 
 * Main source module for the Citadel server
 *
 * Copyright (c) 1987-2017 by the citadel.org team
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
#if HAVE_BACKTRACE
#include <execinfo.h>
#endif
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
 * print the actual stack frame.
 */
void cit_backtrace(void)
{
#ifdef HAVE_BACKTRACE
	void *stack_frames[50];
	size_t size, i;
	char **strings;

	const char *p = IOSTR;
	if (p == NULL) p = "";
	size = backtrace(stack_frames, sizeof(stack_frames) / sizeof(void*));
	strings = backtrace_symbols(stack_frames, size);
	for (i = 0; i < size; i++) {
		if (strings != NULL) {
			syslog(LOG_DEBUG, "citserver: %s %s", p, strings[i]);
		}
		else {
			syslog(LOG_DEBUG, "citserver: %s %p", p, stack_frames[i]);
		}
	}
	free(strings);
#endif
}

void cit_oneline_backtrace(void)
{
#ifdef HAVE_BACKTRACE
	void *stack_frames[50];
	size_t size, i;
	char **strings;
	StrBuf *Buf;

	size = backtrace(stack_frames, sizeof(stack_frames) / sizeof(void*));
	strings = backtrace_symbols(stack_frames, size);
	if (size > 0)
	{
		Buf = NewStrBuf();
		for (i = 1; i < size; i++) {
			if (strings != NULL)
				StrBufAppendPrintf(Buf, "%s : ", strings[i]);
			else
				StrBufAppendPrintf(Buf, "%p : ", stack_frames[i]);
		}
		free(strings);
		syslog(LOG_DEBUG, "citserver: %s %s", IOSTR, ChrPtr(Buf));
		FreeStrBuf(&Buf);
	}
#endif
}


/*
 * print the actual stack frame.
 */
void cit_panic_backtrace(int SigNum)
{
#ifdef HAVE_BACKTRACE
	void *stack_frames[10];
	size_t size, i;
	char **strings;

	printf("caught signal 11\n");
	size = backtrace(stack_frames, sizeof(stack_frames) / sizeof(void*));
	strings = backtrace_symbols(stack_frames, size);
	for (i = 0; i < size; i++) {
		if (strings != NULL) {
			syslog(LOG_DEBUG, "%s", strings[i]);
		}
		else {
			syslog(LOG_DEBUG, "%p", stack_frames[i]);
		}
	}
	free(strings);
#endif
	exit(-1);
}

/*
 * Various things that need to be initialized at startup
 */
void master_startup(void) {
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

	/* Check floor reference counts */
	check_ref_counts();

	syslog(LOG_INFO, "Creating base rooms (if necessary)");
	CtdlCreateRoom(CtdlGetConfigStr("c_baseroom"),	0, "", 0, 1, 0, VIEW_BBS);
	CtdlCreateRoom(AIDEROOM,			3, "", 0, 1, 0, VIEW_BBS);
	CtdlCreateRoom(SYSCONFIGROOM,			3, "", 0, 1, 0, VIEW_BBS);
	CtdlCreateRoom(CtdlGetConfigStr("c_twitroom"),	0, "", 0, 1, 0, VIEW_BBS);

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
	}
	else {
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
int master_cleanup(int exitcode) {
	struct CleanupFunctionHook *fcn;
	static int already_cleaning_up = 0;

	if (already_cleaning_up) while(1) usleep(1000000);
	already_cleaning_up = 1;

	/* Run any cleanup routines registered by loadable modules */
	for (fcn = CleanupHookTable; fcn != NULL; fcn = fcn->next) {
		(*fcn->h_function_pointer)();
	}

	/* Close the AdjRefCount queue file */
	AdjRefCount(-1, 0);

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
		fflush(stdout); fflush(stderr);
		while(1) {
			sleep(32767);
		}
	}
	
	/* Now go away. */
	syslog(LOG_ERR, "citserver: Exiting with status %d", exitcode);
	fflush(stdout); fflush(stderr);
	
	if (restart_server != 0) {
		exitcode = 1;
	}
	else if ((running_as_daemon != 0) && ((exitcode == 0) )) {
		exitcode = CTDLEXIT_SHUTDOWN;
	}
	return(exitcode);
}



/*
 * returns an asterisk if there are any instant messages waiting,
 * space otherwise.
 */
char CtdlCheckExpress(void) {
	if (CC->FirstExpressMessage == NULL) {
		return(' ');
	}
	else {
		return('*');
	}
}


/*
 * Check originating host against the public_clients file.  This determines
 * whether the client is allowed to change the hostname for this session
 * (for example, to show the location of the user rather than the location
 * of the client).
 */
int CtdlIsPublicClient(void)
{
	char buf[1024];
	char addrbuf[1024];
	FILE *fp;
	int i;
	char *public_clientspos;
	char *public_clientsend;
	char *paddr = NULL;
	struct stat statbuf;
	static time_t pc_timestamp = 0;
	static char public_clients[SIZ];
	static char public_clients_file[SIZ];

#define LOCALHOSTSTR "127.0.0.1"

	snprintf(public_clients_file, sizeof public_clients_file, "%s/public_clients", ctdl_etc_dir);

	/*
	 * Check the time stamp on the public_clients file.  If it's been
	 * updated since the last time we were here (or if this is the first
	 * time we've been through the loop), read its contents and learn
	 * the IP addresses of the listed hosts.
	 */
	if (stat(public_clients_file, &statbuf) != 0) {
		/* No public_clients file exists, so bail out */
		syslog(LOG_WARNING, "Warning: '%s' does not exist", public_clients_file);
		return(0);
	}

	if (statbuf.st_mtime > pc_timestamp) {
		begin_critical_section(S_PUBLIC_CLIENTS);
		syslog(LOG_INFO, "Loading %s", public_clients_file);

		public_clientspos = &public_clients[0];
		public_clientsend = public_clientspos + SIZ;
		safestrncpy(public_clientspos, LOCALHOSTSTR, sizeof public_clients);
		public_clientspos += sizeof(LOCALHOSTSTR) - 1;
		
		if (hostname_to_dotted_quad(addrbuf, CtdlGetConfigStr("c_fqdn")) == 0) {
			*(public_clientspos++) = '|';
			paddr = &addrbuf[0];
			while (!IsEmptyStr (paddr) && 
			       (public_clientspos < public_clientsend))
				*(public_clientspos++) = *(paddr++);
		}

		fp = fopen(public_clients_file, "r");
		if (fp != NULL) 
			while ((fgets(buf, sizeof buf, fp)!=NULL) &&
			       (public_clientspos < public_clientsend)){
				char *ptr;
				ptr = buf;
				while (!IsEmptyStr(ptr)) {
					if (*ptr == '#') {
						*ptr = 0;
						break;
					}
				else ptr++;
				}
				ptr--;
				while (ptr>buf && isspace(*ptr)) {
					*(ptr--) = 0;
				}
				if (hostname_to_dotted_quad(addrbuf, buf) == 0) {
					*(public_clientspos++) = '|';
					paddr = addrbuf;
					while (!IsEmptyStr(paddr) && 
					       (public_clientspos < public_clientsend)){
						*(public_clientspos++) = *(paddr++);
					}
				}
			}
		if (fp != NULL) fclose(fp);
		pc_timestamp = time(NULL);
		end_critical_section(S_PUBLIC_CLIENTS);
	}

	syslog(LOG_DEBUG, "Checking whether %s is a local or public client", CC->cs_addr);
	for (i=0; i<num_parms(public_clients); ++i) {
		extract_token(addrbuf, public_clients, i, '|', sizeof addrbuf);
		if (!strcasecmp(CC->cs_addr, addrbuf)) {
			syslog(LOG_DEBUG, "... yes its local.");
			return(1);
		}
	}

	/* No hits.  This is not a public client. */
	syslog(LOG_DEBUG, "... no it isn't.");
	return(0);
}





void citproto_begin_session() {
	if (CC->nologin==1) {
		cprintf("%d %s: Too many users are already online (maximum is %d)\n",
			ERROR + MAX_SESSIONS_EXCEEDED,
			CtdlGetConfigStr("c_nodename"), CtdlGetConfigInt("c_maxsessions")
		);
		CC->kill_me = KILLME_MAX_SESSIONS_EXCEEDED;
	}
	else {
		cprintf("%d %s Citadel server ready.\n", CIT_OK, CtdlGetConfigStr("c_nodename"));
		CC->can_receive_im = 1;
	}
}


void citproto_begin_admin_session() {
	CC->internal_pgm = 1;
	cprintf("%d %s Citadel server ADMIN CONNECTION ready.\n", CIT_OK, CtdlGetConfigStr("c_nodename"));
}


/*
 * This loop performs all asynchronous functions.
 */
void do_async_loop(void) {
	PerformSessionHooks(EVT_ASYNC);
}

