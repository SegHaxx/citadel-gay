// Attempt to  your database from 32-bit to 64-bit.
// Don't run this.  It doesn't work and if you try to run it you will immediately die.
//
// Copyright (c) 2023 by Art Cancro citadel.org
//
// This program is open source software.  Use, duplication, or disclosure
// is subject to the terms of the GNU General Public License, version 3.

#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>
#include <stdio.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netdb.h>
#include <string.h>
#include <pwd.h>
#include <errno.h>
#include <stdarg.h>
#include <limits.h>
#include <libcitadel.h>
#include <zlib.h>
#include <db.h>
#include "../server/sysdep.h"
#include "../server/citadel_defs.h"
#include "../server/server.h"
#include "../server/citadel_dirs.h"
#include "ctdl3264_structs.h"


// Wrapper for realloc() that crashes and burns if the call fails.
void *reallok(void *ptr, size_t size) {
	void *p = realloc(ptr, size);
	if (!p) {
		fprintf(stderr, "realloc() failed to resize %p to %ld bytes, error: %m\n", ptr, size);
		exit(1);
	}
	return p;
}
#define realloc reallok


// Open a database environment
DB_ENV *open_dbenv(char *dirname) {

	DB_ENV *dbenv = NULL;

	int ret;
	int i;
	u_int32_t flags = 0;
	int dbversion_major, dbversion_minor, dbversion_patch;

	printf(	"db: open_dbenv() starting\n"
		"db:    Linked zlib: %s\n"
		"db: Compiled libdb: %s\n"
		"db:   Linked libdb: %s\n",
		zlibVersion(),
 		DB_VERSION_STRING,
 		db_version(&dbversion_major, &dbversion_minor, &dbversion_patch)
	);

	// Create synthetic integer version numbers and compare them.
	// Never run with a libdb older than the one with which it was compiled.
	int compiled_db_version = ( (DB_VERSION_MAJOR * 1000000) + (DB_VERSION_MINOR * 1000) + (DB_VERSION_PATCH) );
	int linked_db_version = ( (dbversion_major * 1000000) + (dbversion_minor * 1000) + (dbversion_patch) );
	if (compiled_db_version > linked_db_version) {
		printf(	"db: ctdl3264 is running with a version of libdb older than the one with which it was compiled.\n"
			"db: This is an invalid configuration.  ctdl3264 will now exit to prevent data loss.");
		exit(CTDLEXIT_DB);
	}

	printf("db: Setting up DB environment\n");
	ret = db_env_create(&dbenv, 0);
	if (ret) {
		printf("db: db_env_create: %s\n", db_strerror(ret));
		printf("db: exit code %d\n", ret);
		exit(CTDLEXIT_DB);
	}

	// We want to specify the shared memory buffer pool cachesize, but everything else is the default.
	ret = dbenv->set_cachesize(dbenv, 0, 64 * 1024, 0);
	if (ret) {
		printf("db: set_cachesize: %s\n", db_strerror(ret));
		dbenv->close(dbenv, 0);
		printf("db: exit code %d\n", ret);
		exit(CTDLEXIT_DB);
	}

	if ((ret = dbenv->set_lk_detect(dbenv, DB_LOCK_DEFAULT))) {
		printf("db: set_lk_detect: %s\n", db_strerror(ret));
		dbenv->close(dbenv, 0);
		printf("db: exit code %d\n", ret);
		exit(CTDLEXIT_DB);
	}

	flags = DB_CREATE | DB_INIT_MPOOL | DB_PRIVATE | DB_INIT_LOG;
	printf("db: dbenv open(dir=%s, flags=%d)\n", dirname, flags);
	ret = dbenv->open(dbenv, dirname, flags, 0);
	if (ret) {
		printf("db: dbenv->open: %s\n", db_strerror(ret));
		dbenv->close(dbenv, 0);
		printf("db: exit code %d\n", ret);
		exit(CTDLEXIT_DB);
	}

	return(dbenv);
}


void close_dbenv(DB_ENV *dbenv) {
	printf("db: closing dbenv\n");
	int ret = dbenv->close(dbenv, 0);
	if (ret) {
		printf("db: dbenv->close: %s\n", db_strerror(ret));
	}
}


