// This module handles HTTP transactions.
//
// Copyright (c) 1996-2022 by the citadel.org team
//
// This program is open source software.  Use, duplication, or
// disclosure are subject to the GNU General Public License v3.

#include "webcit.h"

// Write data to the HTTP client.  Encrypt if necessary.
int client_write(struct client_handle *ch, char *buf, int nbytes) {
	if (is_https) {
		return client_write_ssl(ch, buf, nbytes);
	}
	else {
		return write(ch->sock, buf, nbytes);
	}
}


// Read data from the HTTP client.  Decrypt if necessary.
// Returns number of bytes read, or -1 to indicate an error.
int client_read(struct client_handle *ch, char *buf, int nbytes) {
	if (is_https) {
		return client_read_ssl(ch, buf, nbytes);
	}
	else {
		int bytes_received = 0;
		int bytes_this_block = 0;
		while (bytes_received < nbytes) {
			bytes_this_block = read(ch->sock, &buf[bytes_received], nbytes - bytes_received);
			if (bytes_this_block < 1) {
				return (-1);
			}
			bytes_received += bytes_this_block;
		}
		return (nbytes);
	}
}


// Read a newline-terminated line of text from the client.
// Implemented in terms of client_read() and is therefore transparent...
// Returns the string length or -1 for error.
int client_readline(struct client_handle *ch, char *buf, int maxbytes) {
	int len = 0;
	int c = 0;

	if (buf == NULL) {
		return (-1);
	}

	while (len < maxbytes) {
		c = client_read(ch, &buf[len], 1);
		if (c <= 0) {
			syslog(LOG_DEBUG, "Socket error or zero-length read");
			return (-1);
		}
		if (buf[len] == '\n') {
			if ((len > 0) && (buf[len - 1] == '\r')) {
				--len;
			}
			buf[len] = 0;
			return (len);
		}
		++len;
	}
	return (len);
}


// printf() type function to send data to the web client.
void client_printf(struct client_handle *ch, const char *format, ...) {
	va_list arg_ptr;
	StrBuf *Buf = NewStrBuf();

	va_start(arg_ptr, format);
	StrBufVAppendPrintf(Buf, format, arg_ptr);
	va_end(arg_ptr);

	client_write(ch, (char *) ChrPtr(Buf), StrLength(Buf));
	FreeStrBuf(&Buf);
}


// Push one new header into the response of an HTTP request.
// When completed, the HTTP transaction now owns the memory allocated for key and val.
void add_response_header(struct http_transaction *h, char *key, char *val) {
	struct keyval new_response_header;
	new_response_header.key = key;
	new_response_header.val = val;
	array_append(h->response_headers, &new_response_header);
}


