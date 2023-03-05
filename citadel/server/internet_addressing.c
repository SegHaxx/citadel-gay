// This file contains functions which handle the mapping of Internet addresses
// to users on the Citadel system.
//
// Copyright (c) 1987-2023 by the citadel.org team
//
// This program is open source software.  Use, duplication, or disclosure
// is subject to the terms of the GNU General Public License, version 3.

#include "sysdep.h"
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <ctype.h>
#include <signal.h>
#include <pwd.h>
#include <errno.h>
#include <sys/types.h>
#include <time.h>
#include <sys/wait.h>
#include <string.h>
#include <limits.h>
#include <libcitadel.h>
#include "citadel_defs.h"
#include "server.h"
#include "sysdep_decls.h"
#include "citserver.h"
#include "support.h"
#include "config.h"
#include "msgbase.h"
#include "internet_addressing.h"
#include "user_ops.h"
#include "room_ops.h"
#include "parsedate.h"
#include "database.h"
#include "ctdl_module.h"



char *inetcfg = NULL;

// Return nonzero if the supplied name is an alias for this host.
int CtdlHostAlias(char *fqdn) {
	int config_lines;
	int i;
	char buf[256];
	char host[256], type[256];
	int found = 0;

	if (fqdn == NULL)					return(hostalias_nomatch);
	if (IsEmptyStr(fqdn))					return(hostalias_nomatch);
	if (!strcasecmp(fqdn, "localhost"))			return(hostalias_localhost);
	if (!strcasecmp(fqdn, CtdlGetConfigStr("c_fqdn")))	return(hostalias_localhost);
	if (!strcasecmp(fqdn, CtdlGetConfigStr("c_nodename")))	return(hostalias_localhost);
	if (inetcfg == NULL)					return(hostalias_nomatch);

	config_lines = num_tokens(inetcfg, '\n');
	for (i=0; i<config_lines; ++i) {
		extract_token(buf, inetcfg, i, '\n', sizeof buf);
		extract_token(host, buf, 0, '|', sizeof host);
		extract_token(type, buf, 1, '|', sizeof type);

		found = 0;

		// Process these in a specific order, in case there are multiple matches.
		// We want localhost to override masq, for example.

		if ( (!strcasecmp(type, "masqdomain")) && (!strcasecmp(fqdn, host))) {
			found = hostalias_masq;
		}

		if ( (!strcasecmp(type, "localhost")) && (!strcasecmp(fqdn, host))) {
			found = hostalias_localhost;
		}

		// "directory" used to be a distributed version of "localhost" but they're both the same now
		if ( (!strcasecmp(type, "directory")) && (!strcasecmp(fqdn, host))) {
			found = hostalias_localhost;
		}

		if (found) return(found);
	}
	return(hostalias_nomatch);
}


// Determine whether a given Internet address belongs to the current user
int CtdlIsMe(char *addr, int addr_buf_len) {
	struct recptypes *recp;
	int i;

	recp = validate_recipients(addr, NULL, 0);
	if (recp == NULL) return(0);

	if (recp->num_local == 0) {
		free_recipients(recp);
		return(0);
	}

	for (i=0; i<recp->num_local; ++i) {
		extract_token(addr, recp->recp_local, i, '|', addr_buf_len);
		if (!strcasecmp(addr, CC->user.fullname)) {
			free_recipients(recp);
			return(1);
		}
	}

	free_recipients(recp);
	return(0);
}


// If the last item in a list of recipients was truncated to a partial address,
// remove it completely in order to avoid choking library functions.
void sanitize_truncated_recipient(char *str) {
	if (!str) return;
	if (num_tokens(str, ',') < 2) return;

	int len = strlen(str);
	if (len < 900) return;
	if (len > 998) str[998] = 0;

	char *cptr = strrchr(str, ',');
	if (!cptr) return;

	char *lptr = strchr(cptr, '<');
	char *rptr = strchr(cptr, '>');

	if ( (lptr) && (rptr) && (rptr > lptr) ) return;

	*cptr = 0;
}


// This function is self explanatory.
// (What can I say, I'm in a weird mood today...)
void remove_any_whitespace_to_the_left_or_right_of_at_symbol(char *name) {
	char *ptr;
	if (!name) return;

	for (ptr=name; *ptr; ++ptr) {
		while ( (isspace(*ptr)) && (*(ptr+1)=='@') ) {
			strcpy(ptr, ptr+1);
			if (ptr > name) --ptr;
		}
		while ( (*ptr=='@') && (*(ptr+1)!=0) && (isspace(*(ptr+1))) ) {
			strcpy(ptr+1, ptr+2);
		}
	}
}


// values that can be returned by expand_aliases()
enum {
	EA_ERROR,		// Can't send message due to bad address
	EA_MULTIPLE,		// Alias expanded into multiple recipients -- run me again!
	EA_LOCAL,		// Local message, do no network processing
	EA_INTERNET,		// Convert msg and send as Internet mail
	EA_SKIP			// This recipient has been invalidated -- skip it!
};


