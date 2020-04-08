/* 
 * Server functions which perform operations on user objects.
 *
 * Copyright (c) 1987-2020 by the citadel.org team
 *
 * This program is open source software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License, version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <stdlib.h>
#include <unistd.h>
#include "sysdep.h"
#include <stdio.h>
#include <sys/stat.h>
#include <libcitadel.h>
#include "control.h"
#include "support.h"
#include "citserver.h"
#include "config.h"
#include "citadel_ldap.h"
#include "ctdl_module.h"
#include "user_ops.h"
#include "internet_addressing.h"

/* These pipes are used to talk to the chkpwd daemon, which is forked during startup */
int chkpwd_write_pipe[2];
int chkpwd_read_pipe[2];


/*
 * makeuserkey() - convert a username into the format used as a database key
 *			"key" must be a buffer of at least USERNAME_SIZE
 *			(Key format is the username with all non-alphanumeric characters removed, and converted to lower case.)
 */
void makeuserkey(char *key, const char *username) {
	int i;
	int keylen = 0;

	if (IsEmptyStr(username)) {
		key[0] = 0;
		return;
	}

	int len = strlen(username);
	for (i=0; ((i<=len) && (i<USERNAME_SIZE-1)); ++i) {
		if (isalnum((username[i]))) {
			key[keylen++] = tolower(username[i]);
		}
	}
	key[keylen++] = 0;
}


/*
 * Compare two usernames to see if they are the same user after being keyed for the database
 * Usage is identical to strcmp()
 */
int CtdlUserCmp(char *s1, char *s2) {
	char k1[USERNAME_SIZE];
	char k2[USERNAME_SIZE];

	makeuserkey(k1, s1);
	makeuserkey(k2, s2);
	return(strcmp(k1,k2));
}


/*
 * CtdlGetUser()	retrieve named user into supplied buffer.
 *			returns 0 on success
 */
int CtdlGetUser(struct ctdluser *usbuf, char *name)
{
	char usernamekey[USERNAME_SIZE];
	struct cdbdata *cdbus;

	if (usbuf != NULL) {
		memset(usbuf, 0, sizeof(struct ctdluser));
	}

	makeuserkey(usernamekey, name);
	cdbus = cdb_fetch(CDB_USERS, usernamekey, strlen(usernamekey));

	if (cdbus == NULL) {	/* user not found */
		return(1);
	}
	if (usbuf != NULL) {
		memcpy(usbuf, cdbus->ptr, ((cdbus->len > sizeof(struct ctdluser)) ?  sizeof(struct ctdluser) : cdbus->len));
	}
	cdb_free(cdbus);
	return(0);
}


int CtdlLockGetCurrentUser(void)
{
	CitContext *CCC = CC;
	return CtdlGetUser(&CCC->user, CCC->curr_user);
}


/*
 * CtdlGetUserLock()  -  same as getuser() but locks the record
 */
int CtdlGetUserLock(struct ctdluser *usbuf, char *name)
{
	int retcode;

	retcode = CtdlGetUser(usbuf, name);
	if (retcode == 0) {
		begin_critical_section(S_USERS);
	}
	return(retcode);
}


/*
 * CtdlPutUser()  -  write user buffer into the correct place on disk
 */
void CtdlPutUser(struct ctdluser *usbuf)
{
	char usernamekey[USERNAME_SIZE];

	makeuserkey(usernamekey, usbuf->fullname);
	usbuf->version = REV_LEVEL;
	cdb_store(CDB_USERS, usernamekey, strlen(usernamekey), usbuf, sizeof(struct ctdluser));
}


void CtdlPutCurrentUserLock()
{
	CtdlPutUser(&CC->user);
}


/*
 * CtdlPutUserLock()  -  same as putuser() but locks the record
 */
void CtdlPutUserLock(struct ctdluser *usbuf)
{
	CtdlPutUser(usbuf);
	end_critical_section(S_USERS);
}


/*
 * rename_user()  -  this is tricky because the user's display name is the database key
 *
 * Returns 0 on success or nonzero if there was an error...
 *
 */
int rename_user(char *oldname, char *newname) {
	int retcode = RENAMEUSER_OK;
	struct ctdluser usbuf;

	char oldnamekey[USERNAME_SIZE];
	char newnamekey[USERNAME_SIZE];

	/* Create the database keys... */
	makeuserkey(oldnamekey, oldname);
	makeuserkey(newnamekey, newname);

	/* Lock up and get going */
	begin_critical_section(S_USERS);

	/* We cannot rename a user who is currently logged in */
	if (CtdlIsUserLoggedIn(oldname)) {
		end_critical_section(S_USERS);
		return RENAMEUSER_LOGGED_IN;
	}

	if (CtdlGetUser(&usbuf, newname) == 0) {
		retcode = RENAMEUSER_ALREADY_EXISTS;
	}
	else {

		if (CtdlGetUser(&usbuf, oldname) != 0) {
			retcode = RENAMEUSER_NOT_FOUND;
		}
		else {		/* Sanity checks succeeded.  Now rename the user. */
			if (usbuf.usernum == 0) {
				syslog(LOG_DEBUG, "user_ops: can not rename user \"Citadel\".");
				retcode = RENAMEUSER_NOT_FOUND;
			} else {
				syslog(LOG_DEBUG, "user_ops: renaming <%s> to <%s>", oldname, newname);
				cdb_delete(CDB_USERS, oldnamekey, strlen(oldnamekey));
				safestrncpy(usbuf.fullname, newname, sizeof usbuf.fullname);
				CtdlPutUser(&usbuf);
				cdb_store(CDB_USERSBYNUMBER, &usbuf.usernum, sizeof(long), usbuf.fullname, strlen(usbuf.fullname)+1 );
				retcode = RENAMEUSER_OK;
			}
		}
	
	}

	end_critical_section(S_USERS);
	return(retcode);
}


