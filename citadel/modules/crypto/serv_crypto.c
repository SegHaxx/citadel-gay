/*
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

#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include "sysdep.h"

#ifdef HAVE_OPENSSL
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/rand.h>
#endif

#if TIME_WITH_SYS_TIME
# include <sys/time.h>
# include <time.h>
#else
# if HAVE_SYS_TIME_H
#  include <sys/time.h>
# else
#  include <time.h>
# endif
#endif

#ifdef HAVE_PTHREAD_H
#include <pthread.h>
#endif

#ifdef HAVE_SYS_SELECT_H
#include <sys/select.h>
#endif

#include <stdio.h>
#include <libcitadel.h>
#include "server.h"
#include "serv_crypto.h"
#include "sysdep_decls.h"
#include "citadel.h"
#include "config.h"


#include "ctdl_module.h"

#ifdef HAVE_OPENSSL
SSL_CTX *ssl_ctx;		/* SSL context */
pthread_mutex_t **SSLCritters;	/* Things needing locking */

static unsigned long id_callback(void)
{
	return (unsigned long) pthread_self();
}

void destruct_ssl(void)
{
	int a;
	for (a = 0; a < CRYPTO_num_locks(); a++) 
		free(SSLCritters[a]);
	free (SSLCritters);
}


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
	ret = BN_set_word(bne,e);
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

	SSLCritters = malloc(CRYPTO_num_locks() * sizeof(pthread_mutex_t *));
	if (!SSLCritters) {
		syslog(LOG_ERR, "crypto: can't allocate memory!");
		exit(CTDLEXIT_CRYPTO);
	} else {
		int a;

		for (a = 0; a < CRYPTO_num_locks(); a++) {
			SSLCritters[a] = malloc(sizeof(pthread_mutex_t));
			if (!SSLCritters[a]) {
				syslog(LOG_ERR, "crypto: can't allocate memory!!");
				exit(CTDLEXIT_CRYPTO);
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
		syslog(LOG_ERR, "crypto: SSL_CTX_new failed: %s", ERR_reason_error_string(ERR_get_error()));
		return;
	}
	if (!(SSL_CTX_set_cipher_list(ssl_ctx, CIT_CIPHERS))) {
		syslog(LOG_ERR, "crypto: No ciphers available");
		SSL_CTX_free(ssl_ctx);
		ssl_ctx = NULL;
		return;
	}

	CRYPTO_set_locking_callback(ssl_lock);
	CRYPTO_set_id_callback(id_callback);

	mkdir(ctdl_key_dir, 0700);		// If the keys directory does not exist, create it
	generate_key(file_crpt_file_key);	// If a private key does not exist, create it

	/*
	 * If there is no certificate file on disk, we will be generating a self-signed certificate
	 * in the next step.  Therefore, if we have neither a CSR nor a certificate, generate
	 * the CSR in this step so that the next step may commence.
	 */
	if ( (access(file_crpt_file_cer, R_OK) != 0) && (access(file_crpt_file_csr, R_OK) != 0) ) {
		syslog(LOG_INFO, "crypto: generating a generic certificate signing request.");

		/*
		 * Read our key from the file.  No, we don't just keep this
		 * in memory from the above key-generation function, because
		 * there is the possibility that the key was already on disk
		 * and we didn't just generate it now.
		 */
		fp = fopen(file_crpt_file_key, "r");
		if (fp) {
			rsa = PEM_read_RSAPrivateKey(fp, NULL, NULL, NULL);
			fclose(fp);
		}

		if (rsa) {

			/* Create a public key from the private key */
			if (pk=EVP_PKEY_new(), pk != NULL) {
				EVP_PKEY_assign_RSA(pk, rsa);
				if (req = X509_REQ_new(), req != NULL) {

					/* Set the public key */
					X509_REQ_set_pubkey(req, pk);
					X509_REQ_set_version(req, 0L);

					name = X509_REQ_get_subject_name(req);

					/* Tell it who we are */
					X509_NAME_add_entry_by_txt(name, "C", MBSTRING_ASC, (unsigned const char *)"ZZ", -1, -1, 0);
					X509_NAME_add_entry_by_txt(name, "ST", MBSTRING_ASC, (unsigned const char *)"The World", -1, -1, 0);
					X509_NAME_add_entry_by_txt(name, "L", MBSTRING_ASC, (unsigned const char *)"My Location", -1, -1, 0);
					X509_NAME_add_entry_by_txt(name, "O", MBSTRING_ASC, (unsigned const char *)"Generic certificate", -1, -1, 0);
					X509_NAME_add_entry_by_txt(name, "OU", MBSTRING_ASC, (unsigned const char *)"Citadel server", -1, -1, 0);
					X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC, (unsigned const char *)"*", -1, -1, 0);
					X509_REQ_set_subject_name(req, name);

					/* Sign the CSR */
					if (!X509_REQ_sign(req, pk, EVP_md5())) {
						syslog(LOG_ERR, "crypto: X509_REQ_sign(): error");
					}
					else {
						/* Write it to disk. */	
						fp = fopen(file_crpt_file_csr, "w");
						if (fp != NULL) {
							chmod(file_crpt_file_csr, 0600);
							PEM_write_X509_REQ(fp, req);
							fclose(fp);
						}
					}

					X509_REQ_free(req);
				}
			}

			RSA_free(rsa);
		}

		else {
			syslog(LOG_ERR, "crypto: unable to read private key.");
		}
	}


	/*
	 * Generate a self-signed certificate if we don't have one.
	 */
	if (access(file_crpt_file_cer, R_OK) != 0) {
		syslog(LOG_INFO, "crypto: generating a generic self-signed certificate.");

		/* Same deal as before: always read the key from disk because
		 * it may or may not have just been generated.
		 */
		fp = fopen(file_crpt_file_key, "r");
		if (fp) {
			rsa = PEM_read_RSAPrivateKey(fp, NULL, NULL, NULL);
			fclose(fp);
		}

		/* This also holds true for the CSR. */
		req = NULL;
		cer = NULL;
		pk = NULL;
		if (rsa) {
			if (pk=EVP_PKEY_new(), pk != NULL) {
				EVP_PKEY_assign_RSA(pk, rsa);
			}

			fp = fopen(file_crpt_file_csr, "r");
			if (fp) {
				req = PEM_read_X509_REQ(fp, NULL, NULL, NULL);
				fclose(fp);
			}

			if (req) {
				if (cer = X509_new(), cer != NULL) {
					ASN1_INTEGER_set(X509_get_serialNumber(cer), 0);
					X509_set_issuer_name(cer, X509_REQ_get_subject_name(req));
					X509_set_subject_name(cer, X509_REQ_get_subject_name(req));
					X509_gmtime_adj(X509_get_notBefore(cer),0);
					X509_gmtime_adj(X509_get_notAfter(cer),(long)60*60*24*SIGN_DAYS);
					req_pkey = X509_REQ_get_pubkey(req);
					X509_set_pubkey(cer, req_pkey);
					EVP_PKEY_free(req_pkey);
					
					/* Sign the cert */
					if (!X509_sign(cer, pk, EVP_md5())) {
						syslog(LOG_ERR, "crypto: X509_sign() error");
					}
					else {
						/* Write it to disk. */	
						fp = fopen(file_crpt_file_cer, "w");
						if (fp != NULL) {
							chmod(file_crpt_file_cer, 0600);
							PEM_write_X509(fp, cer);
							fclose(fp);
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
	 */
        SSL_CTX_use_certificate_chain_file(ssl_ctx, file_crpt_file_cer);
        SSL_CTX_use_PrivateKey_file(ssl_ctx, file_crpt_file_key, SSL_FILETYPE_PEM);
        if ( !SSL_CTX_check_private_key(ssl_ctx) ) {
		syslog(LOG_ERR, "crypto: cannot install certificate: %s", ERR_reason_error_string(ERR_get_error()));
        }

	/* Finally let the server know we're here */
	CtdlRegisterProtoHook(cmd_stls, "STLS", "Start SSL/TLS session");
	CtdlRegisterProtoHook(cmd_gtls, "GTLS", "Get SSL/TLS session status");
	CtdlRegisterSessionHook(endtls, EVT_STOP, PRIO_STOP + 10);
}


/*
 * client_write_ssl() Send binary data to the client encrypted.
 */
void client_write_ssl(const char *buf, int nbytes)
{
	int retval;
	int nremain;
	char junk[1];

	nremain = nbytes;

	while (nremain > 0) {
		if (SSL_want_write(CC->ssl)) {
			if ((SSL_read(CC->ssl, junk, 0)) < 1) {
				syslog(LOG_DEBUG, "crypto: SSL_read in client_write: %s", ERR_reason_error_string(ERR_get_error()));
			}
		}
		retval =
		    SSL_write(CC->ssl, &buf[nbytes - nremain], nremain);
		if (retval < 1) {
			long errval;

			errval = SSL_get_error(CC->ssl, retval);
			if (errval == SSL_ERROR_WANT_READ ||
			    errval == SSL_ERROR_WANT_WRITE) {
				sleep(1);
				continue;
			}
			syslog(LOG_DEBUG, "crypto: SSL_write got error %ld, ret %d", errval, retval);
			if (retval == -1) {
				syslog(LOG_DEBUG, "crypto: errno is %d", errno);
			}
			endtls();
			client_write(&buf[nbytes - nremain], nremain);
			return;
		}
		nremain -= retval;
	}
}


/*
 * read data from the encrypted layer.
 */
int client_read_sslbuffer(StrBuf *buf, int timeout)
{
	char sbuf[16384]; /* OpenSSL communicates in 16k blocks, so let's speak its native tongue. */
	int rlen;
	char junk[1];
	SSL *pssl = CC->ssl;

	if (pssl == NULL) return(-1);

	while (1) {
		if (SSL_want_read(pssl)) {
			if ((SSL_write(pssl, junk, 0)) < 1) {
				syslog(LOG_DEBUG, "crypto: SSL_write in client_read");
			}
		}
		rlen = SSL_read(pssl, sbuf, sizeof(sbuf));
		if (rlen < 1) {
			long errval;

			errval = SSL_get_error(pssl, rlen);
			if (errval == SSL_ERROR_WANT_READ || errval == SSL_ERROR_WANT_WRITE) {
				sleep(1);
				continue;
			}
			syslog(LOG_DEBUG, "crypto: SSL_read got error %ld", errval);
			endtls();
			return (-1);
		}
		StrBufAppendBufPlain(buf, sbuf, rlen, 0);
		return rlen;
	}
	return (0);
}

int client_readline_sslbuffer(StrBuf *Line, StrBuf *IOBuf, const char **Pos, int timeout)
{
	CitContext *CCC = CC;
	const char *pos = NULL;
	const char *pLF;
	int len, rlen;
	int nSuccessLess = 0;
	const char *pch = NULL;
	
	if ((Line == NULL) || (Pos == NULL) || (IOBuf == NULL))
	{
		if (Pos != NULL)
		{
			*Pos = NULL;
		}
		return -1;
	}

	pos = *Pos;
	if ((StrLength(IOBuf) > 0) && (pos != NULL) && (pos < ChrPtr(IOBuf) + StrLength(IOBuf))) 
	{
		pch = pos;
		pch = strchr(pch, '\n');
		
		if (pch == NULL) {
			StrBufAppendBufPlain(Line, pos, StrLength(IOBuf) - (pos - ChrPtr(IOBuf)), 0);
			FlushStrBuf(IOBuf);
			*Pos = NULL;
		}
		else {
			int n = 0;
			if ((pch > ChrPtr(IOBuf)) && 
			    (*(pch - 1) == '\r')) {
				n = 1;
			}
			StrBufAppendBufPlain(Line, pos, 
					     (pch - pos - n), 0);

			if (StrLength(IOBuf) <= (pch - ChrPtr(IOBuf) + 1)) {
				FlushStrBuf(IOBuf);
				*Pos = NULL;
			}
			else 
				*Pos = pch + 1;
			return StrLength(Line);
		}
	}

	pLF = NULL;
	while ((nSuccessLess < timeout) && 
	       (pLF == NULL) &&
	       (CCC->ssl != NULL)) {

		rlen = client_read_sslbuffer(IOBuf, timeout);
		if (rlen < 1) {
			return -1;
		}
		else if (rlen > 0) {
			pLF = strchr(ChrPtr(IOBuf), '\n');
		}
	}
	*Pos = NULL;
	if (pLF != NULL) {
		pos = ChrPtr(IOBuf);
		len = pLF - pos;
		if (len > 0 && (*(pLF - 1) == '\r') )
			len --;
		StrBufAppendBufPlain(Line, pos, len, 0);
		if (pLF + 1 >= ChrPtr(IOBuf) + StrLength(IOBuf))
		{
			FlushStrBuf(IOBuf);
		}
		else 
			*Pos = pLF + 1;
		return StrLength(Line);
	}
	return -1;
}


int client_read_sslblob(StrBuf *Target, long bytes, int timeout)
{
	long baselen;
	long RemainRead;
	int retval = 0;
	CitContext *CCC = CC;

	baselen = StrLength(Target);

	if (StrLength(CCC->RecvBuf.Buf) > 0)
	{
		long RemainLen;
		long TotalLen;
		const char *pchs;

		if (CCC->RecvBuf.ReadWritePointer == NULL)
		{
			CCC->RecvBuf.ReadWritePointer = ChrPtr(CCC->RecvBuf.Buf);
		}
		pchs = ChrPtr(CCC->RecvBuf.Buf);
		TotalLen = StrLength(CCC->RecvBuf.Buf);
		RemainLen = TotalLen - (pchs - CCC->RecvBuf.ReadWritePointer);
		if (RemainLen > bytes)
		{
			RemainLen = bytes;
		}
		if (RemainLen > 0)
		{
			StrBufAppendBufPlain(Target, CCC->RecvBuf.ReadWritePointer, RemainLen, 0);
			CCC->RecvBuf.ReadWritePointer += RemainLen;
		}
		if ((ChrPtr(CCC->RecvBuf.Buf) + StrLength(CCC->RecvBuf.Buf)) <= CCC->RecvBuf.ReadWritePointer)
		{
			CCC->RecvBuf.ReadWritePointer = NULL;
			FlushStrBuf(CCC->RecvBuf.Buf);
		}
	}

	if (StrLength(Target) >= bytes + baselen)
	{
		return 1;
	}

	CCC->RecvBuf.ReadWritePointer = NULL;

	while ((StrLength(Target) < bytes + baselen) && (retval >= 0))
	{
		retval = client_read_sslbuffer(CCC->RecvBuf.Buf, timeout);
		if (retval >= 0) {
			RemainRead = bytes - (StrLength (Target) - baselen);
			if (RemainRead < StrLength(CCC->RecvBuf.Buf))
			{
				StrBufAppendBufPlain(
					Target, 
					ChrPtr(CCC->RecvBuf.Buf), 
					RemainRead, 0);
				CCC->RecvBuf.ReadWritePointer = ChrPtr(CCC->RecvBuf.Buf) + RemainRead;
				break;
			}
			StrBufAppendBuf(Target, CCC->RecvBuf.Buf, 0); /* todo: Buf > bytes? */
			FlushStrBuf(CCC->RecvBuf.Buf);
		}
		else 
		{
			FlushStrBuf(CCC->RecvBuf.Buf);
			return -1;
	
		}
	}
	return 1;
}


/*
 * CtdlStartTLS() starts SSL/TLS encryption for the current session.  It
 * must be supplied with pre-generated strings for responses of "ok," "no
 * support for TLS," and "error" so that we can use this in any protocol.
 */
void CtdlStartTLS(char *ok_response, char *nosup_response, char *error_response)
{
	int retval, bits, alg_bits;

	if (!ssl_ctx) {
		syslog(LOG_ERR, "crypto: SSL failed: no ssl_ctx exists?");
		if (nosup_response != NULL) cprintf("%s", nosup_response);
		return;
	}
	if (!(CC->ssl = SSL_new(ssl_ctx))) {
		syslog(LOG_ERR, "crypto: SSL_new failed: %s", ERR_reason_error_string(ERR_get_error()));
		if (error_response != NULL) {
			cprintf("%s", error_response);
		}
		return;
	}
	if (!(SSL_set_fd(CC->ssl, CC->client_socket))) {
		syslog(LOG_ERR, "crypto: SSL_set_fd failed: %s", ERR_reason_error_string(ERR_get_error()));
		SSL_free(CC->ssl);
		CC->ssl = NULL;
		if (error_response != NULL) cprintf("%s", error_response);
		return;
	}
	if (ok_response != NULL) cprintf("%s", ok_response);
	retval = SSL_accept(CC->ssl);
	if (retval < 1) {
		/*
		 * Can't notify the client of an error here; they will
		 * discover the problem at the SSL layer and should
		 * revert to unencrypted communications.
		 */
		long errval;
		char error_string[128];

		errval = SSL_get_error(CC->ssl, retval);
		syslog(LOG_ERR, "crypto: SSL_accept failed: retval=%d, errval=%ld, err=%s",
			retval,
			errval,
			ERR_error_string(errval, error_string)
		);
		SSL_free(CC->ssl);
		CC->ssl = NULL;
		return;
	}
	// BIO_set_close(CC->ssl->rbio, BIO_NOCLOSE); not needed anymore in openssl 1.1 ?
	bits = SSL_CIPHER_get_bits(SSL_get_current_cipher(CC->ssl), &alg_bits);
	syslog(LOG_INFO, "crypto: SSL/TLS using %s on %s (%d of %d bits)",
		SSL_CIPHER_get_name(SSL_get_current_cipher(CC->ssl)),
		SSL_CIPHER_get_version(SSL_get_current_cipher(CC->ssl)),
		bits, alg_bits
	);
	CC->redirect_ssl = 1;
}


/*
 * cmd_stls() starts SSL/TLS encryption for the current session
 */
void cmd_stls(char *params)
{
	char ok_response[SIZ];
	char nosup_response[SIZ];
	char error_response[SIZ];

	unbuffer_output();

	sprintf(ok_response, "%d Begin TLS negotiation now\n", CIT_OK);
	sprintf(nosup_response, "%d TLS not supported here\n", ERROR + CMD_NOT_SUPPORTED);
	sprintf(error_response, "%d TLS negotiation error\n", ERROR + INTERNAL_ERROR);

	CtdlStartTLS(ok_response, nosup_response, error_response);
}


/*
 * cmd_gtls() returns status info about the TLS connection
 */
void cmd_gtls(char *params)
{
	int bits, alg_bits;

	if (!CC->ssl || !CC->redirect_ssl) {
		cprintf("%d Session is not encrypted.\n", ERROR);
		return;
	}
	bits =
	    SSL_CIPHER_get_bits(SSL_get_current_cipher(CC->ssl),
				&alg_bits);
	cprintf("%d %s|%s|%d|%d\n", CIT_OK,
		SSL_CIPHER_get_version(SSL_get_current_cipher(CC->ssl)),
		SSL_CIPHER_get_name(SSL_get_current_cipher(CC->ssl)),
		alg_bits, bits);
}


/*
 * endtls() shuts down the TLS connection
 *
 * WARNING:  This may make your session vulnerable to a known plaintext
 * attack in the current implmentation.
 */
void endtls(void)
{
	if (!CC->ssl) {
		CC->redirect_ssl = 0;
		return;
	}

	syslog(LOG_INFO, "crypto: ending SSL/TLS");
	SSL_shutdown(CC->ssl);
	SSL_free(CC->ssl);
	CC->ssl = NULL;
	CC->redirect_ssl = 0;
}


/*
 * ssl_lock() callback for OpenSSL mutex locks
 */
void ssl_lock(int mode, int n, const char *file, int line)
{
	if (mode & CRYPTO_LOCK)
	{
		pthread_mutex_lock(SSLCritters[n]);
	}
	else {
		pthread_mutex_unlock(SSLCritters[n]);
	}
}
#endif				/* HAVE_OPENSSL */
