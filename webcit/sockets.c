
/*
 * Copyright (c) 1987-2021 by the citadel.org team
 *
 * This program is open source software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 3.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

/*
 * Uncomment this to log all communications with the Citadel server
#define SERV_TRACE 1
 */

#include "webcit.h"
#include "webserver.h"

long MaxRead = -1;		/* should we do READ scattered or all at once? */

/*
 * register the timeout
 */
RETSIGTYPE timeout(int signum) {
	syslog(LOG_WARNING, "Connection timed out; unable to reach citserver\n");
	/* no exit here, since we need to server the connection unreachable thing. exit(3); */
}


/*
 * Client side - connect to a unix domain socket
 */
int connect_to_citadel(char *sockpath) {
	struct sockaddr_un addr;
	int s;

	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	strncpy(addr.sun_path, sockpath, sizeof addr.sun_path);

	s = socket(AF_UNIX, SOCK_STREAM, 0);
	if (s < 0) {
		syslog(LOG_WARNING, "Can't create socket [%s]: %s\n", sockpath, strerror(errno));
		return (-1);
	}

	if (connect(s, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
		syslog(LOG_WARNING, "Can't connect [%s]: %s\n", sockpath, strerror(errno));
		close(s);
		return (-1);
	}
	return s;
}


/*
 *  input string from pipe
 */
int serv_getln(char *strbuf, int bufsize) {
	int len;

	*strbuf = '\0';
	StrBuf_ServGetln(WC->MigrateReadLineBuf);
	len = StrLength(WC->MigrateReadLineBuf);
	if (len > bufsize)
		len = bufsize - 1;
	memcpy(strbuf, ChrPtr(WC->MigrateReadLineBuf), len);
	FlushStrBuf(WC->MigrateReadLineBuf);
	strbuf[len] = '\0';
#ifdef SERV_TRACE
	syslog(LOG_DEBUG, "%3d<<<%s\n", WC->serv_sock, strbuf);
#endif
	return len;
}


int StrBuf_ServGetln(StrBuf * buf) {
	const char *ErrStr = NULL;
	int rc;

	if (!WC->connected)
		return -1;

	FlushStrBuf(buf);
	rc = StrBufTCP_read_buffered_line_fast(buf, WC->ReadBuf, &WC->ReadPos, &WC->serv_sock, 5, 1, &ErrStr);
	if (rc < 0) {
		syslog(LOG_INFO, "StrBuf_ServGetln(): Server connection broken: %s\n", (ErrStr) ? ErrStr : "");
		wc_backtrace(LOG_INFO);
		if (WC->serv_sock > 0)
			close(WC->serv_sock);
		WC->serv_sock = (-1);
		WC->connected = 0;
		WC->logged_in = 0;
	}
#ifdef SERV_TRACE
	else {
		long pos = 0;
		if (WC->ReadPos != NULL)
			pos = WC->ReadPos - ChrPtr(WC->ReadBuf);
		syslog(LOG_DEBUG, "%3d<<<[%ld]%s\n", WC->serv_sock, pos, ChrPtr(buf));
	}
#endif
	return rc;
}

int StrBuf_ServGetBLOBBuffered(StrBuf * buf, long BlobSize) {
	const char *ErrStr;
	int rc;

	rc = StrBufReadBLOBBuffered(buf, WC->ReadBuf, &WC->ReadPos, &WC->serv_sock, 1, BlobSize, NNN_TERM, &ErrStr);
	if (rc < 0) {
		syslog(LOG_INFO, "StrBuf_ServGetBLOBBuffered(): Server connection broken: %s\n", (ErrStr) ? ErrStr : "");
		wc_backtrace(LOG_INFO);
		if (WC->serv_sock > 0)
			close(WC->serv_sock);
		WC->serv_sock = (-1);
		WC->connected = 0;
		WC->logged_in = 0;
	}
#ifdef SERV_TRACE
	else
		syslog(LOG_DEBUG, "%3d<<<BLOB: %d bytes\n", WC->serv_sock, StrLength(buf));
#endif

	return rc;
}

int StrBuf_ServGetBLOB(StrBuf * buf, long BlobSize) {
	const char *ErrStr;
	int rc;

	WC->ReadPos = NULL;
	rc = StrBufReadBLOB(buf, &WC->serv_sock, 1, BlobSize, &ErrStr);
	if (rc < 0) {
		syslog(LOG_INFO, "StrBuf_ServGetBLOB(): Server connection broken: %s\n", (ErrStr) ? ErrStr : "");
		wc_backtrace(LOG_INFO);
		if (WC->serv_sock > 0)
			close(WC->serv_sock);
		WC->serv_sock = (-1);
		WC->connected = 0;
		WC->logged_in = 0;
	}
#ifdef SERV_TRACE
	else
		syslog(LOG_DEBUG, "%3d<<<BLOB: %d bytes\n", WC->serv_sock, StrLength(buf));
#endif

	return rc;
}


void FlushReadBuf(void) {
	long len;
	const char *pch;
	const char *pche;

	len = StrLength(WC->ReadBuf);
	if ((len > 0) && (WC->ReadPos != NULL) && (WC->ReadPos != StrBufNOTNULL)) {
		pch = ChrPtr(WC->ReadBuf);
		pche = pch + len;
		if (WC->ReadPos != pche) {
			syslog(LOG_ERR,
			       "ERROR: somebody didn't eat his soup! Remaing Chars: %ld [%s]\n", (long) (pche - WC->ReadPos), pche);
			syslog(LOG_ERR,
			       "--------------------------------------------------------------------------------\n"
			       "Whole buf: [%s]\n"
			       "--------------------------------------------------------------------------------\n", pch);
			AppendImportantMessage(HKEY
					       ("Suppenkasper alert! watch your webcit logfile and get connected to your favourite opensource Crew."));
		}
	}

	FlushStrBuf(WC->ReadBuf);
	WC->ReadPos = NULL;


}


/*
 *  send binary to server
 *  buf the buffer to write to citadel server
 *  nbytes how many bytes to send to citadel server
 */
int serv_write(const char *buf, int nbytes) {
	int bytes_written = 0;
	int retval;

	FlushReadBuf();
	while (bytes_written < nbytes) {
		retval = write(WC->serv_sock, &buf[bytes_written], nbytes - bytes_written);
		if (retval < 1) {
			const char *ErrStr = strerror(errno);
			syslog(LOG_INFO, "serv_write(): Server connection broken: %s\n", (ErrStr) ? ErrStr : "");
			if (WC->serv_sock > 0)
				close(WC->serv_sock);
			WC->serv_sock = (-1);
			WC->connected = 0;
			WC->logged_in = 0;
			return 0;
		}
		bytes_written = bytes_written + retval;
	}
	return 1;
}


/*
 *  send line to server
 *  string the line to send to the citadel server
 */
int serv_puts(const char *string) {
#ifdef SERV_TRACE
	syslog(LOG_DEBUG, "%3d>>>%s\n", WC->serv_sock, string);
#endif
	FlushReadBuf();

	if (!serv_write(string, strlen(string)))
		return 0;
	return serv_write("\n", 1);
}

/*
 *  send line to server
 *  string the line to send to the citadel server
 */
int serv_putbuf(const StrBuf * string) {
#ifdef SERV_TRACE
	syslog(LOG_DEBUG, "%3d>>>%s\n", WC->serv_sock, ChrPtr(string));
#endif
	FlushReadBuf();

	if (!serv_write(ChrPtr(string), StrLength(string)))
		return 0;
	return serv_write("\n", 1);
}


/*
 *  convenience function to send stuff to the server
 *  format the formatstring
 *  ... the entities to insert into format 
 */
int serv_printf(const char *format, ...) {
	va_list arg_ptr;
	char buf[SIZ];
	size_t len;
	int rc;

	FlushReadBuf();

	va_start(arg_ptr, format);
	vsnprintf(buf, sizeof buf, format, arg_ptr);
	va_end(arg_ptr);

	len = strlen(buf);
	buf[len++] = '\n';
	buf[len] = '\0';
	rc = serv_write(buf, len);
#ifdef SERV_TRACE
	syslog(LOG_DEBUG, ">>>%s", buf);
#endif
	return rc;
}


/*
 * Read binary data from server into memory using a series of server READ commands.
 * returns the read content as StrBuf
 */
int serv_read_binary(StrBuf * Ret, size_t total_len, StrBuf * Buf) {
	size_t bytes_read = 0;
	size_t this_block = 0;
	int rc = 6;
	int ServerRc = 6;

	if (Ret == NULL) {
		return -1;
	}

	while ((bytes_read < total_len) && (ServerRc == 6)) {

		if (WC->serv_sock == -1) {
			FlushStrBuf(Ret);
			return -1;
		}

		serv_printf("READ " SIZE_T_FMT "|" SIZE_T_FMT, bytes_read, total_len - bytes_read);
		if ((rc = StrBuf_ServGetln(Buf) > 0) && (ServerRc = GetServerStatus(Buf, NULL), ServerRc == 6)) {
			if (rc < 0)
				return rc;
			StrBufCutLeft(Buf, 4);
			this_block = StrTol(Buf);
			rc = StrBuf_ServGetBLOBBuffered(Ret, this_block);
			if (rc < 0) {
				syslog(LOG_INFO, "Server connection broken during download\n");
				wc_backtrace(LOG_INFO);
				if (WC->serv_sock > 0)
					close(WC->serv_sock);
				WC->serv_sock = (-1);
				WC->connected = 0;
				WC->logged_in = 0;
				return rc;
			}
			bytes_read += rc;
		}
	}

	return StrLength(Ret);
}


int client_write(StrBuf * ThisBuf) {
	const char *ptr, *eptr;
	long count;
	ssize_t res = 0;
	fd_set wset;
	int fdflags;

	ptr = ChrPtr(ThisBuf);
	count = StrLength(ThisBuf);
	eptr = ptr + count;

	fdflags = fcntl(WC->Hdr->http_sock, F_GETFL);

	while ((ptr < eptr) && (WC->Hdr->http_sock != -1)) {
		if ((fdflags & O_NONBLOCK) == O_NONBLOCK) {
			FD_ZERO(&wset);
			FD_SET(WC->Hdr->http_sock, &wset);
			if (select(WC->Hdr->http_sock + 1, NULL, &wset, NULL, NULL) == -1) {
				syslog(LOG_INFO, "client_write: Socket select failed (%s)\n", strerror(errno));
				return -1;
			}
		}

		if ((WC->Hdr->http_sock == -1) || ((res = write(WC->Hdr->http_sock, ptr, count)), (res == -1))) {
			syslog(LOG_INFO, "client_write: Socket write failed (%s)\n", strerror(errno));
			wc_backtrace(LOG_INFO);
			return -1;
		}
		count -= res;
		ptr += res;
	}
	return 0;
}


int read_serv_chunk(StrBuf * Buf, size_t total_len, size_t *bytes_read) {
	int rc;
	int ServerRc;

	serv_printf("READ " SIZE_T_FMT "|" SIZE_T_FMT, *bytes_read, total_len - (*bytes_read));
	if ((rc = StrBuf_ServGetln(Buf) > 0) && (ServerRc = GetServerStatus(Buf, NULL), ServerRc == 6)) {
		size_t this_block = 0;

		if (rc < 0)
			return rc;

		StrBufCutLeft(Buf, 4);
		this_block = StrTol(Buf);
		rc = StrBuf_ServGetBLOBBuffered(WC->WBuf, this_block);
		if (rc < 0) {
			syslog(LOG_INFO, "Server connection broken during download\n");
			wc_backtrace(LOG_INFO);
			if (WC->serv_sock > 0)
				close(WC->serv_sock);
			WC->serv_sock = (-1);
			WC->connected = 0;
			WC->logged_in = 0;
			return rc;
		}
		*bytes_read += rc;
	}
	return 6;
}

static inline int send_http(StrBuf * Buf) {
#ifdef HAVE_OPENSSL
	if (is_https)
		return client_write_ssl(Buf);
	else
#endif
		return client_write(Buf);
}

/*
 * Read binary data from server into memory using a series of server READ commands.
 * returns the read content as StrBuf
 */
void serv_read_binary_to_http(StrBuf * MimeType, size_t total_len, int is_static, int detect_mime) {
	int ServerRc = 6;
	size_t bytes_read = 0;
	int first = 1;
	int client_con_state = 0;
	int chunked = 0;
	int is_gzip = 0;
	const char *Err = NULL;
	StrBuf *BufHeader = NULL;
	StrBuf *Buf;
	StrBuf *pBuf = NULL;
	vStreamT *SC = NULL;
	IOBuffer ReadBuffer;
	IOBuffer WriteBuffer;


	Buf = NewStrBuf();

	if (WC->Hdr->HaveRange) {
		WC->Hdr->HaveRange++;
		WC->Hdr->TotalBytes = total_len;
		/* open range? or beyound file border? correct the numbers. */
		if ((WC->Hdr->RangeTil == -1) || (WC->Hdr->RangeTil >= total_len))
			WC->Hdr->RangeTil = total_len - 1;
		bytes_read = WC->Hdr->RangeStart;
		total_len = WC->Hdr->RangeTil;
	}
	else
		chunked = total_len > SIZ * 10;	/* TODO: disallow for HTTP / 1.0 */

	if (chunked) {
		BufHeader = NewStrBuf();
	}

	if ((detect_mime != 0) && (bytes_read != 0)) {
		/* need to read first chunk to detect mime, though the client doesn't care */
		size_t bytes_read = 0;
		const char *CT;

		ServerRc = read_serv_chunk(Buf, total_len, &bytes_read);

		if (ServerRc != 6) {
			FreeStrBuf(&BufHeader);
			FreeStrBuf(&Buf);
			return;
		}
		CT = GuessMimeType(SKEY(WC->WBuf));
		FlushStrBuf(WC->WBuf);
		StrBufPlain(MimeType, CT, -1);
		CheckGZipCompressionAllowed(SKEY(MimeType));
		detect_mime = 0;
		FreeStrBuf(&Buf);
	}

	memset(&WriteBuffer, 0, sizeof(IOBuffer));
	if (chunked && !DisableGzip && WC->Hdr->HR.gzip_ok) {
		is_gzip = 1;
		SC = StrBufNewStreamContext(eZLibEncode, &Err);
		if (SC == NULL) {
			syslog(LOG_ERR, "Error while initializing stream context: %s", Err);
			FreeStrBuf(&Buf);
			return;
		}

		memset(&ReadBuffer, 0, sizeof(IOBuffer));
		ReadBuffer.Buf = WC->WBuf;

		WriteBuffer.Buf = NewStrBufPlain(NULL, SIZ * 2);;
		pBuf = WriteBuffer.Buf;
	}
	else {
		pBuf = WC->WBuf;
	}

	if (!detect_mime) {
		http_transmit_headers(ChrPtr(MimeType), is_static, chunked, is_gzip);

		if (send_http(WC->HBuf) < 0) {
			FreeStrBuf(&Buf);
			FreeStrBuf(&WriteBuffer.Buf);
			FreeStrBuf(&BufHeader);
			if (StrBufDestroyStreamContext(eZLibEncode, &SC, &Err) && Err) {
				syslog(LOG_ERR, "Error while destroying stream context: %s", Err);
			}
			return;
		}
	}

	while ((bytes_read < total_len) && (ServerRc == 6) && (client_con_state == 0)) {

		if (WC->serv_sock == -1) {
			FlushStrBuf(WC->WBuf);
			FreeStrBuf(&Buf);
			FreeStrBuf(&WriteBuffer.Buf);
			FreeStrBuf(&BufHeader);
			StrBufDestroyStreamContext(eZLibEncode, &SC, &Err);
			if (StrBufDestroyStreamContext(eZLibEncode, &SC, &Err) && Err) {
				syslog(LOG_ERR, "Error while destroying stream context: %s", Err);
			}
			return;
		}

		ServerRc = read_serv_chunk(Buf, total_len, &bytes_read);
		if (ServerRc != 6)
			break;

		if (detect_mime) {
			const char *CT;
			detect_mime = 0;

			CT = GuessMimeType(SKEY(WC->WBuf));
			StrBufPlain(MimeType, CT, -1);
			if (is_gzip) {
				CheckGZipCompressionAllowed(SKEY(MimeType));
				is_gzip = WC->Hdr->HR.gzip_ok;
			}
			http_transmit_headers(ChrPtr(MimeType), is_static, chunked, is_gzip);

			client_con_state = send_http(WC->HBuf);
		}

		if (is_gzip) {
			int done = (bytes_read == total_len);
			while ((IOBufferStrLength(&ReadBuffer) > 0) && (client_con_state == 0)) {
				int rc;

				do {
					rc = StrBufStreamTranscode(eZLibEncode, &WriteBuffer, &ReadBuffer, NULL, -1, SC, done,
								   &Err);

					if (StrLength(pBuf) > 0) {
						StrBufPrintf(BufHeader, "%s%x\r\n", (first) ? "" : "\r\n", StrLength(pBuf));
						first = 0;
						client_con_state = send_http(BufHeader);
						if (client_con_state == 0) {
							client_con_state = send_http(pBuf);
						}
						FlushStrBuf(pBuf);
					}
				} while ((rc == 1) && (StrLength(pBuf) > 0));
			}
			FlushStrBuf(WC->WBuf);
		}
		else {
			if ((chunked) && (client_con_state == 0)) {
				StrBufPrintf(BufHeader, "%s%x\r\n", (first) ? "" : "\r\n", StrLength(pBuf));
				first = 0;
				client_con_state = send_http(BufHeader);
			}

			if (client_con_state == 0)
				client_con_state = send_http(pBuf);

			FlushStrBuf(pBuf);
		}
	}

	if (SC && StrBufDestroyStreamContext(eZLibEncode, &SC, &Err) && Err) {
		syslog(LOG_ERR, "Error while destroying stream context: %s", Err);
	}
	FreeStrBuf(&WriteBuffer.Buf);
	if ((chunked) && (client_con_state == 0)) {
		StrBufPlain(BufHeader, HKEY("\r\n0\r\n\r\n"));
		if (send_http(BufHeader) < 0) {
			FreeStrBuf(&Buf);
			FreeStrBuf(&BufHeader);
			return;
		}
	}
	FreeStrBuf(&BufHeader);
	FreeStrBuf(&Buf);
}

int ClientGetLine(ParsedHttpHdrs * Hdr, StrBuf * Target) {
	const char *Error;
#ifdef HAVE_OPENSSL
	const char *pch, *pchs;
	int rlen, len, retval = 0;

	if (is_https) {
		int ntries = 0;
		if (StrLength(Hdr->ReadBuf) > 0) {
			pchs = ChrPtr(Hdr->ReadBuf);
			pch = strchr(pchs, '\n');
			if (pch != NULL) {
				rlen = 0;
				len = pch - pchs;
				if (len > 0 && (*(pch - 1) == '\r'))
					rlen++;
				StrBufSub(Target, Hdr->ReadBuf, 0, len - rlen);
				StrBufCutLeft(Hdr->ReadBuf, len + 1);
				return len - rlen;
			}
		}

		while (retval == 0) {
			pch = NULL;
			pchs = ChrPtr(Hdr->ReadBuf);
			if (*pchs != '\0')
				pch = strchr(pchs, '\n');
			if (pch == NULL) {
				retval = client_read_sslbuffer(Hdr->ReadBuf, SLEEPING);
				pchs = ChrPtr(Hdr->ReadBuf);
				pch = strchr(pchs, '\n');
				if (pch == NULL)
					retval = 0;
			}
			if (retval == 0) {
				sleeeeeeeeeep(1);
				ntries++;
			}
			if (ntries > 10)
				return 0;
		}
		if ((retval > 0) && (pch != NULL)) {
			rlen = 0;
			len = pch - pchs;
			if (len > 0 && (*(pch - 1) == '\r'))
				rlen++;
			StrBufSub(Target, Hdr->ReadBuf, 0, len - rlen);
			StrBufCutLeft(Hdr->ReadBuf, len + 1);
			return len - rlen;

		}
		else
			return -1;
	}
	else
#endif
		return StrBufTCP_read_buffered_line_fast(Target, Hdr->ReadBuf, &Hdr->Pos, &Hdr->http_sock, 5, 1, &Error);
}


/* 
 * This is a generic function to set up a master socket for listening on
 * a TCP port.  The server shuts down if the bind fails.  (IPv4/IPv6 version)
 *
 * ip_addr 	IP address to bind
 * port_number	port number to bind
 * queue_len	number of incoming connections to allow in the queue
 */
int webcit_tcp_server(const char *ip_addr, int port_number, int queue_len) {
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
	}
	else if (!strcmp(ip_addr, "0.0.0.0")) {	/* any IPv4 */
		ip_version = 4;
		sin4.sin_addr.s_addr = INADDR_ANY;
	}
	else if ((strchr(ip_addr, '.')) && (!strchr(ip_addr, ':'))) {	/* specific IPv4 */
		ip_version = 4;
		if (inet_pton(AF_INET, ip_addr, &sin4.sin_addr) <= 0) {
			syslog(LOG_WARNING, "Error binding to [%s] : %s\n", ip_addr, strerror(errno));
			return (-WC_EXIT_BIND);
		}
	}
	else {			/* specific IPv6 */
		ip_version = 6;
		if (inet_pton(AF_INET6, ip_addr, &sin6.sin6_addr) <= 0) {
			syslog(LOG_WARNING, "Error binding to [%s] : %s\n", ip_addr, strerror(errno));
			return (-WC_EXIT_BIND);
		}
	}

	if (port_number == 0) {
		syslog(LOG_WARNING, "Cannot start: no port number specified.\n");
		return (-WC_EXIT_BIND);
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
		return (-WC_EXIT_BIND);
	}
	/* Set some socket options that make sense. */
	i = 1;
	setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &i, sizeof(i));

	if (ip_version == 6) {
		b = bind(s, (struct sockaddr *) &sin6, sizeof(sin6));
	}
	else {
		b = bind(s, (struct sockaddr *) &sin4, sizeof(sin4));
	}

	if (b < 0) {
		syslog(LOG_ERR, "Can't bind: %s\n", strerror(errno));
		close(s);
		return (-WC_EXIT_BIND);
	}

	if (listen(s, queue_len) < 0) {
		syslog(LOG_ERR, "Can't listen: %s\n", strerror(errno));
		close(s);
		return (-WC_EXIT_BIND);
	}
	return (s);
}