/*
 * Convert a username into the format used as a database key prior to version 928
 * This only gets called by reindex_user_928()
 */
void makeuserkey_pre928(char *key, const char *username) {
	int i;

	int len = strlen(username);

	if (len >= USERNAME_SIZE) {
		syslog(LOG_INFO, "Username too long: %s", username);
		len = USERNAME_SIZE - 1; 
	}
	for (i=0; i<=len; ++i) {
		key[i] = tolower(username[i]);
	}
}


/*
 * Read a user record using the pre-v928 index format, and write it back using the v928-and-higher index format.
 * This ONLY gets called during an upgrade from version <928 to version >=928.
 */
void reindex_user_928(char *username, void *out_data) {

	char oldkey[USERNAME_SIZE];
	char newkey[USERNAME_SIZE];
	struct cdbdata *cdbus;
	struct ctdluser usbuf;

	makeuserkey_pre928(oldkey, username);
	makeuserkey(newkey, username);

	syslog(LOG_DEBUG, "user_ops: reindex_user_928: %s <%s> --> <%s>", username, oldkey, newkey);

	// Fetch the user record using the old index format
	cdbus = cdb_fetch(CDB_USERS, oldkey, strlen(oldkey));
	if (cdbus == NULL) {
		syslog(LOG_INFO, "user_ops: <%s> not found, were they already reindexed?", username);
		return;
	}
	memcpy(&usbuf, cdbus->ptr, ((cdbus->len > sizeof(struct ctdluser)) ? sizeof(struct ctdluser) : cdbus->len));
	cdb_free(cdbus);

	// delete the old record
	cdb_delete(CDB_USERS, oldkey, strlen(oldkey));

	// write the new record
	cdb_store(CDB_USERS, newkey, strlen(newkey), &usbuf, sizeof(struct ctdluser));
}


/*
 * Index-generating function used by Ctdl[Get|Set]Relationship
 */
int GenerateRelationshipIndex(char *IndexBuf,
			      long RoomID,
			      long RoomGen,
			      long UserID)
{
	struct {
		long iRoomID;
		long iRoomGen;
		long iUserID;
	} TheIndex;

	TheIndex.iRoomID = RoomID;
	TheIndex.iRoomGen = RoomGen;
	TheIndex.iUserID = UserID;

	memcpy(IndexBuf, &TheIndex, sizeof(TheIndex));
	return(sizeof(TheIndex));
}


/*
 * Back end for CtdlSetRelationship()
 */
void put_visit(visit *newvisit)
{
	char IndexBuf[32];
	int IndexLen = 0;

	memset (IndexBuf, 0, sizeof (IndexBuf));
	/* Generate an index */
	IndexLen = GenerateRelationshipIndex(IndexBuf, newvisit->v_roomnum, newvisit->v_roomgen, newvisit->v_usernum);

	/* Store the record */
	cdb_store(CDB_VISIT, IndexBuf, IndexLen,
		  newvisit, sizeof(visit)
	);
}


/*
 * Define a relationship between a user and a room
 */
void CtdlSetRelationship(visit *newvisit, struct ctdluser *rel_user, struct ctdlroom *rel_room) {
	/* We don't use these in Citadel because they're implicit by the
	 * index, but they must be present if the database is exported.
	 */
	newvisit->v_roomnum = rel_room->QRnumber;
	newvisit->v_roomgen = rel_room->QRgen;
	newvisit->v_usernum = rel_user->usernum;

	put_visit(newvisit);
}


/*
 * Locate a relationship between a user and a room
 */
void CtdlGetRelationship(visit *vbuf, struct ctdluser *rel_user, struct ctdlroom *rel_room) {
	char IndexBuf[32];
	int IndexLen;
	struct cdbdata *cdbvisit;

	/* Generate an index */
	IndexLen = GenerateRelationshipIndex(IndexBuf, rel_room->QRnumber, rel_room->QRgen, rel_user->usernum);

	/* Clear out the buffer */
	memset(vbuf, 0, sizeof(visit));

	cdbvisit = cdb_fetch(CDB_VISIT, IndexBuf, IndexLen);
	if (cdbvisit != NULL) {
		memcpy(vbuf, cdbvisit->ptr, ((cdbvisit->len > sizeof(visit)) ?  sizeof(visit) : cdbvisit->len));
		cdb_free(cdbvisit);
	}
	else {
		/* If this is the first time the user has seen this room,
		 * set the view to be the default for the room.
		 */
		vbuf->v_view = rel_room->QRdefaultview;
	}

	/* Set v_seen if necessary */
	if (vbuf->v_seen[0] == 0) {
		snprintf(vbuf->v_seen, sizeof vbuf->v_seen, "*:%ld", vbuf->v_lastseen);
	}
}


void CtdlMailboxName(char *buf, size_t n, const struct ctdluser *who, const char *prefix)
{
	snprintf(buf, n, "%010ld.%s", who->usernum, prefix);
}


