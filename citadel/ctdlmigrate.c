/*
 * Across-the-wire migration utility for Citadel
 *
 * Copyright (c) 2009-2021 citadel.org
 *
 * This program is open source software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <signal.h>
#include <netdb.h>
#include <errno.h>
#include <limits.h>
#include <pwd.h>
#include <time.h>
#include <readline/readline.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <libcitadel.h>
#include "citadel.h"
#include "axdefs.h"
#include "sysdep.h"
#include "config.h"
#include "citadel_dirs.h"



// yeah, this wouldn't work in a multithreaded program.
static int gmaxlen = 0;
static char *gdeftext = NULL;

static int limit_rl(FILE *dummy) {
	if (rl_end > gmaxlen) {
		return '\b';
	}
	return rl_getc(dummy);
}


static int getz_deftext(void) {
	if (gdeftext) {
		rl_insert_text(gdeftext);
		gdeftext = NULL;
		rl_startup_hook = NULL;
	}
	return 0;
}


/*
 * Replacement for gets() that doesn't throw a compiler warning.
 * We're only using it for some simple prompts, so we don't need
 * to worry about attackers exploiting it.
 */
void getz(char *buf, int maxlen, char *default_value, char *prompt) {
	rl_startup_hook = getz_deftext;
	rl_getc_function = limit_rl;
	gmaxlen = maxlen;
	gdeftext = default_value;
	strcpy(buf, readline(prompt));
}


// These variables are used by both main() and ctdlmigrate_exit()
// They are global so that ctdlmigrate_exit can be called from a signal handler
FILE *sourcefp = NULL;
char socket_path[PATH_MAX];
pid_t sshpid = (-1);

void ctdlmigrate_exit(int cmdexit) {
	if (sourcefp) {
	 	printf("Closing the data connection from the source system...\n");
		pclose(sourcefp);
	}
	unlink(socket_path);
	if (sshpid > 0) {
 		printf("Shutting down the SSH session...\n");
		kill(sshpid, SIGKILL);
	}
	printf("\n\n\033[3%dmExit code %d\033[0m\n", (cmdexit ? 1 : 2), cmdexit);
	exit(cmdexit);
}


