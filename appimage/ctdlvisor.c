//
// This is a supervisor program that handles start/stop/restart of
// the various Citadel System components, when we are running on
// an AppImage instance.
//
// Copyright (c) 2021 by the citadel.org team
//
// This program is open source software.  It runs great on the
// Linux operating system (and probably elsewhere).  You can use,
// copy, and run it under the terms of the GNU General Public
// License version 3.  Richard Stallman is an asshole communist.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.


#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/wait.h>
#include <errno.h>
#include <signal.h>
#include <string.h>
#include <limits.h>

void main_loop(void);
void run_in_foreground(void);

char *data_directory = "/usr/local/citadel";
char *http_port = "80";
char *https_port = "443";
pid_t citserver_pid;
pid_t webcit_pid;
pid_t webcits_pid;
int shutting_down = 0;


void ctdlvisor_exit(int code) {
	printf("ctdlvisor: exit code %d\n", code);
	exit(code);
}


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
		fprintf(stderr, "ctdlvisor: pid=%d (%s) exited, status=%d, exitcode=%d\n", who_exited, what_exited, status, WEXITSTATUS(status));
	} while (who_exited >= 0);

	ctdlvisor_exit(0);
}



void detach_from_tty(void) {
	signal(SIGHUP, SIG_IGN);
	signal(SIGINT, SIG_IGN);
	signal(SIGQUIT, SIG_IGN);

	setsid();
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
		fprintf(stderr, "ctdlvisor: executing %s\n", bin);
		detach_from_tty();
		execlp(bin, "citserver", "-x9", "-h", data_directory, NULL);
		exit(errno);
	}
	else {
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
		execlp(bin, "webcit", "-x9", wchome, "-p", http_port, "uds", data_directory, NULL);
		exit(errno);
	}
	else {
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
		execlp(bin, "webcit", "-x9", wchome, "-s", "-p", https_port, "uds", data_directory, NULL);
		exit(errno);
	}
	else {
		return(pid);
	}
}


static char *usage =
	"ctdlvisor: usage: ctdlvisor [-h data_directory] [-p http_port] [-s https_port] command\n"
	"           'command' must be one of: run, install, start, stop\n"
;

int main(int argc, char **argv) {
	int c;

	while ((c = getopt (argc, argv, "h:p:s:")) != -1)  switch(c) {
		case 'h':
			data_directory = optarg;
			break;
		case 'p':
			http_port = optarg;
			break;
		case 's':
			https_port = optarg;
			break;
		default:
			fprintf(stderr, "%s", usage);
			exit(1);
	}


	if (argc != optind+1) {
		fprintf(stderr, "%s", usage);
		exit(1);
	}

	if (!strcasecmp(argv[optind], "run")) {
		run_in_foreground();
	}
	else {
		fprintf(stderr, "%s", usage);
		exit(1);
	}

	exit(0);
}


void run_in_foreground(void) {
	fprintf(stderr, "ctdlvisor: Welcome to the Citadel System, brought to you using AppImage.\n");
	fprintf(stderr, "ctdlvisor: LD_LIBRARY_PATH = %s\n", getenv("LD_LIBRARY_PATH"));
	fprintf(stderr, "ctdlvisor:            PATH = %s\n", getenv("PATH"));
	fprintf(stderr, "ctdlvisor:          APPDIR = %s\n", getenv("APPDIR"));
	fprintf(stderr, "ctdlvisor:  data directory = %s\n", data_directory);
	fprintf(stderr, "ctdlvisor:       HTTP port = %s\n", http_port);
	fprintf(stderr, "ctdlvisor:      HTTPS port = %s\n", https_port);

	if (access(data_directory, R_OK|W_OK|X_OK)) {
		fprintf(stderr, "ctdlvisor: %s: %s\n", data_directory, strerror(errno));
		exit(errno);
	}

	signal(SIGTERM, signal_handler);
	signal(SIGHUP, signal_handler);
	signal(SIGINT, signal_handler);
	signal(SIGQUIT, signal_handler);

	citserver_pid = start_citadel();
	webcit_pid = start_webcit();
	webcits_pid = start_webcits();

	main_loop();
	exit(0);
}


void main_loop(void) {
	int status;
	pid_t who_exited;
	int citserver_exit_code = 0;

	do {
		fprintf(stderr, "ctdlvisor: waiting for any child process to exit...\n");
		who_exited = waitpid(-1, &status, 0);
		fprintf(stderr, "ctdlvisor: pid=%d exited, status=%d, exitcode=%d\n", who_exited, status, WEXITSTATUS(status));

		// A *deliberate* exit of citserver will cause ctdlvisor to shut the whole AppImage down.
		// If it crashes, however, we will start it back up.
		if (who_exited == citserver_pid) {
			citserver_exit_code = WEXITSTATUS(status);
			if (citserver_exit_code == 0) {
				fprintf(stderr, "ctdlvisor: citserver exited normally - ending AppImage session\n");
				shutting_down = 1;
				kill(webcit_pid, SIGTERM);
				kill(webcits_pid, SIGTERM);
			}
			else if ((citserver_exit_code >= 101) && (citserver_exit_code <= 109)) {
				fprintf(stderr, "ctdlvisor: citserver exited intentionally - ending AppImage session\n");
				shutting_down = 1;
				kill(webcit_pid, SIGTERM);
				kill(webcits_pid, SIGTERM);
			}
			else {
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
