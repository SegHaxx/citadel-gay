/*
 * These functions implement the portions of AUTHMODE_LDAP and AUTHMODE_LDAP_AD which
 * actually speak to the LDAP server.
 *
 * Copyright (c) 2011-2017 by the citadel.org development team.
 *
 * This program is open source software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

int ctdl_require_ldap_version = 3;

#define _GNU_SOURCE		// Needed to suppress warning about vasprintf() when running on Linux/Linux
#include <stdio.h>
#include <libcitadel.h>
#include "citserver.h"
#include "citadel_ldap.h"
#include "ctdl_module.h"
#include "user_ops.h"
#include "internet_addressing.h"
#include "config.h"

#ifdef HAVE_LDAP
#define LDAP_DEPRECATED 1 	// Suppress libldap's warning that we are using deprecated API calls
#include <ldap.h>


/*
 * Utility function, supply a search result and get back the fullname (display name, common name, etc) from the first result
 */
void derive_fullname_from_ldap_result(char *fullname, int fullname_size, LDAP *ldserver, LDAPMessage *search_result)
{
	char **values;

	if (fullname == NULL) return;

	if (CtdlGetConfigInt("c_auth_mode") == AUTHMODE_LDAP_AD) {
		values = ldap_get_values(ldserver, search_result, "displayName");
		if (values) {
			if (values[0]) {
				if (fullname) safestrncpy(fullname, values[0], fullname_size);
				syslog(LOG_DEBUG, "ldap: displayName = %s", values[0]);
			}
			ldap_value_free(values);
		}
	}
	else {
		values = ldap_get_values(ldserver, search_result, "cn");
		if (values) {
			if (values[0]) {
				if (fullname) safestrncpy(fullname, values[0], fullname_size);
				syslog(LOG_DEBUG, "ldap: cn = %s", values[0]);
			}
			ldap_value_free(values);
		}
	}
}


/*
 * Utility function, supply a search result and get back the uid from the first result
 */
uid_t derive_uid_from_ldap(LDAP *ldserver, LDAPMessage *entry)
{
	char **values;
	uid_t uid = (-1);

	if (CtdlGetConfigInt("c_auth_mode") == AUTHMODE_LDAP_AD) {
		values = ldap_get_values(ldserver, entry, "objectGUID");	// AD schema: uid hashed from objectGUID
		if (values) {
			if (values[0]) {
				uid = abs(HashLittle(values[0], strlen(values[0])));
			}
			ldap_value_free(values);
		}
	}
	else {
		values = ldap_get_values(ldserver, entry, "uidNumber");		// POSIX schema: uid = uidNumber
		if (values) {
			if (values[0]) {
				uid = atoi(values[0]);
			}
			ldap_value_free(values);
		}
	}

	return(uid);
}


/*
 * Wrapper function for ldap_initialize() that consistently fills in the correct fields
 */
int ctdl_ldap_initialize(LDAP **ld) {

	char server_url[256];
	int ret;

	snprintf(server_url, sizeof server_url, "ldap://%s:%d", CtdlGetConfigStr("c_ldap_host"), CtdlGetConfigInt("c_ldap_port"));
	ret = ldap_initialize(ld, server_url);
	if (ret != LDAP_SUCCESS) {
		syslog(LOG_ERR, "ldap: could not connect to %s : %m", server_url);
		*ld = NULL;
		return(errno);
	}

	return(ret);
}


/*
 * Bind to the LDAP server and return a working handle
 */