void MailboxName(char *buf, size_t n, const struct ctdluser *who, const char *prefix)
{
	snprintf(buf, n, "%010ld.%s", who->usernum, prefix);
}


/*
 * Check to see if the specified user has Internet mail permission
 * (returns nonzero if permission is granted)
 */
int CtdlCheckInternetMailPermission(struct ctdluser *who) {

	/* Do not allow twits to send Internet mail */
	if (who->axlevel <= AxProbU) return(0);

	/* Globally enabled? */
	if (CtdlGetConfigInt("c_restrict") == 0) return(1);

	/* User flagged ok? */
	if (who->flags & US_INTERNET) return(2);

	/* Admin level access? */
	if (who->axlevel >= AxAideU) return(3);

	/* No mail for you! */
	return(0);
}


/*
 * Convenience function.
 */
int CtdlAccessCheck(int required_level)
{
	if (CC->internal_pgm) return(0);
	if (required_level >= ac_internal) {
		cprintf("%d This is not a user-level command.\n", ERROR + HIGHER_ACCESS_REQUIRED);
		return(-1);
	}

	if ((required_level >= ac_logged_in_or_guest) && (CC->logged_in == 0) && (CtdlGetConfigInt("c_guest_logins") == 0)) {
		cprintf("%d Not logged in.\n", ERROR + NOT_LOGGED_IN);
		return(-1);
	}

	if ((required_level >= ac_logged_in) && (CC->logged_in == 0)) {
		cprintf("%d Not logged in.\n", ERROR + NOT_LOGGED_IN);
		return(-1);
	}

	if (CC->user.axlevel >= AxAideU) return(0);
 	if (required_level >= ac_aide) {
		cprintf("%d This command requires Admin access.\n",
			ERROR + HIGHER_ACCESS_REQUIRED);
		return(-1);
	}

	if (is_room_aide()) return(0);
	if (required_level >= ac_room_aide) {
		cprintf("%d This command requires Admin or Room Admin access.\n",
			ERROR + HIGHER_ACCESS_REQUIRED);
		return(-1);
	}

	/* shhh ... succeed quietly */
	return(0);
}


/*
 * Is the user currently logged in an Admin?
 */
int is_aide(void)
{
	if (CC->user.axlevel >= AxAideU)
		return(1);
	else
		return(0);
}


/*
 * Is the user currently logged in an Admin *or* the room Admin for this room?
 */
int is_room_aide(void)
{

	if (!CC->logged_in) {
		return(0);
	}

	if ((CC->user.axlevel >= AxAideU) || (CC->room.QRroomaide == CC->user.usernum)) {
		return(1);
	} else {
		return(0);
	}
}


/*
 * CtdlGetUserByNumber() -	get user by number
 * 			returns 0 if user was found
 *
 * Note: fetching a user this way requires one additional database operation.
 */
int CtdlGetUserByNumber(struct ctdluser *usbuf, long number)
{
	struct cdbdata *cdbun;
	int r;

	cdbun = cdb_fetch(CDB_USERSBYNUMBER, &number, sizeof(long));
	if (cdbun == NULL) {
		syslog(LOG_INFO, "user_ops: %ld not found", number);
		return(-1);
	}

	syslog(LOG_INFO, "user_ops: %ld maps to %s", number, cdbun->ptr);
	r = CtdlGetUser(usbuf, cdbun->ptr);
	cdb_free(cdbun);
	return(r);
}


/*
 * Helper function for rebuild_usersbynumber()
 */
void rebuild_ubn_for_user(char *username, void *data) {
	struct ctdluser u;

	syslog(LOG_DEBUG, "user_ops: rebuilding usersbynumber index for %s", username);
	if (CtdlGetUser(&u, username) == 0) {
		cdb_store(CDB_USERSBYNUMBER, &(u.usernum), sizeof(long), u.fullname, strlen(u.fullname)+1);
	}
}


/*
 * Rebuild the users-by-number index
 */
void rebuild_usersbynumber(void) {
	cdb_trunc(CDB_USERSBYNUMBER);			/* delete the old indices */
	ForEachUser(rebuild_ubn_for_user, NULL);	/* enumerate the users */
}


/*
 * getuserbyuid()	Get user by system uid (for PAM mode authentication)
 *			Returns 0 if user was found
 *			This now uses an extauth index.
 */
int getuserbyuid(struct ctdluser *usbuf, uid_t number)
{
	struct cdbdata *cdbextauth;
	long usernum = 0;
	StrBuf *claimed_id;

	claimed_id = NewStrBuf();
	StrBufPrintf(claimed_id, "uid:%d", number);
	cdbextauth = cdb_fetch(CDB_EXTAUTH, ChrPtr(claimed_id), StrLength(claimed_id));
	FreeStrBuf(&claimed_id);
	if (cdbextauth == NULL) {
		return(-1);
	}

	memcpy(&usernum, cdbextauth->ptr, sizeof(long));
	cdb_free(cdbextauth);

	if (!CtdlGetUserByNumber(usbuf, usernum)) {
		return(0);
	}

	return(-1);
}


/*
 * Back end for cmd_user() and its ilk
 */
