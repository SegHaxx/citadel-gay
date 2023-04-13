// Attempt to convert your database from 32-bit to 64-bit.
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

static DB_ENV *dbenv;		// The DB environment (global)

// Open the database environment
void open_dbenv(char *src_dir) {
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
	// Never allow citserver to run with a libdb older then the one with which it was compiled.
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
	printf("db: dbenv open(dir=%s, flags=%d)\n", src_dir, flags);
	ret = dbenv->open(dbenv, src_dir, flags, 0);
	if (ret) {
		printf("db: dbenv->open: %s\n", db_strerror(ret));
		dbenv->close(dbenv, 0);
		printf("db: exit code %d\n", ret);
		exit(CTDLEXIT_DB);
	}
}


void close_dbenv(void) {
	printf("db: closing dbenv\n");
	int ret = dbenv->close(dbenv, 0);
	if (ret) {
		printf("db: DBENV->close: %s\n", db_strerror(ret));
	}
}


// placeholder convert function for the data types not yet implemented
void null_function(int which_cdb, DBT *in_key, DBT *in_data, DBT *out_key, DBT *out_data) {
}


// convert function for a message in msgmain
void convert_msgmain(int which_cdb, DBT *in_key, DBT *in_data, DBT *out_key, DBT *out_data) {
	int32_t in_msgnum;
	long out_msgnum;
	memcpy(&in_msgnum, in_key->data, sizeof(in_msgnum));
	out_msgnum = (long)in_msgnum;

	if (in_key->size != 4) {
		printf("\033[31m\033[1m *** SOURCE DATABASE IS NOT 32-BIT *** ABORTING *** \033[0m\n");
		abort();
	}

	printf("Message %ld\n", out_msgnum);

	// If the msgnum is negative, we are looking at METADATA
	if (in_msgnum < 0) {
		struct MetaData_32 *meta32 = (struct MetaData_32 *)in_data->data;

		//printf("metadata: msgnum=%d , refcount=%d , content_type=\"%s\" , rfc822len=%d\n", meta32->meta_msgnum, meta32->meta_refcount, meta32->meta_content_type, meta32->meta_rfc822_length);

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
	}

	// If the msgnum is 0 it's probably not a valid record.
	else {
		printf("msgmain: record 0 is impossible\n");
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

	printf("User: %s\n", user64->fullname);
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

	printf("Room: %s\n", room64->QRname);
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
}


// convert function for a msglist
void convert_msglists(int which_cdb, DBT *in_key, DBT *in_data, DBT *out_key, DBT *out_data) {
	int i;

	// msglist records are indexed by a single "long" and contains an array of zero or more "long"s
	// and remember ... "long" is int32_t on the source system
	int32_t in_roomnum;
	long out_roomnum;
	memcpy(&in_roomnum, in_key->data, sizeof(in_roomnum));
	out_roomnum = (long) in_roomnum;

	if (in_key->size != 4) {
		printf("\033[31m\033[1m *** SOURCE DATABASE IS NOT 32-BIT *** ABORTING *** \033[0m\n");
		abort();
	}

	int num_msgs = in_data->size / sizeof(int32_t);
	printf("msglist for room %ld (%d messages)\n", out_roomnum, num_msgs);

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
		printf("#%ld\n", out_msg);
	}
}


// convert function for a visit record
void convert_visits(int which_cdb, DBT *in_key, DBT *in_data, DBT *out_key, DBT *out_data) {

	// the key is a... FIXME
	out_key->size = in_key->size;
	out_key->data = realloc(out_key->data, out_key->size);
	memcpy(out_key->data, in_key->data, in_key->size);

	// data
	struct visit_32 *visit32 = (struct visit_32 *)in_data->data;
	out_data->size = sizeof(struct visit);
	out_data->data = realloc(out_data->data, out_data->size);
	struct visit *visit64 = (struct visit *)out_data->data;

	// FIXME do the conv
}


void (*convert_functions[])(int which_cdb, DBT *in_key, DBT *in_data, DBT *out_key, DBT *out_data) = {
	convert_msgmain,	// CDB_MSGMAIN
	convert_users,		// CDB_USERS
	convert_rooms,		// CDB_ROOMS
	convert_floors,		// CDB_FLOORTAB
	convert_msglists,	// CDB_MSGLISTS
	convert_visits,		// CDB_VISIT
	null_function,		// CDB_DIRECTORY
	null_function,		// CDB_USETABLE
	null_function,		// CDB_BIGMSGS
	null_function,		// CDB_FULLTEXT
	null_function,		// CDB_EUIDINDEX
	null_function,		// CDB_USERSBYNUMBER
	null_function,		// CDB_EXTAUTH
	null_function		// CDB_CONFIG
};


