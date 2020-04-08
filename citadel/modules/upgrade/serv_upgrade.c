/*
 * Transparently handle the upgrading of server data formats.  If we see
 * an existing version number of our database, we can make some intelligent
 * guesses about what kind of data format changes need to be applied, and
 * we apply them transparently.
 *
 * Copyright (c) 1987-2020 by the citadel.org team
 *
 * This program is open source software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include "sysdep.h"
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <signal.h>
#include <pwd.h>
#include <errno.h>
#include <sys/types.h>

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

#include <sys/wait.h>
#include <string.h>
#include <limits.h>
#include <libcitadel.h>
#include "citadel.h"
#include "server.h"
#include "citserver.h"
#include "support.h"
#include "config.h"
#include "control.h"
#include "database.h"
#include "user_ops.h"
#include "msgbase.h"
#include "serv_upgrade.h"
#include "euidindex.h"
#include "ctdl_module.h"
#include "serv_vcard.h"
#include "internet_addressing.h"

/*
 * oldver is the version number of Citadel Server which was active on the previous run of the program, learned from the system configuration.
 * If we are running a new Citadel Server for the first time, oldver will be 0.
 * We keep this value around for the entire duration of the program run because we'll need it during several stages of startup.
 */
int oldver = 0;

/*
 * Try to remove any extra users with number 0
 */
void fix_sys_user_name(void)
{
	struct ctdluser usbuf;
	char usernamekey[USERNAME_SIZE];

	while (CtdlGetUserByNumber(&usbuf, 0) == 0) {
		/* delete user with number 0 and no name */
		if (IsEmptyStr(usbuf.fullname)) {
			cdb_delete(CDB_USERS, "", 0);
		}
		else {
			/* temporarily set this user to -1 */
			usbuf.usernum = -1;
			CtdlPutUser(&usbuf);
		}
	}

	/* Delete any "user 0" accounts */
	while (CtdlGetUserByNumber(&usbuf, -1) == 0) {
		makeuserkey(usernamekey, usbuf.fullname);
		cdb_delete(CDB_USERS, usernamekey, strlen(usernamekey));
	}
}


/* 
 * Back end processing function for reindex_uids()
 */
void reindex_uids_backend(char *username, void *data) {

	struct ctdluser us;

	if (CtdlGetUserLock(&us, username) == 0) {
		syslog(LOG_DEBUG, "Processing <%s> (%d)", us.fullname, us.uid);
		if (us.uid == CTDLUID) {
			us.uid = NATIVE_AUTH_UID;
		}
		CtdlPutUserLock(&us);
		if ((us.uid > 0) && (us.uid != NATIVE_AUTH_UID)) {		// if non-native auth , index by uid
			StrBuf *claimed_id = NewStrBuf();
			StrBufPrintf(claimed_id, "uid:%d", us.uid);
			attach_extauth(&us, claimed_id);
			FreeStrBuf(&claimed_id);
		}
	}
}


/*
 * Build extauth index of all users with uid-based join (system auth, LDAP auth)
 * Also changes all users with a uid of CTDLUID to NATIVE_AUTH_UID (-1)
 */
void reindex_uids(void) {
	syslog(LOG_WARNING, "upgrade: reindexing and applying uid changes");
	ForEachUser(reindex_uids_backend, NULL);
	return;
}


/*
 * These accounts may have been created by code that ran between mid 2008 and early 2011.
 * If present they are no longer in use and may be deleted.
 */
void remove_thread_users(void) {
	char *deleteusers[] = {
		"SYS_checkpoint",
		"SYS_extnotify",
		"SYS_IGnet Queue",
		"SYS_indexer",
		"SYS_network",
		"SYS_popclient",
		"SYS_purger",
		"SYS_rssclient",
		"SYS_select_on_master",
		"SYS_SMTP Send"
	};

	int i;
	struct ctdluser usbuf;
	for (i=0; i<(sizeof(deleteusers)/sizeof(char *)); ++i) {
		if (CtdlGetUser(&usbuf, deleteusers[i]) == 0) {
			usbuf.axlevel = 0;
			strcpy(usbuf.password, "deleteme");
			CtdlPutUser(&usbuf);
			syslog(LOG_INFO,
				"System user account <%s> is no longer in use and will be deleted.",
				deleteusers[i]
			);
		}
	}
}


/*
 * Attempt to guess the name of the time zone currently in use
 * on the underlying host system.
 */
