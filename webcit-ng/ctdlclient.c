/*
 * Functions that handle communication with a Citadel Server
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

struct ctdlsession *cpool = NULL;				// linked list of connections to the Citadel server
pthread_mutex_t cpool_mutex = PTHREAD_MUTEX_INITIALIZER;	// Lock it before modifying


/*
 * Read a specific number of bytes of binary data from the Citadel server.
 * Returns the number of bytes read or -1 for error.
 */
int ctdl_read_binary(struct ctdlsession *ctdl, char *buf, int bytes_requested)
{
	int bytes_read = 0;
	int c = 0;

	while (bytes_read < bytes_requested) {
		c = read(ctdl->sock, &buf[bytes_read], bytes_requested-bytes_read);
		if (c <= 0) {
			syslog(LOG_DEBUG, "Socket error or zero-length read");
			return (-1);
		}
		bytes_read += c;
	}
	return (bytes_read);
}


/*
 * Read a newline-terminated line of text from the Citadel server.
 * Returns the string length or -1 for error.
 */
int ctdl_readline(struct ctdlsession *ctdl, char *buf, int maxbytes)
{
	int len = 0;
	int c = 0;

	if (buf == NULL)
		return (-1);

	while (len < maxbytes) {
		c = read(ctdl->sock, &buf[len], 1);
		if (c <= 0) {
			syslog(LOG_DEBUG, "Socket error or zero-length read");
			return (-1);
		}
		if (buf[len] == '\n') {
			if ((len > 0) && (buf[len - 1] == '\r')) {
				--len;
			}
			buf[len] = 0;
			// syslog(LOG_DEBUG, "\033[33m[ %s\033[0m", buf);
			return (len);
		}
		++len;
	}
	// syslog(LOG_DEBUG, "\033[33m[ %s\033[0m", buf);
	return (len);
}


/*
 * Read lines of text from the Citadel server until a 000 terminator is received.
 * Implemented in terms of ctdl_readline() and is therefore transparent...
 * Returns a newly allocated StrBuf or NULL for error.
 */
StrBuf *ctdl_readtextmsg(struct ctdlsession * ctdl)
{
	char buf[1024];
	StrBuf *sj = NewStrBuf();
	if (!sj) {
		return NULL;
	}

	while ((ctdl_readline(ctdl, buf, sizeof(buf)) >= 0) && (strcmp(buf, "000"))) {
		StrBufAppendPrintf(sj, "%s\n", buf);
	}

	return sj;
}


/*
 * Write to the Citadel server.  For now we're just wrapping write() in case we
 * need to add anything else later.
 */
ssize_t ctdl_write(struct ctdlsession * ctdl, const void *buf, size_t count)
{
	return write(ctdl->sock, buf, count);
}


/*
 * printf() type function to send data to the Citadel Server.
 */
void ctdl_printf(struct ctdlsession *ctdl, const char *format, ...)
{
	va_list arg_ptr;
	StrBuf *Buf = NewStrBuf();

	va_start(arg_ptr, format);
	StrBufVAppendPrintf(Buf, format, arg_ptr);
	va_end(arg_ptr);

	syslog(LOG_DEBUG, "\033[32m] %s\033[0m", ChrPtr(Buf));
	ctdl_write(ctdl, (char *) ChrPtr(Buf), StrLength(Buf));
	ctdl_write(ctdl, "\n", 1);
	FreeStrBuf(&Buf);
}


/*
 * Client side - connect to a unix domain socket
 */
