/*
 * Functions in this module handle SSL encryption when WebCit is running
 * as an HTTPS server.
 *
 * Copyright (c) 1996-2018 by the citadel.org team
 *
 * This program is open source software.  You can redistribute it and/or
 * modify it under the terms of the GNU General Public License, version 3.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include "webcit.h"

SSL_CTX *ssl_ctx;		/* SSL context */
pthread_mutex_t **SSLCritters;	/* Things needing locking */
char *ssl_cipher_list = DEFAULT_SSL_CIPHER_LIST;
void ssl_lock(int mode, int n, const char *file, int line);


/*
 * OpenSSL wants a callback function to identify the currently running thread.
 * Since we are a pthreads program, we convert the output of pthread_self() to a long.
 */
static unsigned long id_callback(void)
{
	return (unsigned long) pthread_self();
}


/*
 * OpenSSL wants a callback function to set and clear various types of locks.
 * Since we are a pthreads program, we use mutexes.
 */
void ssl_lock(int mode, int n, const char *file, int line)
{
	if (mode & CRYPTO_LOCK) {
		pthread_mutex_lock(SSLCritters[n]);
	} else {
		pthread_mutex_unlock(SSLCritters[n]);
	}
}


/*
 * Generate a private key for SSL
 */
void generate_key(char *keyfilename)
{
	int ret = 0;
	RSA *rsa = NULL;
	BIGNUM *bne = NULL;
	int bits = 2048;
	unsigned long e = RSA_F4;
	FILE *fp;

	if (access(keyfilename, R_OK) == 0) {
		return;
	}

	syslog(LOG_INFO, "crypto: generating RSA key pair");

	// generate rsa key
	bne = BN_new();
	ret = BN_set_word(bne, e);
	if (ret != 1) {
		goto free_all;
	}

	rsa = RSA_new();
	ret = RSA_generate_key_ex(rsa, bits, bne, NULL);
	if (ret != 1) {
		goto free_all;
	}
	// write the key file
	fp = fopen(keyfilename, "w");
	if (fp != NULL) {
		chmod(keyfilename, 0600);
		if (PEM_write_RSAPrivateKey(fp,	/* the file */
					    rsa,	/* the key */
					    NULL,	/* no enc */
					    NULL,	/* no passphr */
					    0,	/* no passphr */
					    NULL,	/* no callbk */
					    NULL	/* no callbk */
		    ) != 1) {
			syslog(LOG_ERR, "crypto: cannot write key: %s", ERR_reason_error_string(ERR_get_error()));
			unlink(keyfilename);
		}
		fclose(fp);
	}
	// 4. free
      free_all:
	RSA_free(rsa);
	BN_free(bne);
}


/*
 * Initialize ssl engine, load certs and initialize openssl internals
 */
