// Functions that handle communication with a Citadel Server
//
// Copyright (c) 1987-2022 by the citadel.org team
//
// This program is open source software.  Use, duplication, or
// disclosure are subject to the GNU General Public License v3.

#include "webcit.h"

struct ctdlsession *cpool = NULL;				// linked list of connections to the Citadel server
pthread_mutex_t cpool_mutex = PTHREAD_MUTEX_INITIALIZER;	// Lock it before modifying


// Read a specific number of bytes of binary data from the Citadel server.
// Returns the number of bytes read or -1 for error.
int ctdl_read_binary(struct ctdlsession *ctdl, char *buf, int bytes_requested) {
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


// Read a newline-terminated line of text from the Citadel server.
// Returns the string length or -1 for error.
int ctdl_readline(struct ctdlsession *ctdl, char *buf, int maxbytes) {
	int len = 0;
	int c = 0;

	if (buf == NULL) {
		return (-1);
	}

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
			// syslog(LOG_DEBUG, "[ %s", buf);
			return (len);
		}
		++len;
	}
	// syslog(LOG_DEBUG, "[ %s", buf);
	return (len);
}


// Read lines of text from the Citadel server until a 000 terminator is received.
// Implemented in terms of ctdl_readline() and is therefore transparent...
// Returns a newly allocated StrBuf or NULL for error.
StrBuf *ctdl_readtextmsg(struct ctdlsession *ctdl) {
	char buf[1024];
	StrBuf *sj = NewStrBuf();
	if (!sj) {
		return NULL;
	}

	while (ctdl_readline(ctdl, buf, sizeof(buf)), strcmp(buf, "000")) {
		StrBufAppendPrintf(sj, "%s\n", buf);
	}

	return sj;
}


// Write to the Citadel server.  For now we're just wrapping write() in case we
// need to add anything else later.
ssize_t ctdl_write(struct ctdlsession *ctdl, const void *buf, size_t count) {
	return write(ctdl->sock, buf, count);
}


// printf() type function to send data to the Citadel Server.
void ctdl_printf(struct ctdlsession *ctdl, const char *format, ...) {
	va_list arg_ptr;
	StrBuf *Buf = NewStrBuf();

	va_start(arg_ptr, format);
	StrBufVAppendPrintf(Buf, format, arg_ptr);
	va_end(arg_ptr);

	// syslog(LOG_DEBUG, "] %s", ChrPtr(Buf));
	ctdl_write(ctdl, (char *) ChrPtr(Buf), StrLength(Buf));
	ctdl_write(ctdl, "\n", 1);
	FreeStrBuf(&Buf);
}


