// webcit.h - "header of headers"
//
// Copyright (c) 1996-2022 by the citadel.org team
//
// This program is open source software.  You can redistribute it and/or
// modify it under the terms of the GNU General Public License, version 3.

#define SHOW_ME_VAPPEND_PRINTF

#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <ctype.h>
#include <syslog.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/un.h>
#include <sys/poll.h>
#include <sys/time.h>
#include <string.h>
#include <pwd.h>
#include <errno.h>
#include <stdarg.h>
#include <pthread.h>
#include <signal.h>
#include <syslog.h>
#include <stdarg.h>
#include <limits.h>
#include <iconv.h>
#include <libcitadel.h>
#define OPENSSL_NO_KRB5				// Work around RedHat's b0rken OpenSSL includes
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/rand.h>
#include <expat.h>
#define _(x)	x				// temporary hack until we add i18n back in
//#define DEBUG_HTTP				// uncomment to debug HTTP headers

// XML_StopParser is present in expat 2.x
#if XML_MAJOR_VERSION > 1
#define HAVE_XML_STOPPARSER
#endif

struct client_handle {				// this gets passed up the stack from the webserver to the application code
	int sock;
	SSL *ssl_handle;
};

struct keyval {					// key/value pair (for array)
	char *key;
	char *val;
};

struct http_transaction {			// The lifetime of an HTTP request goes through this data structure.
	char *method;				// The top half is built up by the web server and sent up to the
	char *url;				// application stack.  The second half is built up by the application
	char *http_version;			// stack and sent back down to the web server, which transmits it to
	char *site_prefix;			// the client.
	Array *request_headers;
	char *request_body;
	long request_body_length;
	Array *request_parms;			// anything after the "?" in the URL
	int response_code;
	char *response_string;
	Array *response_headers;
	char *response_body;
	long response_body_length;
};

#define AUTH_MAX 256				// Maximum length of an HTTP AUTH header or equivalent cookie data
struct ctdlsession {
	struct ctdlsession *next;
	int is_bound;				// Nonzero if this record is currently bound to a running thread
	int sock;				// Socket connection to Citadel server
	char auth[AUTH_MAX];			// Auth string (empty if not logged in)
	char whoami[64];			// Display name of currently logged in user (empty if not logged in)
	char room[128];				// What room we are currently in
	int room_current_view;
	int room_default_view;
	int is_trash_folder;			// Nonzero if this room is the user's Trash folder
	int is_room_aide;			// nonzero if the user has aide rights to THIS room
	int can_delete_messages;		// nonzeri if the user is permitted to delete messages in THIS room
	long last_seen;
	int new_messages;
	int total_messages;
	time_t last_access;			// Timestamp of last request that used this session
	time_t num_requests_handled;
	time_t room_mtime;			// Timestampt of the most recent write activity in this room
};

extern char *ssl_cipher_list;
extern int is_https;				// nonzero if we are an HTTPS server today
extern char *ctdl_dir;				// directory where Citadel Server is running
void init_ssl(void);
void starttls(struct client_handle *);
void endtls(struct client_handle *);
int client_write_ssl(struct client_handle *ch, char *buf, int nbytes);
int client_read_ssl(struct client_handle *ch, char *buf, int nbytes);

enum {
	WEBSERVER_HTTP,
	WEBSERVER_HTTPS,
	WEBSERVER_UDS
};

#define TRACE syslog(LOG_DEBUG, "\033[3%dmCHECKPOINT: %s:%d\033[0m", ((__LINE__%6)+1), __FILE__, __LINE__)
#define SLEEPING		180		// TCP connection timeout
#define MAX_WORKER_THREADS	32		// Maximum number of worker threads permitted to exist
#define DEFAULT_SSL_CIPHER_LIST "DEFAULT"       // See http://openssl.org/docs/apps/ciphers.html
#define WEBSERVER_PORT		80
#define WEBSERVER_INTERFACE	"*"
#define CTDL_DIR		"/usr/local/citadel"
#define DEVELOPER_ID		0
#define CLIENT_ID		4
#define TARGET			"webcit02"	/* Window target for inline URL's */
#define ROOMNAMELEN		128		// The size of a roomname string
#define	DAV_MOVE		0		// MOVE=0 COPY=1 don't change these!
#define DAV_COPY		1		// they are the values used in the Citadel Server MOVE command

void worker_entry(int *);
void perform_one_http_transaction(struct client_handle *ch);
void add_response_header(struct http_transaction *h, char *key, char *val);
void perform_request(struct http_transaction *);
void do_204(struct http_transaction *);
void do_404(struct http_transaction *);
void do_412(struct http_transaction *);
void do_502(struct http_transaction *);
void output_static(struct http_transaction *);
int uds_connectsock(char *);
void ctdl_a(struct http_transaction *, struct ctdlsession *);
void ctdl_f(struct http_transaction *, struct ctdlsession *);
void ctdl_r(struct http_transaction *, struct ctdlsession *);
void ctdl_u(struct http_transaction *, struct ctdlsession *);
struct ctdlsession *connect_to_citadel(struct http_transaction *);
void disconnect_from_citadel(struct ctdlsession *);
char *header_val(struct http_transaction *h, char *requested_header);
char *get_url_param(struct http_transaction *h, char *requested_param);
int unescape_input(char *);
void http_redirect(struct http_transaction *h, char *to_where);
char *http_datestring(time_t xtime);
long *get_msglist(struct ctdlsession *c, char *which_msgs);
void caldav_report(struct http_transaction *h, struct ctdlsession *c);
long locate_message_by_uid(struct ctdlsession *c, char *uid);
void ctdl_delete_msgs(struct ctdlsession *c, long *msgnums, int num_msgs);
void dav_delete_message(struct http_transaction *h, struct ctdlsession *c, long msgnum);
void dav_get_message(struct http_transaction *h, struct ctdlsession *c, long msgnum);
void dav_put_message(struct http_transaction *h, struct ctdlsession *c, char *euid, long old_msgnum);
void dav_move_or_copy_message(struct http_transaction *h, struct ctdlsession *c, long msgnum, int move_or_copy);
ssize_t ctdl_write(struct ctdlsession *ctdl, const void *buf, size_t count);
int login_to_citadel(struct ctdlsession *c, char *auth, char *resultbuf);
StrBuf *ctdl_readtextmsg(struct ctdlsession *ctdl);
StrBuf *html2html(const char *supplied_charset, int treat_as_wiki, char *roomname, long msgnum, StrBuf *Source);
void download_mime_component(struct http_transaction *h, struct ctdlsession *c, long msgnum, char *partnum);
StrBuf *text2html(const char *supplied_charset, int treat_as_wiki, char *roomname, long msgnum, StrBuf *Source);
StrBuf *variformat2html(StrBuf *Source);
int ctdl_readline(struct ctdlsession *ctdl, char *buf, int maxbytes);
int ctdl_read_binary(struct ctdlsession *ctdl, char *buf, int bytes_requested);
void ctdl_c(struct http_transaction *h, struct ctdlsession *c);
int webserver(char *webserver_interface, int webserver_port, int webserver_protocol);
void ctdl_printf(struct ctdlsession *ctdl, const char *format,...);
int webcit_tcp_server(const char *ip_addr, int port_number, int queue_len);
void UrlizeText(StrBuf* Target, StrBuf *Source, StrBuf *WrkBuf);
void json_render_one_message(struct http_transaction *h, struct ctdlsession *c, long msgnum);
