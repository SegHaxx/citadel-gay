// Citadel setup utility
//
// Copyright (c) 1987-2023 by the citadel.org team
//
// This program is open source software.  Use, duplication, or disclosure
// is subject to the terms of the GNU General Public License, version 3.

#define SHOW_ME_VAPPEND_PRINTF
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <signal.h>
#include <netdb.h>
#include <errno.h>
#include <limits.h>
#include <pwd.h>
#include <time.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <assert.h>
#include <libcitadel.h>
#include "../server/citadel_defs.h"
#include "../server/server.h"
#include "axdefs.h"
#include "../server/sysdep.h"
#include "../server/citadel_dirs.h"

#ifdef ENABLE_NLS
#ifdef HAVE_XLOCALE_H
#include <xlocale.h>
#endif
#include <libintl.h>
#include <locale.h>
#define _(string)	gettext(string)
#else
#define _(string)	(string)
#endif

#define SERVICE_NAME	"citadel"
#define PROTO_NAME	"tcp"
#define NSSCONF		"/etc/nsswitch.conf"

typedef enum _SetupStep {
	eCitadelHomeDir = 0,
	eSysAdminName = 1,
	eSysAdminPW = 2,
	eUID = 3,
	eIP_ADDR = 4,
	eCTDL_Port = 5,
	eAuthType = 6,
	eLDAP_Host = 7,
	eLDAP_Port = 8,
	eLDAP_Base_DN = 9,
	eLDAP_Bind_DN = 10,
	eLDAP_Bind_PW = 11,
	eMaxQuestions = 12
} eSetupStep;

///"CREATE_XINETD_ENTRY";
/* Environment variables, don't translate! */
const char *EnvNames [eMaxQuestions] = {
        "HOME_DIRECTORY",
	"SYSADMIN_NAME",
	"SYSADMIN_PW",
	"CITADEL_UID",
	"IP_ADDR",
	"CITADEL_PORT",
	"ENABLE_UNIX_AUTH",
	"LDAP_HOST",
	"LDAP_PORT",
	"LDAP_BASE_DN",
	"LDAP_BIND_DN",
	"LDAP_BIND_PW"
};

int setup_type = (-1);
int enable_home = 1;
char admin_name[SIZ];
char admin_pass[SIZ];
char admin_cmd[SIZ];
int serv_sock = (-1) ;

const char *setup_titles[eMaxQuestions];
const char *setup_text[eMaxQuestions];

char *program_title;