LDAP *ctdl_ldap_bind(void) {
	LDAP *ldserver = NULL;
	int i;

	if (ctdl_ldap_initialize(&ldserver) != LDAP_SUCCESS) {
		return(NULL);
	}

	ldap_set_option(ldserver, LDAP_OPT_PROTOCOL_VERSION, &ctdl_require_ldap_version);
	ldap_set_option(ldserver, LDAP_OPT_REFERRALS, (void *)LDAP_OPT_OFF);

	striplt(CtdlGetConfigStr("c_ldap_bind_dn"));
	striplt(CtdlGetConfigStr("c_ldap_bind_pw"));
	i = ldap_simple_bind_s(ldserver,
		(!IsEmptyStr(CtdlGetConfigStr("c_ldap_bind_dn")) ? CtdlGetConfigStr("c_ldap_bind_dn") : NULL),
		(!IsEmptyStr(CtdlGetConfigStr("c_ldap_bind_pw")) ? CtdlGetConfigStr("c_ldap_bind_pw") : NULL)
	);
	if (i != LDAP_SUCCESS) {
		syslog(LOG_ERR, "ldap: Cannot bind: %s (%d)", ldap_err2string(i), i);
		return(NULL);
	}

	return(ldserver);
}


/*
 * Look up a user in the directory to see if this is an account that can be authenticated
 */
int CtdlTryUserLDAP(char *username,
		char *found_dn, int found_dn_size,
		char *fullname, int fullname_size,
		uid_t *uid)
{
	LDAP *ldserver = NULL;
	LDAPMessage *search_result = NULL;
	LDAPMessage *entry = NULL;
	char searchstring[1024];
	struct timeval tv;
	char *user_dn = NULL;

	ldserver = ctdl_ldap_bind();
	if (!ldserver) return(-1);

	if (fullname) safestrncpy(fullname, username, fullname_size);
	tv.tv_sec = 10;
	tv.tv_usec = 0;

	if (CtdlGetConfigInt("c_auth_mode") == AUTHMODE_LDAP_AD) {
		snprintf(searchstring, sizeof(searchstring), "(sAMAccountName=%s)", username);
	}
	else {
		snprintf(searchstring, sizeof(searchstring), "(&(objectclass=posixAccount)(cn=%s))", username);
		// snprintf(searchstring, sizeof(searchstring), "(&(objectclass=posixAccount)(uid=%s))", username);
	}

	syslog(LOG_DEBUG, "ldap: search: %s", searchstring);
	(void) ldap_search_ext_s(
		ldserver,					/* ld				*/
		CtdlGetConfigStr("c_ldap_base_dn"),		/* base				*/
		LDAP_SCOPE_SUBTREE,				/* scope			*/
		searchstring,					/* filter			*/
		NULL,						/* attrs (all attributes)	*/
		0,						/* attrsonly (attrs + values)	*/
		NULL,						/* serverctrls (none)		*/
		NULL,						/* clientctrls (none)		*/
		&tv,						/* timeout			*/
		1,						/* sizelimit (1 result max)	*/
		&search_result					/* res				*/
	);

	/* Ignore the return value of ldap_search_ext_s().  Sometimes it returns an error even when
	 * the search succeeds.  Instead, we check to see whether search_result is still NULL.
	 */
	if (search_result == NULL) {
		syslog(LOG_DEBUG, "ldap: zero search results were returned");
		ldap_unbind(ldserver);
		return(2);
	}

	/* At this point we've got at least one result from our query.  If there are multiple
	 * results, we still only look at the first one.
	 */
	entry = ldap_first_entry(ldserver, search_result);
	if (entry) {

		user_dn = ldap_get_dn(ldserver, entry);
		if (user_dn) {
			syslog(LOG_DEBUG, "ldap: dn = %s", user_dn);
		}

		derive_fullname_from_ldap_result(fullname, fullname_size, ldserver, search_result);
		*uid = derive_uid_from_ldap(ldserver, search_result);
	}

	/* free the results */
	ldap_msgfree(search_result);

	/* unbind so we can go back in as the authenticating user */
	ldap_unbind(ldserver);

	if (!user_dn) {
		syslog(LOG_DEBUG, "ldap: No such user was found.");
		return(4);
	}

	if (found_dn) safestrncpy(found_dn, user_dn, found_dn_size);
	ldap_memfree(user_dn);
	return(0);
}