int uds_connectsock(char *sockpath)
{
	struct sockaddr_un addr;
	int s;

	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	strncpy(addr.sun_path, sockpath, sizeof addr.sun_path);

	s = socket(AF_UNIX, SOCK_STREAM, 0);
	if (s < 0) {
		syslog(LOG_WARNING, "Can't create socket [%s]: %s", sockpath, strerror(errno));
		return (-1);
	}

	if (connect(s, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
		syslog(LOG_WARNING, "Can't connect [%s]: %s", sockpath, strerror(errno));
		close(s);
		return (-1);
	}
	return s;
}


/*
 * TCP client - connect to a host/port 
 */
int tcp_connectsock(char *host, char *service)
{
	struct in6_addr serveraddr;
	struct addrinfo hints;
	struct addrinfo *res = NULL;
	struct addrinfo *ai = NULL;
	int rc = (-1);
	int s = (-1);

	if ((host == NULL) || IsEmptyStr(host))
		return (-1);
	if ((service == NULL) || IsEmptyStr(service))
		return (-1);

	syslog(LOG_DEBUG, "tcp_connectsock(%s,%s)", host, service);

	memset(&hints, 0x00, sizeof(hints));
	hints.ai_flags = AI_NUMERICSERV;
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;

	/*
	 * Handle numeric IPv4 and IPv6 addresses
	 */
	rc = inet_pton(AF_INET, host, &serveraddr);
	if (rc == 1) {		/* dotted quad */
		hints.ai_family = AF_INET;
		hints.ai_flags |= AI_NUMERICHOST;
	} else {
		rc = inet_pton(AF_INET6, host, &serveraddr);
		if (rc == 1) {	/* IPv6 address */
			hints.ai_family = AF_INET6;
			hints.ai_flags |= AI_NUMERICHOST;
		}
	}

	/* Begin the connection process */

	rc = getaddrinfo(host, service, &hints, &res);
	if (rc != 0) {
		syslog(LOG_DEBUG, "%s: %s", host, gai_strerror(rc));
		freeaddrinfo(res);
		return (-1);
	}

	/*
	 * Try all available addresses until we connect to one or until we run out.
	 */
	for (ai = res; ai != NULL; ai = ai->ai_next) {

		if (ai->ai_family == AF_INET)
			syslog(LOG_DEBUG, "Trying IPv4");
		else if (ai->ai_family == AF_INET6)
			syslog(LOG_DEBUG, "Trying IPv6");
		else
			syslog(LOG_WARNING, "This is going to fail.");

		s = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
		if (s < 0) {
			syslog(LOG_WARNING, "socket() failed: %s", strerror(errno));
			freeaddrinfo(res);
			return (-1);
		}
		rc = connect(s, ai->ai_addr, ai->ai_addrlen);
		if (rc >= 0) {
			int fdflags;
			freeaddrinfo(res);

			fdflags = fcntl(rc, F_GETFL);
			if (fdflags < 0) {
				syslog(LOG_ERR, "unable to get socket %d flags! %s", rc, strerror(errno));
				close(rc);
				return -1;
			}
			fdflags = fdflags | O_NONBLOCK;
			if (fcntl(rc, F_SETFL, fdflags) < 0) {
				syslog(LOG_ERR, "unable to set socket %d nonblocking flags! %s", rc, strerror(errno));
				close(s);
				return -1;
			}

			return (s);
		} else {
			syslog(LOG_WARNING, "connect() failed: %s", strerror(errno));
			close(s);
		}
	}
	freeaddrinfo(res);
	return (-1);
}


/*
 * Extract from the headers, the username and password the client is attempting to use.
 * This could be HTTP AUTH or it could be in the cookies.
 */
void extract_auth(struct http_transaction *h, char *authbuf, int authbuflen)
{
	if (authbuf == NULL)
		return;
	authbuf[0] = 0;

	char *authheader = header_val(h, "Authorization");
	if (authheader) {
		if (!strncasecmp(authheader, "Basic ", 6)) {
			safestrncpy(authbuf, &authheader[6], authbuflen);
			return;	// HTTP-AUTH was found -- stop here
		}
	}

	char *cookieheader = header_val(h, "Cookie");
	if (cookieheader) {
		char *wcauth = strstr(cookieheader, "wcauth=");
		if (wcauth) {
			safestrncpy(authbuf, &cookieheader[7], authbuflen);
			char *semicolon = strchr(authbuf, ';');
			if (semicolon != NULL) {
				*semicolon = 0;
			}
			if (strlen(authbuf) < 3) {	// impossibly small
				authbuf[0] = 0;
			}
			return;	// Cookie auth was found -- stop here
		}
	}
	// no authorization found in headers ... this is an anonymous session
}


/*
 * Log in to the Citadel server.  Returns 0 on success or nonzero on error.
 *
 * 'auth' should be a base64-encoded "username:password" combination (like in http-auth)
 *
 * If 'resultbuf' is not NULL, it should be a buffer of at least 1024 characters,
 * and will be filled with the result from a Citadel server command.
 */
int login_to_citadel(struct ctdlsession *c, char *auth, char *resultbuf)
{
	char localbuf[1024];
	char *buf;
	int buflen;
	char supplied_username[AUTH_MAX];
	char supplied_password[AUTH_MAX];

	if (resultbuf != NULL) {
		buf = resultbuf;
	} else {
		buf = localbuf;
	}

	buflen = CtdlDecodeBase64(buf, auth, strlen(auth));
	extract_token(supplied_username, buf, 0, ':', sizeof supplied_username);
	extract_token(supplied_password, buf, 1, ':', sizeof supplied_password);
	syslog(LOG_DEBUG, "Supplied credentials: username=%s, pwlen=%d", supplied_username, (int) strlen(supplied_password));

	ctdl_printf(c, "USER %s", supplied_username);
	ctdl_readline(c, buf, 1024);
	if (buf[0] != '3') {
		syslog(LOG_DEBUG, "No such user: %s", buf);
		return (1);	// no such user; resultbuf will explain why
	}

	ctdl_printf(c, "PASS %s", supplied_password);
	ctdl_readline(c, buf, 1024);

	if (buf[0] == '2') {
		strcpy(c->auth, auth);
		extract_token(c->whoami, &buf[4], 0, '|', sizeof c->whoami);
		syslog(LOG_DEBUG, "Login succeeded: %s", buf);
		return (0);
	}

	syslog(LOG_DEBUG, "Login failed: %s", buf);
	return (1);		// login failed; resultbuf will explain why
}


/*
 * Hunt for, or create, a connection to our Citadel Server
 */
struct ctdlsession *connect_to_citadel(struct http_transaction *h)
{
	struct ctdlsession *cptr = NULL;
	struct ctdlsession *my_session = NULL;
	int is_new_session = 0;
	char buf[1024];
	char auth[AUTH_MAX];
	int r = 0;

	// Does the request carry a username and password?
	extract_auth(h, auth, sizeof auth);
	syslog(LOG_DEBUG, "Session auth: %s", auth);	// remove this log when development is done

	// Lock the connection pool while we claim our connection
	pthread_mutex_lock(&cpool_mutex);
	if (cpool != NULL)
		for (cptr = cpool; ((cptr != NULL) && (my_session == NULL)); cptr = cptr->next) {
			if ((cptr->is_bound == 0) && (!strcmp(cptr->auth, auth))) {
				my_session = cptr;
				my_session->is_bound = 1;
			}
		}
	if (my_session == NULL) {
		syslog(LOG_DEBUG, "No qualifying sessions , starting a new one");
		my_session = malloc(sizeof(struct ctdlsession));
		if (my_session != NULL) {
			memset(my_session, 0, sizeof(struct ctdlsession));
			is_new_session = 1;
			my_session->next = cpool;
			cpool = my_session;
			my_session->is_bound = 1;
		}
	}
	pthread_mutex_unlock(&cpool_mutex);
	if (my_session == NULL) {
		return (NULL);	// oh well
	}

	if (my_session->sock < 3) {
		is_new_session = 1;
	} else {		// make sure our Citadel session is still good
		int test_conn;
		test_conn = ctdl_write(my_session, HKEY("NOOP\n"));
		if (test_conn < 5) {
			syslog(LOG_DEBUG, "Citadel session is broken , must reconnect");
			close(my_session->sock);
			my_session->sock = 0;
			is_new_session = 1;
		} else {
			test_conn = ctdl_readline(my_session, buf, sizeof(buf));
			if (test_conn < 1) {
				syslog(LOG_DEBUG, "Citadel session is broken , must reconnect");
				close(my_session->sock);
				my_session->sock = 0;
				is_new_session = 1;
			}
		}
	}

	if (is_new_session) {
		strcpy(my_session->room, "");
		my_session->sock = tcp_connectsock(ctdlhost, ctdlport);
		ctdl_readline(my_session, buf, sizeof(buf));		// skip past the server greeting banner

		if (!IsEmptyStr(auth)) {				// do we need to log in to Citadel?
			r = login_to_citadel(my_session, auth, NULL);	// FIXME figure out what happens if login failed
		}
	}
	ctdl_printf(my_session, "NOOP");
	ctdl_readline(my_session, buf, sizeof(buf));
	my_session->last_access = time(NULL);
	++my_session->num_requests_handled;

	return (my_session);
}


/*
 * Release our Citadel Server connection back into the pool.
 */
void disconnect_from_citadel(struct ctdlsession *ctdl)
{
	pthread_mutex_lock(&cpool_mutex);
	ctdl->is_bound = 0;
	pthread_mutex_unlock(&cpool_mutex);
}
