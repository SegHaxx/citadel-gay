// system-level password checking for host auth mode
//
// Copyright (c) 1999-2022 by Nathan Bryant, Trey Van Riper, and the citadel.org team
//
// This program is open source software.  Use, duplication, or disclosure
// is subject to the terms of the GNU General Public License, version 3.
// The program is distributed without any warranty, expressed or implied.

#if defined(__linux) || defined(__sun)	// needed for crypt():
#define _XOPEN_SOURCE
#define _XOPEN_SOURCE_EXTENDED 1
#endif

#include <pwd.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <crypt.h>

#include "auth.h"
#include "../server/sysdep.h"

#ifdef HAVE_GETSPNAM
#include <shadow.h>
#endif

#ifdef HAVE_PAM_START
#include <security/pam_appl.h>

// struct appdata: passed to the conversation function
struct appdata {
	const char *name;
	const char *pw;
};

// conv(): the PAM conversation function. this assumes that a
// PAM_PROMPT_ECHO_ON is asking for a username, and a PAM_PROMPT_ECHO_OFF is
// asking for a password. esoteric authentication modules will fail with this
// code, but we can't really support them with the existing client protocol
// anyway. the failure mode should be to deny access, in any case.
static int conv(int num_msg, const struct pam_message **msg, struct pam_response **resp, void *appdata_ptr) {
	struct pam_response *temp_resp;
	struct appdata *data = appdata_ptr;

	if ((temp_resp =
	     malloc(sizeof(struct pam_response[num_msg]))) == NULL)
		return PAM_CONV_ERR;

	while (num_msg--) {
		switch ((*msg)[num_msg].msg_style) {
		case PAM_PROMPT_ECHO_ON:
			temp_resp[num_msg].resp = strdup(data->name);
			break;
		case PAM_PROMPT_ECHO_OFF:
			temp_resp[num_msg].resp = strdup(data->pw);
			break;
		default:
			temp_resp[num_msg].resp = NULL;
		}
		temp_resp[num_msg].resp_retcode = 0;
	}

	*resp = temp_resp;
	return PAM_SUCCESS;
}
#endif				// HAVE_PAM_START


// check that `pass' is the correct password for `uid'
// returns zero if no, nonzero if yes
int validate_password(uid_t uid, const char *pass) {
	if (pass == NULL) {
		return (0);
	}
#ifdef HAVE_PAM_START
	struct pam_conv pc;
	struct appdata data;
	pam_handle_t *ph;
	int i;
#else
	char *crypted_pwd;
#ifdef HAVE_GETSPNAM
	struct spwd *sp;
#endif
#endif
	struct passwd *pw;
	int retval = 0;

	pw = getpwuid(uid);
	if (pw == NULL) {
		return retval;
	}
#ifdef HAVE_PAM_START

#ifdef PAM_DATA_SILENT
	int flags = PAM_DATA_SILENT;
#else
	int flags = 0;
#endif

	pc.conv = conv;
	pc.appdata_ptr = &data;
	data.name = pw->pw_name;
	data.pw = pass;
	if (pam_start("citadel", pw->pw_name, &pc, &ph) != PAM_SUCCESS)
		return (0);

	if ((i = pam_authenticate(ph, flags)) == PAM_SUCCESS) {
		if ((i = pam_acct_mgmt(ph, flags)) == PAM_SUCCESS) {
			retval = -1;
		}
	}

	pam_end(ph, i | flags);
#else
	crypted_pwd = pw->pw_passwd;

#ifdef HAVE_GETSPNAM
	if (pw == NULL)
		return (0);
	if (pw->pw_name == NULL)
		return (0);
	if ((sp = getspnam(pw->pw_name)) != NULL) {
		crypted_pwd = sp->sp_pwdp;
	}
#endif

	if (!strcmp(crypt(pass, crypted_pwd), crypted_pwd)) {
		retval = -1;
	}
#endif				// HAVE_PAM_START

	return retval;
}