void init_ssl(void)
{
	const SSL_METHOD *ssl_method;
	RSA *rsa = NULL;
	X509_REQ *req = NULL;
	X509 *cer = NULL;
	EVP_PKEY *pk = NULL;
	EVP_PKEY *req_pkey = NULL;
	X509_NAME *name = NULL;
	FILE *fp;
	char buf[SIZ];
	int rv = 0;

	SSLCritters = malloc(CRYPTO_num_locks() * sizeof(pthread_mutex_t *));
	if (!SSLCritters) {
		syslog(LOG_ERR, "citserver: can't allocate memory!!");
		exit(1);
	} else {
		int a;
		for (a = 0; a < CRYPTO_num_locks(); a++) {
			SSLCritters[a] = malloc(sizeof(pthread_mutex_t));
			if (!SSLCritters[a]) {
				syslog(LOG_INFO, "citserver: can't allocate memory!!");
				exit(1);
			}
			pthread_mutex_init(SSLCritters[a], NULL);
		}
	}

	/*
	 * Initialize SSL transport layer
	 */
	SSL_library_init();
	SSL_load_error_strings();
	ssl_method = SSLv23_server_method();
	if (!(ssl_ctx = SSL_CTX_new(ssl_method))) {
		syslog(LOG_WARNING, "SSL_CTX_new failed: %s", ERR_reason_error_string(ERR_get_error()));
		return;
	}

	syslog(LOG_INFO, "Requesting cipher list: %s", ssl_cipher_list);
	if (!(SSL_CTX_set_cipher_list(ssl_ctx, ssl_cipher_list))) {
		syslog(LOG_WARNING, "SSL_CTX_set_cipher_list failed: %s", ERR_reason_error_string(ERR_get_error()));
		return;
	}

	CRYPTO_set_locking_callback(ssl_lock);
	CRYPTO_set_id_callback(id_callback);

	/*
	 * Get our certificates in order.
	 * First, create the key/cert directory if it's not there already...
	 */
	mkdir(CTDL_CRYPTO_DIR, 0700);

	/*
	 * If we still don't have a private key, generate one.
	 */
	generate_key(CTDL_KEY_PATH);

	/*
	 * If there is no certificate file on disk, we will be generating a self-signed certificate
	 * in the next step.  Therefore, if we have neither a CSR nor a certificate, generate
	 * the CSR in this step so that the next step may commence.
	 */
	if ((access(CTDL_CER_PATH, R_OK) != 0) && (access(CTDL_CSR_PATH, R_OK) != 0)) {
		syslog(LOG_INFO, "Generating a certificate signing request.");

		/*
		 * Read our key from the file.  No, we don't just keep this
		 * in memory from the above key-generation function, because
		 * there is the possibility that the key was already on disk
		 * and we didn't just generate it now.
		 */
		fp = fopen(CTDL_KEY_PATH, "r");
		if (fp) {
			rsa = PEM_read_RSAPrivateKey(fp, NULL, NULL, NULL);
			fclose(fp);
		}

		if (rsa) {
			/* Create a public key from the private key */
			if (pk = EVP_PKEY_new(), pk != NULL) {
				EVP_PKEY_assign_RSA(pk, rsa);
				if (req = X509_REQ_new(), req != NULL) {
					const char *env;
					/* Set the public key */
					X509_REQ_set_pubkey(req, pk);
					X509_REQ_set_version(req, 0L);
					name = X509_REQ_get_subject_name(req);
					X509_NAME_add_entry_by_txt(name, "O", MBSTRING_ASC,
								   (unsigned char *) "Citadel Server", -1, -1, 0);
					X509_NAME_add_entry_by_txt(name, "OU", MBSTRING_ASC,
								   (unsigned char *) "Default Certificate PLEASE CHANGE",
								   -1, -1, 0);
					X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC, (unsigned char *) "*", -1, -1, 0);

					X509_REQ_set_subject_name(req, name);

					/* Sign the CSR */
					if (!X509_REQ_sign(req, pk, EVP_md5())) {
						syslog(LOG_WARNING, "X509_REQ_sign(): error");
					} else {
						/* Write it to disk. */
						fp = fopen(CTDL_CSR_PATH, "w");
						if (fp != NULL) {
							chmod(CTDL_CSR_PATH, 0600);
							PEM_write_X509_REQ(fp, req);
							fclose(fp);
						} else {
							syslog(LOG_WARNING, "Cannot write key: %s", CTDL_CSR_PATH);
							exit(1);
						}
					}
					X509_REQ_free(req);
				}
			}
			RSA_free(rsa);
		} else {
			syslog(LOG_WARNING, "Unable to read private key.");
		}
	}

	/*
	 * Generate a self-signed certificate if we don't have one.
	 */
	if (access(CTDL_CER_PATH, R_OK) != 0) {
		syslog(LOG_INFO, "Generating a self-signed certificate.");

		/* Same deal as before: always read the key from disk because
		 * it may or may not have just been generated.
		 */
		fp = fopen(CTDL_KEY_PATH, "r");
		if (fp) {
			rsa = PEM_read_RSAPrivateKey(fp, NULL, NULL, NULL);
			fclose(fp);
		}

		/* This also holds true for the CSR. */
		req = NULL;
		cer = NULL;
		pk = NULL;
		if (rsa) {
			if (pk = EVP_PKEY_new(), pk != NULL) {
				EVP_PKEY_assign_RSA(pk, rsa);
			}

			fp = fopen(CTDL_CSR_PATH, "r");
			if (fp) {
				req = PEM_read_X509_REQ(fp, NULL, NULL, NULL);
				fclose(fp);
			}

			if (req) {
				if (cer = X509_new(), cer != NULL) {
					ASN1_INTEGER_set(X509_get_serialNumber(cer), 0);
					X509_set_issuer_name(cer, X509_REQ_get_subject_name(req));
					X509_set_subject_name(cer, X509_REQ_get_subject_name(req));
					X509_gmtime_adj(X509_get_notBefore(cer), 0);
					X509_gmtime_adj(X509_get_notAfter(cer), (long) 60 * 60 * 24 * SIGN_DAYS);

					req_pkey = X509_REQ_get_pubkey(req);
					X509_set_pubkey(cer, req_pkey);
					EVP_PKEY_free(req_pkey);

					/* Sign the cert */
					if (!X509_sign(cer, pk, EVP_md5())) {
						syslog(LOG_WARNING, "X509_sign(): error");
					} else {	/* Write it to disk. */
						fp = fopen(CTDL_CER_PATH, "w");
						if (fp != NULL) {
							chmod(CTDL_CER_PATH, 0600);
							PEM_write_X509(fp, cer);
							fclose(fp);
						} else {
							syslog(LOG_WARNING, "Cannot write key: %s", CTDL_CER_PATH);
							exit(1);
						}
					}
					X509_free(cer);
				}
			}
			RSA_free(rsa);
		}
	}

	/*
	 * Now try to bind to the key and certificate.
	 * Note that we use SSL_CTX_use_certificate_chain_file() which allows
	 * the certificate file to contain intermediate certificates.
	 */
	SSL_CTX_use_certificate_chain_file(ssl_ctx, CTDL_CER_PATH);
	SSL_CTX_use_PrivateKey_file(ssl_ctx, CTDL_KEY_PATH, SSL_FILETYPE_PEM);
	if (!SSL_CTX_check_private_key(ssl_ctx)) {
		syslog(LOG_WARNING, "Cannot install certificate: %s", ERR_reason_error_string(ERR_get_error()));
	}

}