void guess_time_zone(void) {
	FILE *fp;
	char buf[PATH_MAX];

	fp = popen(file_guesstimezone, "r");
	if (fp) {
		if (fgets(buf, sizeof buf, fp) && (strlen(buf) > 2)) {
			buf[strlen(buf)-1] = 0;
			CtdlSetConfigStr("c_default_cal_zone", buf);
			syslog(LOG_INFO, "Configuring timezone: %s", buf);
		}
		fclose(fp);
	}
}


/*
 * Per-room callback function for ingest_old_roominfo_and_roompic_files()
 *
 * This is the second pass, where we process the list of rooms with info or pic files.
 */
void iorarf_oneroom(char *roomname, char *infofile, char *picfile)
{
	FILE *fp;
	long data_length;
	char *unencoded_data;
	char *encoded_data;
	long info_msgnum = 0;
	long pic_msgnum = 0;
	char subject[SIZ];

	// Test for the presence of a legacy "room info file"
	if (!IsEmptyStr(infofile)) {
		fp = fopen(infofile, "r");
	}
	else {
		fp = NULL;
	}
	if (fp) {
		fseek(fp, 0, SEEK_END);
		data_length = ftell(fp);

		if (data_length >= 1) {
			rewind(fp);
			unencoded_data = malloc(data_length);
			if (unencoded_data) {
				fread(unencoded_data, data_length, 1, fp);
				encoded_data = malloc((data_length * 2) + 100);
				if (encoded_data) {
					sprintf(encoded_data, "Content-type: text/plain\nContent-transfer-encoding: base64\n\n");
					CtdlEncodeBase64(&encoded_data[strlen(encoded_data)], unencoded_data, data_length, 1);
					snprintf(subject, sizeof subject, "Imported room banner for %s", roomname);
					info_msgnum = quickie_message("Citadel", NULL, NULL, SYSCONFIGROOM, encoded_data, FMT_RFC822, subject);
					free(encoded_data);
				}
				free(unencoded_data);
			}
		}
		fclose(fp);
		if (info_msgnum > 0) unlink(infofile);
	}

	// Test for the presence of a legacy "room picture file" and import it.
	if (!IsEmptyStr(picfile)) {
		fp = fopen(picfile, "r");
	}
	else {
		fp = NULL;
	}
	if (fp) {
		fseek(fp, 0, SEEK_END);
		data_length = ftell(fp);

		if (data_length >= 1) {
			rewind(fp);
			unencoded_data = malloc(data_length);
			if (unencoded_data) {
				fread(unencoded_data, data_length, 1, fp);
				encoded_data = malloc((data_length * 2) + 100);
				if (encoded_data) {
					sprintf(encoded_data, "Content-type: image/gif\nContent-transfer-encoding: base64\n\n");
					CtdlEncodeBase64(&encoded_data[strlen(encoded_data)], unencoded_data, data_length, 1);
					snprintf(subject, sizeof subject, "Imported room icon for %s", roomname);
					pic_msgnum = quickie_message("Citadel", NULL, NULL, SYSCONFIGROOM, encoded_data, FMT_RFC822, subject);
					free(encoded_data);
				}
				free(unencoded_data);
			}
		}
		fclose(fp);
		if (pic_msgnum > 0) unlink(picfile);
	}

	// Now we have the message numbers of our new banner and icon.  Record them in the room record.
	// NOTE: we are not deleting the old msgnum_info because that position in the record was previously
	// a pointer to the highest message number which existed in the room when the info file was saved,
	// and we don't want to delete messages that are not *actually* old banners.
	struct ctdlroom qrbuf;
	if (CtdlGetRoomLock(&qrbuf, roomname) == 0) {
		qrbuf.msgnum_info = info_msgnum;
		qrbuf.msgnum_pic = pic_msgnum;
		CtdlPutRoomLock(&qrbuf);
	}

}


struct iorarf_list {
	struct iorarf_list *next;
	char name[ROOMNAMELEN];
	char info[PATH_MAX];
	char pic[PATH_MAX];
};


/*
 * Per-room callback function for ingest_old_roominfo_and_roompic_files()
 *
 * This is the first pass, where the list of qualifying rooms is gathered.
 */
