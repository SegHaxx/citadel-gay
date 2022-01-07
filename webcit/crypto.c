// Copyright (c) 1996-2021 by the citadel.org team
//
// This program is open source software.  You can redistribute it and/or
// modify it under the terms of the GNU General Public License, version 3.
// 
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

#include "sysdep.h"
#ifdef HAVE_OPENSSL

#include "webcit.h"
#include "webserver.h"


SSL_CTX *ssl_ctx;		// Global SSL context
char key_file[PATH_MAX] = "";
char cert_file[PATH_MAX] = "";
char *ssl_cipher_list = DEFAULT_SSL_CIPHER_LIST;

pthread_key_t ThreadSSL;	// Per-thread SSL context

void shutdown_ssl(void) {
	ERR_free_strings();
}


// Set the private key and certificate chain for the global SSL Context.
// This is called during initialization, and can be called again later if the certificate changes.
void bind_to_key_and_certificate(void) {
	if (IsEmptyStr(key_file)) {
		snprintf(key_file, sizeof key_file, "%s/keys/citadel.key", ctdl_dir);
	}
	if (IsEmptyStr(cert_file)) {
		snprintf(cert_file, sizeof key_file, "%s/keys/citadel.cer", ctdl_dir);
	}

	syslog(LOG_DEBUG, "crypto: [re]installing key \"%s\" and certificate \"%s\"", key_file, cert_file);

	SSL_CTX_use_certificate_chain_file(ssl_ctx, cert_file);
	SSL_CTX_use_PrivateKey_file(ssl_ctx, key_file, SSL_FILETYPE_PEM);

	if ( !SSL_CTX_check_private_key(ssl_ctx) ) {
		syslog(LOG_WARNING, "crypto: cannot install certificate: %s", ERR_reason_error_string(ERR_get_error()));
	}
}


// initialize ssl engine, load certs and initialize openssl internals
void init_ssl(void) {

#ifndef OPENSSL_NO_EGD
	if (!access("/var/run/egd-pool", F_OK)) {
		RAND_egd("/var/run/egd-pool");
	}
#endif

	if (!RAND_status()) {
		syslog(LOG_WARNING, "PRNG not adequately seeded, won't do SSL/TLS");
		return;
	}

	// Initialize SSL transport layer
	SSL_library_init();
	SSL_load_error_strings();
	if (!(ssl_ctx = SSL_CTX_new(SSLv23_server_method()))) {
		syslog(LOG_WARNING, "SSL_CTX_new failed: %s", ERR_reason_error_string(ERR_get_error()));
		return;
	}

	syslog(LOG_INFO, "Requesting cipher list: %s", ssl_cipher_list);
	if (!(SSL_CTX_set_cipher_list(ssl_ctx, ssl_cipher_list))) {
		syslog(LOG_WARNING, "SSL_CTX_set_cipher_list failed: %s", ERR_reason_error_string(ERR_get_error()));
		return;
	}


	// Now try to bind to the key and certificate.
	bind_to_key_and_certificate();
}


// Check the modification time of the key and certificate -- reload if either one changed
void update_key_and_cert_if_needed(void) {
	static time_t previous_mtime = 0;
	struct stat keystat;
	struct stat certstat;

	if (stat(key_file, &keystat) != 0) {
		syslog(LOG_ERR, "%s: %s", key_file, strerror(errno));
		return;
	}
	if (stat(cert_file, &certstat) != 0) {
		syslog(LOG_ERR, "%s: %s", cert_file, strerror(errno));
		return;
	}

	if ((keystat.st_mtime + certstat.st_mtime) != previous_mtime) {
		bind_to_key_and_certificate();
		previous_mtime = keystat.st_mtime + certstat.st_mtime;
	}
}


