// Across-the-wire migration utility for Citadel
//
// Copyright (c) 2009-2022 citadel.org
//
// This program is open source software.  Use, duplication, or disclosure
// is subject to the terms of the GNU General Public License, version 3.

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
#include "../server/citadel_defs.h"
#include "axdefs.h"
#include "../server/sysdep.h"
#include "../server/config.h"
#include "../server/citadel_dirs.h"


// support for getz() -- (globals would not be appropriate in a multithreaded program)
static int gmaxlen = 0;
static char *gdeftext = NULL;


// support function for getz()
static int limit_rl(FILE *dummy) {
	if (rl_end > gmaxlen) {
		return '\b';
	}
	return rl_getc(dummy);
}


// support function for getz()
static int getz_deftext(void) {
	if (gdeftext) {
		rl_insert_text(gdeftext);
		gdeftext = NULL;
		rl_startup_hook = NULL;
	}
	return 0;
}


// Replacement for gets() that uses libreadline.
void getz(char *buf, int maxlen, char *default_value, char *prompt) {
	rl_startup_hook = getz_deftext;
	rl_getc_function = limit_rl;
	gmaxlen = maxlen;
	gdeftext = default_value;
	strcpy(buf, readline(prompt));
}


// Exit from the program while displaying an error code
void ctdlmigrate_exit(int cmdexit) {
	printf("\n\n\033[3%dmExit code %d\033[0m\n", (cmdexit ? 1 : 2), cmdexit);
	exit(cmdexit);
}


// Connect to a Citadel on a remote host using a TCP/IP socket
static int tcp_connectsock(char *host, char *service) {
	struct in6_addr serveraddr;
	struct addrinfo hints;
	struct addrinfo *res = NULL;
	struct addrinfo *ai = NULL;
	int rc = (-1);
	int sock = (-1);

	if ((host == NULL) || IsEmptyStr(host)) {
		return(-1);
	}
	if ((service == NULL) || IsEmptyStr(service)) {
		return(-1);
	}

	memset(&hints, 0x00, sizeof(hints));
	hints.ai_flags = AI_NUMERICSERV;
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;

	// Handle numeric IPv4 and IPv6 addresses
	rc = inet_pton(AF_INET, host, &serveraddr);
	if (rc == 1) {					// dotted quad
		hints.ai_family = AF_INET;
		hints.ai_flags |= AI_NUMERICHOST;
	}
	else {
		rc = inet_pton(AF_INET6, host, &serveraddr);
		if (rc == 1) {				// IPv6 address
			hints.ai_family = AF_INET6;
			hints.ai_flags |= AI_NUMERICHOST;
		}
	}

	// Begin the connection process
	rc = getaddrinfo(host, service, &hints, &res);
	if (rc != 0) {
		fprintf(stderr, "ctdlmigrate: %s\n", strerror(errno));
		return (-1);
	}

	// Try all available addresses until we connect to one or until we run out.
	for (ai = res; ai != NULL; ai = ai->ai_next) {
		sock = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
		if (sock < 0) {
			fprintf(stderr, "ctdlmigrate: %s\n", strerror(errno));
			return (-1);
		}

		rc = connect(sock, ai->ai_addr, ai->ai_addrlen);
		if (rc >= 0) {
			return (sock);	// Connected!
		}
		else {
			fprintf(stderr, "ctdlmigrate: %s\n", strerror(errno));
			close(sock);	// Failed.  Close the socket to avoid fd leak!
		}
	}
	return (-1);
}