// convert function for a message in msgmain
void convert_msgmain(int which_cdb, DBT *in_key, DBT *in_data, DBT *out_key, DBT *out_data) {
	int32_t in_msgnum;
	long out_msgnum;
	memcpy(&in_msgnum, in_key->data, sizeof(in_msgnum));
	out_msgnum = (long)in_msgnum;

	if (in_key->size != 4) {
		fprintf(stderr, "\033[31m\033[1m *** SOURCE DATABASE IS NOT 32-BIT *** ABORTING *** \033[0m\n");
		abort();
	}

	// If the msgnum is negative, we are looking at METADATA
	if (in_msgnum < 0) {
		struct MetaData_32 *meta32 = (struct MetaData_32 *)in_data->data;

		// printf("\033[32m\033[1mMetadata: msgnum=%d , refcount=%d , content_type=\"%s\" , rfc822len=%d\033[0m\n", meta32->meta_msgnum, meta32->meta_refcount, meta32->meta_content_type, meta32->meta_rfc822_length);

		out_key->size = sizeof(long);
		out_key->data = realloc(out_key->data, out_key->size);
		memcpy(out_key->data, &out_msgnum, sizeof(long));

		out_data->size = sizeof(struct MetaData);
		out_data->data = realloc(out_data->data, out_data->size);
		struct MetaData *meta64 = (struct MetaData *)out_data->data;
		memset(meta64, 0, sizeof(struct MetaData));
		meta64->meta_msgnum		= (long)	meta32->meta_msgnum;
		meta64->meta_refcount		= (int)		meta32->meta_refcount;
		strcpy(meta64->meta_content_type,		meta32->meta_content_type);
		meta64->meta_rfc822_length	= (long)	meta32->meta_rfc822_length;
	}

	// If the msgnum is positive, we are looking at a MESSAGE
	else if (in_msgnum > 0) {
		out_key->size = sizeof(long);
		out_key->data = realloc(out_key->data, out_key->size);
		memcpy(out_key->data, &out_msgnum, sizeof(long));

		// copy the message verbatim
		out_data->size = in_data->size;
		out_data->data = realloc(out_data->data, out_data->size);
		memcpy(out_data->data, in_data->data, out_data->size);
		// printf("\033[32m\033[1mMessage: %ld\033[0m\n", out_msgnum);
	}

	// If the msgnum is 0 it's probably not a valid record.
	else {
		printf("\033[31mmsgmain: message number 0 is impossible, skipping this record\033[0m\n");
	}
}


// convert function for a user record
void convert_users(int which_cdb, DBT *in_key, DBT *in_data, DBT *out_key, DBT *out_data) {

	// The key is a string so we can just copy it over
	out_key->size = in_key->size;
	out_key->data = realloc(out_key->data, out_key->size);
	memcpy(out_key->data, in_key->data, in_key->size);

	struct ctdluser_32 *user32 = (struct ctdluser_32 *)in_data->data;

	out_data->size = sizeof(struct ctdluser);
	out_data->data = realloc(out_data->data, out_data->size);
	struct ctdluser *user64 = (struct ctdluser *)out_data->data;

	user64->version			= (int)		user32->version;
	user64->uid			= (uid_t)	user32->uid;
	strcpy(user64->password,			user32->password);
	user64->flags			= (unsigned)	user32->flags;
	user64->timescalled		= (long)	user32->timescalled;
	user64->posted			= (long)	user32->posted;
	user64->axlevel			= (cit_uint8_t)	user32->axlevel;
	user64->usernum			= (long)	user32->usernum;
	user64->lastcall		= (time_t)	user32->lastcall;
	user64->USuserpurge		= (int)		user32->USuserpurge;
	strcpy(user64->fullname,			user32->fullname);
	user64->msgnum_bio		= (long)	user32->msgnum_bio;
	user64->msgnum_pic		= (long)	user32->msgnum_pic;
	strcpy(user64->emailaddrs,			user32->emailaddrs);
	user64->msgnum_inboxrules	= (long)	user32->msgnum_inboxrules;
	user64->lastproc_inboxrules	= (long)	user32->lastproc_inboxrules;

	// printf("\033[32m\033[1mUser: %s\033[0m\n", user64->fullname);
}