int CtdlTryPasswordLDAP(char *user_dn, const char *password)
{
	LDAP *ldserver = NULL;
	int i = (-1);

	if (IsEmptyStr(password)) {
		syslog(LOG_DEBUG, "ldap: empty passwords are not permitted");
		return(1);
	}

	syslog(LOG_DEBUG, "ldap: trying to bind as %s", user_dn);
	i = ctdl_ldap_initialize(&ldserver);
	if (i == LDAP_SUCCESS) {
		ldap_set_option(ldserver, LDAP_OPT_PROTOCOL_VERSION, &ctdl_require_ldap_version);
		i = ldap_simple_bind_s(ldserver, user_dn, password);
		if (i == LDAP_SUCCESS) {
			syslog(LOG_DEBUG, "ldap: bind succeeded");
		}
		else {
			syslog(LOG_DEBUG, "ldap: Cannot bind: %s (%d)", ldap_err2string(i), i);
		}
		ldap_set_option(ldserver, LDAP_OPT_REFERRALS, (void *)LDAP_OPT_OFF);
		ldap_unbind(ldserver);
	}

	if (i == LDAP_SUCCESS) {
		return(0);
	}

	return(1);
}


//return !0 iff property changed.
int vcard_set_props_iff_different(struct vCard *v,char *propname,int numvals, char **vals) {
	int i;
	char *oldval = "";
	for(i=0;i<numvals;i++) {
	  oldval = vcard_get_prop(v,propname,0,i,0);
	  if (oldval == NULL) break;
	  if (strcmp(vals[i],oldval)) break;
	}
	if (i!=numvals) {
		syslog(LOG_DEBUG, "ldap: vcard property %s, element %d of %d changed from %s to %s\n", propname, i, numvals, oldval, vals[i]);
		for(i=0;i<numvals;i++) vcard_set_prop(v,propname,vals[i],(i==0) ? 0 : 1);
		return 1;
	}
	return 0;
}


//return !0 iff property changed.
int vcard_set_one_prop_iff_different(struct vCard *v,char *propname, char *newfmt, ...) {
	va_list args;
	char *newvalue;
	int changed_something;
	va_start(args,newfmt);
	if (-1==vasprintf(&newvalue,newfmt,args)) {
		syslog(LOG_ERR, "ldap: out of memory!");
		return 0;
	}
	changed_something = vcard_set_props_iff_different(v,propname,1,&newvalue);
	va_end(args);
	free(newvalue);
	return changed_something;
}


/*
 * Learn LDAP attributes and stuff them into the vCard.
 * Returns nonzero if we changed anything.
 */
