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

// Open the various databases we'll be using.  Any database which
// does not exist should be created.  Note that we don't need a
// critical section here, because there aren't any active threads
// manipulating the database yet.
void open_databases(void) {
	int ret;
	int i;
	u_int32_t flags = 0;
	int dbversion_major, dbversion_minor, dbversion_patch;

	printf(	"db: open_databases() starting\n"
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
		printf(	"db: citserver is running with a version of libdb older than the one with which it was compiled.\n"
			"db: This is an invalid configuration.  citserver will now exit to prevent data loss.");
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

	flags = DB_CREATE | DB_INIT_MPOOL | DB_PRIVATE | DB_INIT_TXN | DB_INIT_LOCK | DB_THREAD | DB_INIT_LOG;
	printf("db: dbenv->open(dbenv, %s, %d, 0)\n", ctdl_db_dir, flags);
	ret = dbenv->open(dbenv, ctdl_db_dir, flags, 0);				// try opening the database cleanly
	if (ret == DB_RUNRECOVERY) {
		printf("db: dbenv->open: %s\n", db_strerror(ret));
		printf("db: attempting recovery...\n");
		flags |= DB_RECOVER;
		ret = dbenv->open(dbenv, ctdl_db_dir, flags, 0);			// try recovery
	}
	if (ret == DB_RUNRECOVERY) {
		printf("db: dbenv->open: %s\n", db_strerror(ret));
		printf("db: attempting catastrophic recovery...\n");
		flags &= ~DB_RECOVER;
		flags |= DB_RECOVER_FATAL;
		ret = dbenv->open(dbenv, ctdl_db_dir, flags, 0);			// try catastrophic recovery
	}
	if (ret) {
		printf("db: dbenv->open: %s\n", db_strerror(ret));
		dbenv->close(dbenv, 0);
		printf("db: exit code %d\n", ret);
		exit(CTDLEXIT_DB);
	}
}

 



// Close all of the db database files we've opened.  This can be done in a loop, since it's just a bunch of closes.
void close_databases(void) {
	int i;
	int ret;

	//syslog(LOG_INFO, "db: performing final checkpoint");
	//if ((ret = dbenv->txn_checkpoint(dbenv, 0, 0, 0))) {
		//syslog(LOG_ERR, "db: txn_checkpoint: %s", db_strerror(ret));
	//}

	// Close the handle.
	ret = dbenv->close(dbenv, 0);
	if (ret) {
		printf("db: DBENV->close: %s\n", db_strerror(ret));
	}
}


void null_function(void) {
	printf("FIXME null_function() called which means we have more work to do!\n");
}


void (*convert_functions[])(void) = {
	null_function,		// CDB_MSGMAIN
	null_function,		// CDB_USERS
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

	printf("Converting table %d\n", which_cdb);

	// Begin by opening the database (table)

	DB *dbp;								// One DB handle for one Citadel database
	printf("\033[33m\033[1mdb: opening database %02x\033[0m\n", which_cdb);
	ret = db_create(&dbp, dbenv, 0);					// Create a database handle
	if (ret) {
		printf("db: db_create: %s\n", db_strerror(ret));
		printf("db: exit code %d\n", ret);
		exit(CTDLEXIT_DB);
	}

	snprintf(dbfilename, sizeof dbfilename, "cdb.%02x", which_cdb);			// table names by number
	ret = dbp->open(dbp, NULL, dbfilename, NULL, DB_BTREE, DB_AUTO_COMMIT, 0600);
	if (ret) {
		printf("db: db_open[%02x]: %s\n", which_cdb, db_strerror(ret));
		if (ret == ENOMEM) {
			printf("db: You may need to tune your database; please check http://www.citadel.org for more information.\n");
		}
		printf("db: exit code %d\n", ret);
		exit(CTDLEXIT_DB);
	}
	
	// Call the convert function (this will need some parameters later)
	convert_functions[which_cdb]();

	// Flush the logs...
	printf("\033[33m\033[1mdb: flushing the database logs\033[0m\n");
	if ((ret = dbenv->log_flush(dbenv, NULL))) {
		printf("db: log_flush: %s\n", db_strerror(ret));
	}

	// ...and close the database (table)
	printf("\033[33m\033[1mdb: closing database %02x\033[0m\n", which_cdb);
	ret = dbp->close(dbp, 0);
	if (ret) {
		printf("db: db_close: %s\n", db_strerror(ret));
	}

}


int main(int argc, char **argv) {
	char ctdldir[PATH_MAX]=CTDLDIR;

	// Check to make sure we're running on the target 64-bit system
	if (sizeof(void *) != 8) {
		fprintf(stderr, "%s: this is a %ld-bit system.\n", argv[0], sizeof(void *)*8);
		fprintf(stderr, "%s: you must run this on a 64-bit system, onto which a 32-bit database has been copied.\n", argv[0]);
		exit(1);
	}

	// Parse command line
	int a;
	while ((a = getopt(argc, argv, "h:")) != EOF) {
		switch (a) {
		case 'h':
			strncpy(ctdldir, optarg, sizeof ctdldir);
			break;
		default:
			fprintf(stderr, "%s: usage: %s [-h server_dir]\n", argv[0], argv[0]);
			exit(2);
		}
	}

	open_databases();
	for (int i = 0; i < MAXCDB; ++i) {
		convert_table(i);
	}
	close_databases();

	exit(0);
}
