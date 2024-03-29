// This module handles the expiry of old messages and the purging of old users.
//
// You might also see this module affectionately referred to as TDAP (The Dreaded Auto-Purger).
//
// Copyright (c) 1988-2023 by citadel.org (Art Cancro, Wilifried Goesgens, and others)
//
// This program is open source software.  Use, duplication, or disclosure
// is subject to the terms of the GNU General Public License, version 3.


#include "../../sysdep.h"
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <signal.h>
#include <pwd.h>
#include <errno.h>
#include <sys/types.h>
#include <time.h>
#include <sys/wait.h>
#include <string.h>
#include <limits.h>
#include <libcitadel.h>
#include "../../citadel_defs.h"
#include "../../server.h"
#include "../../citserver.h"
#include "../../support.h"
#include "../../config.h"
#include "policy.h"
#include "../../database.h"
#include "../../msgbase.h"
#include "../../user_ops.h"
#include "../../control.h"
#include "../../threads.h"
#include "../../context.h"

#include "../../ctdl_module.h"


struct PurgeList {
	struct PurgeList *next;
	char name[ROOMNAMELEN];		// use the larger of username or roomname
};

struct VPurgeList {
	struct VPurgeList *next;
	long vp_roomnum;
	long vp_roomgen;
	long vp_usernum;
};

struct ValidRoom {
	struct ValidRoom *next;
	long vr_roomnum;
	long vr_roomgen;
};

struct ValidUser {
	struct ValidUser *next;
	long vu_usernum;
};

struct ctdlroomref {
	struct ctdlroomref *next;
	long msgnum;
};

struct EPurgeList {
	struct EPurgeList *next;
	int ep_keylen;
	char *ep_key;
};


struct PurgeList *UserPurgeList = NULL;
struct PurgeList *RoomPurgeList = NULL;
struct ValidRoom *ValidRoomList = NULL;
struct ValidUser *ValidUserList = NULL;
int messages_purged;
int users_not_purged;
char *users_corrupt_msg = NULL;
char *users_zero_msg = NULL;
struct ctdlroomref *rr = NULL;
int force_purge_now = 0;			// set to nonzero to force a run right now


// First phase of message purge -- gather the locations of messages which
// qualify for purging and write them to a temp file.
void GatherPurgeMessages(struct ctdlroom *qrbuf, void *data) {
	struct ExpirePolicy epbuf;
	long delnum;
	time_t xtime, now;
	struct CtdlMessage *msg = NULL;
	int a;
	struct cdbdata *cdbfr;
	long *msglist = NULL;
	int num_msgs = 0;
	FILE *purgelist;

	purgelist = (FILE *)data;
	fprintf(purgelist, "r=%s\n", qrbuf->QRname);

	time(&now);
	GetExpirePolicy(&epbuf, qrbuf);

	// If the room is set to never expire messages ... do nothing
	if (epbuf.expire_mode == EXPIRE_NEXTLEVEL) return;
	if (epbuf.expire_mode == EXPIRE_MANUAL) return;

	// Don't purge messages containing system configuration, dumbass.
	if (!strcasecmp(qrbuf->QRname, SYSCONFIGROOM)) return;

	// Ok, we got this far ... now let's see what's in the room.
	cdbfr = cdb_fetch(CDB_MSGLISTS, &qrbuf->QRnumber, sizeof(long));

	if (cdbfr != NULL) {
		msglist = malloc(cdbfr->len);
		memcpy(msglist, cdbfr->ptr, cdbfr->len);
		num_msgs = cdbfr->len / sizeof(long);
		cdb_free(cdbfr);
	}

	// Nothing to do if there aren't any messages
	if (num_msgs == 0) {
		if (msglist != NULL) free(msglist);
		return;
	}

	// If the room is set to expire by count, do that.
	if (epbuf.expire_mode == EXPIRE_NUMMSGS) {
		if (num_msgs > epbuf.expire_value) {
			for (a=0; a<(num_msgs - epbuf.expire_value); ++a) {
				fprintf(purgelist, "m=%ld\n", msglist[a]);
				++messages_purged;
			}
		}
	}

	// If the room is set to expire by age...
	if (epbuf.expire_mode == EXPIRE_AGE) {
		for (a=0; a<num_msgs; ++a) {
			delnum = msglist[a];

			msg = CtdlFetchMessage(delnum, 0);	// don't need the body
			if (msg != NULL) {
				xtime = atol(msg->cm_fields[eTimestamp]);
				CM_Free(msg);
			}
			else {
				xtime = 0L;
			}

			if ((xtime > 0L) && (now - xtime > (time_t)(epbuf.expire_value * 86400L))) {
				fprintf(purgelist, "m=%ld\n", delnum);
				++messages_purged;
			}
		}
	}

	if (msglist != NULL) free(msglist);
}