// starts SSL/TLS encryption for the current session.
int starttls(int sock) {
	SSL *newssl;
	int retval, bits, alg_bits;

	// Check the modification time of the key and certificate -- reload if they changed
	update_key_and_cert_if_needed();
	
	// SSL is a thread-specific thing, I think.
	pthread_setspecific(ThreadSSL, NULL);

	if (!ssl_ctx) {
		return(1);
	}
	if (!(newssl = SSL_new(ssl_ctx))) {
		syslog(LOG_WARNING, "SSL_new failed: %s", ERR_reason_error_string(ERR_get_error()));
		return(2);
	}
	if (!(SSL_set_fd(newssl, sock))) {
		syslog(LOG_WARNING, "SSL_set_fd failed: %s", ERR_reason_error_string(ERR_get_error()));
		SSL_free(newssl);
		return(3);
	}
	retval = SSL_accept(newssl);
	if (retval < 1) {
		// Can't notify the client of an error here; they will
		// discover the problem at the SSL layer and should
		// revert to unencrypted communications.
		long errval;
		const char *ssl_error_reason = NULL;

		errval = SSL_get_error(newssl, retval);
		ssl_error_reason = ERR_reason_error_string(ERR_get_error());
		if (ssl_error_reason == NULL) {
			syslog(LOG_WARNING, "first SSL_accept failed: errval=%ld, retval=%d %s", errval, retval, strerror(errval));
		}
		else {
			syslog(LOG_WARNING, "first SSL_accept failed: %s", ssl_error_reason);
		}
		// sleeeeeeeeeep(1);
		// retval = SSL_accept(newssl);
	}
	// if (retval < 1) {
		// long errval;
		// const char *ssl_error_reason = NULL;
// 
		// errval = SSL_get_error(newssl, retval);
		// ssl_error_reason = ERR_reason_error_string(ERR_get_error());
		// if (ssl_error_reason == NULL) {
			// syslog(LOG_WARNING, "second SSL_accept failed: errval=%ld, retval=%d (%s)", errval, retval, strerror(errval));
		// }
		// else {
			// syslog(LOG_WARNING, "second SSL_accept failed: %s", ssl_error_reason);
		// }
		// SSL_free(newssl);
		// newssl = NULL;
		// return(4);
	// }
	else {
		syslog(LOG_INFO, "SSL_accept success");
	}
	/*r = */BIO_set_close(SSL_get_rbio(newssl), BIO_NOCLOSE);
	bits = SSL_CIPHER_get_bits(SSL_get_current_cipher(newssl), &alg_bits);
	syslog(LOG_INFO, "SSL/TLS using %s on %s (%d of %d bits)",
		SSL_CIPHER_get_name(SSL_get_current_cipher(newssl)),
		SSL_CIPHER_get_version(SSL_get_current_cipher(newssl)),
		bits, alg_bits);

	pthread_setspecific(ThreadSSL, newssl);
	syslog(LOG_INFO, "SSL started");
	return(0);
}


// shuts down the TLS connection
//
// WARNING:  This may make your session vulnerable to a known plaintext
// attack in the current implmentation.
void endtls(void) {

	if (THREADSSL == NULL) {
		return;
	}

	syslog(LOG_INFO, "Ending SSL/TLS");
	SSL_shutdown(THREADSSL);
	SSL_get_SSL_CTX(THREADSSL);
	SSL_free(THREADSSL);
	pthread_setspecific(ThreadSSL, NULL);
}


// Send binary data to the client encrypted.
int client_write_ssl(const StrBuf *Buf) {
	const char *buf;
	int retval;
	int nremain;
	long nbytes;
	char junk[1];

	if (THREADSSL == NULL) return -1;

	nbytes = nremain = StrLength(Buf);
	buf = ChrPtr(Buf);

	while (nremain > 0) {
		if (SSL_want_write(THREADSSL)) {
			if ((SSL_read(THREADSSL, junk, 0)) < 1) {
				syslog(LOG_WARNING, "SSL_read in client_write: %s", ERR_reason_error_string(ERR_get_error()));
			}
		}
		retval = SSL_write(THREADSSL, &buf[nbytes - nremain], nremain);
		if (retval < 1) {
			long errval;

			errval = SSL_get_error(THREADSSL, retval);
			if (errval == SSL_ERROR_WANT_READ || errval == SSL_ERROR_WANT_WRITE) {
				sleeeeeeeeeep(1);
				continue;
			}
			syslog(LOG_WARNING, "SSL_write: %s", ERR_reason_error_string(ERR_get_error()));
			if (retval == -1) {
				syslog(LOG_WARNING, "errno is %d\n", errno);
			}
			endtls();
			return -1;
		}
		nremain -= retval;
	}
	return 0;
}


// read data from the encrypted layer.
int client_read_sslbuffer(StrBuf *buf, int timeout) {
	char sbuf[16384];	// OpenSSL communicates in 16k blocks, so let's speak its native tongue.
	int rlen;
	char junk[1];
	SSL *pssl = THREADSSL;

	if (pssl == NULL) return(-1);

	while (1) {
		if (SSL_want_read(pssl)) {
			if ((SSL_write(pssl, junk, 0)) < 1) {
				syslog(LOG_WARNING, "SSL_write in client_read");
			}
		}
		rlen = SSL_read(pssl, sbuf, sizeof(sbuf));
		if (rlen < 1) {
			long errval;

			errval = SSL_get_error(pssl, rlen);
			if (errval == SSL_ERROR_WANT_READ || errval == SSL_ERROR_WANT_WRITE) {
				sleeeeeeeeeep(1);
				continue;
			}
			syslog(LOG_WARNING, "SSL_read in client_read: %s", ERR_reason_error_string(ERR_get_error()));
			endtls();
			return (-1);
		}
		StrBufAppendBufPlain(buf, sbuf, rlen, 0);
		return rlen;
	}
	return (0);
}

#endif				/* HAVE_OPENSSL */
