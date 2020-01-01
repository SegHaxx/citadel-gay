/*
 * Main entry point for the program.
 *
 * Copyright (c) 1996-2020 by the citadel.org team
 *
 * This program is open source software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include "webcit.h"		// All other headers are included from this header.

char *ctdlhost = CTDLHOST;
char *ctdlport = CTDLPORT;

/*
 * Main entry point for the web server.
 */
int main(int argc, char **argv)
{
	int webserver_port = WEBSERVER_PORT;
	char *webserver_interface = WEBSERVER_INTERFACE;
	int running_as_daemon = 0;
	int webserver_protocol = WEBSERVER_HTTP;
	int a;
	char *pid_file = NULL;

	/* Parse command line */
	while ((a = getopt(argc, argv, "u:h:i:p:t:T:B:x:g:dD:G:cfsS:Z:v:")) != EOF)
		switch (a) {
		case 'u':
			setuid(atoi(optarg));
			break;
		case 'h':
			break;
		case 'd':
			running_as_daemon = 1;
			break;
		case 'D':
			running_as_daemon = 1;
			pid_file = strdup(optarg);
			break;
		case 'g':
			// FIXME set up the default landing page
			break;
		case 'B':
		case 't':
		case 'x':
		case 'T':
		case 'v':
			/* The above options are no longer used, but ignored so old scripts don't break */
			break;
		case 'i':
			webserver_interface = optarg;
			break;
		case 'p':
			webserver_port = atoi(optarg);
			break;
		case 'Z':
			// FIXME when gzip is added, disable it if this flag is set
			break;
		case 'f':
			//follow_xff = 1;
			break;
		case 'c':
			break;
		case 's':
			is_https = 1;
			webserver_protocol = WEBSERVER_HTTPS;
			break;
		case 'S':
			is_https = 1;
			webserver_protocol = WEBSERVER_HTTPS;
			//ssl_cipher_list = strdup(optarg);
			break;
		case 'G':
			//DumpTemplateI18NStrings = 1;
			//I18nDump = NewStrBufPlain(HKEY("int templatestrings(void)\n{\n"));
			//I18nDumpFile = optarg;
			break;
		default:
			fprintf(stderr, "usage:\nwebcit "
				"[-i ip_addr] [-p http_port] "
				"[-c] [-f] "
				"[-d] [-Z] [-G i18ndumpfile] "
				"[-u uid] [-h homedirectory] "
				"[-D daemonizepid] [-v] "
				"[-g defaultlandingpage] [-B basesize] " "[-s] [-S cipher_suites]" "[remotehost [remoteport]]\n");
			return 1;
		}

	if (optind < argc) {
		ctdlhost = argv[optind];
		if (++optind < argc)
			ctdlport = argv[optind];
	}

	/* Start the logger */
	openlog("webcit", (running_as_daemon ? (LOG_PID) : (LOG_PID | LOG_PERROR)), LOG_DAEMON);

	/* Tell 'em who's in da house */
	syslog(LOG_NOTICE, "MAKE WEBCIT GREAT AGAIN!");
	syslog(LOG_NOTICE, "Copyright (C) 1996-2020 by the citadel.org team");
	syslog(LOG_NOTICE, " ");
	syslog(LOG_NOTICE, "This program is open source software: you can redistribute it and/or");
	syslog(LOG_NOTICE, "modify it under the terms of the GNU General Public License, version 3.");
	syslog(LOG_NOTICE, " ");
	syslog(LOG_NOTICE, "This program is distributed in the hope that it will be useful,");
	syslog(LOG_NOTICE, "but WITHOUT ANY WARRANTY; without even the implied warranty of");
	syslog(LOG_NOTICE, "MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the");
	syslog(LOG_NOTICE, "GNU General Public License for more details.");
	syslog(LOG_NOTICE, " ");

	/* Ensure that we are linked to the correct version of libcitadel */
	if (libcitadel_version_number() < LIBCITADEL_VERSION_NUMBER) {
		syslog(LOG_INFO, " You are running libcitadel version %d", libcitadel_version_number());
		syslog(LOG_INFO, "WebCit was compiled against version %d", LIBCITADEL_VERSION_NUMBER);
		return (1);
	}

	/* Go into the background if we were asked to run as a daemon */
	if (running_as_daemon) {
		daemon(1, 0);
		if (pid_file != NULL) {
			FILE *pfp = fopen(pid_file, "w");
			if (pfp) {
				fprintf(pfp, "%d\n", getpid());
				fclose(pfp);
			}
		}
	}

	return webserver(webserver_interface, webserver_port, webserver_protocol);
}