// Process alias and routing info for email addresses
int expand_aliases(char *name, char *aliases) {
	int a;
	char aaa[SIZ];
	int at = 0;

	if (aliases) {
		int num_aliases = num_tokens(aliases, '\n');
		for (a=0; a<num_aliases; ++a) {
			extract_token(aaa, aliases, a, '\n', sizeof aaa);
			char *bar = strchr(aaa, '|');
			if (bar) {
				bar[0] = 0;
				++bar;
				string_trim(aaa);
				string_trim(bar);
				if ( (!IsEmptyStr(aaa)) && (!strcasecmp(name, aaa)) ) {
					syslog(LOG_DEBUG, "internet_addressing: global alias <%s> to <%s>", name, bar);
					strcpy(name, bar);
				}
			}
		}
		if (strchr(name, ',')) {
			return(EA_MULTIPLE);
		}
	}

	char original_name[256];				// Now go for the regular aliases
	safestrncpy(original_name, name, sizeof original_name);

	// should these checks still be here, or maybe move them to split_recps() ?
	string_trim(name);
	remove_any_whitespace_to_the_left_or_right_of_at_symbol(name);
	stripallbut(name, '<', '>');

	// Hit the email address directory
	if (CtdlDirectoryLookup(aaa, name, sizeof aaa) == 0) {
		strcpy(name, aaa);
	}

	if (strcasecmp(original_name, name)) {
		syslog(LOG_INFO, "internet_addressing: directory alias <%s> to <%s>", original_name, name);
	}

	// Change "user @ xxx" to "user" if xxx is an alias for this host
	for (a=0; name[a] != '\0'; ++a) {
		if (name[a] == '@') {
			if (CtdlHostAlias(&name[a+1]) == hostalias_localhost) {
				name[a] = 0;
				syslog(LOG_DEBUG, "internet_addressing: host is local, recipient is <%s>", name);
				break;
			}
		}
	}

	// Is this a local or remote recipient?
	at = haschar(name, '@');
	if (at == 0) {
		return(EA_LOCAL);			// no @'s = local address
	}
	else if (at == 1) {
		return(EA_INTERNET);			// one @ = internet address
	}
	else {
		return(EA_ERROR);			// more than one @ = badly formed address
	}
}


// Return a supplied list of email addresses as an array, removing superfluous information and syntax.
// If an existing Array is supplied as "append_to" it will do so; otherwise a new Array is allocated.
Array *split_recps(char *addresses, Array *append_to) {

	if (IsEmptyStr(addresses)) {		// nothing supplied, nothing returned
		return(NULL);
	}

	// Copy the supplied address list into our own memory space, because we are going to modify it.
	char *a = strdup(addresses);
	if (a == NULL) {
		syslog(LOG_ERR, "internet_addressing: malloc() failed: %m");
		return(NULL);
	}

	// Strip out anything in double quotes
	char *l = NULL;
	char *r = NULL;
	do {
		l = strchr(a, '\"');
		r = strrchr(a, '\"');
		if (r > l) {
			strcpy(l, r+1);
		}
	} while (r > l);

	// Transform all qualifying delimiters to commas
	char *t;
	for (t=a; t[0]; ++t) {
		if ((t[0]==';') || (t[0]=='|')) {
			t[0]=',';
		}
	}

	// Tokenize the recipients into an array.  No single recipient should be larger than 256 bytes.
	Array *recipients_array = NULL;
	if (append_to) {
		recipients_array = append_to;			// Append to an existing array of recipients
	}
	else {
		recipients_array = array_new(256);		// This is a new array of recipients
	}

	int num_addresses = num_tokens(a, ',');
	int i;
	for (i=0; i<num_addresses; ++i) {
		char this_address[256];
		extract_token(this_address, a, i, ',', sizeof this_address);
		string_trim(this_address);				// strip leading and trailing whitespace
		stripout(this_address, '(', ')');		// remove any portion in parentheses
		stripallbut(this_address, '<', '>');		// if angle brackets are present, keep only what is inside them
		if (!IsEmptyStr(this_address)) {
			array_append(recipients_array, this_address);
		}
	}

	free(a);						// We don't need this buffer anymore.
	return(recipients_array);				// Return the completed array to the caller.
}


// Validate recipients, count delivery types and errors, and handle aliasing
//
// Returns 0 if all addresses are ok, ret->num_error = -1 if no addresses 
// were specified, or the number of addresses found invalid.
//
// Caller needs to free the result using free_recipients()
//
struct recptypes *validate_recipients(char *supplied_recipients, const char *RemoteIdentifier, int Flags) {
	struct recptypes *ret;
	char *recipients = NULL;
	char append[SIZ];
	long len;
	int mailtype;
	int invalid;
	struct ctdluser tempUS;
	struct ctdlroom original_room;
	int err = 0;
	char errmsg[SIZ];
	char *org_recp;
	char this_recp[256];

	ret = (struct recptypes *) malloc(sizeof(struct recptypes));			// Initialize
	if (ret == NULL) return(NULL);
	memset(ret, 0, sizeof(struct recptypes));					// set all values to null/zero

	if (supplied_recipients == NULL) {
		recipients = strdup("");
	}
	else {
		recipients = strdup(supplied_recipients);
	}

	len = strlen(recipients) + 1024;						// allocate memory
	ret->errormsg = malloc(len);
	ret->recp_local = malloc(len);
	ret->recp_internet = malloc(len);
	ret->recp_room = malloc(len);
	ret->display_recp = malloc(len);
	ret->recp_orgroom = malloc(len);

	ret->errormsg[0] = 0;
	ret->recp_local[0] = 0;
	ret->recp_internet[0] = 0;
	ret->recp_room[0] = 0;
	ret->recp_orgroom[0] = 0;
	ret->display_recp[0] = 0;
	ret->recptypes_magic = RECPTYPES_MAGIC;

	Array *recp_array = split_recps(supplied_recipients, NULL);

	char *aliases = CtdlGetSysConfig(GLOBAL_ALIASES);				// First hit the Global Alias Table