// Second phase of message purge -- read list of msgs from temp file and delete them.
void DoPurgeMessages(FILE *purgelist) {
	char roomname[ROOMNAMELEN];
	long msgnum;
	char buf[SIZ];

	rewind(purgelist);
	strcpy(roomname, "nonexistent room ___ ___");
	while (fgets(buf, sizeof buf, purgelist) != NULL) {
		buf[strlen(buf)-1]=0;
		if (!strncasecmp(buf, "r=", 2)) {
			strcpy(roomname, &buf[2]);
		}
		if (!strncasecmp(buf, "m=", 2)) {
			msgnum = atol(&buf[2]);
			if (msgnum > 0L) {
				CtdlDeleteMessages(roomname, &msgnum, 1, "");
			}
		}
	}
}


void PurgeMessages(void) {
	FILE *purgelist;

	syslog(LOG_DEBUG, "PurgeMessages() called");
	messages_purged = 0;

	purgelist = tmpfile();
	if (purgelist == NULL) {
		syslog(LOG_CRIT, "Can't create purgelist temp file: %s", strerror(errno));
		return;
	}

	CtdlForEachRoom(GatherPurgeMessages, (void *)purgelist );
	DoPurgeMessages(purgelist);
	fclose(purgelist);
}


void AddValidUser(char *username, void *data) {
	struct ValidUser *vuptr;
	struct ctdluser usbuf;

	if (CtdlGetUser(&usbuf, username) != 0) {
		return;
	}

	vuptr = (struct ValidUser *)malloc(sizeof(struct ValidUser));
	vuptr->next = ValidUserList;
	vuptr->vu_usernum = usbuf.usernum;
	ValidUserList = vuptr;
}


void AddValidRoom(struct ctdlroom *qrbuf, void *data) {
	struct ValidRoom *vrptr;

	vrptr = (struct ValidRoom *)malloc(sizeof(struct ValidRoom));
	vrptr->next = ValidRoomList;
	vrptr->vr_roomnum = qrbuf->QRnumber;
	vrptr->vr_roomgen = qrbuf->QRgen;
	ValidRoomList = vrptr;
}


void DoPurgeRooms(struct ctdlroom *qrbuf, void *data) {
	time_t age, purge_secs;
	struct PurgeList *pptr;
	struct ValidUser *vuptr;
	int do_purge = 0;

	// For mailbox rooms, there's only one purging rule: if the user who
	// owns the room still exists, we keep the room; otherwise, we purge
	// it.  Bypass any other rules.
	if (qrbuf->QRflags & QR_MAILBOX) {
		// if user not found, do_purge will be 1
		do_purge = 1;
		for (vuptr=ValidUserList; vuptr!=NULL; vuptr=vuptr->next) {
			if (vuptr->vu_usernum == atol(qrbuf->QRname)) {
				do_purge = 0;
			}
		}
	}
	else {
		// Any of these attributes render a room non-purgable
		if (qrbuf->QRflags & QR_PERMANENT) return;
		if (qrbuf->QRflags & QR_DIRECTORY) return;
		if (qrbuf->QRflags2 & QR2_SYSTEM) return;
		if (!strcasecmp(qrbuf->QRname, SYSCONFIGROOM)) return;
		if (CtdlIsNonEditable(qrbuf)) return;

		// If we don't know the modification date, be safe and don't purge
		if (qrbuf->QRmtime <= (time_t)0) return;

		// If no room purge time is set, be safe and don't purge
		if (CtdlGetConfigLong("c_roompurge") < 0) return;

		// Otherwise, check the date of last modification
		age = time(NULL) - (qrbuf->QRmtime);
		purge_secs = CtdlGetConfigLong("c_roompurge") * 86400;
		if (purge_secs <= (time_t)0) return;
		syslog(LOG_DEBUG, "<%s> is <%ld> seconds old", qrbuf->QRname, (long)age);
		if (age > purge_secs) do_purge = 1;
	} // !QR_MAILBOX

	if (do_purge) {
		pptr = (struct PurgeList *) malloc(sizeof(struct PurgeList));
		pptr->next = RoomPurgeList;
		strcpy(pptr->name, qrbuf->QRname);
		RoomPurgeList = pptr;
	}

}