/*
 * Create a Unix domain socket and listen on it
 * sockpath - file name of the unix domain socket
 * queue_len - Number of incoming connections to allow in the queue
 */
int webcit_uds_server(char *sockpath, int queue_len) {
	struct sockaddr_un addr;
	int s;
	int i;
	int actual_queue_len;

	actual_queue_len = queue_len;
	if (actual_queue_len < 5)
		actual_queue_len = 5;

	i = unlink(sockpath);
	if ((i != 0) && (errno != ENOENT)) {
		syslog(LOG_WARNING, "webcit: can't unlink %s: %s\n", sockpath, strerror(errno));
		return (-WC_EXIT_BIND);
	}

	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	safestrncpy(addr.sun_path, sockpath, sizeof addr.sun_path);

	s = socket(AF_UNIX, SOCK_STREAM, 0);
	if (s < 0) {
		syslog(LOG_WARNING, "webcit: Can't create a unix domain socket: %s\n", strerror(errno));
		return (-WC_EXIT_BIND);
	}

	if (bind(s, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
		syslog(LOG_WARNING, "webcit: Can't bind: %s\n", strerror(errno));
		close(s);
		return (-WC_EXIT_BIND);
	}

	if (listen(s, actual_queue_len) < 0) {
		syslog(LOG_WARNING, "webcit: Can't listen: %s\n", strerror(errno));
		close(s);
		return (-WC_EXIT_BIND);
	}

	chmod(sockpath, 0777);
	return (s);
}


/*
 * Read data from the client socket.
 *
 * sock		socket fd to read from
 * buf		buffer to read into 
 * bytes	number of bytes to read
 * timeout	Number of seconds to wait before timing out
 *
 * Possible return values:
 *      1       Requested number of bytes has been read.
 *      0       Request timed out.
 *	-1   	Connection is broken, or other error.
 */
int client_read_to(ParsedHttpHdrs * Hdr, StrBuf * Target, int bytes, int timeout) {
	const char *Error;
	int retval = 0;

#ifdef HAVE_OPENSSL
	if (is_https) {
		long bufremain = 0;
		long baselen;

		baselen = StrLength(Target);

		if (Hdr->Pos == NULL) {
			Hdr->Pos = ChrPtr(Hdr->ReadBuf);
		}

		if (StrLength(Hdr->ReadBuf) > 0) {
			bufremain = StrLength(Hdr->ReadBuf) - (Hdr->Pos - ChrPtr(Hdr->ReadBuf));

			if (bytes < bufremain)
				bufremain = bytes;
			StrBufAppendBufPlain(Target, Hdr->Pos, bufremain, 0);
			StrBufCutLeft(Hdr->ReadBuf, bufremain);
		}

		if (bytes > bufremain) {
			while ((StrLength(Hdr->ReadBuf) + StrLength(Target) < bytes + baselen) && (retval >= 0))
				retval = client_read_sslbuffer(Hdr->ReadBuf, timeout);
			if (retval >= 0) {
				StrBufAppendBuf(Target, Hdr->ReadBuf, 0);	/* todo: Buf > bytes? */
				return 1;
			}
			else {
				syslog(LOG_INFO, "client_read_ssl() failed\n");
				return -1;
			}
		}
		else
			return 1;
	}
#endif
	retval = StrBufReadBLOBBuffered(Target, Hdr->ReadBuf, &Hdr->Pos, &Hdr->http_sock, 1, bytes, O_TERM, &Error);
	if (retval < 0) {
		syslog(LOG_INFO, "client_read() failed: %s\n", Error);
		wc_backtrace(LOG_DEBUG);
		return retval;
	}

	return 1;
}


/*
 * Begin buffering HTTP output so we can transmit it all in one write operation later.
 */
void begin_burst(void) {
	if (WC->WBuf == NULL) {
		WC->WBuf = NewStrBufPlain(NULL, 32768);
	}
}


/*
 * Finish buffering HTTP output.  [Compress using zlib and] output with a Content-Length: header.
 */
long end_burst(void) {
	const char *ptr, *eptr;
	long count;
	ssize_t res = 0;
	fd_set wset;
	int fdflags;

	if (!DisableGzip && (WC->Hdr->HR.gzip_ok)) {
		if (CompressBuffer(WC->WBuf) > 0)
			hprintf("Content-encoding: gzip\r\n");
		else {
			syslog(LOG_ALERT, "Compression failed: %d [%s] sending uncompressed\n", errno, strerror(errno));
			wc_backtrace(LOG_INFO);
		}
	}

	if (WC->WFBuf != NULL) {
		WildFireSerializePayload(WC->WFBuf, WC->HBuf, &WC->Hdr->nWildfireHeaders, NULL);
		FreeStrBuf(&WC->WFBuf);
	}

	if (WC->Hdr->HR.prohibit_caching)
		hprintf("Pragma: no-cache\r\nCache-Control: no-store\r\nExpires:-1\r\n");
	hprintf("Content-length: %d\r\n\r\n", StrLength(WC->WBuf));

	ptr = ChrPtr(WC->HBuf);
	count = StrLength(WC->HBuf);
	eptr = ptr + count;

#ifdef HAVE_OPENSSL
	if (is_https) {
		client_write_ssl(WC->HBuf);
		client_write_ssl(WC->WBuf);
		return (count);
	}
#endif

	if (WC->Hdr->http_sock == -1) {
		return -1;
	}
	fdflags = fcntl(WC->Hdr->http_sock, F_GETFL);

	while ((ptr < eptr) && (WC->Hdr->http_sock != -1)) {
		if ((fdflags & O_NONBLOCK) == O_NONBLOCK) {
			FD_ZERO(&wset);
			FD_SET(WC->Hdr->http_sock, &wset);
			if (select(WC->Hdr->http_sock + 1, NULL, &wset, NULL, NULL) == -1) {
				syslog(LOG_DEBUG, "client_write: Socket select failed (%s)\n", strerror(errno));
				return -1;
			}
		}

		if ((WC->Hdr->http_sock == -1) || (res = write(WC->Hdr->http_sock, ptr, count)) == -1) {
			syslog(LOG_DEBUG, "client_write: Socket write failed (%s)\n", strerror(errno));
			wc_backtrace(LOG_INFO);
			return res;
		}
		count -= res;
		ptr += res;
	}

	ptr = ChrPtr(WC->WBuf);
	count = StrLength(WC->WBuf);
	eptr = ptr + count;

	while ((ptr < eptr) && (WC->Hdr->http_sock != -1)) {
		if ((fdflags & O_NONBLOCK) == O_NONBLOCK) {
			FD_ZERO(&wset);
			FD_SET(WC->Hdr->http_sock, &wset);
			if (select(WC->Hdr->http_sock + 1, NULL, &wset, NULL, NULL) == -1) {
				syslog(LOG_INFO, "client_write: Socket select failed (%s)\n", strerror(errno));
				return -1;
			}
		}

		if ((WC->Hdr->http_sock == -1) || (res = write(WC->Hdr->http_sock, ptr, count)) == -1) {
			syslog(LOG_INFO, "client_write: Socket write failed (%s)\n", strerror(errno));
			wc_backtrace(LOG_INFO);
			return res;
		}
		count -= res;
		ptr += res;
	}

	return StrLength(WC->WBuf);
}


/*
 * lingering_close() a`la Apache. see
 * http://httpd.apache.org/docs/2.0/misc/fin_wait_2.html for rationale
 */
int lingering_close(int fd) {
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

void HttpNewModule_TCPSOCKETS(ParsedHttpHdrs * httpreq) {

	httpreq->ReadBuf = NewStrBufPlain(NULL, SIZ * 4);
}

void HttpDetachModule_TCPSOCKETS(ParsedHttpHdrs * httpreq) {

	FlushStrBuf(httpreq->ReadBuf);
	ReAdjustEmptyBuf(httpreq->ReadBuf, 4 * SIZ, SIZ);
}

void HttpDestroyModule_TCPSOCKETS(ParsedHttpHdrs * httpreq) {

	FreeStrBuf(&httpreq->ReadBuf);
}


void SessionNewModule_TCPSOCKETS(wcsession * sess) {
	sess->CLineBuf = NewStrBuf();
	sess->MigrateReadLineBuf = NewStrBuf();
}

void SessionDestroyModule_TCPSOCKETS(wcsession * sess) {
	FreeStrBuf(&sess->CLineBuf);
	FreeStrBuf(&sess->ReadBuf);
	sess->connected = 0;
	sess->ReadPos = NULL;
	FreeStrBuf(&sess->MigrateReadLineBuf);
	if (sess->serv_sock > 0) {
		syslog(LOG_DEBUG, "Closing socket %d", sess->serv_sock);
		close(sess->serv_sock);
	}
	sess->serv_sock = -1;
}