	int r;
	for (r=0; (recp_array && r<array_len(recp_array)); ++r) {
		org_recp = (char *)array_get_element_at(recp_array, r);
		strncpy(this_recp, org_recp, sizeof this_recp);

		int i;
		for (i=0; i<3; ++i) {						// pass three times through the aliaser
			mailtype = expand_aliases(this_recp, aliases);
	
			// If an alias expanded to multiple recipients, strip off those recipients and append them
			// to the end of the array.  This loop will hit those again when it gets there.
			if (mailtype == EA_MULTIPLE) {
				recp_array = split_recps(this_recp, recp_array);
			}
		}

		// This loop searches for duplicate recipients in the final list and marks them to be skipped.
		int j;
		for (j=0; j<r; ++j) {
			if (!strcasecmp(this_recp, (char *)array_get_element_at(recp_array, j) )) {
				mailtype = EA_SKIP;
			}
		}

		syslog(LOG_DEBUG, "Recipient #%d of type %d is <%s>", r, mailtype, this_recp);
		invalid = 0;
		errmsg[0] = 0;
		switch(mailtype) {
		case EA_LOCAL:					// There are several types of "local" recipients.

			// Old BBS conventions require mail to "sysop" to go somewhere.  Send it to the admin room.
			if (!strcasecmp(this_recp, "sysop")) {
				++ret->num_room;
				strcpy(this_recp, CtdlGetConfigStr("c_aideroom"));
				if (!IsEmptyStr(ret->recp_room)) {
					strcat(ret->recp_room, "|");
				}
				strcat(ret->recp_room, this_recp);
			}

			// This handles rooms which can receive posts via email.
			else if (!strncasecmp(this_recp, "room_", 5)) {
				original_room = CC->room;				// Remember where we parked

				char mail_to_room[ROOMNAMELEN];
				char *m;
				strncpy(mail_to_room, &this_recp[5], sizeof mail_to_room);
				for (m = mail_to_room; *m; ++m) {
					if (m[0] == '_') m[0]=' ';
				}
				if (!CtdlGetRoom(&CC->room, mail_to_room)) {		// Find the room they asked for

					err = CtdlDoIHavePermissionToPostInThisRoom(	// check for write permissions to room
						errmsg, 
						sizeof errmsg, 
						Flags,
						0					// 0 means "this is not a reply"
					);
					if (err) {
						++ret->num_error;
						invalid = 1;
					} 
					else {
						++ret->num_room;
						if (!IsEmptyStr(ret->recp_room)) {
							strcat(ret->recp_room, "|");
						}
						strcat(ret->recp_room, CC->room.QRname);
	
						if (!IsEmptyStr(ret->recp_orgroom)) {
							strcat(ret->recp_orgroom, "|");
						}
						strcat(ret->recp_orgroom, this_recp);
	
					}
				}
				else {							// no such room exists
					++ret->num_error;
					invalid = 1;
				}
						
				// Restore this session's original room location.
				CC->room = original_room;

			}

			// This handles the most common case, which is mail to a user's inbox.
			else if (CtdlGetUser(&tempUS, this_recp) == 0) {
				++ret->num_local;
				strcpy(this_recp, tempUS.fullname);
				if (!IsEmptyStr(ret->recp_local)) {
					strcat(ret->recp_local, "|");
				}
				strcat(ret->recp_local, this_recp);
			}

			// No match for this recipient
			else {
				++ret->num_error;
				invalid = 1;
			}
			break;
		case EA_INTERNET:
			// Yes, you're reading this correctly: if the target domain points back to the local system,
			// the address is invalid.  That's because if the address were valid, we would have
			// already translated it to a local address by now.
			if (IsDirectory(this_recp, 0)) {
				++ret->num_error;
				invalid = 1;
			}
			else {
				++ret->num_internet;
				if (!IsEmptyStr(ret->recp_internet)) {
					strcat(ret->recp_internet, "|");
				}
				strcat(ret->recp_internet, this_recp);
			}
			break;
		case EA_MULTIPLE:
		case EA_SKIP:
			// no action required, anything in this slot has already been processed elsewhere
			break;
		case EA_ERROR:
			++ret->num_error;
			invalid = 1;
			break;
		}
		if (invalid) {
			if (IsEmptyStr(errmsg)) {
				snprintf(append, sizeof append, "Invalid recipient: %s", this_recp);
			}
			else {
				snprintf(append, sizeof append, "%s", errmsg);
			}
			if ( (strlen(ret->errormsg) + strlen(append) + 3) < SIZ) {
				if (!IsEmptyStr(ret->errormsg)) {
					strcat(ret->errormsg, "; ");
				}
				strcat(ret->errormsg, append);
			}
		}
		else {
			if (IsEmptyStr(ret->display_recp)) {
				strcpy(append, this_recp);
			}
			else {
				snprintf(append, sizeof append, ", %s", this_recp);
			}
			if ( (strlen(ret->display_recp)+strlen(append)) < SIZ) {
				strcat(ret->display_recp, append);
			}
		}
	}

	if (aliases != NULL) {		// ok, we're done with the global alias list now
		free(aliases);
	}

	if ( (ret->num_local + ret->num_internet + ret->num_room + ret->num_error) == 0) {
		ret->num_error = (-1);
		strcpy(ret->errormsg, "No recipients specified.");
	}

	syslog(LOG_DEBUG, "internet_addressing: validate_recipients() = %d local, %d room, %d SMTP, %d error",
		ret->num_local, ret->num_room, ret->num_internet, ret->num_error
	);

	free(recipients);
	if (recp_array) {
		array_free(recp_array);
	}

	return(ret);
}


// Destructor for recptypes
void free_recipients(struct recptypes *valid) {

	if (valid == NULL) {
		return;
	}

	if (valid->recptypes_magic != RECPTYPES_MAGIC) {
		syslog(LOG_ERR, "internet_addressing: attempt to call free_recipients() on some other data type!");
		exit(CTDLEXIT_BAD_MAGIC);
	}

	if (valid->errormsg != NULL)		free(valid->errormsg);
	if (valid->recp_local != NULL)		free(valid->recp_local);
	if (valid->recp_internet != NULL)	free(valid->recp_internet);
	if (valid->recp_room != NULL)		free(valid->recp_room);
	if (valid->recp_orgroom != NULL)        free(valid->recp_orgroom);
	if (valid->display_recp != NULL)	free(valid->display_recp);
	if (valid->bounce_to != NULL)		free(valid->bounce_to);
	if (valid->envelope_from != NULL)	free(valid->envelope_from);
	if (valid->sending_room != NULL)	free(valid->sending_room);
	free(valid);
}