int CtdlLoginExistingUser(const char *trythisname)
{
	char username[SIZ];
	int found_user;

	syslog(LOG_DEBUG, "user_ops: CtdlLoginExistingUser(%s)", trythisname);

	if ((CC->logged_in)) {
		return login_already_logged_in;
	}

	if (trythisname == NULL) return login_not_found;
	
	if (!strncasecmp(trythisname, "SYS_", 4))
	{
		syslog(LOG_DEBUG, "user_ops: system user \"%s\" is not allowed to log in.", trythisname);
		return login_not_found;
	}

	/* Continue attempting user validation... */
	safestrncpy(username, trythisname, sizeof (username));
	striplt(username);

	if (IsEmptyStr(username)) {
		return login_not_found;
	}

	if (CtdlGetConfigInt("c_auth_mode") == AUTHMODE_HOST) {

		/* host auth mode */

		struct passwd pd;
		struct passwd *tempPwdPtr;
		char pwdbuffer[256];
	
		syslog(LOG_DEBUG, "user_ops: asking host about <%s>", username);
#ifdef HAVE_GETPWNAM_R
#ifdef SOLARIS_GETPWUID
		syslog(LOG_DEBUG, "user_ops: calling getpwnam_r()");
		tempPwdPtr = getpwnam_r(username, &pd, pwdbuffer, sizeof pwdbuffer);
#else // SOLARIS_GETPWUID
		syslog(LOG_DEBUG, "user_ops: calling getpwnam_r()");
		getpwnam_r(username, &pd, pwdbuffer, sizeof pwdbuffer, &tempPwdPtr);
#endif // SOLARIS_GETPWUID
#else // HAVE_GETPWNAM_R
		syslog(LOG_DEBUG, "user_ops: SHOULD NEVER GET HERE!!!");
		tempPwdPtr = NULL;
#endif // HAVE_GETPWNAM_R
		if (tempPwdPtr == NULL) {
			syslog(LOG_DEBUG, "user_ops: no such user <%s>", username);
			return login_not_found;
		}
	
		/* Locate the associated Citadel account.  If not found, make one attempt to create it. */
		found_user = getuserbyuid(&CC->user, pd.pw_uid);
		if (found_user != 0) {
			create_user(username, CREATE_USER_DO_NOT_BECOME_USER, pd.pw_uid);
			found_user = getuserbyuid(&CC->user, pd.pw_uid);
		}
		syslog(LOG_DEBUG, "user_ops: found it: uid=%ld, gecos=%s here: %d", (long)pd.pw_uid, pd.pw_gecos, found_user);

	}

#ifdef HAVE_LDAP
	else if ((CtdlGetConfigInt("c_auth_mode") == AUTHMODE_LDAP) || (CtdlGetConfigInt("c_auth_mode") == AUTHMODE_LDAP_AD)) {
	
		/* LDAP auth mode */

		uid_t ldap_uid;
		char ldap_cn[256];
		char ldap_dn[256];

		found_user = CtdlTryUserLDAP(username, ldap_dn, sizeof ldap_dn, ldap_cn, sizeof ldap_cn, &ldap_uid);
		if (found_user != 0) {
			return login_not_found;
		}

		found_user = getuserbyuid(&CC->user, ldap_uid);
		if (found_user != 0) {
			create_user(ldap_cn, CREATE_USER_DO_NOT_BECOME_USER, ldap_uid);
			found_user = getuserbyuid(&CC->user, ldap_uid);
		}

		if (found_user == 0) {
			if (CC->ldap_dn != NULL) free(CC->ldap_dn);
			CC->ldap_dn = strdup(ldap_dn);
		}

	}
#endif

	else {
		/* native auth mode */

		recptypes *valid = NULL;
	
		/* First, try to log in as if the supplied name is a display name */
		found_user = CtdlGetUser(&CC->user, username);
	
		/* If that didn't work, try to log in as if the supplied name * is an e-mail address */
		if (found_user != 0) {
			valid = validate_recipients(username, NULL, 0);
			if (valid != NULL) {
				if (valid->num_local == 1) {
					found_user = CtdlGetUser(&CC->user, valid->recp_local);
				}
				free_recipients(valid);
			}
		}
	}

	/* Did we find something? */
	if (found_user == 0) {
		if (((CC->nologin)) && (CC->user.axlevel < AxAideU)) {
			return login_too_many_users;
		} else {
			safestrncpy(CC->curr_user, CC->user.fullname, sizeof CC->curr_user);
			return login_ok;
		}
	}
	return login_not_found;
}


/*
 * session startup code which is common to both cmd_pass() and cmd_newu()
 */