void convert_table(int which_cdb) {
	int ret;
	int compressed;
	char dbfilename[32];

	printf("\033[32m\033[1mConverting table %d\033[0m\n", which_cdb);

	// shamelessly swiped from https://docs.oracle.com/database/bdb181/html/programmer_reference/am_cursor.html
	DB *dbp;
	DBC *dbcp;
	DBT in_key, in_data, out_key, out_data, uncomp_data;
	int num_rows = 0;

	// create a database handle
	ret = db_create(&dbp, dbenv, 0);
	if (ret) {
		printf("db: db_create: %s\n", db_strerror(ret));
		printf("db: exit code %d\n", ret);
		exit(CTDLEXIT_DB);
	}

	// open the file
	snprintf(dbfilename, sizeof dbfilename, "cdb.%02x", which_cdb);
	printf("\033[33m\033[1mdb: opening %s\033[0m\n", dbfilename);
	ret = dbp->open(dbp, NULL, dbfilename, NULL, DB_BTREE, 0, 0600);
	if (ret) {
		printf("db: db_open: %s\n", db_strerror(ret));
		printf("db: exit code %d\n", ret);
		exit(CTDLEXIT_DB);
	}

	// Acquire a cursor
	if ((ret = dbp->cursor(dbp, NULL, &dbcp, 0)) != 0) {
		printf("db: db_cursor: %s\n", db_strerror(ret));
		printf("db: exit code %d\n", ret);
		exit(CTDLEXIT_DB);
	}

	// Zero out these database keys
	memset(&in_key, 0, sizeof(in_key));		// input
	memset(&in_data, 0, sizeof(in_data));
	memset(&out_key, 0, sizeof(out_key));		// output
	memset(&out_data, 0, sizeof(out_data));
	memset(&uncomp_data, 0, sizeof(uncomp_data));	// decompressed input (the key doesn't change)

	// Walk through the database, calling convert functions as we go and clearing buffers before each call.
	while (out_key.size = 0, out_data.size = 0, (ret = dbcp->get(dbcp, &in_key, &in_data, DB_NEXT)) == 0) {
		compressed = 0;

		// Do we need to decompress?
		static int32_t magic = COMPRESS_MAGIC;
		if ( (in_data.size >= sizeof(struct CtdlCompressHeader_32)) && (!memcmp(in_data.data, &magic, sizeof(magic))) ) {

			// yes, we need to decompress
			compressed = 1;
			struct CtdlCompressHeader_32 comp;
			memcpy(&comp, in_data.data, sizeof(struct CtdlCompressHeader_32));
			printf("\033[31m\033[1mCOMPRESSED , uncompressed_len=%d , compressed_len=%d\033[0m\n", comp.uncompressed_len, comp.compressed_len);

			uncomp_data.size = comp.uncompressed_len - sizeof(struct CtdlCompressHeader_32);
			uncomp_data.data = realloc(uncomp_data.data, uncomp_data.size);

			uLongf destLen;
			if (uncompress((Bytef *)uncomp_data.data, (uLongf *)&destLen, (const Bytef *)in_data.data+sizeof(struct CtdlCompressHeader_32), (uLong)in_data.size) != Z_OK) {
				printf("db: uncompress() error\n");
				exit(CTDLEXIT_DB);
			}
		}

		// Call the convert function registered to this table
		convert_functions[which_cdb](which_cdb, &in_key, (compressed ? &uncomp_data : &in_data), &out_key, &out_data);

		// write the converted record to the new database
		if (out_key.size > 0) {
			printf("DB: %02x ,  in_keylen: %3d ,  in_datalen: %d , dataptr: %lx\n", which_cdb, (int)in_key.size, (int)in_data.size, (long unsigned int)in_data.data);
			printf("DB: %02x , out_keylen: %3d , out_datalen: %d , dataptr: %lx\n", which_cdb, (int)out_key.size, (int)out_data.size, (long unsigned int)out_data.data);

			// Knowing the total number of rows isn't critical to the program.  It's just for the user to know.
			++num_rows;
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

	// Flush the logs...
	//printf("\033[33m\033[1mdb: flushing the database logs\033[0m\n");
	//if ((ret = dbenv->log_flush(dbenv, NULL))) {
		//printf("db: log_flush: %s\n", db_strerror(ret));
	//}

	// ...and close the database (table)
	printf("\033[33m\033[1mdb: closing database %02x\033[0m\n", which_cdb);
	ret = dbp->close(dbp, 0);
	if (ret) {
		printf("db: db_close: %s\n", db_strerror(ret));
	}

}


int main(int argc, char **argv) {
	char *src_dir = NULL;
	int confirmed = 0;

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
	printf("a running Citadel server.  We guarantee data corruption if you do not   \n");
	printf("observe this warning!  The source [-s] directory should contain a copy  \n");
	printf("of the database from your 32-bit system.  The destination [-d] directory\n");
	printf("should be empty and will receive your 64-bit database.                  \n");
	printf("------------------------------------------------------------------------\n");
	printf("     Source 32-bit directory: %s\n", src_dir);
	printf("Destination 64-bit directory: %s\n", "FIXME");
	printf("------------------------------------------------------------------------\n");

	if (confirmed == 1) {
		printf("You have specified the [-y] flag, so processing will continue.\n");
	}
	else {
		printf("Please consult the documentation to learn how to proceed.\n");
		exit(0);
	}

	open_dbenv(src_dir);
	for (int i = 0; i < MAXCDB; ++i) {
		convert_table(i);
	}
	close_dbenv();

	exit(0);
}