int PurgeRooms(void) {
	struct PurgeList *pptr;
	int num_rooms_purged = 0;
	struct ctdlroom qrbuf;
	struct ValidUser *vuptr;
	char *transcript = NULL;

	syslog(LOG_DEBUG, "PurgeRooms() called");

	// Load up a table full of valid user numbers so we can delete
	// user-owned rooms for users who no longer exist
	ForEachUser(AddValidUser, NULL);

	// Then cycle through the room file
	CtdlForEachRoom(DoPurgeRooms, NULL);

	// Free the valid user list
	while (ValidUserList != NULL) {
		vuptr = ValidUserList->next;
		free(ValidUserList);
		ValidUserList = vuptr;
	}

	transcript = malloc(SIZ);
	strcpy(transcript, "The following rooms have been auto-purged:\n");

	while (RoomPurgeList != NULL) {
		if (CtdlGetRoom(&qrbuf, RoomPurgeList->name) == 0) {
			transcript=realloc(transcript, strlen(transcript)+SIZ);
			snprintf(&transcript[strlen(transcript)], SIZ, " %s\n", qrbuf.QRname);
			CtdlDeleteRoom(&qrbuf);
			++num_rooms_purged;
		}
		pptr = RoomPurgeList->next;
		free(RoomPurgeList);
		RoomPurgeList = pptr;
	}

	if (num_rooms_purged > 0) CtdlAideMessage(transcript, "Room Autopurger Message");
	free(transcript);

	syslog(LOG_DEBUG, "Purged %d rooms.", num_rooms_purged);
	return(num_rooms_purged);
}


// Back end function to check user accounts for expiration.
void do_user_purge(char *username, void *data) {
	int purge;
	time_t now;
	time_t purge_time;
	struct PurgeList *pptr;
	struct ctdluser us;

	if (CtdlGetUser(&us, username) != 0) {
		return;
	}

	// Set purge time; if the user overrides the system default, use it
	if (us.USuserpurge > 0) {
		purge_time = ((time_t)us.USuserpurge) * 86400;
	}
	else {
		purge_time = CtdlGetConfigLong("c_userpurge") * 86400;
	}

	// The default rule is to not purge.
	purge = 0;
	
	// If the user has not logged in for the configured amount of time, expire the account.
	if (CtdlGetConfigLong("c_userpurge") > 0) {
		now = time(NULL);
		if ((now - us.lastcall) > purge_time) purge = 1;
	}

	// If the account is marked as permanent, don't purge it.
	if (us.flags & US_PERM) purge = 0;

	// If the account is an administrator, don't purge it.
	if (us.axlevel == 6) purge = 0;

	// If the access level is 0, the record should already have been
	// deleted, but maybe the user was logged in at the time or something.
	// Delete the record now.
	if (us.axlevel == 0) purge = 1;

	// If the user set his/her password to 'deleteme', he/she
	// wishes to be deleted, so purge the record.
	// Moved this lower down so that aides and permanent users get purged if they ask to be.
	if (!strcasecmp(us.password, "deleteme")) purge = 1;
	
	// any negative user number, is also impossible.
	if (us.usernum < 0L) purge = 1;
	
	// Don't purge user 0. That user is there for the system
	if (us.usernum == 0L) purge = 0;
	
	// If the user has no full name entry then we can't purge them since the actual purge can't find them.
	// This shouldn't happen but does somehow.
	if (IsEmptyStr(us.fullname)) {
		purge = 0;
		
		if (us.usernum > 0L) {
			purge=0;
			if (users_corrupt_msg == NULL) {
				users_corrupt_msg = malloc(SIZ);
				strcpy(users_corrupt_msg,
					"The auto-purger found the following user numbers with no name.\n"
					"The system has no way to purge a user with no name,"
					" and should not be able to create them either.\n"
					"This indicates corruption of the user DB or possibly a bug.\n"
					"It may be a good idea to restore your DB from a backup.\n"
				);
			}
		
			users_corrupt_msg=realloc(users_corrupt_msg, strlen(users_corrupt_msg)+30);
			snprintf(&users_corrupt_msg[strlen(users_corrupt_msg)], 29, " %ld\n", us.usernum);
		}
	}

	if (purge == 1) {
		pptr = (struct PurgeList *) malloc(sizeof(struct PurgeList));
		pptr->next = UserPurgeList;
		strcpy(pptr->name, us.fullname);
		UserPurgeList = pptr;
	}
	else {
		++users_not_purged;
	}

}


