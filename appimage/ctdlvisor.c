// This is a supervisor program that handles start/stop/restart of
// the various Citadel System components, when we are running on
// an AppImage instance.
//
// Copyright (c) 2021 by the citadel.org team
//
// This program is open source software.  Use, duplication, or disclosure
// is subject to the terms of the GNU General Public License, version 3.
// The program is distributed without any warranty, expressed or implied.

#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/wait.h>
#include <errno.h>
#include <signal.h>
#include <string.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/stat.h>

pid_t citserver_pid;
pid_t webcit_pid;
pid_t webcits_pid;
int shutting_down = 0;

// Call this instead of exit() just for common diagnostics etc.
void ctdlvisor_exit(int code) {
	printf("ctdlvisor: exit code %d\n", code);
	exit(code);
}


// Interrupting this program with a signal will begin an orderly shutdown.
void signal_handler(int signal) {
	fprintf(stderr, "ctdlvisor: caught signal %d\n", signal);

	while(shutting_down) {
		fprintf(stderr, "ctdlvisor: already shutting down\n");
		sleep(1);
	}

	int status;
	pid_t who_exited;
	char *what_exited = NULL;

	shutting_down = 1;
	kill(citserver_pid, SIGTERM);
	kill(webcit_pid, SIGTERM);
	kill(webcits_pid, SIGTERM);
	do {
		fprintf(stderr, "ctdlvisor: waiting for all child process to exit...\n");
		who_exited = waitpid(-1, &status, 0);
		if (who_exited == citserver_pid) {
			what_exited = "Citadel Server";
		}
		else if (who_exited == webcit_pid) {
			what_exited = "WebCit HTTP";
		}
		else if (who_exited == webcits_pid) {
			what_exited = "WebCit HTTPS";
		}
		else {
			what_exited = "unknown";
		}
		if (who_exited >= 0) {
			fprintf(stderr, "ctdlvisor: %d (%s) ended, status=%d\n", who_exited, what_exited, status);
		}
	} while (who_exited >= 0);
	ctdlvisor_exit(0);
}


void detach_from_tty(void) {
	signal(SIGHUP, SIG_IGN);
	signal(SIGINT, SIG_IGN);
	signal(SIGQUIT, SIG_IGN);

	setsid();	// become our own process group leader
	umask(0);
	if (    (freopen("/dev/null", "r", stdin) != stdin) ||
		(freopen("/dev/null", "w", stdout) != stdout) ||
		(freopen("/dev/null", "w", stderr) != stderr)
	) {
		fprintf(stderr, "sysdep: unable to reopen stdio: %s\n", strerror(errno));
	}
}


pid_t start_citadel() {
	char bin[1024];
	sprintf(bin, "%s/usr/local/citadel/citserver", getenv("APPDIR"));
	pid_t pid = fork();
	if (pid == 0) {
		fprintf(stderr, "ctdlvisor: executing %s with data directory %s\n", bin, getenv("CTDL_DIR"));
		detach_from_tty();
		execlp(bin, "citserver", "-x9", "-h", getenv("CTDL_DIR"), NULL);
		exit(errno);
	}
	else {
		fprintf(stderr, "ctdlvisor: citserver running on pid=%d\n", pid);
		return(pid);
	}
}


pid_t start_webcit() {
	char bin[1024];
	sprintf(bin, "%s/usr/local/webcit/webcit", getenv("APPDIR"));
	char wchome[1024];
	sprintf(wchome, "-h%s/usr/local/webcit", getenv("APPDIR"));
	pid_t pid = fork();
	if (pid == 0) {
		fprintf(stderr, "ctdlvisor: executing %s\n", bin);
		detach_from_tty();
		execlp(bin, "webcit", "-x9", wchome, "-p", getenv("HTTP_PORT"), "uds", getenv("CTDL_DIR"), NULL);
		exit(errno);
	}
	else {
		fprintf(stderr, "ctdlvisor: webcit (HTTP) running on pid=%d\n", pid);
		return(pid);
	}
}


pid_t start_webcits() {
	char bin[1024];
	sprintf(bin, "%s/usr/local/webcit/webcit", getenv("APPDIR"));
	char wchome[1024];
	sprintf(wchome, "-h%s/usr/local/webcit", getenv("APPDIR"));
	pid_t pid = fork();
	if (pid == 0) {
		fprintf(stderr, "ctdlvisor: executing %s\n", bin);
		detach_from_tty();
		execlp(bin, "webcit", "-x9", wchome, "-s", "-p", getenv("HTTPS_PORT"), "uds", getenv("CTDL_DIR"), NULL);
		exit(errno);
	}
	else {
		fprintf(stderr, "ctdlvisor: webcit (HTTPS) running on pid=%d\n", pid);
		return(pid);
	}
}