void SetTitles(void) {
	int have_run_dir;
#ifndef HAVE_RUN_DIR
	have_run_dir = 1;
#else
	have_run_dir = 0;
#endif

#ifdef ENABLE_NLS
	setlocale(LC_MESSAGES, getenv("LANG"));
	bindtextdomain("citadel-setup", LOCALEDIR"/locale");
	textdomain("citadel-setup");
	bind_textdomain_codeset("citadel-setup","UTF8");
#endif

	setup_titles[eCitadelHomeDir] = _("Citadel Home Directory");
	if (have_run_dir)
		setup_text[eCitadelHomeDir] = _(
"Enter the full pathname of the directory in which the Citadel\n"
"installation you are creating or updating resides.  If you\n"
"specify a directory other than the default, you will need to\n"
"specify the -h flag to the server when you start it up.\n");
	else
		setup_text[eCitadelHomeDir] = _(
"Enter the subdirectory name for an alternate installation of "
"Citadel. To do a default installation just leave it blank."
"If you specify a directory other than the default, you will need to\n"
"specify the -h flag to the server when you start it up.\n"
"note that it may not have a leading /");

	setup_titles[eSysAdminName] = _("Citadel administrator username:");
	setup_text[eSysAdminName] = _(
"Please enter the name of the Citadel user account that should be granted "
"administrative privileges once created. If using internal authentication "
"this user account will be created if it does not exist. For external "
"authentication this user account has to exist.");

	setup_titles[eSysAdminPW] = _("Administrator password:");
	setup_text[eSysAdminPW] = _(
"Enter a password for the system administrator. When setup\n"
"completes it will attempt to create the administrator user\n"
"and set the password specified here.\n");

	setup_titles[eUID] = _("Citadel User ID:");
	setup_text[eUID] = _(
"Citadel needs to run under its own user ID.  This would\n"
"typically be called \"citadel\", but if you are running Citadel\n"
"as a public site, you might also call it \"bbs\" or \"guest\".\n"
"The server will run under this user ID.  Please specify that\n"
"user ID here.  You may specify either a user name or a numeric\n"
"UID.\n");

	setup_titles[eIP_ADDR] = _("Listening address for the Citadel server:");
	setup_text[eIP_ADDR] = _(
"Please specify the IP address which the server should be listening to. "
"You can name a specific IPv4 or IPv6 address, or you can specify\n"
"\"*\" for \"any address\", \"::\" for \"any IPv6 address\", or \"0.0.0.0\"\n"
"for \"any IPv4 address\". If you leave this blank, Citadel will\n"
"listen on all addresses. "
"This can usually be left to the default unless multiple instances of Citadel "
"are running on the same computer.");

	setup_titles[eCTDL_Port] = _("Server port number:");
	setup_text[eCTDL_Port] = _(
"Specify the TCP port number on which your server will run.\n"
"Normally, this will be port 504, which is the official port\n"
"assigned by the IANA for Citadel servers.  You will only need\n"
"to specify a different port number if you run multiple instances\n"
"of Citadel on the same computer and there is something else\n"
"already using port 504.\n");

	setup_titles[eAuthType] = _("Authentication method to use:");
	setup_text[eAuthType] = _(
"Please choose the user authentication mode. By default Citadel will use its "
"own internal user accounts database. If you choose Host, Citadel users will "
"have accounts on the host system, authenticated via /etc/passwd or a PAM "
"source. LDAP chooses an RFC 2307 compliant directory server, the last option "
"chooses the nonstandard MS Active Directory LDAP scheme."
"\n"
"Do not change this option unless you are sure it is required, since changing "
"back requires a full reinstall of Citadel."
"\n"
" 0. Self contained authentication\n"
" 1. Host system integrated authentication\n"
" 2. External LDAP - RFC 2307 POSIX schema\n"
" 3. External LDAP - MS Active Directory schema\n"
"\n"
"For help: http://www.citadel.org/authmodes.html\n"
"\n"
"ANSWER \"0\" UNLESS YOU COMPLETELY UNDERSTAND THIS OPTION.\n");

	setup_titles[eLDAP_Host] = _("LDAP host:");
	setup_text[eLDAP_Host] = _(
"Please enter the host name or IP address of your LDAP server.\n");

	setup_titles[eLDAP_Port] = _("LDAP port number:");
	setup_text[eLDAP_Port] = _(
"Please enter the port number of the LDAP service (usually 389).\n");

	setup_titles[eLDAP_Base_DN] = _("LDAP base DN:");
	setup_text[eLDAP_Base_DN] = _(
"Please enter the Base DN to search for authentication\n"
"(for example: dc=example,dc=com)\n");

	setup_titles[eLDAP_Bind_DN] = _("LDAP bind DN:");
	setup_text[eLDAP_Bind_DN] = _(
"Please enter the DN of an account to use for binding to the LDAP server for "
"performing queries. The account does not require any other privileges. If "
"your LDAP server allows anonymous queries, you can leave this blank.\n");

	setup_titles[eLDAP_Bind_PW] = _("LDAP bind password:");
	setup_text[eLDAP_Bind_PW] = _(
"If you entered a Bind DN in the previous question, you must now enter\n"
"the password associated with that account.  Otherwise, you can leave this\n"
"blank.\n");
}


void cls(void) {
	printf("\033[2J\033[H\033[44m\033[1m\033[K\n");
	printf("  %s  \033[K\n", program_title);
	printf("\033[K\n");
	printf("\033[0m\n");
}


void title(const char *text) {
	cls();
	printf("\033[1m\033[32m<\033[33m%s\033[32m>\033[0m\n", text);
}


int yesno(const char *question, int default_value) {
	int answer = 0;
	char buf[SIZ];

	do {
		printf("\033[31m\033[32m%s\n%s [\033[33m%s\033[32m]\033[0m --> ", question, _("Yes/No"), ( default_value ? _("Yes") : _("No") ));
		if (fgets(buf, sizeof buf, stdin)) {
			answer = tolower(buf[0]);
			if ((buf[0]==0) || (buf[0]==13) || (buf[0]==10)) {
				answer = default_value;
			}
			else if (answer == 'y') {
				answer = 1;
			}
			else if (answer == 'n') {
				answer = 0;
			}
		}
	} while ((answer < 0) || (answer > 1));
	return (answer);
}


