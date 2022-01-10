
/*
 * Number of days for which self-signed certs are valid.
 */
#define SIGN_DAYS	1100	// Just over three years

// Which ciphers will be offered; see https://www.openssl.org/docs/manmaster/man1/ciphers.html
//#define CIT_CIPHERS	"ALL:RC4+RSA:+SSLv2:+TLSv1:!MD5:@STRENGTH"
#define CIT_CIPHERS	"DEFAULT"

#ifdef HAVE_OPENSSL
#define OPENSSL_NO_KRB5		/* work around redhat b0rken ssl headers */
void init_ssl(void);
void client_write_ssl (const char *buf, int nbytes);
int client_read_sslbuffer(StrBuf *buf, int timeout);
int client_readline_sslbuffer(StrBuf *Target, StrBuf *Buffer, const char **Pos, int timeout);
int client_read_sslblob(StrBuf *Target, long want_len, int timeout);
void cmd_stls(char *params);
void cmd_gtls(char *params);
void endtls(void);
void CtdlStartTLS(char *ok_response, char *nosup_response, char *error_response);
extern SSL_CTX *ssl_ctx;  

#endif