int PurgeUsers(void) {
	struct PurgeList *pptr;
	int num_users_purged = 0;
	char *transcript = NULL;

	syslog(LOG_DEBUG, "PurgeUsers() called");
	users_not_purged = 0;

	switch(CtdlGetConfigInt("c_auth_mode")) {
		case AUTHMODE_NATIVE:
			ForEachUser(do_user_purge, NULL);
			break;
		default:
			syslog(LOG_DEBUG, "User purge for auth mode %d is not implemented.", CtdlGetConfigInt("c_auth_mode"));
			break;
	}

	transcript = malloc(SIZ);

	if (users_not_purged == 0) {
		strcpy(transcript, "The auto-purger was told to purge every user.  It is\n"
				"refusing to do this because it usually indicates a problem\n"
				"such as an inability to communicate with a name service.\n"
		);
		while (UserPurgeList != NULL) {
			pptr = UserPurgeList->next;
			free(UserPurgeList);
			UserPurgeList = pptr;
			++num_users_purged;
		}
	}

	else {
		strcpy(transcript, "The following users have been auto-purged:\n");
		while (UserPurgeList != NULL) {
			transcript=realloc(transcript, strlen(transcript)+SIZ);
			snprintf(&transcript[strlen(transcript)], SIZ, " %s\n", UserPurgeList->name);
			purge_user(UserPurgeList->name);
			pptr = UserPurgeList->next;
			free(UserPurgeList);
			UserPurgeList = pptr;
			++num_users_purged;
		}
	}

	if (num_users_purged > 0) CtdlAideMessage(transcript, "User Purge Message");
	free(transcript);

	if (users_corrupt_msg) {
		CtdlAideMessage(users_corrupt_msg, "User Corruption Message");
		free (users_corrupt_msg);
		users_corrupt_msg = NULL;
	}
	
	if(users_zero_msg) {
		CtdlAideMessage(users_zero_msg, "User Zero Message");
		free (users_zero_msg);
		users_zero_msg = NULL;
	}
		
	syslog(LOG_DEBUG, "Purged %d users.", num_users_purged);
	return(num_users_purged);
}