// convert function for a room record
void convert_rooms(int which_cdb, DBT *in_key, DBT *in_data, DBT *out_key, DBT *out_data) {

	// The key is a string so we can just copy it over
	out_key->size = in_key->size;
	out_key->data = realloc(out_key->data, out_key->size);
	memcpy(out_key->data, in_key->data, in_key->size);

	// data
	struct ctdlroom_32 *room32 = (struct ctdlroom_32 *)in_data->data;
	out_data->size = sizeof(struct ctdlroom);
	out_data->data = realloc(out_data->data, out_data->size);
	struct ctdlroom *room64 = (struct ctdlroom *)out_data->data;

	strcpy(room64->QRname,				room32->QRname);
	strcpy(room64->QRpasswd,			room32->QRpasswd);
	room64->QRroomaide		= (long)	room32->QRroomaide;
	room64->QRhighest		= (long)	room32->QRhighest;
	room64->QRgen			= (time_t)	room32->QRgen;
	room64->QRflags			= (unsigned)	room32->QRflags;
	strcpy(room64->QRdirname,			room32->QRdirname);
	room64->msgnum_info		= (long)	room32->msgnum_info;
	room64->QRfloor			= (char)	room32->QRfloor;
	room64->QRmtime			= (time_t)	room32->QRmtime;
	room64->QRep.expire_mode	= (int)		room32->QRep.expire_mode;
	room64->QRep.expire_value	= (int)		room32->QRep.expire_value;
	room64->QRnumber		= (long)	room32->QRnumber;
	room64->QRorder			= (char)	room32->QRorder;
	room64->QRflags2		= (unsigned)	room32->QRflags2;
	room64->QRdefaultview		= (int)		room32->QRdefaultview;
	room64->msgnum_pic		= (long)	room32->msgnum_pic;

	// printf("\033[32m\033[1mRoom: %s\033[0m\n", room64->QRname);
}


// convert function for a floor record
void convert_floors(int which_cdb, DBT *in_key, DBT *in_data, DBT *out_key, DBT *out_data) {

	// the key is an "int", and "int" is 32-bits on both 32 and 64 bit platforms.
	out_key->size = in_key->size;
	out_key->data = realloc(out_key->data, out_key->size);
	memcpy(out_key->data, in_key->data, in_key->size);

	// data
	struct floor_32 *floor32 = (struct floor_32 *)in_data->data;
	out_data->size = sizeof(struct floor);
	out_data->data = realloc(out_data->data, out_data->size);
	struct floor *floor64 = (struct floor *)out_data->data;

	// these are probably bit-for-bit identical, actually ... but we do it the "right" way anyway
	floor64->f_flags		= (unsigned short)	floor32->f_flags;
	strcpy(floor64->f_name,					floor32->f_name);
	floor64->f_ref_count		= (int)			floor32->f_ref_count;
	floor64->f_ep.expire_mode	= (int)			floor32->f_ep.expire_mode;
	floor64->f_ep.expire_value	= (int)			floor32->f_ep.expire_value;

	// printf("\033[32m\033[1mFloor: %s\033[0m\n", floor64->f_name);
}


// convert function for a msglist or a fulltext index record
// (both are indexed by a long and the data is arrays of longs)
void convert_msglists(int which_cdb, DBT *in_key, DBT *in_data, DBT *out_key, DBT *out_data) {
	int i;

	char *table = (which_cdb == CDB_FULLTEXT) ? "FullText" : "Msglist";

	// records are indexed by a single "long" and contains an array of zero or more "long"s
	// and remember ... "long" is int32_t on the source system
	int32_t in_roomnum;
	long out_roomnum;
	memcpy(&in_roomnum, in_key->data, sizeof(in_roomnum));
	out_roomnum = (long) in_roomnum;

	if (in_key->size != 4) {
		fprintf(stderr, "\033[31m\033[1m *** SOURCE DATABASE IS NOT 32-BIT *** ABORTING *** \033[0m\n");
		abort();
	}

	int num_msgs = in_data->size / sizeof(int32_t);
	// printf("\033[32m\033[1m%s: key %ld (%d messages)\033[0m\n", table, out_roomnum, num_msgs);

	// the key is a "long"
	out_key->size = sizeof(out_roomnum);
	out_key->data = realloc(out_key->data, out_key->size);
	memcpy(out_key->data, &out_roomnum, sizeof(out_roomnum));

	// the data is another array, but a wider type
	out_data->size = sizeof(long) * num_msgs;
	out_data->data = realloc(out_data->data, out_data->size);

	int32_t in_msg = 0;
	long out_msg = 0;
	for (i=0; i<num_msgs; ++i) {
		memcpy(&in_msg, (in_data->data + (i * sizeof(int32_t))), sizeof(int32_t));
		out_msg = (long) in_msg;
		memcpy((out_data->data + (i * sizeof(long))), &out_msg, sizeof(long));
		// printf("msg#%ld\n", out_msg);
	}
}


