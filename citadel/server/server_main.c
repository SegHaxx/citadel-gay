// citserver's main() function lives here.
// 
// Copyright (c) 1987-2022 by the citadel.org team
//
// This program is open source software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License version 3.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <grp.h>
#include <sys/file.h>
#include <libcitadel.h>
#include "citserver.h"
#include "modules_init.h"
#include "config.h"
#include "control.h"
#include "serv_extensions.h"
#include "citadel_dirs.h"
#include "user_ops.h"

uid_t ctdluid = 0;
const char *CitadelServiceUDS="citadel-UDS";
const char *CitadelServiceTCP="citadel-TCP";
int sanity_diag_mode = 0;


// Create or remove a lock file, so we only have one Citadel Server running at a time.
// Set 'op' to nonzero to lock, zero to unlock.
void ctdl_lockfile(int op) {
	static char lockfilename[PATH_MAX];
	static FILE *fp;

	if (op) {
		syslog(LOG_DEBUG, "main: creating lockfile");
		snprintf(lockfilename, sizeof lockfilename, "%s/citadel.lock", ctdl_run_dir);
		fp = fopen(lockfilename, "w");
		if (!fp) {
			syslog(LOG_ERR, "%s: %m", lockfilename);
			exit(CTDLEXIT_DB);
		}
		if (flock(fileno(fp), (LOCK_EX|LOCK_NB)) != 0) {
			syslog(LOG_ERR, "main: cannot lock %s (is another citserver running?)", lockfilename);
			exit(CTDLEXIT_DB);
		}
		return;
	}

	syslog(LOG_DEBUG, "main: removing lockfile");
	unlink(lockfilename);
	flock(fileno(fp), LOCK_UN);
	fclose(fp);
}