void do_login(void)
{
	CC->logged_in = 1;
	syslog(LOG_NOTICE, "user_ops: <%s> logged in", CC->curr_user);

	CtdlGetUserLock(&CC->user, CC->curr_user);
	++(CC->user.timescalled);
	CC->previous_login = CC->user.lastcall;
	time(&CC->user.lastcall);

	/* If this user's name is the name of the system administrator
	 * (as specified in setup), automatically assign access level 6.
	 */
	if ( (!IsEmptyStr(CtdlGetConfigStr("c_sysadm"))) && (!strcasecmp(CC->user.fullname, CtdlGetConfigStr("c_sysadm"))) ) {
		CC->user.axlevel = AxAideU;
	}

	/* If we're authenticating off the host system, automatically give root the highest level of access. */
	if (CtdlGetConfigInt("c_auth_mode") == AUTHMODE_HOST) {
		if (CC->user.uid == 0) {
			CC->user.axlevel = AxAideU;
		}
	}
	CtdlPutUserLock(&CC->user);

	/* If we are using LDAP authentication, extract the user's email addresses from the directory. */
#ifdef HAVE_LDAP
	if ((CtdlGetConfigInt("c_auth_mode") == AUTHMODE_LDAP) || (CtdlGetConfigInt("c_auth_mode") == AUTHMODE_LDAP_AD)) {
		char new_emailaddrs[512];
		if (CtdlGetConfigInt("c_ldap_sync_email_addrs") > 0) {
			if (extract_email_addresses_from_ldap(CC->ldap_dn, new_emailaddrs) == 0) {
				CtdlSetEmailAddressesForUser(CC->user.fullname, new_emailaddrs);
			}
		}
	}
#endif

	/* If the user does not have any email addresses assigned, generate one. */
	if (IsEmptyStr(CC->user.emailaddrs)) {
		AutoGenerateEmailAddressForUser(&CC->user);
	}

	/* Populate the user principal identity, which is consistent and never aliased */
	strcpy(CC->cs_principal_id, "wowowowow");
	makeuserkey(CC->cs_principal_id, CC->user.fullname);
	strcat(CC->cs_principal_id, "@");
	strcat(CC->cs_principal_id, CtdlGetConfigStr("c_fqdn"));

	/*
	 * Populate cs_inet_email and cs_inet_other_emails with valid email addresses from the user record
	 */
	strcpy(CC->cs_inet_email, CC->user.emailaddrs);
	char *firstsep = strstr(CC->cs_inet_email, "|");
	if (firstsep) {
		strcpy(CC->cs_inet_other_emails, firstsep+1);
		*firstsep = 0;
	}
	else {
		CC->cs_inet_other_emails[0] = 0;
	}

	/* Create any personal rooms required by the system.
	 * (Technically, MAILROOM should be there already, but just in case...)
	 */
	CtdlCreateRoom(MAILROOM, 4, "", 0, 1, 0, VIEW_MAILBOX);
	CtdlCreateRoom(SENTITEMS, 4, "", 0, 1, 0, VIEW_MAILBOX);
	CtdlCreateRoom(USERTRASHROOM, 4, "", 0, 1, 0, VIEW_MAILBOX);
	CtdlCreateRoom(USERDRAFTROOM, 4, "", 0, 1, 0, VIEW_MAILBOX);

	/* Run any startup routines registered by loadable modules */
	PerformSessionHooks(EVT_LOGIN);

	/* Enter the lobby */
	CtdlUserGoto(CtdlGetConfigStr("c_baseroom"), 0, 0, NULL, NULL, NULL, NULL);
}


void logged_in_response(void)
{
	cprintf("%d %s|%d|%ld|%ld|%u|%ld|%ld\n",
		CIT_OK, CC->user.fullname, CC->user.axlevel,
		CC->user.timescalled, CC->user.posted,
		CC->user.flags, CC->user.usernum,
		CC->previous_login
	);
}


void CtdlUserLogout(void)
{
	CitContext *CCC = MyContext();

	syslog(LOG_DEBUG, "user_ops: CtdlUserLogout() logging out <%s> from session %d", CCC->curr_user, CCC->cs_pid);

	/* Run any hooks registered by modules... */
	PerformSessionHooks(EVT_LOGOUT);
	
	/*
	 * Clear out some session data.  Most likely, the CitContext for this
	 * session is about to get nuked when the session disconnects, but
	 * since it's possible to log in again without reconnecting, we cannot
	 * make that assumption.
	 */
	CCC->logged_in = 0;

	/* Check to see if the user was deleted while logged in and purge them if necessary */
	if ((CCC->user.axlevel == AxDeleted) && (CCC->user.usernum)) {
		purge_user(CCC->user.fullname);
	}

	/* Clear out the user record in memory so we don't behave like a ghost */
	memset(&CCC->user, 0, sizeof(struct ctdluser));
	CCC->curr_user[0] = 0;
	CCC->cs_inet_email[0] = 0;
	CCC->cs_inet_other_emails[0] = 0;
	CCC->cs_inet_fn[0] = 0;

	/* Free any output buffers */
	unbuffer_output();
}


/*
 * Validate a password on the host unix system by talking to the chkpwd daemon
 */
static int validpw(uid_t uid, const char *pass)
{
	char buf[256];
	int rv = 0;

	if (IsEmptyStr(pass)) {
		syslog(LOG_DEBUG, "user_ops: refusing to chkpwd for uid=%d with empty password", uid);
		return 0;
	}

	syslog(LOG_DEBUG, "user_ops: validating password for uid=%d using chkpwd...", uid);

	begin_critical_section(S_CHKPWD);
	rv = write(chkpwd_write_pipe[1], &uid, sizeof(uid_t));
	if (rv == -1) {
		syslog(LOG_ERR, "user_ops: communication with chkpwd broken: %m");
		end_critical_section(S_CHKPWD);
		return 0;
	}
	rv = write(chkpwd_write_pipe[1], pass, 256);
	if (rv == -1) {
		syslog(LOG_ERR, "user_ops: communication with chkpwd broken: %m");
		end_critical_section(S_CHKPWD);
		return 0;
	}
	rv = read(chkpwd_read_pipe[0], buf, 4);
	if (rv == -1) {
		syslog(LOG_ERR, "user_ops: ommunication with chkpwd broken: %m");
		end_critical_section(S_CHKPWD);
		return 0;
	}
	end_critical_section(S_CHKPWD);

	if (!strncmp(buf, "PASS", 4)) {
		syslog(LOG_DEBUG, "user_ops: chkpwd pass");
		return(1);
	}

	syslog(LOG_DEBUG, "user_ops: chkpwd fail");
	return 0;
}