// convert function for a visit record
void convert_visits(int which_cdb, DBT *in_key, DBT *in_data, DBT *out_key, DBT *out_data) {

	// data
	struct visit_32 *visit32 = (struct visit_32 *)in_data->data;
	out_data->size = sizeof(struct visit);
	out_data->data = realloc(out_data->data, out_data->size);
	struct visit *visit64 = (struct visit *)out_data->data;

	//  the data (zero it out so it will compress well)
	memset(visit64, 0, sizeof(struct visit));
	visit64->v_roomnum		= (long)	visit32->v_roomnum;
	visit64->v_roomgen		= (long)	visit32->v_roomgen;
	visit64->v_usernum		= (long)	visit32->v_usernum;
	visit64->v_lastseen		= (long)	visit32->v_lastseen;
	visit64->v_flags		= (unsigned)	visit32->v_flags;
	strcpy(visit64->v_seen,				visit32->v_seen);
	strcpy(visit64->v_answered,			visit32->v_answered);
	visit64->v_view			= (int)		visit32->v_view;

	// printf("\033[32m\033[1mVisit: room %ld, gen %ld, user %ld\033[0m\n", visit64->v_roomnum, visit64->v_roomgen, visit64->v_usernum);

	// create the key (which is based on the data, so there is no need to convert the old key)
	out_key->size = sizeof(struct visit_index);
	out_key->data = realloc(out_key->data, out_key->size);
	struct visit_index *newvisitindex = (struct visit_index *) out_key->data;
	newvisitindex->iRoomID		=		visit64->v_roomnum;
	newvisitindex->iRoomGen		=		visit64->v_roomgen;
	newvisitindex->iUserID		=		visit64->v_usernum;
}


// convert function for a directory record
void convert_dir(int which_cdb, DBT *in_key, DBT *in_data, DBT *out_key, DBT *out_data) {

	// the key is a string
	out_key->size = in_key->size;
	out_key->data = realloc(out_key->data, out_key->size + 1);
	memcpy(out_key->data, in_key->data, in_key->size);
	char *k = (char *)out_key->data;
	k[out_key->size] = 0;

	// the data is also a string
	out_data->size = in_data->size;
	out_data->data = realloc(out_data->data, out_data->size + 1);
	memcpy(out_data->data, in_data->data, in_data->size);
	char *d = (char *)out_data->data;
	d[out_data->size] = 0;

	// please excuse my friend, he isn't null terminated
	// printf("\033[32m\033[1mDirectory entry: %s -> %s\033[0m\n", (char *)out_key->data, (char *)out_data->data);
}


// convert function for a use table record
void convert_usetable(int which_cdb, DBT *in_key, DBT *in_data, DBT *out_key, DBT *out_data) {

	// the key is a string
	out_key->size = in_key->size;
	out_key->data = realloc(out_key->data, out_key->size);
	memcpy(out_key->data, in_key->data, in_key->size);

	// the data is a "struct UseTable"
	struct UseTable_32 *use32 = (struct UseTable_32 *)in_data->data;
	out_data->size = sizeof(struct UseTable);
	out_data->data = realloc(out_data->data, out_data->size);
	memset(out_data->data, 0, out_data->size);
	struct UseTable *use64 = (struct UseTable *)out_data->data;

	//  the data
	strcpy(use64->ut_msgid,				use32->ut_msgid);
	use64->ut_timestamp		= (time_t)	use32->ut_timestamp;
}


// convert function for large message texts
void convert_bigmsgs(int which_cdb, DBT *in_key, DBT *in_data, DBT *out_key, DBT *out_data) {

	// The key is a packed long
	int32_t in_msgnum;
	long out_msgnum;
	memcpy(&in_msgnum, in_key->data, sizeof(in_msgnum));
	out_msgnum = (long)in_msgnum;

	if (in_key->size != 4) {
		fprintf(stderr, "\033[31m\033[1m *** SOURCE DATABASE IS NOT 32-BIT *** ABORTING *** \033[0m\n");
		abort();
	}

	// the data is binary-ish but has no packed integers
	out_data->size = in_data->size;
	out_data->data = realloc(out_data->data, out_data->size);
	memcpy(out_data->data, in_data->data, in_data->size);

	// printf("\033[32m\033[1mBigmsg %ld , length %d\033[0m\n", out_msgnum, out_data->size);
}


