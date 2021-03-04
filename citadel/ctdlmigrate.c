/*
 * Across-the-wire migration utility for Citadel
 *
 * Yes, we used goto, and gets(), and committed all sorts of other heinous sins here.
 * The scope of this program isn't wide enough to make a difference.  If you don't like
 * it you can rewrite it.
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
 *
 * (Note: a useful future enhancement might be to support "-h" on both sides)
 *
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
#include <sys/socket.h>
#include <sys/un.h>
#include <libcitadel.h>
#include "citadel.h"
#include "axdefs.h"
#include "sysdep.h"
#include "config.h"
#include "citadel_dirs.h"



/*
 * Replacement for gets() that doesn't throw a compiler warning.
 * We're only using it for some simple prompts, so we don't need
 * to worry about attackers exploiting it.
 */
void getz(char *buf) {
	char *ptr;

	ptr = fgets(buf, SIZ, stdin);
	if (!ptr) {
		buf[0] = 0;
		return;
	}

	ptr = strchr(buf, '\n');
	if (ptr) *ptr = 0;
}


int uds_connectsock(char *sockpath) {
	int s;
	struct sockaddr_un addr;

	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	strncpy(addr.sun_path, sockpath, sizeof addr.sun_path);

	s = socket(AF_UNIX, SOCK_STREAM, 0);
	if (s < 0) {
		fprintf(stderr, "sendcommand: Can't create socket: %s\n", strerror(errno));
		exit(3);
	}

	if (connect(s, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
		fprintf(stderr, "sendcommand: can't connect: %s\n", strerror(errno));
		close(s);
		exit(3);
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
	int cmdexit;
	char cmd[PATH_MAX];
	char buf[PATH_MAX];
	char socket_path[PATH_MAX];
	char remote_user[SIZ];
	char remote_host[SIZ];
	char remote_sendcommand[PATH_MAX];
	FILE *sourcefp = NULL;
	int linecount = 0;
	int a;
	int local_admin_socket = (-1);

	/* Parse command line */
	while ((a = getopt(argc, argv, "h:")) != EOF) {
		switch (a) {
		case 'h':
			strncpy(ctdldir, optarg, sizeof ctdldir);
			break;
		default:
			fprintf(stderr, "sendcommand: usage: ctdlmigrate [-h server_dir]\n");
			return(1);
		}
	}

	if (chdir(ctdldir) != 0) {
		fprintf(stderr, "sendcommand: %s: %s\n", ctdldir, strerror(errno));
		exit(errno);
	}

	printf(	"\033[2J\033[H\n"
		"-------------------------------------------\n"
		"Over-the-wire migration utility for Citadel\n"
		"-------------------------------------------\n"
		"\n"
		"This utility is designed to migrate your Citadel installation\n"
		"to a new host system via a network connection, without disturbing\n"
		"the source system.  The target may be a different CPU architecture\n"
		"and/or operating system.  The source system should be running\n"
		"Citadel version %d or newer, and the target system should be running\n"
		"either the same version or a newer version.  You will also need\n"
		"the 'rsync' utility, and OpenSSH v4 or newer.\n"
		"\n"
		"You must run this utility on the TARGET SYSTEM.  Any existing data\n"
		"on this system will be ERASED.  Your target system will be on this\n"
		"host in %s.\n"
		"\n"
		"Do you wish to continue? "
		,
		EXPORT_REV_MIN, ctdldir
	);

	if ((fgets(yesno, sizeof yesno, stdin) == NULL) || (tolower(yesno[0]) != 'y')) {
		exit(0);
	}

	printf("\n\nGreat!  First we will check some things out here on our target\n"
		"system to make sure it is ready to receive data.\n\n");

	printf("Checking connectivity to Citadel in %s...\n", ctdldir);
	local_admin_socket = uds_connectsock("citadel-admin.socket");


	serv_gets(local_admin_socket, buf);
	puts(buf);
	if (buf[0] != '2') {
		exit(1);
	}
	serv_puts(local_admin_socket, "ECHO Connection to Citadel Server succeeded.");
	serv_gets(local_admin_socket, buf);
	puts(buf);
	if (buf[0] != '2') {
		exit(1);
	}

	printf("\nOK, this side is ready to go.  Now we must connect to the source system.\n\n");

	printf("Enter the host name or IP address of the source system\n"
		"(example: ctdl.foo.org)\n"
		"--> ");
	getz(remote_host);

	while (IsEmptyStr(remote_user)) {
		printf("\nEnter the name of a user on %s who has full access to Citadel files\n"
			"(usually root)\n--> ",
			remote_host);
		getz(remote_user);
	}

	printf("\nEstablishing an SSH connection to the source system...\n\n");
	sprintf(socket_path, "/tmp/ctdlmigrate.XXXXXX");
	mktemp(socket_path);
	unlink(socket_path);
	snprintf(cmd, sizeof cmd, "ssh -MNf -S %s %s@%s", socket_path, remote_user, remote_host);
	cmdexit = system(cmd);
	printf("\n");
	if (cmdexit != 0) {
		printf("This program was unable to establish an SSH session to the source system.\n\n");
		exit(cmdexit);
	}

	printf("\nTesting a command over the connection...\n\n");
	snprintf(cmd, sizeof cmd, "ssh -S %s %s@%s 'echo Remote commands are executing successfully.'",
		socket_path, remote_user, remote_host);
	cmdexit = system(cmd);
	printf("\n");
	if (cmdexit != 0) {
		printf("Remote commands are not succeeding.\n\n");
		exit(cmdexit);
	}

	printf("\nLocating the remote 'sendcommand' and Citadel installation...\n");
	snprintf(remote_sendcommand, sizeof remote_sendcommand, "/usr/local/citadel/sendcommand");
	snprintf(cmd, sizeof cmd, "ssh -S %s %s@%s %s NOOP",
		socket_path, remote_user, remote_host, remote_sendcommand);
	cmdexit = system(cmd);
	if (cmdexit != 0) {
		snprintf(remote_sendcommand, sizeof remote_sendcommand, "/usr/sbin/sendcommand");
		snprintf(cmd, sizeof cmd, "ssh -S %s %s@%s %s NOOP",
			socket_path, remote_user, remote_host, remote_sendcommand);
		cmdexit = system(cmd);
	}
	if (cmdexit != 0) {
		printf("\nUnable to locate Citadel programs on the remote system.  Please enter\n"
			"the name of the directory on %s which contains the 'sendcommand' program.\n"
			"(example: /opt/foo/citadel)\n"
			"--> ", remote_host);
		getz(buf);
		snprintf(remote_sendcommand, sizeof remote_sendcommand, "%s/sendcommand", buf);
		snprintf(cmd, sizeof cmd, "ssh -S %s %s@%s %s NOOP",
			socket_path, remote_user, remote_host, remote_sendcommand);
		cmdexit = system(cmd);
	}
	printf("\n");
	if (cmdexit != 0) {
		printf("ctdlmigrate was unable to attach to the remote Citadel system.\n\n");
		exit(cmdexit);
	}

	printf("\033[2J\n");
	printf("\033[2;0H\033[33mMigrating from %s\033[0m\n\n", remote_host);

	snprintf(cmd, sizeof cmd, "ssh -S %s %s@%s %s -w3600 MIGR export", socket_path, remote_user, remote_host, remote_sendcommand);
	sourcefp = popen(cmd, "r");
	if (!sourcefp) {
		printf("\n%s\n\n", strerror(errno));
		exit(2);
	}

	serv_puts(local_admin_socket, "MIGR import");
	serv_gets(local_admin_socket, buf);
	if (buf[0] != '4') {
		printf("\n%s\n", buf);
		exit(3);
	}

	char *ptr;
	time_t time_started = time(NULL);
	time_t last_update = time(NULL);
	while (ptr = fgets(buf, SIZ, sourcefp), (ptr != NULL)) {
		ptr = strchr(buf, '\n');
		if (ptr) *ptr = 0;
		++linecount;
		if (!strncasecmp(buf, "<progress>", 10)) {
			printf("\033[11;0HPercent complete: \033[32m%d\033[0m\n", atoi(&buf[10]));
		}
		if (time(NULL) != last_update) {
			last_update = time(NULL);
			printf("\033[10;0H  Lines received: \033[32m%d\033[0m\n", linecount);
		}
		serv_puts(local_admin_socket, buf);
	}

	serv_puts(local_admin_socket, "000");

	// FIXME restart the local server now

	pclose(sourcefp);
 	printf("\nShutting down the socket connection...\n\n");
	snprintf(cmd, sizeof cmd, "ssh -S %s -N -O exit %s@%s", socket_path, remote_user, remote_host);
	system(cmd);
	unlink(socket_path);
	exit(0);
}
