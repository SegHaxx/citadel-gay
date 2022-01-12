// Functions in this module handle SSL encryption when WebCit is running
// as an HTTPS server.
//
// Copyright (c) 1996-2022 by the citadel.org team
//
// This program is open source software.  It runs great on the
// Linux operating system (and probably elsewhere).  You can use,
// copy, and run it under the terms of the GNU General Public
// License version 3.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

#include "webcit.h"

SSL_CTX *ssl_ctx;		// global SSL context
char key_file[PATH_MAX] = "";
char cert_file[PATH_MAX] = "";
char *ssl_cipher_list = DEFAULT_SSL_CIPHER_LIST;


// Set the private key and certificate chain for the global SSL Context.
// This is called during initialization, and can be called again later if the certificate changes.
void bind_to_key_and_certificate(void) {
	SSL_CTX *old_ctx, *new_ctx;

	if (!(new_ctx = SSL_CTX_new(SSLv23_server_method()))) {
		syslog(LOG_WARNING, "SSL_CTX_new failed: %s", ERR_reason_error_string(ERR_get_error()));
		return;
	}

	syslog(LOG_INFO, "Requesting cipher list: %s", ssl_cipher_list);
	if (!(SSL_CTX_set_cipher_list(new_ctx, ssl_cipher_list))) {
		syslog(LOG_WARNING, "SSL_CTX_set_cipher_list failed: %s", ERR_reason_error_string(ERR_get_error()));
		return;
	}

	if (IsEmptyStr(key_file)) {
		snprintf(key_file, sizeof key_file, "%s/keys/citadel.key", ctdl_dir);
	}
	if (IsEmptyStr(cert_file)) {
		snprintf(cert_file, sizeof key_file, "%s/keys/citadel.cer", ctdl_dir);
	}

	syslog(LOG_DEBUG, "crypto: [re]installing key \"%s\" and certificate \"%s\"", key_file, cert_file);

	SSL_CTX_use_certificate_chain_file(new_ctx, cert_file);
	SSL_CTX_use_PrivateKey_file(new_ctx, key_file, SSL_FILETYPE_PEM);

	if ( !SSL_CTX_check_private_key(new_ctx) ) {
		syslog(LOG_WARNING, "crypto: cannot install certificate: %s", ERR_reason_error_string(ERR_get_error()));
	}

	old_ctx = ssl_ctx;
	ssl_ctx = new_ctx;
	sleep(1);
	SSL_CTX_free(old_ctx);
}


// Initialize ssl engine, load certs and initialize openssl internals
void init_ssl(void) {

	// Initialize the OpenSSL library
	SSL_load_error_strings();
	ERR_load_crypto_strings();
	OpenSSL_add_all_algorithms();
	SSL_library_init();

	// Now try to bind to the key and certificate.
	bind_to_key_and_certificate();
}


// Check the modification time of the key and certificate -- reload if they changed
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
void starttls(struct client_handle *ch) {
	int retval, bits, alg_bits;

	if (!ssl_ctx) {
		return;
	}

	// Check the modification time of the key and certificate -- reload if they changed
	update_key_and_cert_if_needed();

	if (!(ch->ssl_handle = SSL_new(ssl_ctx))) {
		syslog(LOG_WARNING, "SSL_new failed: %s", ERR_reason_error_string(ERR_get_error()));
		return;
	}
	if (!(SSL_set_fd(ch->ssl_handle, ch->sock))) {
		syslog(LOG_WARNING, "SSL_set_fd failed: %s", ERR_reason_error_string(ERR_get_error()));
		SSL_free(ch->ssl_handle);
		return;
	}
	retval = SSL_accept(ch->ssl_handle);
	if (retval < 1) {
		syslog(LOG_WARNING, "SSL_accept failed: %s", ERR_reason_error_string(ERR_get_error()));
	}
	else {
		syslog(LOG_INFO, "SSL_accept success");
	}
	bits = SSL_CIPHER_get_bits(SSL_get_current_cipher(ch->ssl_handle), &alg_bits);
	syslog(LOG_INFO, "SSL/TLS using %s on %s (%d of %d bits)",
	       SSL_CIPHER_get_name(SSL_get_current_cipher(ch->ssl_handle)),
	       SSL_CIPHER_get_version(SSL_get_current_cipher(ch->ssl_handle)), bits, alg_bits);
	syslog(LOG_INFO, "SSL started");
}


// shuts down the TLS connection
void endtls(struct client_handle *ch) {
	syslog(LOG_INFO, "Ending SSL/TLS");
	if (ch->ssl_handle != NULL) {
		SSL_shutdown(ch->ssl_handle);
		SSL_get_SSL_CTX(ch->ssl_handle);
		SSL_free(ch->ssl_handle);
	}
	ch->ssl_handle = NULL;
}


// Send binary data to the client encrypted.
int client_write_ssl(struct client_handle *ch, char *buf, int nbytes) {
	int retval;
	int nremain;
	char junk[1];

	if (ch->ssl_handle == NULL)
		return (-1);

	nremain = nbytes;
	while (nremain > 0) {
		if (SSL_want_write(ch->ssl_handle)) {
			if ((SSL_read(ch->ssl_handle, junk, 0)) < 1) {
				syslog(LOG_WARNING, "SSL_read in client_write: %s", ERR_reason_error_string(ERR_get_error()));
			}
		}
		retval = SSL_write(ch->ssl_handle, &buf[nbytes - nremain], nremain);
		if (retval < 1) {
			long errval;

			errval = SSL_get_error(ch->ssl_handle, retval);
			if (errval == SSL_ERROR_WANT_READ || errval == SSL_ERROR_WANT_WRITE) {
				sleep(1);
				continue;
			}
			syslog(LOG_WARNING, "SSL_write: %s", ERR_reason_error_string(ERR_get_error()));
			if (retval == -1) {
				syslog(LOG_WARNING, "errno is %d", errno);
				endtls(ch);
			}
			return -1;
		}
		nremain -= retval;
	}
	return 0;
}


// read data from the encrypted layer
int client_read_ssl(struct client_handle *ch, char *buf, int nbytes) {
	int bytes_read = 0;
	int rlen = 0;
	char junk[1];

	if (ch->ssl_handle == NULL)
		return (-1);

	while (bytes_read < nbytes) {
		if (SSL_want_read(ch->ssl_handle)) {
			if ((SSL_write(ch->ssl_handle, junk, 0)) < 1) {
				syslog(LOG_WARNING, "SSL_write in client_read");
			}
		}
		rlen = SSL_read(ch->ssl_handle, &buf[bytes_read], nbytes - bytes_read);
		if (rlen < 1) {
			long errval;
			errval = SSL_get_error(ch->ssl_handle, rlen);
			if (errval == SSL_ERROR_WANT_READ || errval == SSL_ERROR_WANT_WRITE) {
				sleep(1);
				continue;
			}
			syslog(LOG_WARNING, "SSL_read error %ld", errval);
			endtls(ch);
			return (-1);
		}
		bytes_read += rlen;
	}
	return (bytes_read);
}
