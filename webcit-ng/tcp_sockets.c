/*
 * TCP sockets layer
 *
 * Copyright (c) 1987-2018 by the citadel.org team
 *
 * This program is open source software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include "webcit.h"

/*
 * lingering_close() a`la Apache. see
 * http://httpd.apache.org/docs/2.0/misc/fin_wait_2.html for rationale
 */
int lingering_close(int fd)
{
	char buf[SIZ];
	int i;
	fd_set set;
	struct timeval tv, start;

	gettimeofday(&start, NULL);
	if (fd == -1)
		return -1;
	shutdown(fd, 1);
	do {
		do {
			gettimeofday(&tv, NULL);
			tv.tv_sec = SLEEPING - (tv.tv_sec - start.tv_sec);
			tv.tv_usec = start.tv_usec - tv.tv_usec;
			if (tv.tv_usec < 0) {
				tv.tv_sec--;
				tv.tv_usec += 1000000;
			}
			FD_ZERO(&set);
			FD_SET(fd, &set);
			i = select(fd + 1, &set, NULL, NULL, &tv);
		} while (i == -1 && errno == EINTR);

		if (i <= 0)
			break;

		i = read(fd, buf, sizeof buf);
	} while (i != 0 && (i != -1 || errno == EINTR));

	return close(fd);
}


/* 
 * This is a generic function to set up a master socket for listening on
 * a TCP port.  The server shuts down if the bind fails.  (IPv4/IPv6 version)
 *
 * ip_addr 	IP address to bind
 * port_number	port number to bind
 * queue_len	number of incoming connections to allow in the queue
 */
int webcit_tcp_server(const char *ip_addr, int port_number, int queue_len)
{
	const char *ipv4broadcast = "0.0.0.0";
	int IsDefault = 0;
	struct protoent *p;
	struct sockaddr_in6 sin6;
	struct sockaddr_in sin4;
	int s, i, b;
	int ip_version = 6;

      retry:
	memset(&sin6, 0, sizeof(sin6));
	memset(&sin4, 0, sizeof(sin4));
	sin6.sin6_family = AF_INET6;
	sin4.sin_family = AF_INET;

	if ((ip_addr == NULL)	/* any IPv6 */
	    ||(IsEmptyStr(ip_addr))
	    || (!strcmp(ip_addr, "*"))
	    ) {
		IsDefault = 1;
		ip_version = 6;
		sin6.sin6_addr = in6addr_any;
	} else if (!strcmp(ip_addr, "0.0.0.0")) {	/* any IPv4 */
		ip_version = 4;
		sin4.sin_addr.s_addr = INADDR_ANY;
	} else if ((strchr(ip_addr, '.')) && (!strchr(ip_addr, ':'))) {	/* specific IPv4 */
		ip_version = 4;
		if (inet_pton(AF_INET, ip_addr, &sin4.sin_addr) <= 0) {
			syslog(LOG_WARNING, "Error binding to [%s] : %s\n", ip_addr, strerror(errno));
			return (-1);
		}
	} else {		/* specific IPv6 */

		ip_version = 6;
		if (inet_pton(AF_INET6, ip_addr, &sin6.sin6_addr) <= 0) {
			syslog(LOG_WARNING, "Error binding to [%s] : %s\n", ip_addr, strerror(errno));
			return (-1);
		}
	}

	if (port_number == 0) {
		syslog(LOG_WARNING, "Cannot start: no port number specified.\n");
		return (-1);
	}
	sin6.sin6_port = htons((u_short) port_number);
	sin4.sin_port = htons((u_short) port_number);

	p = getprotobyname("tcp");

	s = socket(((ip_version == 6) ? PF_INET6 : PF_INET), SOCK_STREAM, (p->p_proto));
	if (s < 0) {
		if (IsDefault && (errno == EAFNOSUPPORT)) {
			s = 0;
			ip_addr = ipv4broadcast;
			goto retry;
		}
		syslog(LOG_WARNING, "Can't create a listening socket: %s\n", strerror(errno));
		return (-1);
	}
	/* Set some socket options that make sense. */
	i = 1;
	setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &i, sizeof(i));

	if (ip_version == 6) {
		b = bind(s, (struct sockaddr *) &sin6, sizeof(sin6));
	} else {
		b = bind(s, (struct sockaddr *) &sin4, sizeof(sin4));
	}

	if (b < 0) {
		syslog(LOG_ERR, "Can't bind: %s\n", strerror(errno));
		close(s);
		return (-1);
	}

	if (listen(s, queue_len) < 0) {
		syslog(LOG_ERR, "Can't listen: %s\n", strerror(errno));
		close(s);
		return (-1);
	}
	return (s);
}


/*
 * Create a Unix domain socket and listen on it
 * sockpath - file name of the unix domain socket
 * queue_len - Number of incoming connections to allow in the queue
 */
int webcit_uds_server(char *sockpath, int queue_len)
{
	struct sockaddr_un addr;
	int s;
	int i;
	int actual_queue_len;

	actual_queue_len = queue_len;
	if (actual_queue_len < 5)
		actual_queue_len = 5;

	i = unlink(sockpath);
	if ((i != 0) && (errno != ENOENT)) {
		syslog(LOG_WARNING, "webcit: can't unlink %s: %s", sockpath, strerror(errno));
		return (-1);
	}

	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	safestrncpy(addr.sun_path, sockpath, sizeof addr.sun_path);

	s = socket(AF_UNIX, SOCK_STREAM, 0);
	if (s < 0) {
		syslog(LOG_WARNING, "webcit: Can't create a unix domain socket: %s", strerror(errno));
		return (-1);
	}

	if (bind(s, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
		syslog(LOG_WARNING, "webcit: Can't bind: %s", strerror(errno));
		close(s);
		return (-1);
	}

	if (listen(s, actual_queue_len) < 0) {
		syslog(LOG_WARNING, "webcit: Can't listen: %s", strerror(errno));
		close(s);
		return (-1);
	}

	chmod(sockpath, 0777);
	return (s);
}