// Purge visits
//
// This is a really cumbersome "garbage collection" function.  We have to
// delete visits which refer to rooms and/or users which no longer exist.  In
// order to prevent endless traversals of the room and user files, we first
// build linked lists of rooms and users which _do_ exist on the system, then
// traverse the visit file, checking each record against those two lists and
// purging the ones that do not have a match on _both_ lists.  (Remember, if
// either the room or user being referred to is no longer on the system, the
// record is useless and should be removed.)
//
int PurgeVisits(void) {
	struct cdbdata *cdbvisit;
	struct visit vbuf;
	struct VPurgeList *VisitPurgeList = NULL;
	struct VPurgeList *vptr;
	int purged = 0;
	char IndexBuf[32];
	int IndexLen;
	struct ValidRoom *vrptr;
	struct ValidUser *vuptr;
	int RoomIsValid, UserIsValid;

	// First, load up a table full of valid room/gen combinations
	CtdlForEachRoom(AddValidRoom, NULL);

	// Then load up a table full of valid user numbers
	ForEachUser(AddValidUser, NULL);

	// Now traverse through the visits, purging irrelevant records...
	cdb_rewind(CDB_VISIT);
	while(cdbvisit = cdb_next_item(CDB_VISIT), cdbvisit != NULL) {
		memset(&vbuf, 0, sizeof(struct visit));
		memcpy(&vbuf, cdbvisit->ptr,
			( (cdbvisit->len > sizeof(struct visit)) ?
			  sizeof(struct visit) : cdbvisit->len) );
		cdb_free(cdbvisit);

		RoomIsValid = 0;
		UserIsValid = 0;

		// Check to see if the room exists
		for (vrptr=ValidRoomList; vrptr!=NULL; vrptr=vrptr->next) {
			if ( (vrptr->vr_roomnum==vbuf.v_roomnum)
			     && (vrptr->vr_roomgen==vbuf.v_roomgen))
				RoomIsValid = 1;
		}

		// Check to see if the user exists
		for (vuptr=ValidUserList; vuptr!=NULL; vuptr=vuptr->next) {
			if (vuptr->vu_usernum == vbuf.v_usernum)
				UserIsValid = 1;
		}

		// Put the record on the purge list if it's dead
		if ((RoomIsValid==0) || (UserIsValid==0)) {
			vptr = (struct VPurgeList *)
				malloc(sizeof(struct VPurgeList));
			vptr->next = VisitPurgeList;
			vptr->vp_roomnum = vbuf.v_roomnum;
			vptr->vp_roomgen = vbuf.v_roomgen;
			vptr->vp_usernum = vbuf.v_usernum;
			VisitPurgeList = vptr;
		}

	}

	// Free the valid room/gen combination list
	while (ValidRoomList != NULL) {
		vrptr = ValidRoomList->next;
		free(ValidRoomList);
		ValidRoomList = vrptr;
	}

	// Free the valid user list
	while (ValidUserList != NULL) {
		vuptr = ValidUserList->next;
		free(ValidUserList);
		ValidUserList = vuptr;
	}

	// Now delete every visit on the purged list
	while (VisitPurgeList != NULL) {
		IndexLen = GenerateRelationshipIndex(IndexBuf,
				VisitPurgeList->vp_roomnum,
				VisitPurgeList->vp_roomgen,
				VisitPurgeList->vp_usernum);
		cdb_delete(CDB_VISIT, IndexBuf, IndexLen);
		vptr = VisitPurgeList->next;
		free(VisitPurgeList);
		VisitPurgeList = vptr;
		++purged;
	}

	return(purged);
}


// Purge the use table of old entries.
// Holy crap, this is WAY better.  We need to replace most linked lists with arrays.
int PurgeUseTable(StrBuf *ErrMsg) {
	int purged = 0;
	int total = 0;
	struct cdbdata *cdbut;
	struct UseTable ut;
	Array *purge_list = array_new(sizeof(int));

	// Phase 1: traverse through the table, discovering old records...

	syslog(LOG_DEBUG, "Purge use table: phase 1");
	cdb_rewind(CDB_USETABLE);
	while(cdbut = cdb_next_item(CDB_USETABLE), cdbut != NULL) {
		++total;
		if (cdbut->len > sizeof(struct UseTable))
			memcpy(&ut, cdbut->ptr, sizeof(struct UseTable));
		else {
			memset(&ut, 0, sizeof(struct UseTable));
			memcpy(&ut, cdbut->ptr, cdbut->len);
		}
		cdb_free(cdbut);

		if ( (time(NULL) - ut.timestamp) > USETABLE_RETAIN ) {
			array_append(purge_list, &ut.hash);
			++purged;
		}
	}

	// Phase 2: delete the records
	syslog(LOG_DEBUG, "Purge use table: phase 2");
	int i;
	for (i=0; i<purged; ++i) {
		struct UseTable *u = (struct UseTable *)array_get_element_at(purge_list, i);
		cdb_delete(CDB_USETABLE, &u->hash, sizeof(int));
	}
	array_free(purge_list);

	syslog(LOG_DEBUG, "Purge use table: finished (purged %d of %d records)", purged, total);
	return(purged);
}