// convert function for EUID Index records
void convert_euidindex(int which_cdb, DBT *in_key, DBT *in_data, DBT *out_key, DBT *out_data) {

	// The structure of an euidindex record *key* is:
	// |----room_number----|----------EUID-------------|
	//    (sizeof long)       (actual length of euid)

	// The structure of an euidindex record *value* is:
	// |-----msg_number----|----room_number----|----------EUID-------------|
	//    (sizeof long)       (sizeof long)       (actual length of euid)

	int32_t in_msgnum = 0;
	int32_t in_roomnum = 0;
	char euid[SIZ];
	long out_msgnum = 0;
	long out_roomnum = 0;

	memcpy(&in_msgnum, in_data->data, sizeof(in_msgnum));
	memcpy(&in_roomnum, in_data->data+sizeof(int32_t), sizeof(in_msgnum));
	strcpy(euid, in_data->data+(sizeof(int32_t)*2));

	out_msgnum = (long) in_msgnum;
	out_roomnum = (long) in_roomnum;
	// printf("euidindex: msgnum=%ld, roomnum=%ld, euid=\"%s\"\n", out_msgnum, out_roomnum, euid);

	out_key->size = sizeof(long) + strlen(euid) + 1;
	out_key->data = realloc(out_key->data, out_key->size);
	memcpy(out_key->data, &out_roomnum, sizeof(out_roomnum));
	strcpy(out_key->data+sizeof(out_roomnum), euid);

	out_data->size = sizeof(long) + sizeof(long) + strlen(euid) + 1;
	out_data->data = realloc(out_data->data, out_data->size);
	memcpy(out_data->data, &out_msgnum, sizeof(out_msgnum));
	memcpy(out_data->data+sizeof(out_msgnum), &out_roomnum, sizeof(out_roomnum));
	strcpy(out_data->data+sizeof(out_msgnum)+sizeof(out_roomnum), euid);


	//int i;
	//char ch;
//
	//printf("  in_key:             ");
	//for (i=0; i<in_key->size; ++i) {
		//ch = 0;
		//memcpy(&ch, in_key->data+i, 1);
		//printf("%02X ", (int) ch);
	//}
	//printf("\n");
//
	//printf(" out_key: ");
	//for (i=0; i<out_key->size; ++i) {
		//ch = 0;
		//memcpy(&ch, out_key->data+i, 1);
		//printf("%02X ", (int) ch);
	//}
	//printf("\n");
//
	//printf(" in_data:                         ");
	//for (i=0; i<in_data->size; ++i) {
		//ch = 0;
		//memcpy(&ch, in_data->data+i, 1);
	//	printf("%02X ", (int) ch);
	//}
	//printf("\n");
//
	//printf("out_data: ");
	//for (i=0; i<out_data->size; ++i) {
		//ch = 0;
		//memcpy(&ch, out_data->data+i, 1);
		//printf("%02X ", (int) ch);
	//}
	//printf("\n");


}


// convert users-by-number records
void convert_usersbynumber(int which_cdb, DBT *in_key, DBT *in_data, DBT *out_key, DBT *out_data) {

	// key is a long
	// and remember ... "long" is int32_t on the source system
	int32_t in_usernum;
	long out_usernum;
	memcpy(&in_usernum, in_key->data, sizeof(in_usernum));
	out_usernum = (long) in_usernum;

	if (in_key->size != 4) {
		fprintf(stderr, "\033[31m\033[1m *** SOURCE DATABASE IS NOT 32-BIT *** ABORTING *** \033[0m\n");
		abort();
	}

	out_key->size = sizeof(out_usernum);
	out_key->data = realloc(out_key->data, out_key->size);
	memcpy(out_key->data, &out_usernum, sizeof(out_usernum));

	// value is a string
	out_data->size = in_data->size;
	out_data->data = realloc(out_data->data, out_data->size);
	memcpy(out_data->data, in_data->data, in_data->size);

	// printf("usersbynumber: %ld --> %s\n", out_usernum, (char *)out_data->data);
}