int Ctdl_LDAP_to_vCard(char *ldap_dn, struct vCard *v)
{
	int changed_something = 0;
	LDAP *ldserver = NULL;
	struct timeval tv;
	LDAPMessage *search_result = NULL;
	LDAPMessage *entry = NULL;
	char **givenName;
	char **sn;
	char **cn;
	char **initials;
	char **o;
	char **street;
	char **l;
	char **st;
	char **postalCode;
	char **telephoneNumber;
	char **mobile;
	char **homePhone;
	char **facsimileTelephoneNumber;
	char **mail;
	char **uid;
	char **homeDirectory;
	char **uidNumber;
	char **loginShell;
	char **gidNumber;
	char **c;
	char **title;
	char **uuid;
	char *attrs[] = { "*","+",NULL};

	if (!ldap_dn) return(0);
	if (!v) return(0);

	ldserver = ctdl_ldap_bind();
	if (!ldserver) return(-1);

	tv.tv_sec = 10;
	tv.tv_usec = 0;

	syslog(LOG_DEBUG, "ldap: search: %s", ldap_dn);
	(void) ldap_search_ext_s(
		ldserver,				// ld
		ldap_dn,				// base
		LDAP_SCOPE_SUBTREE,			// scope
		NULL,					// filter
		attrs,					// attrs (all attributes)
		0,					// attrsonly (attrs + values)
		NULL,					// serverctrls (none)
		NULL,					// clientctrls (none)
		&tv,					// timeout
		1,					// sizelimit (1 result max)
		&search_result				// res
	);
	
	/* Ignore the return value of ldap_search_ext_s().  Sometimes it returns an error even when
	 * the search succeeds.  Instead, we check to see whether search_result is still NULL.
	 */
	if (search_result == NULL) {
		syslog(LOG_DEBUG, "ldap: zero search results were returned");
		ldap_unbind(ldserver);
		return(0);
	}

	/* At this point we've got at least one result from our query.  If there are multiple
	 * results, we still only look at the first one.
	 */
	entry = ldap_first_entry(ldserver, search_result);
	if (entry) {
		syslog(LOG_DEBUG, "ldap: search got user details for vcard.");
		givenName=ldap_get_values(ldserver, search_result, "givenName");
		sn=ldap_get_values(ldserver, search_result, "sn");
		cn=ldap_get_values(ldserver, search_result, "cn");
		initials=ldap_get_values(ldserver, search_result, "initials");
		title=ldap_get_values(ldserver, search_result, "title");
		o=ldap_get_values(ldserver, search_result, "o");
		street=ldap_get_values(ldserver, search_result, "street");
		l=ldap_get_values(ldserver, search_result, "l");
		st=ldap_get_values(ldserver, search_result, "st");
		postalCode=ldap_get_values(ldserver, search_result, "postalCode");
		telephoneNumber=ldap_get_values(ldserver, search_result, "telephoneNumber");
		mobile=ldap_get_values(ldserver, search_result, "mobile");
		homePhone=ldap_get_values(ldserver, search_result, "homePhone");
		facsimileTelephoneNumber=ldap_get_values(ldserver, search_result, "facsimileTelephoneNumber");
		mail=ldap_get_values(ldserver, search_result, "mail");
		uid=ldap_get_values(ldserver, search_result, "uid");
		homeDirectory=ldap_get_values(ldserver, search_result, "homeDirectory");
		uidNumber=ldap_get_values(ldserver, search_result, "uidNumber");
		loginShell=ldap_get_values(ldserver, search_result, "loginShell");
		gidNumber=ldap_get_values(ldserver, search_result, "gidNumber");
		c=ldap_get_values(ldserver, search_result, "c");
		uuid=ldap_get_values(ldserver, search_result, "entryUUID");

		if (street && l && st && postalCode && c) changed_something |= vcard_set_one_prop_iff_different(v,"adr",";;%s;%s;%s;%s;%s",street[0],l[0],st[0],postalCode[0],c[0]);
		if (telephoneNumber) changed_something |= vcard_set_one_prop_iff_different(v,"tel;work","%s",telephoneNumber[0]);
		if (facsimileTelephoneNumber) changed_something |= vcard_set_one_prop_iff_different(v,"tel;fax","%s",facsimileTelephoneNumber[0]);
		if (mobile) changed_something |= vcard_set_one_prop_iff_different(v,"tel;cell","%s",mobile[0]);
		if (homePhone) changed_something |= vcard_set_one_prop_iff_different(v,"tel;home","%s",homePhone[0]);
		if (givenName && sn) {
			if (initials) {
				changed_something |= vcard_set_one_prop_iff_different(v,"n","%s;%s;%s",sn[0],givenName[0],initials[0]);
			}
			else {
				changed_something |= vcard_set_one_prop_iff_different(v,"n","%s;%s",sn[0],givenName[0]);
			}
		}
		if (mail) {
			changed_something |= vcard_set_props_iff_different(v,"email;internet",ldap_count_values(mail),mail);
		}
		if (uuid) changed_something |= vcard_set_one_prop_iff_different(v,"X-uuid","%s",uuid[0]);
		if (o) changed_something |= vcard_set_one_prop_iff_different(v,"org","%s",o[0]);
		if (cn) changed_something |= vcard_set_one_prop_iff_different(v,"fn","%s",cn[0]);
		if (title) changed_something |= vcard_set_one_prop_iff_different(v,"title","%s",title[0]);
		
		if (givenName) ldap_value_free(givenName);
		if (initials) ldap_value_free(initials);
		if (sn) ldap_value_free(sn);
		if (cn) ldap_value_free(cn);
		if (o) ldap_value_free(o);
		if (street) ldap_value_free(street);
		if (l) ldap_value_free(l);
		if (st) ldap_value_free(st);
		if (postalCode) ldap_value_free(postalCode);
		if (telephoneNumber) ldap_value_free(telephoneNumber);
		if (mobile) ldap_value_free(mobile);
		if (homePhone) ldap_value_free(homePhone);
		if (facsimileTelephoneNumber) ldap_value_free(facsimileTelephoneNumber);
		if (mail) ldap_value_free(mail);
		if (uid) ldap_value_free(uid);
		if (homeDirectory) ldap_value_free(homeDirectory);
		if (uidNumber) ldap_value_free(uidNumber);
		if (loginShell) ldap_value_free(loginShell);
		if (gidNumber) ldap_value_free(gidNumber);
		if (c) ldap_value_free(c);
		if (title) ldap_value_free(title);
		if (uuid) ldap_value_free(uuid);
	}
	/* free the results */
	ldap_msgfree(search_result);

	/* unbind so we can go back in as the authenticating user */
	ldap_unbind(ldserver);
	return(changed_something);	/* tell the caller whether we made any changes */
}