/* 
 * Start up the chkpwd daemon so validpw() has something to talk to
 */
void start_chkpwd_daemon(void) {
	pid_t chkpwd_pid;
	struct stat filestats;
	int i;

	syslog(LOG_DEBUG, "user_ops: starting chkpwd daemon for host authentication mode");

	if ((stat(file_chkpwd, &filestats)==-1) || (filestats.st_size==0)) {
		syslog(LOG_ERR, "user_ops: %s: %m", file_chkpwd);
		abort();
	}
	if (pipe(chkpwd_write_pipe) != 0) {
		syslog(LOG_ERR, "user_ops: unable to create pipe for chkpwd daemon: %m");
		abort();
	}
	if (pipe(chkpwd_read_pipe) != 0) {
		syslog(LOG_ERR, "user_ops: unable to create pipe for chkpwd daemon: %m");
		abort();
	}

	chkpwd_pid = fork();
	if (chkpwd_pid < 0) {
		syslog(LOG_ERR, "user_ops: unable to fork chkpwd daemon: %m");
		abort();
	}
	if (chkpwd_pid == 0) {
		dup2(chkpwd_write_pipe[0], 0);
		dup2(chkpwd_read_pipe[1], 1);
		for (i=2; i<256; ++i) close(i);
		execl(file_chkpwd, file_chkpwd, NULL);
		syslog(LOG_ERR, "user_ops: unable to exec chkpwd daemon: %m");
		abort();
		exit(errno);
	}
}


int CtdlTryPassword(const char *password, long len)
{
	int code;
	CitContext *CCC = CC;

	if ((CCC->logged_in)) {
		syslog(LOG_WARNING, "user_ops: CtdlTryPassword: already logged in");
		return pass_already_logged_in;
	}
	if (!strcmp(CCC->curr_user, NLI)) {
		syslog(LOG_WARNING, "user_ops: CtdlTryPassword: no user selected");
		return pass_no_user;
	}
	if (CtdlGetUser(&CCC->user, CCC->curr_user)) {
		syslog(LOG_ERR, "user_ops: CtdlTryPassword: internal error");
		return pass_internal_error;
	}
	if (password == NULL) {
		syslog(LOG_INFO, "user_ops: CtdlTryPassword: NULL password string supplied");
		return pass_wrong_password;
	}

	else if (CtdlGetConfigInt("c_auth_mode") == AUTHMODE_HOST) {

		/* host auth mode */

		if (validpw(CCC->user.uid, password)) {
			code = 0;

			/*
			 * sooper-seekrit hack: populate the password field in the
			 * citadel database with the password that the user typed,
			 * if it's correct.  This allows most sites to convert from
			 * host auth to native auth if they want to.  If you think
			 * this is a security hazard, comment it out.
			 */

			CtdlGetUserLock(&CCC->user, CCC->curr_user);
			safestrncpy(CCC->user.password, password, sizeof CCC->user.password);
			CtdlPutUserLock(&CCC->user);

			/*
			 * (sooper-seekrit hack ends here)
			 */
		}
		else {
			code = (-1);
		}
	}

#ifdef HAVE_LDAP
	else if ((CtdlGetConfigInt("c_auth_mode") == AUTHMODE_LDAP) || (CtdlGetConfigInt("c_auth_mode") == AUTHMODE_LDAP_AD)) {

		/* LDAP auth mode */

		if ((CCC->ldap_dn) && (!CtdlTryPasswordLDAP(CCC->ldap_dn, password))) {
			code = 0;
		}
		else {
			code = (-1);
		}
	}
#endif

	else {

		/* native auth mode */
		char *pw;

		pw = (char*) malloc(len + 1);
		memcpy(pw, password, len + 1);
		strproc(pw);
		strproc(CCC->user.password);
		code = strcasecmp(CCC->user.password, pw);
		if (code != 0) {
			strproc(pw);
			strproc(CCC->user.password);
			code = strcasecmp(CCC->user.password, pw);
		}
		free (pw);
	}

	if (!code) {
		do_login();
		return pass_ok;
	}
	else {
		syslog(LOG_WARNING, "user_ops: bad password specified for <%s> Service <%s> Port <%ld> Remote <%s / %s>",
			CCC->curr_user,
			CCC->ServiceName,
			CCC->tcp_port,
			CCC->cs_host,
			CCC->cs_addr
		);
		return pass_wrong_password;
	}
}


/*
 * Delete a user record *and* all of its related resources.
 */