char *qp_encode_email_addrs(char *source) {
	char *user, *node, *name;
	const char headerStr[] = "=?UTF-8?Q?";
	char *Encoded;
	char *EncodedName;
	char *nPtr;
	int need_to_encode = 0;
	long SourceLen;
	long EncodedMaxLen;
	long nColons = 0;
	long *AddrPtr;
	long *AddrUtf8;
	long nAddrPtrMax = 50;
	long nmax;
	int InQuotes = 0;
	int i, n;

	if (source == NULL) return source;
	if (IsEmptyStr(source)) return source;
	syslog(LOG_DEBUG, "internet_addressing: qp_encode_email_addrs <%s>", source);

	AddrPtr = malloc (sizeof (long) * nAddrPtrMax);
	AddrUtf8 = malloc (sizeof (long) * nAddrPtrMax);
	memset(AddrUtf8, 0, sizeof (long) * nAddrPtrMax);
	*AddrPtr = 0;
	i = 0;
	while (!IsEmptyStr (&source[i])) {
		if (nColons >= nAddrPtrMax){
			long *ptr;

			ptr = (long *) malloc(sizeof (long) * nAddrPtrMax * 2);
			memcpy (ptr, AddrPtr, sizeof (long) * nAddrPtrMax);
			free (AddrPtr), AddrPtr = ptr;

			ptr = (long *) malloc(sizeof (long) * nAddrPtrMax * 2);
			memset(&ptr[nAddrPtrMax], 0, sizeof (long) * nAddrPtrMax);

			memcpy (ptr, AddrUtf8, sizeof (long) * nAddrPtrMax);
			free (AddrUtf8), AddrUtf8 = ptr;
			nAddrPtrMax *= 2;				
		}
		if (((unsigned char) source[i] < 32) || ((unsigned char) source[i] > 126)) {
			need_to_encode = 1;
			AddrUtf8[nColons] = 1;
		}
		if (source[i] == '"') {
			InQuotes = !InQuotes;
		}
		if (!InQuotes && source[i] == ',') {
			AddrPtr[nColons] = i;
			nColons++;
		}
		i++;
	}
	if (need_to_encode == 0) {
		free(AddrPtr);
		free(AddrUtf8);
		return source;
	}

	SourceLen = i;
	EncodedMaxLen = nColons * (sizeof(headerStr) + 3) + SourceLen * 3;
	Encoded = (char*) malloc (EncodedMaxLen);

	for (i = 0; i < nColons; i++) {
		source[AddrPtr[i]++] = '\0';
	}
	// TODO: if libidn, this might get larger
	user = malloc(SourceLen + 1);
	node = malloc(SourceLen + 1);
	name = malloc(SourceLen + 1);

	nPtr = Encoded;
	*nPtr = '\0';
	for (i = 0; i < nColons && nPtr != NULL; i++) {
		nmax = EncodedMaxLen - (nPtr - Encoded);
		if (AddrUtf8[i]) {
			process_rfc822_addr(&source[AddrPtr[i]], user, node, name);
			// TODO: libIDN here !
			if (IsEmptyStr(name)) {
				n = snprintf(nPtr, nmax, (i==0)?"%s@%s" : ",%s@%s", user, node);
			}
			else {
				EncodedName = rfc2047encode(name, strlen(name));			
				n = snprintf(nPtr, nmax, (i==0)?"%s <%s@%s>" : ",%s <%s@%s>", EncodedName, user, node);
				free(EncodedName);
			}
		}
		else { 
			n = snprintf(nPtr, nmax, (i==0)?"%s" : ",%s", &source[AddrPtr[i]]);
		}
		if (n > 0 )
			nPtr += n;
		else { 
			char *ptr, *nnPtr;
			ptr = (char*) malloc(EncodedMaxLen * 2);
			memcpy(ptr, Encoded, EncodedMaxLen);
			nnPtr = ptr + (nPtr - Encoded), nPtr = nnPtr;
			free(Encoded), Encoded = ptr;
			EncodedMaxLen *= 2;
			i--; // do it once more with properly lengthened buffer
		}
	}
	for (i = 0; i < nColons; i++)
		source[--AddrPtr[i]] = ',';

	free(user);
	free(node);
	free(name);
	free(AddrUtf8);
	free(AddrPtr);
	return Encoded;
}


// Unfold a multi-line field into a single line, removing multi-whitespaces
void unfold_rfc822_field(char **field, char **FieldEnd) 
{
	int quote = 0;
	char *pField = *field;
	char *sField;
	char *pFieldEnd = *FieldEnd;

	while (isspace(*pField))
		pField++;
	// remove leading/trailing whitespace
	;

	while (isspace(*pFieldEnd))
		pFieldEnd --;

	*FieldEnd = pFieldEnd;
	// convert non-space whitespace to spaces, and remove double blanks
	for (sField = *field = pField; 
	     sField < pFieldEnd; 
	     pField++, sField++)
	{
		if ((*sField=='\r') || (*sField=='\n'))
		{
			int offset = 1;
			while ( ( (*(sField + offset) == '\r') || (*(sField + offset) == '\n' )) && (sField + offset < pFieldEnd) ) {
				offset ++;
			}
			sField += offset;
			*pField = *sField;
		}
		else {
			if (*sField=='\"') quote = 1 - quote;
			if (!quote) {
				if (isspace(*sField)) {
					*pField = ' ';
					pField++;
					sField++;
					
					while ((sField < pFieldEnd) && 
					       isspace(*sField))
						sField++;
					*pField = *sField;
				}
				else *pField = *sField;
			}
			else *pField = *sField;
		}
	}
	*pField = '\0';
	*FieldEnd = pField - 1;
}