// Connect to a Citadel on a remote host using a unix domaion socket
int uds_connectsock(char *sockpath) {
	int s;
	struct sockaddr_un addr;

	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	strcpy(addr.sun_path, sockpath);

	s = socket(AF_UNIX, SOCK_STREAM, 0);
	if (s < 0) {
		fprintf(stderr, "ctdlmigrate: Can't create socket: %s\n", strerror(errno));
		return(-1);
	}

	if (connect(s, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
		fprintf(stderr, "ctdlmigrate: can't connect: %s\n", strerror(errno));
		close(s);
		return(-1);
	}

	return s;
}


// input binary data from socket
void serv_read(int serv_sock, char *buf, int bytes) {
	int len = 0;
	int rlen = 0;

	while (len < bytes) {
		rlen = read(serv_sock, &buf[len], bytes - len);
		if (rlen < 1) {
			return;
		}
		len = len + rlen;
	}
}


// send binary to server
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


// input string from socket - implemented in terms of serv_read()
void serv_gets(int serv_sock, char *buf) {
	int i;

	// Read one character at a time.
	for (i = 0;; i++) {
		serv_read(serv_sock, &buf[i], 1);
		if (buf[i] == '\n' || i == (SIZ-1))
			break;
	}

	// If we got a long line, discard characters until the newline.
	if (i == (SIZ-1)) {
		while (buf[i] != '\n') {
			serv_read(serv_sock, &buf[i], 1);
		}
	}

	// Strip all trailing nonprintables (crlf)
	buf[i] = 0;
}


// send line to server - implemented in terms of serv_write()
void serv_puts(int serv_sock, char *buf) {
	serv_write(serv_sock, buf, strlen(buf));
	serv_write(serv_sock, "\n", 1);
}


// send formatted printable data to the server
void serv_printf(int serv_sock, const char *format, ...) {   
	va_list arg_ptr;   
	char buf[1024];
   
	va_start(arg_ptr, format);   
	if (vsnprintf(buf, sizeof buf, format, arg_ptr) == -1)
		buf[sizeof buf - 2] = '\n';
	serv_write(serv_sock, buf, strlen(buf)); 
	va_end(arg_ptr);
}


// You know what main() does.  If you don't, you shouldn't be trying to understand this program.
int main(int argc, char *argv[]) {
	char ctdldir[PATH_MAX]=CTDLDIR;
	char yesno[2];
	int cmdexit = 0;				// when something fails, set cmdexit to nonzero, and skip to the end
	char buf[PATH_MAX];

	char remote_user[128];
	char remote_host[128];
	char remote_pass[128];
	char *remote_port = "504";

	int linecount = 0;
	int a;
	int remote_server_socket = (-1);
	int local_admin_socket = (-1);
	int progress = 0;

	// Parse command line
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
		"either the same version or a newer version.\n"
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
		cmdexit = 101;
	}

	if (!cmdexit) {
		printf(	"\033[2J\033[H\n"
			"\033[32m╔═══════════════════════════════════════════════╗\n"
			"║                                               ║\n"
			"║       \033[33mValidate source and target systems\033[32m      ║\n"
			"║                                               ║\n"
			"╚═══════════════════════════════════════════════╝\033[0m\n"
			"\n\n");
	
		printf("First we must validate that the local target system is running\n");
		printf("and ready to receive data.\n");
		printf("Checking connectivity to Citadel in %s...\n", ctdldir);
		local_admin_socket = uds_connectsock("citadel-admin.socket");
		if (local_admin_socket < 0) {
			cmdexit = 102;
		}
	}

	if (!cmdexit) {
		serv_gets(local_admin_socket, buf);
		puts(buf);
		if (buf[0] != '2') {
			cmdexit = 103;
		}
	}

	if (!cmdexit) {
		serv_puts(local_admin_socket, "ECHO Connection to Citadel Server succeeded.");
		serv_gets(local_admin_socket, buf);
		puts(buf);
		if (buf[0] != '2') {
			cmdexit = 104;
		}
	}

	if (!cmdexit) {
		printf("\n");
		printf("OK, this side is ready to go.  Now we must connect to the source system.\n\n");

		getz(remote_host, sizeof remote_host, NULL,	"Enter the name or IP address of the source system: ");
		getz(remote_user, sizeof remote_user, "admin",	"  Enter the user name of an administrator account: ");
		getz(remote_pass, sizeof remote_pass, NULL,	"              Enter the password for this account: ");

		remote_server_socket = tcp_connectsock(remote_host, remote_port);
		if (remote_server_socket < 0) {
			cmdexit = 105;
		}
	}

	if (!cmdexit) {
		serv_gets(remote_server_socket, buf);
		puts(buf);
		if (buf[0] != '2') {
			cmdexit = 106;
		}
	}

	if (!cmdexit) {
		serv_printf(remote_server_socket, "USER %s\n", remote_user);
		serv_gets(remote_server_socket, buf);
		puts(buf);
		if (buf[0] != '3') {
			cmdexit = 107;
		}
	}

	if (!cmdexit) {
		serv_printf(remote_server_socket, "PASS %s\n", remote_pass);
		serv_gets(remote_server_socket, buf);
		puts(buf);
		if (buf[0] != '2') {
			cmdexit = 108;
		}
	}

	if (!cmdexit) {
		printf(	"\033[2J\033[H\n"
			"\033[32m╔═══════════════════════════════════════════════════════════════════╗\n"
			"║                                                                   ║\n"
			"║ \033[33mMigrating from: %-50s\033[32m║\n"
			"║                                                                   ║\n"
			"╠═══════════════════════════════════════════════════════════════════╣\n"
			"║                                                                   ║\n"
			"║ Lines received: 0                           Percent complete: 0   ║\n"
			"║                                                                   ║\n"
			"╚═══════════════════════════════════════════════════════════════════╝\033[0m\n"
			"\n", remote_host
		);
	}

	if (!cmdexit) {
		serv_puts(remote_server_socket, "MIGR export");
		serv_gets(remote_server_socket, buf);
		if (buf[0] != '1') {
			printf("\n\033[31m%s\033[0m\n", buf);
			cmdexit = 109;
		}
	}

	if (!cmdexit) {
		serv_puts(local_admin_socket, "MIGR import");
		serv_gets(local_admin_socket, buf);
		if (buf[0] != '4') {
			printf("\n\033[31m%s\033[0m\n", buf);
			cmdexit = 110;
		}
	}

	if (!cmdexit) {
		char *ptr;
		time_t last_update = time(NULL);

		while (serv_gets(remote_server_socket, buf), strcmp(buf, "000")) {
			ptr = strchr(buf, '\n');
			if (ptr) *ptr = 0;	// remove the newline character
			++linecount;
			if (!strncasecmp(buf, "<progress>", 10)) {
				progress = atoi(&buf[10]);
				printf("\033[8;65H\033[33m%d\033[0m\033[12;0H\n", progress);
			}
			if (time(NULL) != last_update) {
				last_update = time(NULL);
				printf("\033[8;19H\033[33m%d\033[0m\033[12;0H\n", linecount);
			}
			serv_puts(local_admin_socket, buf);
		}
	
		serv_puts(local_admin_socket, "000");
	}

	if ( (cmdexit == 0) && (progress < 100) ) {
		printf("\033[31mERROR: source stream ended before 100 percent of data was received.\033[0m\n");
		cmdexit = 111;
	}

	if (!cmdexit) {
		printf("\033[36mMigration is complete.  Restarting the target server.\033[0m\n");
		serv_puts(local_admin_socket, "DOWN 1");
		serv_gets(local_admin_socket, buf);
		puts(buf);
		printf("\033[36mIt is recommended that you shut down the source server now.\033[0m\n");
		printf("\033[36mRemember to copy over your SSL keys and file libraries if appropriate.\033[0m\n");
	}

	close(remote_server_socket);
	close(local_admin_socket);
	ctdlmigrate_exit(cmdexit);
}