int purge_user(char pname[])
{
	struct ctdluser usbuf;
	char usernamekey[USERNAME_SIZE];

	makeuserkey(usernamekey, pname);

	/* If the name is empty we can't find them in the DB any way so just return */
	if (IsEmptyStr(pname)) {
		return(ERROR + NO_SUCH_USER);
	}

	if (CtdlGetUser(&usbuf, pname) != 0) {
		syslog(LOG_ERR, "user_ops: cannot purge user <%s> - not found", pname);
		return(ERROR + NO_SUCH_USER);
	}

	/* Don't delete a user who is currently logged in.  Instead, just
	 * set the access level to 0, and let the account get swept up
	 * during the next purge.
	 */
	if (CtdlIsUserLoggedInByNum(usbuf.usernum)) {
		syslog(LOG_WARNING, "user_ops: <%s> is logged in; not deleting", pname);
		usbuf.axlevel = AxDeleted;
		CtdlPutUser(&usbuf);
		return(1);
	}

	syslog(LOG_NOTICE, "user_ops: deleting <%s>", pname);

	/* Perform any purge functions registered by server extensions */
	PerformUserHooks(&usbuf, EVT_PURGEUSER);

	/* delete any existing user/room relationships */
	cdb_delete(CDB_VISIT, &usbuf.usernum, sizeof(long));

	/* delete the users-by-number index record */
	cdb_delete(CDB_USERSBYNUMBER, &usbuf.usernum, sizeof(long));

	/* delete the userlog entry */
	cdb_delete(CDB_USERS, usernamekey, strlen(usernamekey));

	return(0);
}


int internal_create_user(char *username, struct ctdluser *usbuf, uid_t uid)
{
	if (!CtdlGetUser(usbuf, username)) {
		return(ERROR + ALREADY_EXISTS);
	}

	/* Go ahead and initialize a new user record */
	memset(usbuf, 0, sizeof(struct ctdluser));
	safestrncpy(usbuf->fullname, username, sizeof usbuf->fullname);
	strcpy(usbuf->password, "");
	usbuf->uid = uid;

	/* These are the default flags on new accounts */
	usbuf->flags = US_LASTOLD | US_DISAPPEAR | US_PAGINATOR | US_FLOORS;

	usbuf->timescalled = 0;
	usbuf->posted = 0;
	usbuf->axlevel = CtdlGetConfigInt("c_initax");
	usbuf->lastcall = time(NULL);

	/* fetch a new user number */
	usbuf->usernum = get_new_user_number();

	/* add user to the database */
	CtdlPutUser(usbuf);
	cdb_store(CDB_USERSBYNUMBER, &usbuf->usernum, sizeof(long), usbuf->fullname, strlen(usbuf->fullname)+1);

	/* If non-native auth, index by uid */
	if ((usbuf->uid > 0) && (usbuf->uid != NATIVE_AUTH_UID)) {
		StrBuf *claimed_id = NewStrBuf();
		StrBufPrintf(claimed_id, "uid:%d", usbuf->uid);
		attach_extauth(usbuf, claimed_id);
		FreeStrBuf(&claimed_id);
	}

	return(0);
}


/*
 * create_user()  -  back end processing to create a new user
 *
 * Set 'newusername' to the desired account name.
 * Set 'become_user' to CREATE_USER_BECOME_USER if this is self-service account creation and we want to
 *                   actually log in as the user we just created, otherwise set it to CREATE_USER_DO_NOT_BECOME_USER
 * Set 'uid' to some uid_t value to associate the account with an external auth user, or (-1) for native auth
 */
int create_user(char *username, int become_user, uid_t uid)
{
	struct ctdluser usbuf;
	struct ctdlroom qrbuf;
	char mailboxname[ROOMNAMELEN];
	char buf[SIZ];
	int retval;

	strproc(username);
	if ((retval = internal_create_user(username, &usbuf, uid)) != 0) {
		return retval;
	}

	/*
	 * Give the user a private mailbox and a configuration room.
	 * Make the latter an invisible system room.
	 */
	CtdlMailboxName(mailboxname, sizeof mailboxname, &usbuf, MAILROOM);
	CtdlCreateRoom(mailboxname, 5, "", 0, 1, 1, VIEW_MAILBOX);

	CtdlMailboxName(mailboxname, sizeof mailboxname, &usbuf, USERCONFIGROOM);
	CtdlCreateRoom(mailboxname, 5, "", 0, 1, 1, VIEW_BBS);
	if (CtdlGetRoomLock(&qrbuf, mailboxname) == 0) {
		qrbuf.QRflags2 |= QR2_SYSTEM;
		CtdlPutRoomLock(&qrbuf);
	}

	/* Perform any create functions registered by server extensions */
	PerformUserHooks(&usbuf, EVT_NEWUSER);

	/* Everything below this line can be bypassed if administratively
	 * creating a user, instead of doing self-service account creation
	 */

	if (become_user == CREATE_USER_BECOME_USER) {
		/* Now become the user we just created */
		memcpy(&CC->user, &usbuf, sizeof(struct ctdluser));
		safestrncpy(CC->curr_user, username, sizeof CC->curr_user);
		do_login();
	
		/* Check to make sure we're still who we think we are */
		if (CtdlGetUser(&CC->user, CC->curr_user)) {
			return(ERROR + INTERNAL_ERROR);
		}
	}
	
	snprintf(buf, SIZ, 
		"New user account <%s> has been created, from host %s [%s].\n",
		username,
		CC->cs_host,
		CC->cs_addr
	);
	CtdlAideMessage(buf, "User Creation Notice");
	syslog(LOG_NOTICE, "user_ops: <%s> created", username);
	return(0);
}


/*
 * set password - back end api code
 */
void CtdlSetPassword(char *new_pw)
{
	CtdlGetUserLock(&CC->user, CC->curr_user);
	safestrncpy(CC->user.password, new_pw, sizeof(CC->user.password));
	CtdlPutUserLock(&CC->user);
	syslog(LOG_INFO, "user_ops: password changed for <%s>", CC->curr_user);
	PerformSessionHooks(EVT_SETPASS);
}