// Purge the EUID Index of old records.
int PurgeEuidIndexTable(void) {
	int purged = 0;
	struct cdbdata *cdbei;
	struct EPurgeList *el = NULL;
	struct EPurgeList *eptr; 
	long msgnum;
	struct CtdlMessage *msg = NULL;

	// Phase 1: traverse through the table, discovering old records...
	syslog(LOG_DEBUG, "Purge EUID index: phase 1");
	cdb_rewind(CDB_EUIDINDEX);
	while(cdbei = cdb_next_item(CDB_EUIDINDEX), cdbei != NULL) {

		memcpy(&msgnum, cdbei->ptr, sizeof(long));

		msg = CtdlFetchMessage(msgnum, 0);
		if (msg != NULL) {
			CM_Free(msg);	// it still exists, so do nothing
		}
		else {
			eptr = (struct EPurgeList *) malloc(sizeof(struct EPurgeList));
			if (eptr != NULL) {
				eptr->next = el;
				eptr->ep_keylen = cdbei->len - sizeof(long);
				eptr->ep_key = malloc(cdbei->len);
				memcpy(eptr->ep_key, &cdbei->ptr[sizeof(long)], eptr->ep_keylen);
				el = eptr;
			}
			++purged;
		}

	       cdb_free(cdbei);

	}

	// Phase 2: delete the records
	syslog(LOG_DEBUG, "Purge euid index: phase 2");
	while (el != NULL) {
		cdb_delete(CDB_EUIDINDEX, el->ep_key, el->ep_keylen);
		free(el->ep_key);
		eptr = el->next;
		free(el);
		el = eptr;
	}

	syslog(LOG_DEBUG, "Purge euid index: finished (purged %d records)", purged);
	return(purged);
}


void purge_databases(void) {
	int retval;
	static time_t last_purge = 0;
	time_t now;
	struct tm tm;

	// Do the auto-purge if the current hour equals the purge hour,
	// but not if the operation has already been performed in the
	// last twelve hours.  This is usually enough granularity.
	now = time(NULL);
	localtime_r(&now, &tm);
	if (((tm.tm_hour != CtdlGetConfigInt("c_purge_hour")) || ((now - last_purge) < 43200)) && (force_purge_now == 0)) {
		return;
	}

	syslog(LOG_INFO, "Auto-purger: starting.");

	if (!server_shutting_down) {
		retval = PurgeUsers();
		syslog(LOG_NOTICE, "Purged %d users.", retval);
	}
		
	if (!server_shutting_down) {
		PurgeMessages();
	       	syslog(LOG_NOTICE, "Expired %d messages.", messages_purged);
	}

	if (!server_shutting_down) {
       		retval = PurgeRooms();
       		syslog(LOG_NOTICE, "Expired %d rooms.", retval);
	}

	if (!server_shutting_down) {
       		retval = PurgeVisits();
       		syslog(LOG_NOTICE, "Purged %d visits.", retval);
	}

	if (!server_shutting_down) {
		StrBuf *ErrMsg;
		ErrMsg = NewStrBuf();
		retval = PurgeUseTable(ErrMsg);
       		syslog(LOG_NOTICE, "Purged %d entries from the use table.", retval);
		FreeStrBuf(&ErrMsg);
	}

	if (!server_shutting_down) {
       		retval = PurgeEuidIndexTable();
       		syslog(LOG_NOTICE, "Purged %d entries from the EUID index.", retval);
	}

	//if (!server_shutting_down) {
	//	FIXME this is where we could do a non-interactive delete of zero-refcount messages
	//}

	if ( (!server_shutting_down) && (CtdlGetConfigInt("c_shrink_db_files") != 0) ) {
		cdb_compact();					// Shrink the DB files on disk
	}

	if (!server_shutting_down) {
	       	syslog(LOG_INFO, "Auto-purger: finished.");
		last_purge = now;				// So we don't do it again soon
		force_purge_now = 0;
	}
	else {
	       	syslog(LOG_INFO, "Auto-purger: STOPPED.");
	}
}


// Manually initiate a run of The Dreaded Auto-Purger (tm)
void cmd_tdap(char *argbuf) {
	if (CtdlAccessCheck(ac_aide)) return;
	force_purge_now = 1;
	cprintf("%d Manually initiating a purger run now.\n", CIT_OK);
}


// Initialization function, called from modules_init.c
char *ctdl_module_init_expire(void) {
	if (!threading) {
		CtdlRegisterProtoHook(cmd_tdap, "TDAP", "Manually initiate auto-purger");
		CtdlRegisterProtoHook(cmd_gpex, "GPEX", "Get expire policy");
		CtdlRegisterProtoHook(cmd_spex, "SPEX", "Set expire policy");
		CtdlRegisterSessionHook(purge_databases, EVT_TIMER, PRIO_CLEANUP + 20);
	}

	// return our module name for the log
	return "expire";
}