/*
 * Extract a user's Internet email addresses from LDAP.
 * Returns zero if we got a valid set of addresses; nonzero for error.
 */
int extract_email_addresses_from_ldap(char *ldap_dn, char *emailaddrs)
{
	LDAP *ldserver = NULL;
	struct timeval tv;
	LDAPMessage *search_result = NULL;
	LDAPMessage *entry = NULL;
	char **mail;
	char *attrs[] = { "*","+",NULL};

	if (!ldap_dn) return(1);
	if (!emailaddrs) return(1);

	ldserver = ctdl_ldap_bind();
	if (!ldserver) return(-1);

	tv.tv_sec = 10;
	tv.tv_usec = 0;

	syslog(LOG_DEBUG, "ldap: search: %s", ldap_dn);
	(void) ldap_search_ext_s(
		ldserver,				// ld
		ldap_dn,				// base
		LDAP_SCOPE_SUBTREE,			// scope
		NULL,					// filter
		attrs,					// attrs (all attributes)
		0,					// attrsonly (attrs + values)
		NULL,					// serverctrls (none)
		NULL,					// clientctrls (none)
		&tv,					// timeout
		1,					// sizelimit (1 result max)
		&search_result				// res
	);
	
	/* Ignore the return value of ldap_search_ext_s().  Sometimes it returns an error even when
	 * the search succeeds.  Instead, we check to see whether search_result is still NULL.
	 */
	if (search_result == NULL) {
		syslog(LOG_DEBUG, "ldap: zero search results were returned");
		ldap_unbind(ldserver);
		return(4);
	}

	/* At this point we've got at least one result from our query.  If there are multiple
	 * results, we still only look at the first one.
	 */
	emailaddrs[0] = 0;	/* clear out any previous results */
	entry = ldap_first_entry(ldserver, search_result);
	if (entry) {
		syslog(LOG_DEBUG, "ldap: search got user details");
		mail = ldap_get_values(ldserver, search_result, "mail");

		if (mail) {
			int q;
			for (q=0; q<ldap_count_values(mail); ++q) {
				if (IsDirectory(mail[q], 0)) {
					if ((strlen(emailaddrs) + strlen(mail[q]) + 2) > 512) {
						syslog(LOG_ERR, "ldap: can't fit all email addresses into user record");
					}
					else {
						if (!IsEmptyStr(emailaddrs)) {
							strcat(emailaddrs, "|");
						}
						strcat(emailaddrs, mail[q]);
					}
				}
			}
		}
	}

	/* free the results */
	ldap_msgfree(search_result);

	/* unbind so we can go back in as the authenticating user */
	ldap_unbind(ldserver);
	return(0);
}


/*
 * Scan LDAP for users and populate Citadel's user database with everyone
 */