/*
 * API function for cmd_invt_kick() and anything else that needs to
 * invite or kick out a user to/from a room.
 * 
 * Set iuser to the name of the user, and op to 1=invite or 0=kick
 */
int CtdlInvtKick(char *iuser, int op) {
	struct ctdluser USscratch;
	visit vbuf;
	char bbb[SIZ];

	if (CtdlGetUser(&USscratch, iuser) != 0) {
		return(1);
	}

	CtdlGetRelationship(&vbuf, &USscratch, &CC->room);
	if (op == 1) {
		vbuf.v_flags = vbuf.v_flags & ~V_FORGET & ~V_LOCKOUT;
		vbuf.v_flags = vbuf.v_flags | V_ACCESS;
	}
	if (op == 0) {
		vbuf.v_flags = vbuf.v_flags & ~V_ACCESS;
		vbuf.v_flags = vbuf.v_flags | V_FORGET | V_LOCKOUT;
	}
	CtdlSetRelationship(&vbuf, &USscratch, &CC->room);

	/* post a message in Aide> saying what we just did */
	snprintf(bbb, sizeof bbb, "%s has been %s \"%s\" by %s.\n",
		iuser,
		((op == 1) ? "invited to" : "kicked out of"),
		CC->room.QRname,
		(CC->logged_in ? CC->user.fullname : "an administrator")
	);
	CtdlAideMessage(bbb,"User Admin Message");
	return(0);
}


/*
 * Forget (Zap) the current room (API call)
 * Returns 0 on success
 */
int CtdlForgetThisRoom(void) {
	visit vbuf;

	/* On some systems, Admins are not allowed to forget rooms */
	if (is_aide() && (CtdlGetConfigInt("c_aide_zap") == 0)
	   && ((CC->room.QRflags & QR_MAILBOX) == 0)  ) {
		return(1);
	}

	CtdlGetUserLock(&CC->user, CC->curr_user);
	CtdlGetRelationship(&vbuf, &CC->user, &CC->room);

	vbuf.v_flags = vbuf.v_flags | V_FORGET;
	vbuf.v_flags = vbuf.v_flags & ~V_ACCESS;

	CtdlSetRelationship(&vbuf, &CC->user, &CC->room);
	CtdlPutUserLock(&CC->user);

	/* Return to the Lobby, so we don't end up in an undefined room */
	CtdlUserGoto(CtdlGetConfigStr("c_baseroom"), 0, 0, NULL, NULL, NULL, NULL);
	return(0);
}


/* 
 * Traverse the user file and perform a callback for each user record.
 * (New improved version that runs in two phases so that callbacks can perform writes without having a r/o cursor open)
 */
void ForEachUser(void (*CallBack) (char *, void *out_data), void *in_data)
{
	struct cdbdata *cdbus;
	struct ctdluser *usptr;

	struct feu {
		struct feu *next;
		char username[USERNAME_SIZE];
	};
	struct feu *ufirst = NULL;
	struct feu *ulast = NULL;
	struct feu *f = NULL;

	cdb_rewind(CDB_USERS);

	// Phase 1 : build a linked list of all our user account names
	while (cdbus = cdb_next_item(CDB_USERS), cdbus != NULL) {
		usptr = (struct ctdluser *) cdbus->ptr;

		if (strlen(usptr->fullname) > 0) {
			f = malloc(sizeof(struct feu));
			f->next = NULL;
			strncpy(f->username, usptr->fullname, USERNAME_SIZE);

			if (ufirst == NULL) {
				ufirst = f;
				ulast = f;
			}
			else {
				ulast->next = f;
				ulast = f;
			}
		}
	}

	// Phase 2 : perform the callback for each user while de-allocating the list
	while (ufirst != NULL) {
		(*CallBack) (ufirst->username, in_data);
		f = ufirst;
		ufirst = ufirst->next;
		free(f);
	}
}


/*
 * Count the number of new mail messages the user has
 */
int NewMailCount()
{
	int num_newmsgs = 0;
	num_newmsgs = CC->newmail;
	CC->newmail = 0;
	return(num_newmsgs);
}


/*
 * Count the number of new mail messages the user has
 */
int InitialMailCheck()
{
	int num_newmsgs = 0;
	int a;
	char mailboxname[ROOMNAMELEN];
	struct ctdlroom mailbox;
	visit vbuf;
	struct cdbdata *cdbfr;
	long *msglist = NULL;
	int num_msgs = 0;

	CtdlMailboxName(mailboxname, sizeof mailboxname, &CC->user, MAILROOM);
	if (CtdlGetRoom(&mailbox, mailboxname) != 0)
		return(0);
	CtdlGetRelationship(&vbuf, &CC->user, &mailbox);

	cdbfr = cdb_fetch(CDB_MSGLISTS, &mailbox.QRnumber, sizeof(long));

	if (cdbfr != NULL) {
		msglist = malloc(cdbfr->len);
		memcpy(msglist, cdbfr->ptr, cdbfr->len);
		num_msgs = cdbfr->len / sizeof(long);
		cdb_free(cdbfr);
	}
	if (num_msgs > 0)
		for (a = 0; a < num_msgs; ++a) {
			if (msglist[a] > 0L) {
				if (msglist[a] > vbuf.v_lastseen) {
					++num_newmsgs;
				}
			}
		}
	if (msglist != NULL)
		free(msglist);

	return(num_newmsgs);
}
