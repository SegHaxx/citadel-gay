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


pid_t start_citadel() {
	char bin[1024];
	sprintf(bin, "%s/usr/local/citadel/citserver", getenv("APPDIR"));
	pid_t pid = fork();
	if (pid == 0) {
		printf("Executing %s\n", bin);
		execlp(bin, "citserver", "-x9", "-h/usr/local/citadel", NULL);
		perror("execlp");
		exit(errno);
	}
	else {
		return(pid);
	}
}


pid_t start_webcit() {
	char bin[1024];
	sprintf(bin, "%s/usr/local/webcit/webcit", getenv("APPDIR"));
	pid_t pid = fork();
	if (pid == 0) {
		printf("Executing %s\n", bin);
		execlp(bin, "webcit", "-x9", "-p80", "uds", "/usr/local/citadel", NULL);
		perror("execlp");
		exit(errno);
	}
	else {
		return(pid);
	}
}


pid_t start_webcits() {
	char bin[1024];
	sprintf(bin, "%s/usr/local/webcit/webcit", getenv("APPDIR"));
	pid_t pid = fork();
	if (pid == 0) {
		printf("Executing %s\n", bin);
		execlp(bin, "webcit", "-x9", "-s", "-p443", "uds", "/usr/local/citadel", NULL);
		perror("execlp");
		exit(errno);
	}
	else {
		return(pid);
	}
}


main() {
	int status;
	pid_t who_exited;

	pid_t citserver_pid = start_citadel();
	pid_t webcit_pid = start_webcit();
	pid_t webcits_pid = start_webcits();

	do {
		printf("LD_LIBRARY_PATH = %s\n", getenv("LD_LIBRARY_PATH"));
		printf("PATH = %s\n", getenv("PATH"));
		printf("APPDIR = %s\n", getenv("APPDIR"));

		printf("waiting...\n");
		who_exited = waitpid(-1, &status, 0);
		printf("pid=%d exited, status=%d\n", who_exited, status);

		if (who_exited == citserver_pid) {
			if (WEXITSTATUS(status) == 0) {
				printf("ctdlvisor: citserver exited normally - ending AppImage session\n");
				exit(0);
			}
			citserver_pid = start_citadel();
		}

		if (who_exited == webcit_pid)		webcit_pid = start_webcit();
		if (who_exited == webcits_pid)		webcits_pid = start_webcits();

		sleep(1);				// slow down any accidental loops

	} while (who_exited >= 0);

	printf("ctdlvisor: exiting.\n");
	exit(0);
}