// convert function for a config record
void convert_config(int which_cdb, DBT *in_key, DBT *in_data, DBT *out_key, DBT *out_data) {

	// the key is a string
	out_key->size = in_key->size;
	out_key->data = realloc(out_key->data, out_key->size + 1);
	memcpy(out_key->data, in_key->data, in_key->size);
	char *k = (char *)out_key->data;
	k[out_key->size] = 0;

	// the data is a pair of strings
	out_data->size = in_data->size;
	out_data->data = realloc(out_data->data, out_data->size + 1);
	memcpy(out_data->data, in_data->data, in_data->size);
	char *d = (char *)out_data->data;
	d[out_data->size] = 0;

	// please excuse my friend, he isn't null terminated
	// printf("\033[32m\033[1mConfig entry: %s -> %s\033[0m\n", (char *)out_key->data, (char *)out_data->data+strlen(out_data->data)+1);
}


// For obsolete databases, zero all the output
void zero_function(int which_cdb, DBT *in_key, DBT *in_data, DBT *out_key, DBT *out_data) {
	out_key->size = 0;
	out_data->size = 0;
}


void (*convert_functions[])(int which_cdb, DBT *in_key, DBT *in_data, DBT *out_key, DBT *out_data) = {
	convert_msgmain,	// CDB_MSGMAIN
	convert_users,		// CDB_USERS
	convert_rooms,		// CDB_ROOMS
	convert_floors,		// CDB_FLOORTAB
	convert_msglists,	// CDB_MSGLISTS
	convert_visits,		// CDB_VISIT
	convert_dir,		// CDB_DIRECTORY
	convert_usetable,	// CDB_USETABLE
	convert_bigmsgs,	// CDB_BIGMSGS
	convert_msglists,	// CDB_FULLTEXT
	convert_euidindex,	// CDB_EUIDINDEX
	convert_usersbynumber,	// CDB_USERSBYNUMBER
	zero_function,		// CDB_UNUSED1 (obsolete)
	convert_config		// CDB_CONFIG
};


