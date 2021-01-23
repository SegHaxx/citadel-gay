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

char *data_directory = "/usr/local/citadel";
char *http_port = "80";
char *https_port = "443";
pid_t citserver_pid;
pid_t webcit_pid;
pid_t webcits_pid;


void signal_handler(int signal) {
	fprintf(stderr, "ctdlvisor: caught signal %d", signal);

	int status;
	pid_t who_exited;
	char *what_exited = NULL;

	do {
		fprintf(stderr, "ctdlvisor: waiting for any child process to exit...\n");
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

	printf("ctdlvisor: exiting from signal catcher.\n");
	exit(0);
}



pid_t start_citadel() {
	char bin[1024];
	sprintf(bin, "%s/usr/local/citadel/citserver", getenv("APPDIR"));
	pid_t pid = fork();
	if (pid == 0) {
		fprintf(stderr, "ctdlvisor: executing %s\n", bin);
		close(0); close(1); close(2);
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
		close(0); close(1); close(2);
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
		close(0); close(1); close(2);
		execlp(bin, "webcit", "-x9", wchome, "-s", "-p", https_port, "uds", data_directory, NULL);
		exit(errno);
	}
	else {
		return(pid);
	}
}


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
			fprintf(stderr, "ctdlvisor: usage: ctdlvisor [-h data_directory] [-p http_port] [-s https_port]\n");
			exit(1);
	}

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
}


void main_loop(void) {
	int status;
	pid_t who_exited;
	int shutting_down = 0;
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

	printf("ctdlvisor: exit code %d\n", citserver_exit_code);
	exit(citserver_exit_code);
}