// Split an RFC822-style address into userid, host, and full name
void process_rfc822_addr(const char *rfc822, char *user, char *node, char *name) {
	int a;

	strcpy(user, "");
	strcpy(node, CtdlGetConfigStr("c_fqdn"));
	strcpy(name, "");

	if (rfc822 == NULL) return;

	// extract full name - first, it's From minus <userid>
	strcpy(name, rfc822);
	stripout(name, '<', '>');

	// and anything to the right of a @
	for (a = 0; name[a] != '\0'; ++a) {
		if (name[a] == '@') {
			name[a] = 0;
			break;
		}
	}

	// but if there are parentheses, that changes the rules...
	if ((haschar(rfc822, '(') == 1) && (haschar(rfc822, ')') == 1)) {
		strcpy(name, rfc822);
		stripallbut(name, '(', ')');
	}

	// but if there are a set of quotes, that supersedes everything
	if (haschar(rfc822, 34) == 2) {
		strcpy(name, rfc822);
		while ((!IsEmptyStr(name)) && (name[0] != 34)) {
			strcpy(&name[0], &name[1]);
		}
		strcpy(&name[0], &name[1]);
		for (a = 0; name[a] != '\0'; ++a)
			if (name[a] == 34) {
				name[a] = 0;
				break;
			}
	}
	// extract user id
	strcpy(user, rfc822);

	// first get rid of anything in parens
	stripout(user, '(', ')');

	// if there's a set of angle brackets, strip it down to that
	if ((haschar(user, '<') == 1) && (haschar(user, '>') == 1)) {
		stripallbut(user, '<', '>');
	}

	// and anything to the right of a @
	for (a = 0; user[a] != '\0'; ++a) {
		if (user[a] == '@') {
			user[a] = 0;
			break;
		}
	}

	// extract node name
	strcpy(node, rfc822);

	// first get rid of anything in parens
	stripout(node, '(', ')');

	// if there's a set of angle brackets, strip it down to that
	if ((haschar(node, '<') == 1) && (haschar(node, '>') == 1)) {
		stripallbut(node, '<', '>');
	}

	// If no node specified, tack ours on instead
	if (haschar(node, '@') == 0) {
		strcpy(node, CtdlGetConfigStr("c_nodename"));
	}
	else {
		// strip anything to the left of a @
		while ((!IsEmptyStr(node)) && (haschar(node, '@') > 0)) {
			strcpy(node, &node[1]);
		}
	}

	// strip leading and trailing spaces in all strings
	string_trim(user);
	string_trim(node);
	string_trim(name);

	// If we processed a string that had the address in angle brackets
	// but no name outside the brackets, we now have an empty name.  In
	// this case, use the user portion of the address as the name.
	if ((IsEmptyStr(name)) && (!IsEmptyStr(user))) {
		strcpy(name, user);
	}
}


// convert_field() is a helper function for convert_internet_message().
// Given start/end positions for an rfc822 field, it converts it to a Citadel
// field if it wants to, and unfolds it if necessary.
//
// Returns 1 if the field was converted and inserted into the Citadel message
// structure, implying that the source field should be removed from the
// message text.
int convert_field(struct CtdlMessage *msg, const char *beg, const char *end) {
	char *key, *value, *valueend;
	long len;
	const char *pos;
	int i;
	const char *colonpos = NULL;
	int processed = 0;
	char user[1024];
	char node[1024];
	char name[1024];
	char addr[1024];
	time_t parsed_date;
	long valuelen;

	for (pos = end; pos >= beg; pos--) {
		if (*pos == ':') colonpos = pos;
	}

	if (colonpos == NULL) return(0);	/* no colon? not a valid header line */

	len = end - beg;
	key = malloc(len + 2);
	memcpy(key, beg, len + 1);
	key[len] = '\0';
	valueend = key + len;
	* ( key + (colonpos - beg) ) = '\0';
	value = &key[(colonpos - beg) + 1];
	// printf("Header: [%s]\nValue: [%s]\n", key, value);
	unfold_rfc822_field(&value, &valueend);
	valuelen = valueend - value + 1;
	// printf("UnfoldedValue: [%s]\n", value);

	// Here's the big rfc822-to-citadel loop.

	// Date/time is converted into a unix timestamp.  If the conversion
	// fails, we replace it with the time the message arrived locally.
	if (!strcasecmp(key, "Date")) {
		parsed_date = parsedate(value);
		if (parsed_date < 0L) parsed_date = time(NULL);

		if (CM_IsEmpty(msg, eTimestamp))
			CM_SetFieldLONG(msg, eTimestamp, parsed_date);
		processed = 1;
	}

	else if (!strcasecmp(key, "From")) {
		process_rfc822_addr(value, user, node, name);
		syslog(LOG_DEBUG, "internet_addressing: converted to <%s@%s> (%s)", user, node, name);
		snprintf(addr, sizeof(addr), "%s@%s", user, node);
		if (CM_IsEmpty(msg, eAuthor) && !IsEmptyStr(name)) {
			CM_SetField(msg, eAuthor, name, -1);
		}
		if (CM_IsEmpty(msg, erFc822Addr) && !IsEmptyStr(addr)) {
			CM_SetField(msg, erFc822Addr, addr, -1);
		}
		processed = 1;
	}

	else if (!strcasecmp(key, "Subject")) {
		if (CM_IsEmpty(msg, eMsgSubject))
			CM_SetField(msg, eMsgSubject, value, valuelen);
		processed = 1;
	}

	else if (!strcasecmp(key, "List-ID")) {
		if (CM_IsEmpty(msg, eListID))
			CM_SetField(msg, eListID, value, valuelen);
		processed = 1;
	}

	else if (!strcasecmp(key, "To")) {
		if (CM_IsEmpty(msg, eRecipient))
			CM_SetField(msg, eRecipient, value, valuelen);
		processed = 1;
	}

	else if (!strcasecmp(key, "CC")) {
		if (CM_IsEmpty(msg, eCarbonCopY))
			CM_SetField(msg, eCarbonCopY, value, valuelen);
		processed = 1;
	}

	else if (!strcasecmp(key, "Message-ID")) {
		if (!CM_IsEmpty(msg, emessageId)) {
			syslog(LOG_WARNING, "internet_addressing: duplicate message id");
		}
		else {
			char *pValue;
			long pValueLen;

			pValue = value;
			pValueLen = valuelen;
			// Strip angle brackets
			while (haschar(pValue, '<') > 0) {
				pValue ++;
				pValueLen --;
			}

			for (i = 0; i <= pValueLen; ++i)
				if (pValue[i] == '>') {
					pValueLen = i;
					break;
				}

			CM_SetField(msg, emessageId, pValue, pValueLen);
		}

		processed = 1;
	}

	else if (!strcasecmp(key, "Return-Path")) {
		if (CM_IsEmpty(msg, eMessagePath))
			CM_SetField(msg, eMessagePath, value, valuelen);
		processed = 1;
	}

	else if (!strcasecmp(key, "Envelope-To")) {
		if (CM_IsEmpty(msg, eenVelopeTo))
			CM_SetField(msg, eenVelopeTo, value, valuelen);
		processed = 1;
	}

	else if (!strcasecmp(key, "References")) {
		CM_SetField(msg, eWeferences, value, valuelen);
		processed = 1;
	}

	else if (!strcasecmp(key, "Reply-To")) {
		CM_SetField(msg, eReplyTo, value, valuelen);
		processed = 1;
	}

	else if (!strcasecmp(key, "In-reply-to")) {
		if (CM_IsEmpty(msg, eWeferences)) // References: supersedes In-reply-to:
			CM_SetField(msg, eWeferences, value, valuelen);
		processed = 1;
	}



	// Clean up and move on.
	free(key);	// Don't free 'value', it's actually the same buffer
	return processed;
}