void convert_table(int which_cdb, DB_ENV *src_dbenv, DB_ENV *dst_dbenv) {
	int ret;
	int compressed;
	char dbfilename[32];
	uLongf destLen = 0;

	printf("\033[43m\033[K\033[0m\n");
	printf("\033[43m\033[30m Converting table %02x \033[K\033[0m\n", which_cdb);
	printf("\033[43m\033[K\033[0m\n");

	// shamelessly swiped from https://docs.oracle.com/database/bdb181/html/programmer_reference/am_cursor.html
	DB *src_dbp, *dst_dbp;
	DBC *src_dbcp;
	DBT in_key, in_data, out_key, out_data, uncomp_data, recomp_data;
	int num_rows = 0;

	snprintf(dbfilename, sizeof dbfilename, "cdb.%02x", which_cdb);

	// create a database handle for the source table
	ret = db_create(&src_dbp, src_dbenv, 0);
	if (ret) {
		printf("db: db_create: %s\n", db_strerror(ret));
		printf("db: exit code %d\n", ret);
		exit(CTDLEXIT_DB);
	}

	// open the file containing the source table
	printf("\033[33m\033[1mdb: opening source %s\033[0m\n", dbfilename);
	ret = src_dbp->open(src_dbp, NULL, dbfilename, NULL, DB_BTREE, 0, 0600);
	if (ret) {
		printf("db: db_open: %s\n", db_strerror(ret));
		printf("db: exit code %d\n", ret);
		exit(CTDLEXIT_DB);
	}

	// create a database handle for the destination table
	ret = db_create(&dst_dbp, dst_dbenv, 0);
	if (ret) {
		printf("db: db_create: %s\n", db_strerror(ret));
		printf("db: exit code %d\n", ret);
		exit(CTDLEXIT_DB);
	}

	// open the file containing the destination table
	printf("\033[33m\033[1mdb: opening destination %s\033[0m\n", dbfilename);
	ret = dst_dbp->open(dst_dbp, NULL, dbfilename, NULL, DB_BTREE, (DB_CREATE | DB_TRUNCATE), 0600);
	if (ret) {
		printf("db: db_open: %s\n", db_strerror(ret));
		printf("db: exit code %d\n", ret);
		exit(CTDLEXIT_DB);
	}

	// Acquire a cursor to read the source table
	printf("\033[33m\033[1mdb: acquiring cursor\033[0m\n");
	if ((ret = src_dbp->cursor(src_dbp, NULL, &src_dbcp, 0)) != 0) {
		printf("db: db_cursor: %s\n", db_strerror(ret));
		printf("db: exit code %d\n", ret);
		exit(CTDLEXIT_DB);
	}

	// Zero out these database keys
	memset(&in_key,		0, sizeof(DBT));	// input
	memset(&in_data,	0, sizeof(DBT));
	memset(&out_key,	0, sizeof(DBT));	// output
	memset(&out_data,	0, sizeof(DBT));
	memset(&uncomp_data,	0, sizeof(DBT));	// decompressed input (the key doesn't change)
	memset(&recomp_data,	0, sizeof(DBT));	// recompressed input (the key doesn't change)

	// Walk through the database, calling convert functions as we go and clearing buffers before each call.
	while (out_key.size = 0, out_data.size = 0, (ret = src_dbcp->get(src_dbcp, &in_key, &in_data, DB_NEXT)) == 0) {


		if (in_key.size == 0) {
			printf("\033[31mzero length key, skipping\033[0m\n");
		}

		else if (in_data.size == 0) {
			printf("\033[31mzero length data, skipping\033[0m\n");
		}

		else {	// Both key and data are >0 length so we're good to go

			// Do we need to decompress?
			static int32_t magic = COMPRESS_MAGIC;
			compressed = 0;
			if ( (in_data.size >= sizeof(struct CtdlCompressHeader_32)) && (!memcmp(in_data.data, &magic, sizeof(magic))) ) {
	
				// yes, we need to decompress
				compressed = 1;
				struct CtdlCompressHeader_32 comp32;
				memcpy(&comp32, in_data.data, sizeof(struct CtdlCompressHeader_32));
				uncomp_data.size = comp32.uncompressed_len;
				uncomp_data.data = realloc(uncomp_data.data, uncomp_data.size);
				destLen = (uLongf)comp32.uncompressed_len;
	
				ret = uncompress((Bytef *)uncomp_data.data, (uLongf *)&destLen, (const Bytef *)in_data.data+sizeof(struct CtdlCompressHeader_32), (uLong)comp32.compressed_len);
				if (ret != Z_OK) {
					printf("db: uncompress() error %d\n", ret);
					exit(CTDLEXIT_DB);
				}
				// printf("DB: %02x ,  in_keylen: %-3d ,  in_datalen: %-10d , dataptr: %012lx \033[31m(decompressed)\033[0m\n", which_cdb, (int)in_key.size, comp32.uncompressed_len, (long unsigned int)uncomp_data.data);
			}
			else {
				// printf("DB: %02x ,  in_keylen: %-3d ,  in_datalen: %-10d , dataptr: %012lx\n", which_cdb, (int)in_key.size, (int)in_data.size, (long unsigned int)in_data.data);
			}
	
			// Call the convert function registered to this table
			convert_functions[which_cdb](which_cdb, &in_key, (compressed ? &uncomp_data : &in_data), &out_key, &out_data);
	
			// The logic here is that if the source data was compressed, we compress the output too
			if (compressed) {
				struct CtdlCompressHeader zheader;
				memset(&zheader, 0, sizeof(struct CtdlCompressHeader));
				zheader.magic = COMPRESS_MAGIC;
				zheader.uncompressed_len = out_data.size;
				recomp_data.data = realloc(recomp_data.data, ((out_data.size * 101) / 100) + 100 + sizeof(struct CtdlCompressHeader));
	
				if (compress2((Bytef *)(recomp_data.data + sizeof(struct CtdlCompressHeader)), &destLen, (Bytef *)out_data.data, (uLongf) out_data.size, 1) != Z_OK) {
					printf("db: compress() error\n");
					exit(CTDLEXIT_DB);
				}
				recomp_data.size = destLen;
			}
	
			// write the converted record to the target database
			if (out_key.size > 0) {
	
				// If we compressed the output, write recomp_data instead of out_data
				if (compressed) {
					// printf("DB: %02x , out_keylen: %-3d , out_datalen: %-10d , dataptr: %012lx \033[31m(compressed)\033[0m\n", which_cdb, (int)out_key.size, (int)recomp_data.size, (long unsigned int)recomp_data.data);
					ret = dst_dbp->put(dst_dbp, NULL, &out_key, &recomp_data, 0);
				}
				else {
					// printf("DB: %02x , out_keylen: %-3d , out_datalen: %-10d , dataptr: %012lx\n", which_cdb, (int)out_key.size, (int)out_data.size, (long unsigned int)out_data.data);
					ret = dst_dbp->put(dst_dbp, NULL, &out_key, &out_data, 0);
				}
	
				if (ret) {
					printf("db: cdb_put(%d): %s", which_cdb, db_strerror(ret));
					exit(CTDLEXIT_DB);
				}
	
				// Knowing the total number of rows isn't critical to the program.  It's just for the user to know.
				++num_rows;
				printf("   \033[32m%d\033[0m\r", num_rows);
				fflush(stdout);
			}
		}
	}

	if (ret != DB_NOTFOUND) {
		printf("db: db_get: %s\n", db_strerror(ret));
		printf("db: exit code %d\n", ret);
		exit(CTDLEXIT_DB);
	}

	printf("%d rows\n", num_rows);

	// free any leftover out_data pointers
	free(out_key.data);
	free(out_data.data);
	free(uncomp_data.data);
	free(recomp_data.data);

	// ...and close the database (table)
	printf("\033[33m\033[1mdb: closing source %02x\033[0m\n", which_cdb);
	ret = src_dbp->close(src_dbp, 0);
	if (ret) {
		printf("db: db_close: %s\n", db_strerror(ret));
	}

	printf("\033[33m\033[1mdb: closing destination %02x\033[0m\n", which_cdb);
	ret = dst_dbp->close(dst_dbp, 0);
	if (ret) {
		printf("db: db_close: %s\n", db_strerror(ret));
	}

	printf("\n");

}