void iorarf_backend(struct ctdlroom *qrbuf, void *data)
{
	FILE *fp;
	struct iorarf_list **iorarf_list = (struct iorarf_list **)data;

	struct iorarf_list *i = malloc(sizeof(struct iorarf_list));
	i->next = *iorarf_list;
	strcpy(i->name, qrbuf->QRname);
	strcpy(i->info, "");
	strcpy(i->pic, "");

	// Test for the presence of a legacy "room info file"
	assoc_file_name(i->info, sizeof i->info, qrbuf, ctdl_info_dir);
	fp = fopen(i->info, "r");
	if (fp) {
		fclose(fp);
	}
	else {
		i->info[0] = 0;
	}

	// Test for the presence of a legacy "room picture file"
	assoc_file_name(i->pic, sizeof i->pic, qrbuf, ctdl_image_dir);
	fp = fopen(i->pic, "r");
	if (fp) {
		fclose(fp);
	}
	else {
		i->pic[0] = 0;
	}

	if ( (!IsEmptyStr(i->info)) || (!IsEmptyStr(i->pic)) ) {
		*iorarf_list = i;
	}
	else {
		free(i);
	}
}


/*
 * Prior to Citadel Server version 902, room info and pictures (which comprise the
 * displayed banner for each room) were stored in the filesystem.  If we are upgrading
 * from version >000 to version >=902, ingest those files into the database.
 */
void ingest_old_roominfo_and_roompic_files(void)
{
	struct iorarf_list *il = NULL;

	CtdlForEachRoom(iorarf_backend, &il);

	struct iorarf_list *p;
	while (il) {
		iorarf_oneroom(il->name, il->info, il->pic);
		p = il->next;
		free(il);
		il = p;
	}

	unlink(ctdl_info_dir);
}


/*
 * For upgrades in which a new config setting appears for the first time, set default values.
 * For new installations (oldver == 0) also set default values.
 */
void update_config(void) {

	if (oldver < 606) {
		CtdlSetConfigInt("c_rfc822_strict_from", 0);
	}

	if (oldver < 609) {
		CtdlSetConfigInt("c_purge_hour", 3);
	}

	if (oldver < 615) {
		CtdlSetConfigInt("c_ldap_port", 389);
	}

	if (oldver < 623) {
		CtdlSetConfigStr("c_ip_addr", "*");
	}

	if (oldver < 650) {
		CtdlSetConfigInt("c_enable_fulltext", 1);
	}

	if (oldver < 652) {
		CtdlSetConfigInt("c_auto_cull", 1);
	}

	if (oldver < 725) {
		CtdlSetConfigInt("c_xmpp_c2s_port", 5222);
		CtdlSetConfigInt("c_xmpp_s2s_port", 5269);
	}

	if (oldver < 830) {
		CtdlSetConfigInt("c_nntp_port", 119);
		CtdlSetConfigInt("c_nntps_port", 563);
	}

	if (IsEmptyStr(CtdlGetConfigStr("c_default_cal_zone"))) {
		guess_time_zone();
	}
}


/*
 * Helper function for move_inet_addrs_from_vcards_to_user_records()
 *
 * Call this function as a ForEachUser backend in order to queue up
 * user names, or call it with a null user to make it do the processing.
 * This allows us to maintain the list as a static instead of passing
 * pointers around.
 */
void miafvtur_backend(char *username, void *data) {
	struct ctdluser usbuf;
	char primary_inet_email[512] = { 0 };
	char other_inet_emails[512] = { 0 };
	char combined_inet_emails[512] = { 0 };

	if (CtdlGetUser(&usbuf, username) != 0) {
		return;
	}

	struct vCard *v = vcard_get_user(&usbuf);
	if (!v) return;
	extract_inet_email_addrs(primary_inet_email, sizeof primary_inet_email, other_inet_emails, sizeof other_inet_emails, v, 1);
	vcard_free(v);
	
	if ( (IsEmptyStr(primary_inet_email)) && (IsEmptyStr(other_inet_emails)) ) {
		return;
	}

	snprintf(combined_inet_emails, 512, "%s%s%s",
		(!IsEmptyStr(primary_inet_email) ? primary_inet_email : ""),
		((!IsEmptyStr(primary_inet_email)&&(!IsEmptyStr(other_inet_emails))) ? "|" : ""),
		(!IsEmptyStr(other_inet_emails) ? other_inet_emails : "")
	);

	CtdlSetEmailAddressesForUser(usbuf.fullname, combined_inet_emails);
}


/*
 * If our system still has a "refcount_adjustments.dat" sitting around from an old version, ingest it now.
 */
int ProcessOldStyleAdjRefCountQueue(void)
{
	int r;
	FILE *fp;
	struct arcq arcq_rec;
	int num_records_processed = 0;

	fp = fopen(file_arcq, "rb");
	if (fp == NULL) {
		return(num_records_processed);
	}

	syslog(LOG_INFO, "msgbase: ingesting %s", file_arcq);

	while (fread(&arcq_rec, sizeof(struct arcq), 1, fp) == 1) {
		AdjRefCount(arcq_rec.arcq_msgnum, arcq_rec.arcq_delta);
		++num_records_processed;
	}

	fclose(fp);
	r = unlink(file_arcq);
	if (r != 0) {
		syslog(LOG_ERR, "%s: %m", file_arcq);
	}

	return(num_records_processed);
}