// Convert RFC822 references format (References) to Citadel references format (Weferences)
void convert_references_to_wefewences(char *str) {
	int bracket_nesting = 0;
	char *ptr = str;
	char *moveptr = NULL;
	char ch;

	while(*ptr) {
		ch = *ptr;
		if (ch == '>') {
			--bracket_nesting;
			if (bracket_nesting < 0) bracket_nesting = 0;
		}
		if ((ch == '>') && (bracket_nesting == 0) && (*(ptr+1)) && (ptr>str) ) {
			*ptr = '|';
			++ptr;
		}
		else if (bracket_nesting > 0) {
			++ptr;
		}
		else {
			moveptr = ptr;
			while (*moveptr) {
				*moveptr = *(moveptr+1);
				++moveptr;
			}
		}
		if (ch == '<') ++bracket_nesting;
	}

}


// Convert an RFC822 message (headers + body) to a CtdlMessage structure.
// NOTE: the supplied buffer becomes part of the CtdlMessage structure, and
// will be deallocated when CM_Free() is called.  Therefore, the
// supplied buffer should be DEREFERENCED.  It should not be freed or used
// again.
struct CtdlMessage *convert_internet_message(char *rfc822) {
	StrBuf *RFCBuf = NewStrBufPlain(rfc822, -1);
	free (rfc822);
	return convert_internet_message_buf(&RFCBuf);
}


struct CtdlMessage *convert_internet_message_buf(StrBuf **rfc822)
{
	struct CtdlMessage *msg;
	const char *pos, *beg, *end, *totalend;
	int done, alldone = 0;
	int converted;
	StrBuf *OtherHeaders;

	msg = malloc(sizeof(struct CtdlMessage));
	if (msg == NULL) return msg;

	memset(msg, 0, sizeof(struct CtdlMessage));
	msg->cm_magic = CTDLMESSAGE_MAGIC;	// self check
	msg->cm_anon_type = 0;			// never anonymous
	msg->cm_format_type = FMT_RFC822;	// internet message

	pos = ChrPtr(*rfc822);
	totalend = pos + StrLength(*rfc822);
	done = 0;
	OtherHeaders = NewStrBufPlain(NULL, StrLength(*rfc822));

	while (!alldone) {

		/* Locate beginning and end of field, keeping in mind that
		 * some fields might be multiline
		 */
		end = beg = pos;

		while ((end < totalend) && 
		       (end == beg) && 
		       (done == 0) ) 
		{

			if ( (*pos=='\n') && ((*(pos+1))!=0x20) && ((*(pos+1))!=0x09) )
			{
				end = pos;
			}

			/* done with headers? */
			if ((*pos=='\n') &&
			    ( (*(pos+1)=='\n') ||
			      (*(pos+1)=='\r')) ) 
			{
				alldone = 1;
			}

			if (pos >= (totalend - 1) )
			{
				end = pos;
				done = 1;
			}

			++pos;

		}

		/* At this point we have a field.  Are we interested in it? */
		converted = convert_field(msg, beg, end);

		/* Strip the field out of the RFC822 header if we used it */
		if (!converted) {
			StrBufAppendBufPlain(OtherHeaders, beg, end - beg, 0);
			StrBufAppendBufPlain(OtherHeaders, HKEY("\n"), 0);
		}

		/* If we've hit the end of the message, bail out */
		if (pos >= totalend)
			alldone = 1;
	}
	StrBufAppendBufPlain(OtherHeaders, HKEY("\n"), 0);
	if (pos < totalend)
		StrBufAppendBufPlain(OtherHeaders, pos, totalend - pos, 0);
	FreeStrBuf(rfc822);
	CM_SetAsFieldSB(msg, eMesageText, &OtherHeaders);

	/* Follow-up sanity checks... */

	/* If there's no timestamp on this message, set it to now. */
	if (CM_IsEmpty(msg, eTimestamp)) {
		CM_SetFieldLONG(msg, eTimestamp, time(NULL));
	}

	/* If a W (references, or rather, Wefewences) field is present, we
	 * have to convert it from RFC822 format to Citadel format.
	 */
	if (!CM_IsEmpty(msg, eWeferences)) {
		/// todo: API!
		convert_references_to_wefewences(msg->cm_fields[eWeferences]);
	}

	return msg;
}


/*
 * Look for a particular header field in an RFC822 message text.  If the
 * requested field is found, it is unfolded (if necessary) and returned to
 * the caller.  The field name is stripped out, leaving only its contents.
 * The caller is responsible for freeing the returned buffer.  If the requested
 * field is not present, or anything else goes wrong, it returns NULL.
 */