int main(int argc, char **argv) {
	char *src_dir = NULL;
	char *dst_dir = NULL;
	int confirmed = 0;
	static DB_ENV *src_dbenv;		// Source DB environment (global)
	static DB_ENV *dst_dbenv;		// Source DB environment (global)

	// Check to make sure we're running on the target 64-bit system
	if (sizeof(void *) != 8) {
		fprintf(stderr, "%s: this is a %ld-bit system.\n", argv[0], sizeof(void *)*8);
		fprintf(stderr, "%s: you must run this on a 64-bit system, onto which a 32-bit database has been copied.\n", argv[0]);
		exit(1);
	}

	// Parse command line
	int a;
	while ((a = getopt(argc, argv, "s:d:y")) != EOF) {
		switch (a) {
		case 's':
			src_dir = optarg;
			break;
		case 'd':
			dst_dir = optarg;
			break;
		case 'y':
			confirmed = 1;
			break;
		default:
			fprintf(stderr, "%s: usage: %s -s source_dir -d dest_dir\n", argv[0], argv[0]);
			exit(2);
		}
	}

	// Warn the user
	printf("------------------------------------------------------------------------\n");
	printf("ctdl3264 converts a Citadel database written on a 32-bit system to one  \n");
	printf("that can be run on a 64-bit system.  It is intended to be run OFFLINE.  \n");
	printf("Neither the source nor the target data directories should be mounted by \n");
	printf("a running Citadel server.  We \033[1mguarantee\033[0m data corruption if you do not   \n");
	printf("observe this warning!  The source [-s] directory should contain a copy  \n");
	printf("of the database from your 32-bit system.  The destination [-d] directory\n");
	printf("should be empty and will receive your 64-bit database.                  \n");
	printf("------------------------------------------------------------------------\n");
	printf("     Source 32-bit directory: %s\n", src_dir);
	printf("Destination 64-bit directory: %s\n", dst_dir);
	printf("------------------------------------------------------------------------\n");

	if (confirmed == 1) {
		printf("You have specified the [-y] flag, so processing will continue.\n");
	}
	else {
		printf("Please read [ https://www.citadel.org/ctdl3264.html ] to learn how to proceed.\n");
		exit(0);
	}

	src_dbenv = open_dbenv(src_dir);
	dst_dbenv = open_dbenv(dst_dir);
	for (int i = 0; i < MAXCDB; ++i) {
		convert_table(i, src_dbenv, dst_dbenv);
	}
	close_dbenv(src_dbenv);
	close_dbenv(dst_dbenv);

	exit(0);
}
