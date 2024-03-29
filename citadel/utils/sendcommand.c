// Command-line utility to transmit a server command.
//
// Copyright (c) 1987-2023 by the citadel.org team
//
// This program is open source software.  Use, duplication, or disclosure
// is subject to the terms of the GNU General Public License, version 3.

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <string.h>
#include <fcntl.h>
#include <stdio.h>
#include <ctype.h>
#include <signal.h>
#include <errno.h>
#include <limits.h>
#include <sys/socket.h>
#include <sys/un.h>
#include "../server/citadel_defs.h"
#include "../server/server.h"
#include "../server/citadel_dirs.h"
#include <libcitadel.h>

int serv_sock = (-1);

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


// input binary data from socket
void serv_read(char *buf, int bytes) {
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


// send binary to server
void serv_write(char *buf, int nbytes) {
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


// input string from socket - implemented in terms of serv_read()
void serv_gets(char *buf) {
	int i;

	// Read one character at a time.
	for (i = 0;; i++) {
		serv_read(&buf[i], 1);
		if (buf[i] == '\n' || i == (SIZ-1))
			break;
	}

	// If we got a long line, discard characters until the newline.
	if (i == (SIZ-1)) {
		while (buf[i] != '\n') {
			serv_read(&buf[i], 1);
		}
	}

	// Strip all trailing nonprintables (crlf)
	buf[i] = 0;
}


// send line to server - implemented in terms of serv_write()
void serv_puts(char *buf) {
	serv_write(buf, strlen(buf));
	serv_write("\n", 1);
}


// Main loop.  Do things and have fun.
int main(int argc, char **argv) {
	int a;
	int watchdog = 60;
	char buf[SIZ];
	int xfermode = 0;
	char ctdldir[PATH_MAX]=CTDLDIR;

	// Parse command line
	while ((a = getopt(argc, argv, "h:w:")) != EOF) {
		switch (a) {
		case 'h':
			strncpy(ctdldir, optarg, sizeof ctdldir);
			break;
		case 'w':
			watchdog = atoi(optarg);
			break;
		default:
			fprintf(stderr, "sendcommand: usage: sendcommand [-h server_dir] [-w watchdog_timeout]\n");
			return(1);
		}
	}

	fprintf(stderr, "sendcommand: started (pid=%d) connecting to Citadel server with data directory %s\n",
		(int) getpid(),
		ctdldir
	);
	fflush(stderr);

	if (chdir(ctdldir) != 0) {
		fprintf(stderr, "sendcommand: %s: %s\n", ctdldir, strerror(errno));
		exit(errno);
	}

	alarm(watchdog);
	serv_sock = uds_connectsock(file_citadel_admin_socket);
	serv_gets(buf);
	fprintf(stderr, "%s\n", buf);

	strcpy(buf, "");
	for (a=optind; a<argc; ++a) {
		if (a != optind) {
			strcat(buf, " ");
		}
		strcat(buf, argv[a]);
	}

	fprintf(stderr, "%s\n", buf);
	serv_puts(buf);
	serv_gets(buf);
	fprintf(stderr, "%s\n", buf);

	xfermode = buf[0];

	if ((xfermode == '4') || (xfermode == '8')) {		// send text
		while (fgets(buf, sizeof buf, stdin) > 0) {
			if (buf[strlen(buf)-1] == '\n') {
				buf[strlen(buf)-1] = 0;
			}
			serv_puts(buf);
		}
		serv_puts("000");
	}

	if ((xfermode == '1') || (xfermode == '8')) {		// receive text
		while(serv_gets(buf), strcmp(buf, "000")) {
			printf("%s\n", buf);
		}
	}
	
	if (xfermode == '6') {					// receive binary
		size_t len = atoi(&buf[4]);
		size_t bytes_remaining = len;

		while (bytes_remaining > 0) {
			size_t this_block = bytes_remaining;
			if (this_block > SIZ) this_block = SIZ;
			serv_read(buf, this_block);
			fwrite(buf, this_block, 1, stdout);
			bytes_remaining -= this_block;
		}
	}

	close(serv_sock);
	alarm(0);						// cancel the watchdog timer

	fprintf(stderr, "sendcommand: processing ended.\n");
	if (xfermode == '5') {
		return(1);
	}
	return(0);
}