char *rfc822_fetch_field(const char *rfc822, const char *fieldname) {
	char *fieldbuf = NULL;
	const char *end_of_headers;
	const char *field_start;
	const char *ptr;
	char *cont;
	char fieldhdr[SIZ];

	/* Should never happen, but sometimes we get stupid */
	if (rfc822 == NULL) return(NULL);
	if (fieldname == NULL) return(NULL);

	snprintf(fieldhdr, sizeof fieldhdr, "%s:", fieldname);

	/* Locate the end of the headers, so we don't run past that point */
	end_of_headers = cbmstrcasestr(rfc822, "\n\r\n");
	if (end_of_headers == NULL) {
		end_of_headers = cbmstrcasestr(rfc822, "\n\n");
	}
	if (end_of_headers == NULL) return (NULL);

	field_start = cbmstrcasestr(rfc822, fieldhdr);
	if (field_start == NULL) return(NULL);
	if (field_start > end_of_headers) return(NULL);

	fieldbuf = malloc(SIZ);
	strcpy(fieldbuf, "");

	ptr = field_start;
	ptr = cmemreadline(ptr, fieldbuf, SIZ-strlen(fieldbuf) );
	while ( (isspace(ptr[0])) && (ptr < end_of_headers) ) {
		strcat(fieldbuf, " ");
		cont = &fieldbuf[strlen(fieldbuf)];
		ptr = cmemreadline(ptr, cont, SIZ-strlen(fieldbuf) );
		string_trim(cont);
	}

	strcpy(fieldbuf, &fieldbuf[strlen(fieldhdr)]);
	string_trim(fieldbuf);

	return(fieldbuf);
}


/*****************************************************************************
 *                      DIRECTORY MANAGEMENT FUNCTIONS                       *
 *****************************************************************************/

/*
 * Generate the index key for an Internet e-mail address to be looked up
 * in the database.
 */
void directory_key(char *key, char *addr) {
	int i;
	int keylen = 0;

	for (i=0; !IsEmptyStr(&addr[i]); ++i) {
		if (!isspace(addr[i])) {
			key[keylen++] = tolower(addr[i]);
		}
	}
	key[keylen++] = 0;

	syslog(LOG_DEBUG, "internet_addressing: directory key is <%s>", key);
}


/*
 * Return nonzero if the supplied address is in one of "our" domains
 */
int IsDirectory(char *addr, int allow_masq_domains) {
	char domain[256];
	int h;

	extract_token(domain, addr, 1, '@', sizeof domain);
	string_trim(domain);

	h = CtdlHostAlias(domain);

	if ( (h == hostalias_masq) && allow_masq_domains)
		return(1);
	
	if (h == hostalias_localhost) {
		return(1);
	}
	else {
		return(0);
	}
}


/*
 * Add an Internet e-mail address to the directory for a user
 */
int CtdlDirectoryAddUser(char *internet_addr, char *citadel_addr) {
	char key[SIZ];

	if (IsDirectory(internet_addr, 0) == 0) {
		return 0;
	}
	syslog(LOG_DEBUG, "internet_addressing: create directory entry: %s --> %s", internet_addr, citadel_addr);
	directory_key(key, internet_addr);
	cdb_store(CDB_DIRECTORY, key, strlen(key), citadel_addr, strlen(citadel_addr)+1 );
	return 1;
}


/*
 * Delete an Internet e-mail address from the directory.
 *
 * (NOTE: we don't actually use or need the citadel_addr variable; it's merely
 * here because the callback API expects to be able to send it.)
 */
int CtdlDirectoryDelUser(char *internet_addr, char *citadel_addr) {
	char key[SIZ];
	
	syslog(LOG_DEBUG, "internet_addressing: delete directory entry: %s --> %s", internet_addr, citadel_addr);
	directory_key(key, internet_addr);
	return cdb_delete(CDB_DIRECTORY, key, strlen(key) ) == 0;
}


/*
 * Look up an Internet e-mail address in the directory.
 * On success: returns 0, and Citadel address stored in 'target'
 * On failure: returns nonzero
 */
int CtdlDirectoryLookup(char *target, char *internet_addr, size_t targbuflen) {
	struct cdbdata *cdbrec;
	char key[SIZ];

	/* Dump it in there unchanged, just for kicks */
	if (target != NULL) {
		safestrncpy(target, internet_addr, targbuflen);
	}

	/* Only do lookups for addresses with hostnames in them */
	if (num_tokens(internet_addr, '@') != 2) return(-1);

	/* Only do lookups for domains in the directory */
	if (IsDirectory(internet_addr, 0) == 0) return(-1);

	directory_key(key, internet_addr);
	cdbrec = cdb_fetch(CDB_DIRECTORY, key, strlen(key) );
	if (cdbrec != NULL) {
		if (target != NULL) {
			safestrncpy(target, cdbrec->ptr, targbuflen);
		}
		cdb_free(cdbrec);
		return(0);
	}

	return(-1);
}


/*
 * Harvest any email addresses that someone might want to have in their
 * "collected addresses" book.
 */
char *harvest_collected_addresses(struct CtdlMessage *msg) {
	char *coll = NULL;
	char addr[256];
	char user[256], node[256], name[256];
	int is_harvestable;
	int i, j, h;
	eMsgField field = 0;

	if (msg == NULL) return(NULL);

	is_harvestable = 1;
	strcpy(addr, "");	
	if (!CM_IsEmpty(msg, eAuthor)) {
		strcat(addr, msg->cm_fields[eAuthor]);
	}
	if (!CM_IsEmpty(msg, erFc822Addr)) {
		strcat(addr, " <");
		strcat(addr, msg->cm_fields[erFc822Addr]);
		strcat(addr, ">");
		if (IsDirectory(msg->cm_fields[erFc822Addr], 0)) {
			is_harvestable = 0;
		}
	}

	if (is_harvestable) {
		coll = strdup(addr);
	}
	else {
		coll = strdup("");
	}

	if (coll == NULL) return(NULL);

	/* Scan both the R (To) and Y (CC) fields */
	for (i = 0; i < 2; ++i) {
		if (i == 0) field = eRecipient;
		if (i == 1) field = eCarbonCopY;

		if (!CM_IsEmpty(msg, field)) {
			for (j=0; j<num_tokens(msg->cm_fields[field], ','); ++j) {
				extract_token(addr, msg->cm_fields[field], j, ',', sizeof addr);
				if (strstr(addr, "=?") != NULL) {
					utf8ify_rfc822_string(addr);
				}
				process_rfc822_addr(addr, user, node, name);
				h = CtdlHostAlias(node);
				if (h != hostalias_localhost) {
					coll = realloc(coll, strlen(coll) + strlen(addr) + 4);
					if (coll == NULL) return(NULL);
					if (!IsEmptyStr(coll)) {
						strcat(coll, ",");
					}
					string_trim(addr);
					strcat(coll, addr);
				}
			}
		}
	}

	if (IsEmptyStr(coll)) {
		free(coll);
		return(NULL);
	}
	return(coll);
}