// Here's where it all begins.
int main(int argc, char **argv) {

	char facility[32];
	struct passwd pw, *pwp = NULL;
	char pwbuf[SIZ];
	int drop_root_perms = 1;
	int max_log_level = LOG_INFO;
	char *ctdldir = CTDLDIR;
	int syslog_facility = LOG_DAEMON;
	uid_t u = 0;
	struct passwd *p = NULL;
#ifdef HAVE_RUN_DIR
	struct stat filestats;
#endif

	// Tell 'em who's in da house
	syslog(LOG_INFO, " ");
	syslog(LOG_INFO, " ");
	syslog(LOG_INFO, "*** Citadel server engine ***\n");
 	syslog(LOG_INFO, "Version %d (build %s) ***", REV_LEVEL, BUILD_ID);
	syslog(LOG_INFO, "Copyright (C) 1987-2022 by the Citadel development team.");
	syslog(LOG_INFO, " ");
	syslog(LOG_INFO, "This program is open source software.  Use, duplication, or disclosure");
	syslog(LOG_INFO, "is subject to the terms of the GNU General Public License, version 3.");
	syslog(LOG_INFO, " ");
	syslog(LOG_INFO, "%s", libcitadel_version_string());

	// parse command-line arguments
	int g;
	while ((g=getopt(argc, argv, "cl:dh:x:t:B:Dru:s:")) != EOF) switch(g) {

		// test this binary for compatibility and exit
		case 'c':
			fprintf(stderr, "%s: binary compatibility confirmed\n", argv[0]);
			exit(0);
			break;

		// identify the desired syslog facility
		case 'l':
			safestrncpy(facility, optarg, sizeof(facility));
			syslog_facility = SyslogFacility(facility);
			break;

		// run in the background if -d was specified
		case 'd':
			running_as_daemon = 1;
			break;

		// specify the data directory
		case 'h':
			ctdldir = optarg;
			break;

		// identify the desired logging severity level
		case 'x':
			max_log_level = atoi(optarg);
			break;

		// deprecated flags from old versions -- ignore silently to prevent breaking scripts
		case 't':
                case 'B':
		case 'D':
			break;

		// -r tells the server not to drop root permissions.
		// Don't use this unless you know what you're doing.
		case 'r':
			drop_root_perms = 0;
			break;

		// -u tells the server what uid to run under...
		case 'u':
			u = atoi(optarg);
			if (u > 0) {
				ctdluid = u;
			}
			else {
				p = getpwnam(optarg);
				if (p) {
					u = p->pw_uid;
				}
			}
			if (u > 0) {
				ctdluid = u;
			}
			break;

		// -s tells the server to behave differently during sanity checks
		case 's':
			sanity_diag_mode = atoi(optarg);
			break;

		// any other parameter makes it crash and burn
		default:
			fprintf(stderr,	"citserver: usage: "
					"citserver "
					"[-l LogFacility] "
					"[-x MaxLogLevel] "
					"[-d] [-r] "
					"[-u user] "
					"[-h HomeDir]\n"
			);
			exit(1);
	}

	if (chdir(ctdldir) != 0) {
		syslog(LOG_ERR, "main: unable to change directory to [%s]: %m", ctdldir);
		exit(CTDLEXIT_HOME);
	}
	else {
		syslog(LOG_INFO, "main: running in data directory %s", ctdldir);
	}

	if ((ctdluid == 0) && (drop_root_perms == 0)) {
		fprintf(stderr, "citserver: cannot determine user to run as; please specify -r or -u options\n");
		exit(CTDLEXIT_UNUSER);
	}

	// Last ditch effort to determine the user name ... if there's a user called "citadel" then use that
	if (ctdluid == 0) {
		p = getpwnam("citadel");
		if (!p) {
			p = getpwnam("bbs");
		}
		if (!p) {
			p = getpwnam("guest");
		}
		if (p) {
			u = p->pw_uid;
		}
		if (u > 0) {
			ctdluid = u;
		}
	}

	// initialize the master context
	InitializeMasterCC();
	InitializeMasterTSD();

	setlogmask(LOG_UPTO(max_log_level));
	openlog("citserver",
		( running_as_daemon ? (LOG_PID) : (LOG_PID | LOG_PERROR) ),
		syslog_facility
	);

	// daemonize, if we were asked to
	if (running_as_daemon) {
		start_daemon(0);
		drop_root_perms = 1;
	}

	if ((mkdir(ctdl_run_dir, 0755) != 0) && (errno != EEXIST)) {
		syslog(LOG_ERR, "main: unable to create run directory [%s]: %m", ctdl_run_dir);
	}

	if (chown(ctdl_run_dir, ctdluid, (pwp==NULL)?-1:pw.pw_gid) != 0) {
		syslog(LOG_ERR, "main: unable to set the access rights for [%s]: %m", ctdl_run_dir);
	}

	ctdl_lockfile(1);
	init_sysdep();						// Initialize...
	master_startup();					// Do non system dependent startup functions
	check_control();					// Check/sanitize/initialize control record, fix user indexes
	syslog(LOG_INFO, "main: upgrading modules");		// Run any upgrade entry points
	pre_startup_upgrades();

	// Load the user for the masterCC or create them if they don't exist
	if (CtdlGetUser(&masterCC.user, "SYS_Citadel")) {
		// User doesn't exist. We can't use create user here as the user number needs to be 0
		strcpy(masterCC.user.fullname, "SYS_Citadel") ;
		CtdlPutUser(&masterCC.user);
		CtdlGetUser(&masterCC.user, "SYS_Citadel");	// Just to be safe
	}
	
	// Bind the server to a Unix-domain socket (user client access)
	CtdlRegisterServiceHook(0,
				file_citadel_socket,
				citproto_begin_session,
				do_command_loop,
				do_async_loop,
				CitadelServiceUDS);

	// Bind the server to a Unix-domain socket (admin client access)
	CtdlRegisterServiceHook(0,
				file_citadel_admin_socket,
				citproto_begin_admin_session,
				do_command_loop,
				do_async_loop,
				CitadelServiceUDS);
	chmod(file_citadel_admin_socket, S_IRWXU);		// protect the admin socket - it offers high privilege

	// Bind the server to our favorite TCP port (usually 504).
	CtdlRegisterServiceHook(CtdlGetConfigInt("c_port_number"),
				NULL,
				citproto_begin_session,
				do_command_loop,
				do_async_loop,
				CitadelServiceTCP);

	// Load any server-side extensions available here.
	syslog(LOG_INFO, "main: initializing server extensions");
	initialize_modules(0);

	// If we need host auth, start our chkpwd daemon.
	if (CtdlGetConfigInt("c_auth_mode") == AUTHMODE_HOST) {
		start_chkpwd_daemon();
	}

	// check, whether we're fired up another time after a crash.
	// if, post an aide message, so the admin has a chance to react.
	checkcrash();

	// Now that we've bound the sockets, change to the Citadel user id and its corresponding group ids
	if (drop_root_perms) {
		cdb_chmod_data();	// make sure we own our data files
		getpwuid_r(ctdluid, &pw, pwbuf, sizeof(pwbuf), &pwp);
		if (pwp == NULL) {
			syslog(LOG_ERR, "main: WARNING, getpwuid(%ld): %m Group IDs will be incorrect.", (long)CTDLUID);
		}
		else {
			initgroups(pw.pw_name, pw.pw_gid);
			if (setgid(pw.pw_gid)) {
				syslog(LOG_ERR, "main: setgid(%ld): %m", (long)pw.pw_gid);
			}
		}
		syslog(LOG_INFO, "main: changing uid to %ld", (long)CTDLUID);
		if (setuid(CTDLUID) != 0) {
			syslog(LOG_ERR, "main: setuid() failed: %m");
		}
#if defined (HAVE_SYS_PRCTL_H) && defined (PR_SET_DUMPABLE)
		prctl(PR_SET_DUMPABLE, 1);
#endif
	}

	// We want to check for idle sessions once per minute
	CtdlRegisterSessionHook(terminate_idle_sessions, EVT_TIMER, PRIO_CLEANUP + 1);

	// Go into multithreaded mode.  When this call exits, the server is stopping.
	go_threading();
	
	// Get ready to shut down the server.
	int exit_code = master_cleanup(exit_signal);
	ctdl_lockfile(0);
	if (restart_server) {
		syslog(LOG_INFO, "main: *** CITADEL SERVER IS RESTARTING ***");
		execv(argv[0], argv);
	}
	return(exit_code);
}