void CtdlSynchronizeUsersFromLDAP(void)
{
	LDAP *ldserver = NULL;
	LDAPMessage *search_result = NULL;
	LDAPMessage *entry = NULL;
	char *user_dn = NULL;
	char searchstring[1024];
	struct timeval tv;

	if ((CtdlGetConfigInt("c_auth_mode") != AUTHMODE_LDAP) && (CtdlGetConfigInt("c_auth_mode") != AUTHMODE_LDAP_AD)) {
		return;		// not running LDAP
	}

	syslog(LOG_INFO, "ldap: synchronizing Citadel user database from LDAP");

	ldserver = ctdl_ldap_bind();
	if (!ldserver) return;

	tv.tv_sec = 10;
	tv.tv_usec = 0;

	if (CtdlGetConfigInt("c_auth_mode") == AUTHMODE_LDAP_AD) {
			snprintf(searchstring, sizeof(searchstring), "(&(objectClass=user)(objectClass=person)(!(objectClass=computer)))");
	} else {
			snprintf(searchstring, sizeof(searchstring), "(objectClass=inetOrgPerson)");
	}

	syslog(LOG_DEBUG, "ldap: search: %s", searchstring);
	(void) ldap_search_ext_s(
		ldserver,					// ld
		CtdlGetConfigStr("c_ldap_base_dn"),		// base
		LDAP_SCOPE_SUBTREE,				// scope
		searchstring,					// filter
		NULL,						// attrs (all attributes)
		0,						// attrsonly (attrs + values)
		NULL,						// serverctrls (none)
		NULL,						// clientctrls (none)
		&tv,						// timeout
		INT_MAX,					// sizelimit (max)
		&search_result					// result
	);

	/* Ignore the return value of ldap_search_ext_s().  Sometimes it returns an error even when
	 * the search succeeds.  Instead, we check to see whether search_result is still NULL.
	 */
	if (search_result == NULL) {
		syslog(LOG_DEBUG, "ldap: zero search results were returned");
		ldap_unbind(ldserver);
		return;
	}

	syslog(LOG_DEBUG, "ldap: %d entries returned", ldap_count_entries(ldserver, search_result));
	entry = ldap_first_entry(ldserver, search_result);
	while (entry) {

		user_dn = ldap_get_dn(ldserver, entry);
		if (user_dn) {
			syslog(LOG_DEBUG, "ldap: found %s", user_dn);

			int fullname_size = 256;
			char fullname[256] = { 0 } ;
			uid_t uid = (-1);
			char new_emailaddrs[512] = { 0 } ;

			derive_fullname_from_ldap_result(fullname, fullname_size, ldserver, entry);
			uid = derive_uid_from_ldap(ldserver, entry);
			syslog(LOG_DEBUG, "ldap: display name: <%s> , uid = <%d>", fullname, uid);

			// now create or update the user
			int found_user;
			struct ctdluser usbuf;

			found_user = getuserbyuid(&usbuf, uid);
			if (found_user != 0) {
				create_user(fullname, CREATE_USER_DO_NOT_BECOME_USER, uid);
				found_user = getuserbyuid(&usbuf, uid);
				strcpy(fullname, usbuf.fullname);
			}

			if (found_user == 0) {		// user record exists
				// now update the account email addresses if necessary
				if (CtdlGetConfigInt("c_ldap_sync_email_addrs") > 0) {
					if (extract_email_addresses_from_ldap(user_dn, new_emailaddrs) == 0) {
						if (strcmp(usbuf.emailaddrs, new_emailaddrs)) {				// update only if changed
							CtdlSetEmailAddressesForUser(usbuf.fullname, new_emailaddrs);
						}
					}
				}
			}
			ldap_memfree(user_dn);
		}

		entry = ldap_next_entry(ldserver, entry);
	}

	/* free the results */
	ldap_msgfree(search_result);

	/* unbind so we can go back in as the authenticating user */
	ldap_unbind(ldserver);
}

#endif /* HAVE_LDAP */