// Client side - connect to a unix domain socket
int uds_connectsock(char *sockpath) {
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


// Extract from the headers, the username and password the client is attempting to use.
// This could be HTTP AUTH or it could be in the cookies.
void extract_auth(struct http_transaction *h, char *authbuf, int authbuflen) {
	if (authbuf == NULL) {
		return;
	}

	memset(authbuf, 0, authbuflen);

	char *authheader = header_val(h, "Authorization");
	if (authheader) {
		if (!strncasecmp(authheader, "Basic ", 6)) {
			safestrncpy(authbuf, &authheader[6], authbuflen);
			return;		// HTTP-AUTH was found -- stop here
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
			return;		// Cookie auth was found -- stop here
		}
	}
	// no authorization found in headers ... this is an anonymous session
}


// Log in to the Citadel server.  Returns 0 on success or nonzero on error.
//
// 'auth' should be a base64-encoded "username:password" combination (like in http-auth)
//
// If 'resultbuf' is not NULL, it should be a buffer of at least 1024 characters,
// and will be filled with the result from a Citadel server command.
int login_to_citadel(struct ctdlsession *c, char *auth, char *resultbuf) {
	char localbuf[1024];
	char *buf;
	int buflen;
	char supplied_username[AUTH_MAX];
	char supplied_password[AUTH_MAX];

	if (resultbuf != NULL) {
		buf = resultbuf;
	}
	else {
		buf = localbuf;
	}

	buflen = CtdlDecodeBase64(buf, auth, strlen(auth));
	extract_token(supplied_username, buf, 0, ':', sizeof supplied_username);
	extract_token(supplied_password, buf, 1, ':', sizeof supplied_password);
	syslog(LOG_DEBUG, "Supplied credentials: username=%s, password=(%d bytes)", supplied_username, (int) strlen(supplied_password));

	ctdl_printf(c, "USER %s", supplied_username);
	ctdl_readline(c, buf, 1024);
	if (buf[0] != '3') {
		syslog(LOG_DEBUG, "No such user: %s", buf);
		return(1);	// no such user; resultbuf will explain why
	}

	ctdl_printf(c, "PASS %s", supplied_password);
	ctdl_readline(c, buf, 1024);

	if (buf[0] == '2') {
		extract_token(c->whoami, &buf[4], 0, '|', sizeof c->whoami);
		syslog(LOG_DEBUG, "Logged in as %s", c->whoami);

		// Re-encode the auth string so it contains the properly formatted username
		char new_auth_string[1024];
		snprintf(new_auth_string, sizeof(new_auth_string),  "%s:%s", c->whoami, supplied_password);
		CtdlEncodeBase64(c->auth, new_auth_string, strlen(new_auth_string), BASE64_NO_LINEBREAKS);

		return(0);
	}

	syslog(LOG_DEBUG, "Login failed: %s", &buf[4]);
	return(1);		// login failed; resultbuf will explain why
}


// This is a variant of the "server connection pool" design pattern.  We go through our list
// of connections to Citadel Server, looking for a connection that is at once:
// 1. Not currently serving a WebCit transaction (is_bound)
// 2a. Is logged in to Citadel as the correct user, if the HTTP session is logged in; or
// 2b. Is NOT logged in to Citadel, if the HTTP session is not logged in.
// If we find a qualifying connection, we bind to it for the duration of this WebCit HTTP transaction.
// Otherwise, we create a new connection to Citadel Server and add it to the pool.
struct ctdlsession *connect_to_citadel(struct http_transaction *h) {
	struct ctdlsession *cptr = NULL;
	struct ctdlsession *my_session = NULL;
	int is_new_session = 0;
	char buf[1024];
	char auth[AUTH_MAX];
	int r = 0;

	// Does the request carry a username and password?
	extract_auth(h, auth, sizeof auth);

	// Lock the connection pool while we claim our connection
	pthread_mutex_lock(&cpool_mutex);
	if (cpool != NULL) {
		for (cptr = cpool; ((cptr != NULL) && (my_session == NULL)); cptr = cptr->next) {
			if ((cptr->is_bound == 0) && (!strcmp(cptr->auth, auth))) {
				my_session = cptr;
				my_session->is_bound = 1;
			}
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
		return(NULL);						// Could not create a new session (yikes!)
	}

	if (my_session->sock < 3) {
		is_new_session = 1;
	}
	else {								// make sure our Citadel session is still working
		int test_conn;
		test_conn = ctdl_write(my_session, HKEY("NOOP\n"));
		if (test_conn < 5) {
			syslog(LOG_DEBUG, "Citadel session is broken , must reconnect");
			close(my_session->sock);
			my_session->sock = 0;
			is_new_session = 1;
		}
		else {
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
		static char *ctdl_sock_path = NULL;
		if (!ctdl_sock_path) {
			ctdl_sock_path = malloc(PATH_MAX);
			snprintf(ctdl_sock_path, PATH_MAX, "%s/citadel.socket", ctdl_dir);
		}
		my_session->sock = uds_connectsock(ctdl_sock_path);
		ctdl_readline(my_session, buf, sizeof(buf));		// skip past the server greeting banner

		if (!IsEmptyStr(auth)) {				// do we need to log in to Citadel?
			r = login_to_citadel(my_session, auth, NULL);	// FIXME figure out what happens if login failed
		}
	}

	ctdl_printf(my_session, "NOOP");
	ctdl_readline(my_session, buf, sizeof(buf));
	my_session->last_access = time(NULL);
	++my_session->num_requests_handled;
	return(my_session);
}


// Release our Citadel Server connection back into the pool.
void disconnect_from_citadel(struct ctdlsession *ctdl) {
	pthread_mutex_lock(&cpool_mutex);
	ctdl->is_bound = 0;
	pthread_mutex_unlock(&cpool_mutex);
}