int uds_connectsock(char *sockpath) {
	int s;
	struct sockaddr_un addr;

	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	strcpy(addr.sun_path, sockpath);

	s = socket(AF_UNIX, SOCK_STREAM, 0);
	if (s < 0) {
		fprintf(stderr, "ctdlmigrate: Can't create socket: %s\n", strerror(errno));
		ctdlmigrate_exit(3);
	}

	if (connect(s, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
		fprintf(stderr, "ctdlmigrate: can't connect: %s\n", strerror(errno));
		close(s);
		ctdlmigrate_exit(3);
	}

	return s;
}


/*
 * input binary data from socket
 */
void serv_read(int serv_sock, char *buf, int bytes) {
	int len, rlen;

	len = 0;
	while (len < bytes) {
		rlen = read(serv_sock, &buf[len], bytes - len);
		if (rlen < 1) {
			return;
		}
		len = len + rlen;
	}
}


/*
 * send binary to server
 */
void serv_write(int serv_sock, char *buf, int nbytes) {
	int bytes_written = 0;
	int retval;
	while (bytes_written < nbytes) {
		retval = write(serv_sock, &buf[bytes_written], nbytes - bytes_written);
		if (retval < 1) {
			return;
		}
		bytes_written = bytes_written + retval;
	}
}


/*
 * input string from socket - implemented in terms of serv_read()
 */
void serv_gets(int serv_sock, char *buf) {
	int i;

	/* Read one character at a time.
	 */
	for (i = 0;; i++) {
		serv_read(serv_sock, &buf[i], 1);
		if (buf[i] == '\n' || i == (SIZ-1))
			break;
	}

	/* If we got a long line, discard characters until the newline.
	 */
	if (i == (SIZ-1)) {
		while (buf[i] != '\n') {
			serv_read(serv_sock, &buf[i], 1);
		}
	}

	/* Strip all trailing nonprintables (crlf)
	 */
	buf[i] = 0;
}


/*
 * send line to server - implemented in terms of serv_write()
 */
void serv_puts(int serv_sock, char *buf) {
	serv_write(serv_sock, buf, strlen(buf));
	serv_write(serv_sock, "\n", 1);
}


int main(int argc, char *argv[]) {
	char ctdldir[PATH_MAX]=CTDLDIR;
	char yesno[5];
	int cmdexit = 0;				// when something fails, set cmdexit to nonzero, and skip to the end
	char cmd[PATH_MAX];
	char buf[PATH_MAX];
	char remote_user[SIZ];
	char remote_host[SIZ];
	char remote_sendcommand[PATH_MAX];
	int linecount = 0;
	int a;
	int local_admin_socket = (-1);
	int progress = 0;

	/* Parse command line */
	while ((a = getopt(argc, argv, "h:")) != EOF) {
		switch (a) {
		case 'h':
			strcpy(ctdldir, optarg);
			break;
		default:
			fprintf(stderr, "ctdlmigrate: usage: ctdlmigrate [-h server_dir]\n");
			return(1);
		}
	}

	if (chdir(ctdldir) != 0) {
		fprintf(stderr, "ctdlmigrate: %s: %s\n", ctdldir, strerror(errno));
		exit(errno);
	}

	signal(SIGINT, ctdlmigrate_exit);
	signal(SIGQUIT, ctdlmigrate_exit);
	signal(SIGTERM, ctdlmigrate_exit);
	signal(SIGSEGV, ctdlmigrate_exit);
	signal(SIGHUP, ctdlmigrate_exit);
	signal(SIGPIPE, ctdlmigrate_exit);

	printf(	"\033[2J\033[H\n"
		"          \033[32m╔═══════════════════════════════════════════════╗\n"
		"          ║                                               ║\n"
		"          ║    \033[33mCitadel over-the-wire migration utility    \033[32m║\n"
		"          ║                                               ║\n"
		"          ╚═══════════════════════════════════════════════╝\033[0m\n"
		"\n"
		"This utility is designed to migrate your Citadel installation\n"
		"to a new host system via a network connection, without disturbing\n"
		"the source system.  The target may be a different CPU architecture\n"
		"and/or operating system.  The source system should be running\n"
		"Citadel version \033[33m%d\033[0m or newer, and the target system should be running\n"
		"either the same version or a newer version.  You will also need\n"
		"the \033[33mrsync\033[0m utility, and OpenSSH v4 or newer.\n"
		"\n"
		"You must run this utility on the TARGET SYSTEM.  Any existing data\n"
		"on this system will be ERASED.  Your target system will be on this\n"
		"host in %s.\n"
		"\n",
		EXPORT_REV_MIN, ctdldir
	);

	getz(yesno, 1, NULL, "Do you wish to continue? ");
	puts(yesno);
	if (tolower(yesno[0]) != 'y') {
		cmdexit = 1;
	}

	if (!cmdexit) {

		printf(	"\033[2J\033[H\n"
			"          \033[32m╔═══════════════════════════════════════════════╗\n"
			"          ║                                               ║\n"
			"          ║       \033[33mValidate source and target systems\033[32m      ║\n"
			"          ║                                               ║\n"
			"          ╚═══════════════════════════════════════════════╝\033[0m\n"
			"\n\n");
	
		printf("First we must validate that the local target system is running and ready to receive data.\n");
		printf("Checking connectivity to Citadel in %s...\n", ctdldir);
		local_admin_socket = uds_connectsock("citadel-admin.socket");

		serv_gets(local_admin_socket, buf);
		puts(buf);
		if (buf[0] != '2') {
			cmdexit = 1;
		}
	}

	if (!cmdexit) {
		serv_puts(local_admin_socket, "ECHO Connection to Citadel Server succeeded.");
		serv_gets(local_admin_socket, buf);
		puts(buf);
		if (buf[0] != '2') {
			cmdexit = 1;
		}
	}

	if (!cmdexit) {
		printf("\n");
		printf("OK, this side is ready to go.  Now we must connect to the source system.\n\n");
		printf("Enter the host name or IP address of the source system\n");
		printf("(example: ctdl.foo.org)\n");
		getz(remote_host, sizeof remote_host, NULL, "--> ");
	
		while (IsEmptyStr(remote_user)) {
			printf("\n");
			printf("Enter the name of a user on %s who has full access to Citadel files.\n", remote_host);
			getz(remote_user, sizeof remote_user, "root", "--> ");
		}

		printf("\n");
		printf("Establishing an SSH connection to the source system...\n");
		sprintf(socket_path, "/tmp/ctdlmigrate-socket.%ld.%d", time(NULL), getpid());
		unlink(socket_path);

		snprintf(cmd, sizeof cmd, "ssh -MNf -S %s -l %s %s", socket_path, remote_user, remote_host);
		sshpid = fork();
		if (sshpid < 0) {
			printf("%s\n", strerror(errno));
			cmdexit = errno;
		}
		else if (sshpid == 0) {
			execl("/bin/bash", "bash", "-c", cmd, (char *) NULL);
			exit(1);
		}
		else {						// Wait for SSH to go into the background
			waitpid(sshpid, &cmdexit, 0);
		}
	}

	if (!cmdexit) {
		printf("\nTesting a command over the connection...\n\n");
		snprintf(cmd, sizeof cmd, "ssh -S %s %s@%s 'echo Remote commands are executing successfully.'",
			socket_path, remote_user, remote_host);
		cmdexit = system(cmd);
		printf("\n");
		if (cmdexit != 0) {
			printf("\033[31mRemote commands are not succeeding.\033[0m\n\n");
		}
	}

	if (!cmdexit) {
		printf("\nLocating the remote 'sendcommand' and Citadel installation...\n");
		snprintf(remote_sendcommand, sizeof remote_sendcommand, "/usr/local/citadel/sendcommand");
		snprintf(cmd, sizeof cmd, "ssh -S %s %s@%s %s NOOP", socket_path, remote_user, remote_host, remote_sendcommand);
		cmdexit = system(cmd);

		if (cmdexit) {
			snprintf(remote_sendcommand, sizeof remote_sendcommand, "/usr/sbin/sendcommand");
			snprintf(cmd, sizeof cmd, "ssh -S %s %s@%s %s NOOP", socket_path, remote_user, remote_host, remote_sendcommand);
			cmdexit = system(cmd);
		}

		if (cmdexit) {
			printf("\n");
			printf("Unable to locate Citadel programs on the remote system.  Please enter\n"
				"the name of the directory on %s which contains the 'sendcommand' program.\n"
				"(example: /opt/foo/citadel)\n", remote_host);
			getz(remote_host, sizeof remote_host, "/usr/local/citadel", "--> ");
			snprintf(remote_sendcommand, sizeof remote_sendcommand, "%s/sendcommand", buf);
			snprintf(cmd, sizeof cmd, "ssh -S %s %s@%s %s NOOP", socket_path, remote_user, remote_host, remote_sendcommand);
			cmdexit = system(cmd);
			if (!cmdexit) {
				printf("ctdlmigrate was unable to attach to the remote Citadel system.\n\n");
			}
		}
	}

	if (!cmdexit) {
		printf(	"\033[2J\033[H\n"
			"          \033[32m╔═══════════════════════════════════════════════════════════════════╗\n"
			"          ║                                                                   ║\n"
			"          ║ \033[33mMigrating from: %-50s\033[32m║\n"
			"          ║                                                                   ║\n"
			"          ╟═══════════════════════════════════════════════════════════════════╣\n"
			"          ║                                                                   ║\n"
			"          ║ Lines received: 0                           Percent complete: 0   ║\n"
			"          ║                                                                   ║\n"
			"          ╚═══════════════════════════════════════════════════════════════════╝\033[0m\n"
			"\n", remote_host
		);

		snprintf(cmd, sizeof cmd, "ssh -S %s %s@%s %s -w3600 MIGR export", socket_path, remote_user, remote_host, remote_sendcommand);
		sourcefp = popen(cmd, "r");
		if (!sourcefp) {
			cmdexit = errno;
			printf("\n%s\n\n", strerror(errno));
		}
	}

	if (!cmdexit) {
		serv_puts(local_admin_socket, "MIGR import");
		serv_gets(local_admin_socket, buf);
		if (buf[0] != '4') {
			printf("\n\033[31m%s\033[0m\n", buf);
			cmdexit = 3;
		}
	}

	if (!cmdexit) {
		char *ptr;
		time_t last_update = time(NULL);
		while (ptr = fgets(buf, SIZ, sourcefp), (ptr != NULL)) {
			ptr = strchr(buf, '\n');
			if (ptr) *ptr = 0;	// remove the newline character
			++linecount;
			if (!strncasecmp(buf, "<progress>", 10)) {
				progress = atoi(&buf[10]);
				printf("\033[8;75H\033[33m%d\033[0m\033[20;0H\n", progress);
			}
			if (time(NULL) != last_update) {
				last_update = time(NULL);
				printf("\033[8;29H\033[33m%d\033[0m\033[20;0H\n", linecount);
			}
			serv_puts(local_admin_socket, buf);
		}
	
		serv_puts(local_admin_socket, "000");
	}

	if ( (cmdexit == 0) && (progress < 100) ) {
		printf("\033[31mERROR: source stream ended before 100 percent of data was received.\033[0m\n");
		ctdlmigrate_exit(86);
	}

	// FIXME restart the local server now

	ctdlmigrate_exit(cmdexit);
}