/*
 * Helper function for CtdlRebuildDirectoryIndex()
 */
void CtdlRebuildDirectoryIndex_backend(char *username, void *data) {

	int j = 0;
	struct ctdluser usbuf;

	if (CtdlGetUser(&usbuf, username) != 0) {
		return;
	}

	if ( (!IsEmptyStr(usbuf.fullname)) && (!IsEmptyStr(usbuf.emailaddrs)) ) {
		for (j=0; j<num_tokens(usbuf.emailaddrs, '|'); ++j) {
			char one_email[512];
			extract_token(one_email, usbuf.emailaddrs, j, '|', sizeof one_email);
			CtdlDirectoryAddUser(one_email, usbuf.fullname);
		}
	}
}


/*
 * Initialize the directory database (erasing anything already there)
 */
void CtdlRebuildDirectoryIndex(void) {
	syslog(LOG_INFO, "internet_addressing: rebuilding email address directory index");
	cdb_trunc(CDB_DIRECTORY);
	ForEachUser(CtdlRebuildDirectoryIndex_backend, NULL);
}


// Configure Internet email addresses for a user account, updating the Directory Index in the process
void CtdlSetEmailAddressesForUser(char *requested_user, char *new_emailaddrs) {
	struct ctdluser usbuf;
	int i;
	char buf[SIZ];

	if (CtdlGetUserLock(&usbuf, requested_user) != 0) {	// We can lock because the DirectoryIndex functions don't lock.
		return;						// Silently fail here if the specified user does not exist.
	}

	syslog(LOG_DEBUG, "internet_addressing: setting email addresses for <%s> to <%s>", usbuf.fullname, new_emailaddrs);

	// Delete all of the existing directory index records for the user (easier this way)
	for (i=0; i<num_tokens(usbuf.emailaddrs, '|'); ++i) {
		extract_token(buf, usbuf.emailaddrs, i, '|', sizeof buf);
		CtdlDirectoryDelUser(buf, requested_user);
	}

	strcpy(usbuf.emailaddrs, new_emailaddrs);		// make it official.

	// Index all of the new email addresses (they've already been sanitized)
	for (i=0; i<num_tokens(usbuf.emailaddrs, '|'); ++i) {
		extract_token(buf, usbuf.emailaddrs, i, '|', sizeof buf);
		CtdlDirectoryAddUser(buf, requested_user);
	}

	CtdlPutUserLock(&usbuf);
}


/*
 * Auto-generate an Internet email address for a user account
 */
void AutoGenerateEmailAddressForUser(struct ctdluser *user) {
	char synthetic_email_addr[1024];
	int i, j;
	int u = 0;

	for (i=0; u==0; ++i) {
		if (i == 0) {
			// first try just converting the user name to lowercase and replacing spaces with underscores
			snprintf(synthetic_email_addr, sizeof synthetic_email_addr, "%s@%s", user->fullname, CtdlGetConfigStr("c_fqdn"));
			for (j=0; ((synthetic_email_addr[j] != '\0')&&(synthetic_email_addr[j] != '@')); j++) {
				synthetic_email_addr[j] = tolower(synthetic_email_addr[j]);
				if (!isalnum(synthetic_email_addr[j])) {
					synthetic_email_addr[j] = '_';
				}
			}
		}
		else if (i == 1) {
			// then try 'ctdl' followed by the user number
			snprintf(synthetic_email_addr, sizeof synthetic_email_addr, "ctdl%08lx@%s", user->usernum, CtdlGetConfigStr("c_fqdn"));
		}
		else if (i > 1) {
			// oof.  just keep trying other numbers until we find one
			snprintf(synthetic_email_addr, sizeof synthetic_email_addr, "ctdl%08x@%s", i, CtdlGetConfigStr("c_fqdn"));
		}
		u = CtdlDirectoryLookup(NULL, synthetic_email_addr, 0);
		syslog(LOG_DEBUG, "user_ops: address <%s> lookup returned <%d>", synthetic_email_addr, u);
	}

	CtdlSetEmailAddressesForUser(user->fullname, synthetic_email_addr);
	strncpy(CC->user.emailaddrs, synthetic_email_addr, sizeof(user->emailaddrs));
	syslog(LOG_DEBUG, "user_ops: auto-generated email address <%s> for <%s>", synthetic_email_addr, user->fullname);
}


// Determine whether the supplied email address is subscribed to the supplied room's mailing list service.
int is_email_subscribed_to_list(char *email, char *room_name) {
	struct ctdlroom room;
	long roomnum;
	char *roomnetconfig;
	int found_it = 0;

	if (CtdlGetRoom(&room, room_name)) {
		return(0);					// room not found, so definitely not subscribed
	}

	// If this room has the QR2_SMTP_PUBLIC flag set, anyone may email a post to this room, even non-subscribers.
	if (room.QRflags2 & QR2_SMTP_PUBLIC) {
		return(1);
	}

	roomnum = room.QRnumber;
	roomnetconfig = LoadRoomNetConfigFile(roomnum);
	if (roomnetconfig == NULL) {
		return(0);
	}

	// We're going to do a very sloppy match here and simply search for the specified email address
	// anywhere in the room's netconfig.  If you don't like this, fix it yourself.
	if (bmstrcasestr(roomnetconfig, email)) {
		found_it = 1;
	}
	else {
		found_it = 0;
	}

	free(roomnetconfig);
	return(found_it);
}