// Entry point for this layer.  Socket is connected.  If running as an HTTPS
// server, SSL has already been negotiated.  Now perform one transaction.
void perform_one_http_transaction(struct client_handle *ch) {
	char buf[1024];
	int len;
	int lines_read = 0;
	struct http_transaction h;
	char *c, *d;
	struct sockaddr_in sin;
	int i;					// general purpose iterator variable

	memset(&h, 0, sizeof h);
	h.request_headers = array_new(sizeof(struct keyval));
	h.request_parms = array_new(sizeof(struct keyval));
	h.response_headers = array_new(sizeof(struct keyval));

	while (len = client_readline(ch, buf, sizeof buf), (len > 0)) {
		++lines_read;

		if (lines_read == 1) {		// First line is method and URL.
			c = strchr(buf, ' ');	// IGnore the HTTP-version.
			if (c == NULL) {
				h.method = strdup("NULL");
				h.url = strdup("/");
			}
			else {
				*c = 0;
				h.method = strdup(buf);
				++c;
				d = c;
				c = strchr(d, ' ');
				if (c != NULL) {
					*c = 0;
				}
				++c;
				h.url = strdup(d);
			}
		}
		else {				// Subsequent lines are headers.
			c = strchr(buf, ':');	// Header line folding is obsolete so we don't support it.
			if (c != NULL) {

				struct keyval new_request_header;
				*c = 0;
				new_request_header.key = strdup(buf);
				++c;
				new_request_header.val = strdup(c);
				string_trim(new_request_header.key);
				string_trim(new_request_header.val);
				array_append(h.request_headers, &new_request_header);
#ifdef DEBUG_HTTP
				syslog(LOG_DEBUG, "{ %s: %s", new_request_header.key, new_request_header.val);
#endif
			}
		}

	}

	// If the URL had any query parameters in it, parse them out now.
	char *p = (h.url ? strchr(h.url, '?') : NULL);
	if (p) {
		*p++ = 0;						// insert a null to remove parameters from the URL
		char *tok, *saveptr = NULL;
		for (tok = strtok_r(p, "&", &saveptr); tok!=NULL; tok = strtok_r(NULL, "&", &saveptr)) {
			char *eq = strchr(tok, '=');
			if (eq) {
				*eq++ = 0;
				unescape_input(eq);
				struct keyval kv;
				kv.key = strdup(tok);
				kv.val = strdup(eq);
				array_append(h.request_parms, &kv);
#ifdef DEBUG_HTTP
				syslog(LOG_DEBUG, "| %s = %s", kv.key, kv.val);
#endif
			}
		}
	}

	// build up the site prefix, such as https://foo.bar.com:4343
	h.site_prefix = malloc(256);
	strcpy(h.site_prefix, (is_https ? "https://" : "http://"));
	char *hostheader = header_val(&h, "Host");
	if (hostheader) {
		strcat(h.site_prefix, hostheader);
	}
	else {
		strcat(h.site_prefix, "127.0.0.1");
	}
	socklen_t llen = sizeof(sin);
	if (getsockname(ch->sock, (struct sockaddr *) &sin, &llen) >= 0) {
		sprintf(&h.site_prefix[strlen(h.site_prefix)], ":%d", ntohs(sin.sin_port));
	}

	// Now try to read in the request body (if one is present)
	if (len < 0) {
		syslog(LOG_DEBUG, "Client disconnected");
	}
	else {
//#ifdef DEBUG_HTTP
		syslog(LOG_DEBUG, "< %s %s", h.method, h.url);
//#endif

		// If there is a request body, read it now.
		char *ccl = header_val(&h, "Content-Length");
		if (ccl) {
			h.request_body_length = atol(ccl);
		}
		if (h.request_body_length > 0) {
			syslog(LOG_DEBUG, "Reading request body (%ld bytes)", h.request_body_length);
			h.request_body = malloc(h.request_body_length);
			client_read(ch, h.request_body, h.request_body_length);

			// Write the entire request body to stderr -- not what you want during normal operation.
			#ifdef BODY_TO_STDERR
			write(2, HKEY("---\n"));
			write(2, h.request_body, h.request_body_length);
			write(2, HKEY("---\n"));
			#endif

		}

		// Now pass control up to the next layer to perform the request.
		perform_request(&h);

		// Write the entire response body to stderr -- not what you want during normal operation.
		#ifdef BODY_TO_STDERR
		write(2, HKEY("---\n"));
		write(2, h.response_body, h.response_body_length);
		write(2, HKEY("---\n"));
		#endif

		// Output the results back to the client.
#ifdef DEBUG_HTTP
		syslog(LOG_DEBUG, "> %03d %s", h.response_code, h.response_string);
#endif
		client_printf(ch, "HTTP/1.1 %03d %s\r\n", h.response_code, h.response_string);
		client_printf(ch, "Connection: close\r\n");
		client_printf(ch, "Content-Length: %ld\r\n", h.response_body_length);
		char *datestring = http_datestring(time(NULL));
		if (datestring) {
			client_printf(ch, "Date: %s\r\n", datestring);
			free(datestring);
		}

		client_printf(ch, "Content-encoding: identity\r\n");	// change if we gzip/deflate
		int number_of_response_headers = array_len(h.response_headers);
		for (i=0; i<number_of_response_headers; ++i) {
			struct keyval *kv = array_get_element_at(h.response_headers, i);
#ifdef DEBUG_HTTP
			syslog(LOG_DEBUG, "} %s: %s", kv->key, kv->val);
#endif
			client_printf(ch, "%s: %s\r\n", kv->key, kv->val);
		}
		client_printf(ch, "\r\n");
		if ((h.response_body_length > 0) && (h.response_body != NULL)) {
			client_write(ch, h.response_body, h.response_body_length);
		}
	}

	// free the transaction memory
	while (array_len(h.request_headers) > 0) {
		struct keyval *kv = array_get_element_at(h.request_headers, 0);
		if (kv->key) free(kv->key);
		if (kv->val) free(kv->val);
		array_delete_element_at(h.request_headers, 0);
	}
	array_free(h.request_headers);
	while (array_len(h.request_parms) > 0) {
		struct keyval *kv = array_get_element_at(h.request_parms, 0);
		if (kv->key) free(kv->key);
		if (kv->val) free(kv->val);
		array_delete_element_at(h.request_parms, 0);
	}
	array_free(h.request_parms);
	while (array_len(h.response_headers) > 0) {
		struct keyval *kv = array_get_element_at(h.response_headers, 0);
		if (kv->key) free(kv->key);
		if (kv->val) free(kv->val);
		array_delete_element_at(h.response_headers, 0);
	}
	array_free(h.response_headers);
	if (h.method) {
		free(h.method);
	}
	if (h.url) {
		free(h.url);
	}
	if (h.request_body) {
		free(h.request_body);
	}
	if (h.response_string) {
		free(h.response_string);
	}
	if (h.site_prefix) {
		free(h.site_prefix);
	}
}


// Utility function to fetch a specific header from a completely read-in request.
// Returns the value of the requested header, or NULL if the specified header was not sent.
// The caller does NOT own the memory of the returned pointer, but can count on the pointer
// to still be valid through the end of the transaction.
char *header_val(struct http_transaction *h, char *requested_header) {
	struct keyval *kv;
	int i;
	for (i=0; i<array_len(h->request_headers); ++i) {
		kv = array_get_element_at(h->request_headers, i);
		if (!strcasecmp(kv->key, requested_header)) {
			return (kv->val);
		}
	}
	return (NULL);
}


// Utility function to fetch a specific URL parameter from a completely read-in request.
// Returns the value of the requested parameter, or NULL if the specified parameter was not sent.
// The caller does NOT own the memory of the returned pointer, but can count on the pointer
// to still be valid through the end of the transaction.
char *get_url_param(struct http_transaction *h, char *requested_param) {
	struct keyval *kv;
	int i;
	for (i=0; i<array_len(h->request_parms); ++i) {
		kv = array_get_element_at(h->request_parms, i);
		if (!strcasecmp(kv->key, requested_param)) {
			return (kv->val);
		}
	}
	return (NULL);
}