void main_loop(void) {
	int status;
	pid_t who_exited;
	int citserver_exit_code = 0;

	do {
		who_exited = waitpid(-1, &status, 0);
		fprintf(stderr, "ctdlvisor: pid=%d exited, status=%d, exitcode=%d\n", who_exited, status, WEXITSTATUS(status));

		// A *deliberate* exit of citserver will cause ctdlvisor to shut the whole AppImage down.
		// If it crashes, however, we will start it back up.
		if (who_exited == citserver_pid) {
			citserver_exit_code = WEXITSTATUS(status);
			if ((WIFEXITED(status)) && (citserver_exit_code == 0)) {
				fprintf(stderr, "ctdlvisor: citserver exited normally - ending AppImage session\n");
				shutting_down = 1;
				kill(webcit_pid, SIGTERM);
				kill(webcits_pid, SIGTERM);
			}
			else if ((WIFEXITED(status)) && (citserver_exit_code >= 101) && (citserver_exit_code <= 109)) {
				fprintf(stderr, "ctdlvisor: citserver exited intentionally - ending AppImage session\n");
				shutting_down = 1;
				kill(webcit_pid, SIGTERM);
				kill(webcits_pid, SIGTERM);
			}
			else {
				if (WIFSIGNALED(status)) {
					fprintf(stderr, "ctdlvisor: citserver crashed on signal %d\n", WTERMSIG(status));
				}
				citserver_pid = start_citadel();
			}
		}

		// WebCit processes are restarted if they exit for any reason.
		if ((who_exited == webcit_pid) && (!shutting_down))	webcit_pid = start_webcit();
		if ((who_exited == webcits_pid)	&& (!shutting_down))	webcits_pid = start_webcits();

		// If we somehow end up in an endless loop, at least slow it down.
		sleep(1);

	} while (who_exited >= 0);
	ctdlvisor_exit(citserver_exit_code);
}


void install_client_link(void) {

	FILE *fp;
	char path_to_link[PATH_MAX];
	snprintf(path_to_link, sizeof path_to_link, "%s/citadel", getenv("CTDL_DIR"));
	fp = fopen(path_to_link, "w");
	if (!fp) {
		fprintf(stderr, "%s\n", strerror(errno));
		return;
	}

	fprintf(fp,	"#!/bin/bash\n"
			"export APPDIR=%s\n"
			"export LD_LIBRARY_PATH=${APPDIR}/usr/bin:$LD_LIBRARY_PATH\n"
			"export PATH=${APPDIR}/usr/bin:$PATH\n"
			"exec citadel\n"
	,
			getenv("APPDIR")
	);

	fchmod(fileno(fp), 0755);
	fclose(fp);
}


int main(int argc, char **argv) {
	int a;
	int migrate_mode = 0;

	if (getenv("APPDIR") == NULL) {
		fprintf(stderr, "ctdlvisor: APPDIR is not set.  This program must be run from within an AppImage.\n");
		ctdlvisor_exit(1);
	}

	/* parse command-line arguments */
	while ((a=getopt(argc, argv, "cm")) != EOF) switch(a) {

		// test this binary for compatibility and exit
		case 'c':
			fprintf(stderr, "%s: binary compatibility confirmed\n", argv[0]);
			exit(0);
			break;

		// run ctdlmigrate only
		case 'm':
			migrate_mode = 1;
			break;

		// any other parameter makes it crash and burn
		default:
			fprintf(stderr,	"usage\n");
			exit(1);
	}

	fprintf(stderr, "ctdlvisor: Welcome to the Citadel System, brought to you using AppImage.\n");
	fprintf(stderr, "ctdlvisor: LD_LIBRARY_PATH = %s\n", getenv("LD_LIBRARY_PATH"));
	fprintf(stderr, "ctdlvisor:            PATH = %s\n", getenv("PATH"));
	fprintf(stderr, "ctdlvisor:          APPDIR = %s\n", getenv("APPDIR"));
	fprintf(stderr, "ctdlvisor:  data directory = %s\n", getenv("CTDL_DIR"));
	fprintf(stderr, "ctdlvisor:       HTTP port = %s\n", getenv("HTTP_PORT"));
	fprintf(stderr, "ctdlvisor:      HTTPS port = %s\n", getenv("HTTPS_PORT"));

	if (access(getenv("CTDL_DIR"), R_OK|W_OK|X_OK)) {
		fprintf(stderr, "ctdlvisor: %s: %s\n", getenv("CTDL_DIR"), strerror(errno));
		ctdlvisor_exit(errno);
	}

	signal(SIGHUP, signal_handler);

	// "migrate mode" means we just start the server and then run ctdlmigrate interactively.
	if (migrate_mode) {
		citserver_pid = start_citadel();
		fprintf(stderr, "ctdlvisor: waiting a moment for citserver to initialize...\n");
		sleep(5);
		char bin[1024];
		sprintf(bin, "%s/usr/local/citadel/ctdlmigrate -h %s", getenv("APPDIR"), getenv("CTDL_DIR"));
		system(bin);
		kill(citserver_pid, SIGTERM);
	}

	// Otherwise, it's just a normal happy day in Citadel land.
	else {
		signal(SIGTERM, signal_handler);
		signal(SIGINT, signal_handler);
		signal(SIGQUIT, signal_handler);
	
		citserver_pid = start_citadel();		// start Citadel Server
		webcit_pid = start_webcit();			// start WebCit HTTP
		webcits_pid = start_webcits();			// start WebCit HTTPS

		install_client_link();
	
		main_loop();
	}

	ctdlvisor_exit(0);
}