/*
 * starts SSL/TLS encryption for the current session.
 */
void starttls(struct client_handle *ch)
{
	int retval, bits, alg_bits;

	if (!ssl_ctx) {
		return;
	}
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
		long errval;
		const char *ssl_error_reason = NULL;

		errval = SSL_get_error(ch->ssl_handle, retval);
		ssl_error_reason = ERR_reason_error_string(ERR_get_error());
		if (ssl_error_reason == NULL) {
			syslog(LOG_WARNING, "SSL_accept failed: errval=%ld, retval=%d %s", errval, retval, strerror(errval));
		} else {
			syslog(LOG_WARNING, "SSL_accept failed: %s\n", ssl_error_reason);
		}
		sleep(1);
		retval = SSL_accept(ch->ssl_handle);
	}
	if (retval < 1) {
		long errval;
		const char *ssl_error_reason = NULL;

		errval = SSL_get_error(ch->ssl_handle, retval);
		ssl_error_reason = ERR_reason_error_string(ERR_get_error());
		if (ssl_error_reason == NULL) {
			syslog(LOG_WARNING, "SSL_accept failed: errval=%ld, retval=%d (%s)", errval, retval, strerror(errval));
		} else {
			syslog(LOG_WARNING, "SSL_accept failed: %s", ssl_error_reason);
		}
		SSL_free(ch->ssl_handle);
		ch->ssl_handle = NULL;
		return;
	} else {
		syslog(LOG_INFO, "SSL_accept success");
	}
	bits = SSL_CIPHER_get_bits(SSL_get_current_cipher(ch->ssl_handle), &alg_bits);
	syslog(LOG_INFO, "SSL/TLS using %s on %s (%d of %d bits)",
	       SSL_CIPHER_get_name(SSL_get_current_cipher(ch->ssl_handle)),
	       SSL_CIPHER_get_version(SSL_get_current_cipher(ch->ssl_handle)), bits, alg_bits);

	syslog(LOG_INFO, "SSL started");
}


/*
 * shuts down the TLS connection
 */
void endtls(struct client_handle *ch)
{
	syslog(LOG_INFO, "Ending SSL/TLS");
	if (ch->ssl_handle != NULL) {
		SSL_shutdown(ch->ssl_handle);
		SSL_get_SSL_CTX(ch->ssl_handle);
		SSL_free(ch->ssl_handle);
	}
	ch->ssl_handle = NULL;
}


/*
 * Send binary data to the client encrypted.
 */
int client_write_ssl(struct client_handle *ch, char *buf, int nbytes)
{
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
			syslog(LOG_WARNING, "SSL_write got error %ld, ret %d", errval, retval);
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


/*
 * read data from the encrypted layer.
 */
int client_read_ssl(struct client_handle *ch, char *buf, int nbytes)
{
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