/*
 * Prior to version 912 we kept a user's various Internet email addresses in their vCards.
 * This function moves them over to the user record, which is where we keep them now.
 */
void move_inet_addrs_from_vcards_to_user_records(void)
{
	ForEachUser(miafvtur_backend, NULL);
	CtdlRebuildDirectoryIndex();
}


/*
 * Create a default administrator account so we can log in to a new installation
 */
void create_default_admin_account(void) {
	struct ctdluser usbuf;

	create_user(DEFAULT_ADMIN_USERNAME, CREATE_USER_DO_NOT_BECOME_USER, (-1));
	CtdlGetUser(&usbuf, DEFAULT_ADMIN_USERNAME);
	safestrncpy(usbuf.password, DEFAULT_ADMIN_PASSWORD, sizeof(usbuf.password));
	usbuf.axlevel = AxAideU;
	CtdlPutUser(&usbuf);
	CtdlSetConfigStr("c_sysadm", DEFAULT_ADMIN_USERNAME);
}


/*
 * Based on the server version number reported by the existing database,
 * run in-place data format upgrades until everything is up to date.
 */
void pre_startup_upgrades(void) {

	oldver = CtdlGetConfigInt("MM_hosted_upgrade_level");
	syslog(LOG_INFO, "Existing database version on disk is %d", oldver);
	update_config();

	if (oldver < REV_LEVEL) {
		syslog(LOG_WARNING, "Running pre-startup database upgrades.");
	}
	else {
		return;
	}

	if ((oldver > 000) && (oldver < 591)) {
		syslog(LOG_EMERG, "This database is too old to be upgraded.  Citadel server will exit.");
		exit(EXIT_FAILURE);
	}
	if ((oldver > 000) && (oldver < 913)) {
		reindex_uids();
	}
	if ((oldver > 000) && (oldver < 659)) {
		rebuild_euid_index();
	}
	if (oldver < 735) {
		fix_sys_user_name();
	}
	if (oldver < 736) {
		rebuild_usersbynumber();
	}
	if (oldver < 790) {
		remove_thread_users();
	}
	if (oldver < 810) {
		struct ctdlroom QRoom;
		if (!CtdlGetRoom(&QRoom, SMTP_SPOOLOUT_ROOM)) {
			QRoom.QRdefaultview = VIEW_QUEUE;
			CtdlPutRoom(&QRoom);
		}
	}

	if ((oldver > 000) && (oldver < 902)) {
		ingest_old_roominfo_and_roompic_files();
	}

	CtdlSetConfigInt("MM_hosted_upgrade_level", REV_LEVEL);

	/*
	 * Negative values for maxsessions are not allowed.
	 */
	if (CtdlGetConfigInt("c_maxsessions") < 0) {
		CtdlSetConfigInt("c_maxsessions", 0);
	}

	/* We need a system default message expiry policy, because this is
	 * the top level and there's no 'higher' policy to fall back on.
	 * By default, do not expire messages at all.
	 */
	if (CtdlGetConfigInt("c_ep_mode") == 0) {
		CtdlSetConfigInt("c_ep_mode", EXPIRE_MANUAL);
		CtdlSetConfigInt("c_ep_value", 0);
	}

	/*
	 * If this is the first run on an empty database, create a default administrator
	 */
	if (oldver == 0) {
		create_default_admin_account();
	}
}


/*
 * Based on the server version number reported by the existing database,
 * run in-place data format upgrades until everything is up to date.
 */
void post_startup_upgrades(void) {

	syslog(LOG_INFO, "Existing database version on disk is %d", oldver);

	if (oldver < REV_LEVEL) {
		syslog(LOG_WARNING, "Running post-startup database upgrades.");
	}
	else {
		return;
	}

	if ((oldver > 000) && (oldver < 912)) {
		move_inet_addrs_from_vcards_to_user_records();
	}

	if ((oldver > 000) && (oldver < 922)) {
		ProcessOldStyleAdjRefCountQueue();
	}
}


CTDL_MODULE_UPGRADE(upgrade)
{
	pre_startup_upgrades();
	
	/* return our module id for the Log */
	return "upgrade";
}


CTDL_MODULE_INIT(upgrade)
{
	if (!threading) {
		post_startup_upgrades();
	}

	/* return our module name for the log */
        return "upgrade";
}