void important_message(const char *title, const char *msgtext) {
	char buf[SIZ];

	cls();
	printf("%s\n%s\n\n", title, msgtext);
	printf("%s", _("Press return to continue..."));
	if (fgets(buf, sizeof buf, stdin)) {
		;
	}
}


void important_msgnum(int msgnum) {
	important_message(_("Important Message"), setup_text[msgnum]);
}


void display_error(char *error_message_format, ...) {
	StrBuf *Msg;
	va_list arg_ptr;

	Msg = NewStrBuf();
	va_start(arg_ptr, error_message_format);
	StrBufVAppendPrintf(Msg, error_message_format, arg_ptr);
	va_end(arg_ptr);

	important_message(_("Error"), ChrPtr(Msg));
	FreeStrBuf(&Msg);
}


void progress(char *text, long int curr, long int cmax) {
	long a = 0;
	long i = 0;

	if (curr == 0) {
		cls();
		printf("%s\n", text);
		printf("\033[1m\033[33m[\033[32m............................................................................\033[33m]\033[0m\r");
	}
	else if (curr == cmax) {
		printf("\r%79s\n", "");
	}
	else {
		printf("\033[1m\033[33m[\033[32m");
		a = (curr * 100) / cmax;
		a = a * 76;
		a = a / 100;
		for (i=0; i<a; ++i) {
			printf("*");
		}
		printf("\033[0m\r");
	}
	fflush(stdout);
}


