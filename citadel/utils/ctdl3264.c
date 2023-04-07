// Attempt to convert your database from 32-bit to 64-bit.
// Don't run this.  It doesn't work and if you try to run it you will immediately die.
//
// Copyright (c) 2023 by the citadel.org team
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
void null_function(int which_cdb, DBT *key, DBT *data) {
	//printf("DB: %02x , keylen: %3d , datalen: %d , dataptr: %x\n", which_cdb, (int)key->size, (int)data->size, data->data);
}


// convert function for a message in msgmain
void convert_msgmain(int which_cdb, DBT *key, DBT *data) {
	int32_t msgnum;
	memcpy(&msgnum, key->data, sizeof(msgnum));
	printf("msgmain: len is %d , key is %d\n", key->size, msgnum);

	if (key->size != 4) {
		printf("\033[31m\033[1m *** SOURCE DATABASE IS NOT 32-BIT *** ABORTING *** \033[0m\n");
		abort();
	}


	if (msgnum < 0) {
		struct MetaData_32 meta;
		if (data->size != sizeof meta) {
			printf("\033[31mmetadata: db says %d bytes , struct says %ld bytes\033[0m\n", data->size, sizeof meta);
			abort();
		}
		memset(&meta, 0, sizeof meta);
		memcpy(&meta, data->data, data->size);

		printf("metadata: msgnum=%d , refcount=%d , content_type=\"%s\" , rfc822len=%d\n",
        meta.meta_msgnum,
        meta.meta_refcount,
        meta.meta_content_type,
        meta.meta_rfc822_length);



	}

	// If the msgnum is positive, we are looking at a MESSAGE
	// If the msgnum is negative, we are looking at METADATA
}


// convert function for a message in msgmain
void convert_users(int which_cdb, DBT *key, DBT *data) {
	char userkey[64];
	memcpy(userkey, key->data, key->size);
	userkey[key->size] = 0;
	printf("users: len is %d , key is %s\n", key->size, userkey);
}


void (*convert_functions[])(int which_cdb, DBT *key, DBT *data) = {
	convert_msgmain,	// CDB_MSGMAIN
	convert_users,		// CDB_USERS
	null_function,		// CDB_ROOMS
	null_function,		// CDB_FLOORTAB
	null_function,		// CDB_MSGLISTS
	null_function,		// CDB_VISIT
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
	char dbfilename[32];

	printf("\033[32m\033[1mConverting table %d\033[0m\n", which_cdb);

	// shamelessly swiped from https://docs.oracle.com/database/bdb181/html/programmer_reference/am_cursor.html
	DB *dbp;
	DBC *dbcp;
	DBT key, data;
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

	// Initialize the key/data return pair.
	memset(&key, 0, sizeof(key));
	memset(&data, 0, sizeof(data));

	// Walk through the database and print out the key/data pairs.
	while ((ret = dbcp->get(dbcp, &key, &data, DB_NEXT)) == 0) {

		++num_rows;

		// Call the convert function (this will need some parameters later)
		convert_functions[which_cdb](which_cdb, &key, &data);
	}

	if (ret != DB_NOTFOUND) {
		printf("db: db_get: %s\n", db_strerror(ret));
		printf("db: exit code %d\n", ret);
		exit(CTDLEXIT_DB);
	}

	printf("%d rows\n", num_rows);

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