int uds_connectsock(char *sockpath) {
	int s;
	struct sockaddr_un addr;

	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	strcpy(addr.sun_path, sockpath);

	s = socket(AF_UNIX, SOCK_STREAM, 0);
	if (s < 0) {
		return(-1);
	}

	if (connect(s, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
		close(s);
		return(-1);
	}

	return s;
}


// input binary data from socket
void serv_read(char *buf, int bytes) {
	int len, rlen;

	len = 0;
	while (len < bytes) {
		rlen = read(serv_sock, &buf[len], bytes - len);
		if (rlen < 1) {
			return;
		}
		len = len + rlen;
	}
}


// send binary to server
void serv_write(char *buf, int nbytes) {
	int bytes_written = 0;
	int retval;
	while (bytes_written < nbytes) {
		retval = write(serv_sock, &buf[bytes_written], nbytes - bytes_written);
		if (retval < 1) {
			return;
		}
		bytes_written = bytes_written + retval;
	}
}


// input string from socket - implemented in terms of serv_read()
void serv_gets(char *buf) {
	int i;

	// Read one character at a time.
	for (i = 0;; i++) {
		serv_read(&buf[i], 1);
		if (buf[i] == '\n' || i == (SIZ-1))
			break;
	}

	// If we got a long line, discard characters until the newline.
	if (i == (SIZ-1)) {
		while (buf[i] != '\n') {
			serv_read(&buf[i], 1);
		}
	}

	// Strip all trailing nonprintables (crlf)
	buf[i] = 0;
}


// send line to server - implemented in terms of serv_write()
void serv_puts(char *buf) {
	serv_write(buf, strlen(buf));
	serv_write("\n", 1);
}


// Convenience functions to get/set system configuration entries
void getconf_str(char *buf, char *key) {
	char cmd[SIZ];
	char ret[SIZ];

	sprintf(cmd, "CONF GETVAL|%s", key);
	serv_puts(cmd);
	serv_gets(ret);
	if (ret[0] == '2') {
		extract_token(buf, &ret[4], 0, '|', SIZ);
	}
	else {
		strcpy(buf, "");
	}
}


int getconf_int(char *key) {
	char buf[SIZ];
	getconf_str(buf, key);
	return atoi(buf);
}


void setconf_str(char *key, char *val) {
	char buf[SIZ];

	sprintf(buf, "CONF PUTVAL|%s|%s", key, val);
	serv_puts(buf);
	serv_gets(buf);
}


void setconf_int(char *key, int val) {
	char buf[SIZ];

	sprintf(buf, "CONF PUTVAL|%s|%d", key, val);
	serv_puts(buf);
	serv_gets(buf);
}


// On systems which use xinetd, see if we can offer to install Citadel as
// the default telnet target.
void check_xinetd_entry(void) {
	char *filename = "/etc/xinetd.d/telnet";
	FILE *fp;
	char buf[SIZ];
	int already_citadel = 0;
	int rv;

	fp = fopen(filename, "r+");
	if (fp == NULL) return;		// Not there.  Oh well...

	while (fgets(buf, sizeof buf, fp) != NULL) {
		if (strstr(buf, "/citadel") != NULL) {
			already_citadel = 1;
		}
	}
	fclose(fp);
	if (already_citadel) return;	// Already set up this way.

	// Otherwise, prompt the user to create an entry.
	if (getenv("CREATE_XINETD_ENTRY") != NULL) {
		if (strcasecmp(getenv("CREATE_XINETD_ENTRY"), "yes")) {
			return;
		}
	}
	else {
		snprintf(buf, sizeof buf,
			 _("Setup can configure the \"xinetd\" service to automatically\n"
			   "connect incoming telnet sessions to Citadel, bypassing the\n"
			   "host system login: prompt.  Would you like to do this?\n"
			 )
		);
		if (yesno(buf, 1) == 0) {
			return;
		}
	}

	fp = fopen(filename, "w");
	fprintf(fp,
		"# description: telnet service for Citadel users\n"
		"service telnet\n"
		"{\n"
		"	disable	= no\n"
		"	flags		= REUSE\n"
		"	socket_type	= stream\n"
		"	wait		= no\n"
		"	user		= root\n"
		"	server		= /usr/sbin/in.telnetd\n"
		"	server_args	= -h -L %s/citadel\n"
		"	log_on_failure	+= USERID\n"
		"}\n",
		ctdl_bin_dir
	);
	fclose(fp);

	// Now try to restart the service.  (This only works on systemd; others will need to restart it manually.)
	rv = system("systemctl restart xinetd >/dev/null 2>&1");
	if (rv != 0) {
		rv = system("service xinetd restart >/dev/null 2>&1");
	}
	if (rv != 0) {
		display_error(_("failed to restart xinetd.\n"));
	}
}


void strprompt(const char *prompt_title, const char *prompt_text, char *Target, char *DefValue) {
	char buf[SIZ] = "";
	char setupmsg[SIZ];

	strcpy(setupmsg, "");

	title(prompt_title);
	printf("\n%s\n", prompt_text);
	printf("%s\n%s\n", _("This is currently set to:"), Target);
	printf("%s\n", _("Enter new value or press return to leave unchanged:"));
	if (fgets(buf, sizeof buf, stdin)) {
		buf[strlen(buf) - 1] = 0;
	}
	if (!IsEmptyStr(buf)) {
		strcpy(Target, buf);
	}
}


void set_bool_val(int msgpos, int *ip, char *DefValue) {
	title(setup_titles[msgpos]);
	*ip = yesno(setup_text[msgpos], *ip);
}


void set_str_val(int msgpos, char *Target, char *DefValue) {
	strprompt(setup_titles[msgpos], 
		  setup_text[msgpos], 
		  Target, 
		  DefValue
	);
}


// like set_str_val() but for numeric values
void set_int_val(int msgpos, int *target, char *default_value) {
	char buf[32];
	sprintf(buf, "%d", *target);
	do {
		set_str_val(msgpos, buf, default_value);
	} while ( (strcmp(buf, "0")) && (atoi(buf) == 0) );
	*target = atoi(buf);
}


void edit_value(int curr) {
	struct passwd *pw = NULL;
	char ctdluidname[256];
	char buf[SIZ];
	char *default_value = NULL;
	int ctdluid = 0;
	int portnum = 0;
	int auth = 0;
	int lportnum = 0;

	if (default_value == NULL) {
		default_value = "";
	}

	switch (curr) {

	case eSysAdminName:
		getconf_str(admin_name, "c_sysadm");
		set_str_val(curr, admin_name, default_value);
		setconf_str("c_sysadm", admin_name);
		break;

	case eSysAdminPW:
		set_str_val(curr, admin_pass, default_value);
		break;
	
	case eUID:
		ctdluid = getconf_int("c_ctdluid");
		pw = getpwuid(ctdluid);
		if (pw == NULL) {
			set_int_val(curr, &ctdluid, default_value);
		}
		else {
			strcpy(ctdluidname, pw->pw_name);
			set_str_val(curr, ctdluidname, default_value);
			pw = getpwnam(ctdluidname);
			if (pw != NULL) {
				ctdluid = pw->pw_uid;
			}
			else if (atoi(ctdluidname) > 0) {
				ctdluid = atoi(ctdluidname);
			}
		}
		setconf_int("c_ctdluid", ctdluid);
		break;

	case eIP_ADDR:
		getconf_str(buf, "c_ip_addr");
		set_str_val(curr, buf, default_value);
		setconf_str("c_ip_addr", buf);
		break;

	case eCTDL_Port:
		portnum = getconf_int("c_port_number");
		set_int_val(curr, &portnum, default_value);
		setconf_int("c_port_number", portnum);
		break;

	case eAuthType:
		auth = getconf_int("c_auth_mode");
		set_int_val(curr, &auth, default_value);
		setconf_int("c_auth_mode", auth);
		break;

	case eLDAP_Host:
		getconf_str(buf, "c_ldap_host");
		if (IsEmptyStr(buf)) {
			strcpy(buf, "localhost");
		}
		set_str_val(curr, buf, default_value);
		setconf_str("c_ldap_host", buf);
		break;

	case eLDAP_Port:
		lportnum = getconf_int("c_ldap_port");
		if (lportnum == 0) {
			lportnum = 389;
		}
		set_int_val(curr, &lportnum, default_value);
		setconf_int("c_ldap_port", lportnum);
		break;

	case eLDAP_Base_DN:
		getconf_str(buf, "c_ldap_base_dn");
		set_str_val(curr, buf, default_value);
		setconf_str("c_ldap_base_dn", buf);
		break;

	case eLDAP_Bind_DN:
		getconf_str(buf, "c_ldap_bind_dn");
		set_str_val(curr, buf, default_value);
		setconf_str("c_ldap_bind_dn", buf);
		break;

	case eLDAP_Bind_PW:
		getconf_str(buf, "c_ldap_bind_pw");
		set_str_val(curr, buf, default_value);
		setconf_str("c_ldap_bind_pw", buf);
		break;
	}
}


// Messages that are no longer in use.
// We keep them here so we don't lose the translations if we need them later.
void unused_messages(void) {
	important_message(_("Setup finished"),
		_("Setup of the Citadel server is complete.\n"
		"If you will be using WebCit, please run its\n"
		"setup program now; otherwise, run './citadel'\n"
		"to log in.\n")
	);
	important_message(_("Setup failed"),
		_("Setup is finished, but the Citadel server failed to start.\n"
		"Go back and check your configuration.\n")
	);
	important_message(_("Setup finished"),
		_("Setup is finished.  You may now start the server.")
	);
}


int main(int argc, char *argv[]) {
	int a, i;
	int curr;
	char buf[1024]; 
	char aaa[128];
	char ctdldir[PATH_MAX]=CTDLDIR;
	struct passwd *pw;
	gid_t gid;
	char *activity = NULL;
	
	// Keep a mild groove on
	program_title = _("Citadel Server setup");

	// set an invalid setup type
	setup_type = (-1);

	// parse command line args
	for (a = 0; a < argc; ++a) {
		if (!strncmp(argv[a], "-u", 2)) {
			strcpy(aaa, argv[a]);
			strcpy(aaa, &aaa[2]);
			setup_type = atoi(aaa);
		}
		else if (!strncmp(argv[a], "-h", 2)) {
			safestrncpy(ctdldir, &argv[a][2], sizeof ctdldir);
		}
	}

	if (chdir(ctdldir) != 0) {
		fprintf(stderr, "sendcommand: %s: %s\n", ctdldir, strerror(errno));
		exit(errno);
	}

	SetTitles();

	// Connect to the running Citadel server.
	char *connectingmsg = _("Connecting to Citadel server");
	for (i=0; ((i<30) && (serv_sock < 0)) ; ++i) {		// wait for server to start up
		progress(connectingmsg, i, 30);
        	serv_sock = uds_connectsock(file_citadel_admin_socket);
		sleep(1);
	}
	progress(connectingmsg, 30, 30);

	if (serv_sock < 0) { 
		display_error(
			"%s: %s %s\n", 
			_("Setup could not connect to a running Citadel server."),
			strerror(errno), file_citadel_admin_socket
		);
		exit(1);
	}

	// read the server greeting
	serv_gets(buf);
	if (buf[0] != '2') {
		display_error("%s\n", buf);
		exit(2);
	}

	// Are we connected to the correct Citadel server?
	serv_puts("INFO");
	serv_gets(buf);
	if (buf[0] != '1') {
		display_error("%s\n", buf);
		exit(3);
	}
	a = 0;
	while (serv_gets(buf), strcmp(buf, "000")) {
		if (a == 5) {
			if (atoi(buf) != REV_LEVEL) {
				display_error("%s\n", _("Your setup program and Citadel server are from different versions."));
				exit(4);
			}
		}
		++a;
	}

	printf("\n\n\n	       *** %s ***\n\n", program_title);

	// Go through a series of dialogs prompting for config info
	for (curr = 1; curr < eMaxQuestions; ++curr) {
		edit_value(curr);

		if (	(curr == eAuthType)
			&& (getconf_int("c_auth_mode") != AUTHMODE_LDAP)
			&& (getconf_int("c_auth_mode") != AUTHMODE_LDAP_AD)
		) {
			curr += 5;	// skip LDAP questions if we're not authenticating against LDAP
		}

		if (curr == eSysAdminName) {
			if (getconf_int("c_auth_mode") == AUTHMODE_NATIVE) { // for native auth mode, fetch the admin's existing pw
				snprintf(buf, sizeof buf, "AGUP %s", admin_name);
				serv_puts(buf);
				serv_gets(buf);
				if (buf[0] == '2') {
					extract_token(admin_pass, &buf[4], 1, '|', sizeof admin_pass);
				}
			}
			else {
				++curr;		// skip the password question for non-native auth modes
			}
		}
	}

	if ((pw = getpwuid( getconf_int("c_ctdluid") )) == NULL) {
		gid = getgid();
	}
	else {
		gid = pw->pw_gid;
	}

	// setup now must be run after Citadel Server is already running, so we don't need this anymore.
	//if (create_run_directories(getconf_int("c_ctdluid"), gid) != 0) {
		//display_error("%s\n", _("failed to create directories"));
	//}
		
	activity = _("Reconfiguring Citadel server");
	progress(activity, 0, 5);
	sleep(1);					// Let the message appear briefly

	// Create the administrator account.  It's ok if the command fails if this user already exists.
	if (getconf_int("c_auth_mode") == AUTHMODE_NATIVE) {
		progress(activity, 1, 5);
		snprintf(buf, sizeof buf, "CREU %s|%s", admin_name, admin_pass);
		serv_puts(buf);
		progress(activity, 2, 5);
		serv_gets(buf);
	}
	progress(activity, 3, 5);

	// Assign the desired password and access level to the administrator account.
	if (getconf_int("c_auth_mode") == AUTHMODE_NATIVE) {
		snprintf(buf, sizeof buf, "AGUP %s", admin_name);
		serv_puts(buf);
		progress(activity, 4, 5);
		serv_gets(buf);
		if (buf[0] == '2') {
			int admin_flags = extract_int(&buf[4], 2);
			int admin_times_called = extract_int(&buf[4], 3);
			int admin_msgs_posted = extract_int(&buf[4], 4);
			snprintf(buf, sizeof buf, "ASUP %s|%s|%d|%d|%d|6",
				admin_name, admin_pass, admin_flags, admin_times_called, admin_msgs_posted
			);
			serv_puts(buf);
			serv_gets(buf);
		}
	}
	progress(activity, 5, 5);

	check_xinetd_entry();	// Check /etc/xinetd.d/telnet

	// Restart citserver
	activity = _("Restarting Citadel server to apply changes");
	progress(activity, 0, 51);

	serv_puts("TIME");
	serv_gets(buf);
	long original_start_time = extract_long(&buf[4], 3);

	progress(activity, 1, 51);
	serv_puts("DOWN 1");
	progress(activity, 2, 51);
	serv_gets(buf);
	if (buf[0] != '2') {
		display_error("%s\n", buf);
		exit(6);
	}

	close(serv_sock);
	serv_sock = (-1);

	for (i=3; i<=6; ++i) {					// wait for server to shut down
		progress(activity, i, 51);
		sleep(1);
	}

	for (i=7; ((i<=48) && (serv_sock < 0)) ; ++i) {		// wait for server to start up
		progress(activity, i, 51);
        	serv_sock = uds_connectsock(file_citadel_admin_socket);
		sleep(1);
	}

	progress(activity, 49, 51);
	serv_gets(buf);

	progress(activity, 50, 51);
	serv_puts("TIME");
	serv_gets(buf);
	long new_start_time = extract_long(&buf[4], 3);

	close(serv_sock);
	progress(activity, 51, 51);

	if ((original_start_time == new_start_time) || (new_start_time <= 0)) {
		display_error("%s\n", _("Setup failed to restart Citadel server.  Please restart it manually."));
		exit(7);
	}

	exit(0);
	return 0;
}
