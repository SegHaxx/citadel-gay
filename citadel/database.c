/*
 * This is a data store backend for the Citadel server which uses Berkeley DB.
 *
 * Copyright (c) 1987-2019 by the citadel.org team
 *
 * This program is open source software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

/*****************************************************************************
       Tunable configuration parameters for the Berkeley DB back end
 *****************************************************************************/

/* Citadel will checkpoint the db at the end of every session, but only if
 * the specified number of kilobytes has been written, or if the specified
 * number of minutes has passed, since the last checkpoint.
 */
#define MAX_CHECKPOINT_KBYTES	256
#define MAX_CHECKPOINT_MINUTES	15

/*****************************************************************************/

#include "sysdep.h"
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <stdio.h>
#include <dirent.h>
#include <zlib.h>

#include <db.h>

#if DB_VERSION_MAJOR < 5
#error Citadel requires Berkeley DB v5.0 or newer.  Please upgrade.
#endif

#include <libcitadel.h>

#include "ctdl_module.h"
#include "control.h"
#include "citserver.h"
#include "config.h"

static DB *dbp[MAXCDB];		/* One DB handle for each Citadel database */
static DB_ENV *dbenv;		/* The DB environment (global) */


void cdb_abort(void)
{
	syslog(LOG_DEBUG, "db: citserver is stopping in order to prevent data loss. uid=%d gid=%d euid=%d egid=%d",
		getuid(), getgid(), geteuid(), getegid()
	);
	exit(CTDLEXIT_DB);
}


/* Verbose logging callback */
void cdb_verbose_log(const DB_ENV * dbenv, const char *msg)
{
	if (!IsEmptyStr(msg)) {
		syslog(LOG_DEBUG, "db: %s", msg);
	}
}


/* Verbose logging callback */
void cdb_verbose_err(const DB_ENV * dbenv, const char *errpfx, const char *msg)
{
	syslog(LOG_ERR, "db: %s", msg);
}


/* wrapper for txn_abort() that logs/aborts on error */
static void txabort(DB_TXN *tid)
{
	int ret;

	ret = tid->abort(tid);

	if (ret) {
		syslog(LOG_ERR, "db: txn_abort: %s", db_strerror(ret));
		cdb_abort();
	}
}


/* wrapper for txn_commit() that logs/aborts on error */
static void txcommit(DB_TXN *tid)
{
	int ret;

	ret = tid->commit(tid, 0);

	if (ret) {
		syslog(LOG_ERR, "db: txn_commit: %s", db_strerror(ret));
		cdb_abort();
	}
}


/* wrapper for txn_begin() that logs/aborts on error */
static void txbegin(DB_TXN **tid)
{
	int ret;

	ret = dbenv->txn_begin(dbenv, NULL, tid, 0);

	if (ret) {
		syslog(LOG_ERR, "db: txn_begin: %s", db_strerror(ret));
		cdb_abort();
	}
}


/* panic callback */
static void dbpanic(DB_ENV * env, int errval)
{
	syslog(LOG_ERR, "db: PANIC: %s", db_strerror(errval));
}


static void cclose(DBC * cursor)
{
	int ret;

	if ((ret = cursor->c_close(cursor))) {
		syslog(LOG_ERR, "db: c_close: %s", db_strerror(ret));
		cdb_abort();
	}
}


static void bailIfCursor(DBC ** cursors, const char *msg)
{
	int i;

	for (i = 0; i < MAXCDB; i++)
		if (cursors[i] != NULL) {
			syslog(LOG_ERR, "db: cursor still in progress on cdb %02x: %s", i, msg);
			cdb_abort();
		}
}


void cdb_check_handles(void)
{
	bailIfCursor(TSD->cursors, "in check_handles");

	if (TSD->tid != NULL) {
		syslog(LOG_ERR, "db: transaction still in progress!");
		cdb_abort();
	}
}


/*
 * Request a checkpoint of the database.  Called once per minute by the thread manager.
 */
void cdb_checkpoint(void)
{
	int ret;

	syslog(LOG_DEBUG, "db: -- checkpoint --");
	ret = dbenv->txn_checkpoint(dbenv, MAX_CHECKPOINT_KBYTES, MAX_CHECKPOINT_MINUTES, 0);

	if (ret != 0) {
		syslog(LOG_ERR, "db: cdb_checkpoint() txn_checkpoint: %s", db_strerror(ret));
		cdb_abort();
	}

	/* After a successful checkpoint, we can cull the unused logs */
	if (CtdlGetConfigInt("c_auto_cull")) {
		ret = dbenv->log_set_config(dbenv, DB_LOG_AUTO_REMOVE, 1);
	}
	else {
		ret = dbenv->log_set_config(dbenv, DB_LOG_AUTO_REMOVE, 0);
	}
}


/*
 * Open the various databases we'll be using.  Any database which
 * does not exist should be created.  Note that we don't need a
 * critical section here, because there aren't any active threads
 * manipulating the database yet.
 */
void open_databases(void)
{
	int ret;
	int i;
	char dbfilename[32];
	u_int32_t flags = 0;
	int dbversion_major, dbversion_minor, dbversion_patch;

	syslog(LOG_DEBUG, "db: open_databases() starting");
	syslog(LOG_DEBUG, "db: Compiled libdb: %s", DB_VERSION_STRING);
	syslog(LOG_DEBUG, "db:   Linked libdb: %s", db_version(&dbversion_major, &dbversion_minor, &dbversion_patch));
	syslog(LOG_DEBUG, "db:    Linked zlib: %s", zlibVersion());

	/*
	 * Silently try to create the database subdirectory.  If it's already there, no problem.
	 */
	if ((mkdir(ctdl_data_dir, 0700) != 0) && (errno != EEXIST)) {
		syslog(LOG_ERR, "db: unable to create database directory [%s]: %m", ctdl_data_dir);
	}
	if (chmod(ctdl_data_dir, 0700) != 0) {
		syslog(LOG_ERR, "db: unable to set database directory permissions [%s]: %m", ctdl_data_dir);
	}
	if (chown(ctdl_data_dir, CTDLUID, (-1)) != 0) {
		syslog(LOG_ERR, "db: unable to set the owner for [%s]: %m", ctdl_data_dir);
	}
	syslog(LOG_DEBUG, "db: Setting up DB environment");
	// db_env_set_func_yield((int (*)(u_long,  u_long))sched_yield);
	ret = db_env_create(&dbenv, 0);
	if (ret) {
		syslog(LOG_ERR, "db: db_env_create: %s", db_strerror(ret));
		syslog(LOG_ERR, "db: exit code %d", ret);
		exit(CTDLEXIT_DB);
	}
	dbenv->set_errpfx(dbenv, "citserver");
	dbenv->set_paniccall(dbenv, dbpanic);
	dbenv->set_errcall(dbenv, cdb_verbose_err);
	dbenv->set_errpfx(dbenv, "ctdl");
	dbenv->set_msgcall(dbenv, cdb_verbose_log);
	dbenv->set_verbose(dbenv, DB_VERB_DEADLOCK, 1);
	dbenv->set_verbose(dbenv, DB_VERB_RECOVERY, 1);

	/*
	 * We want to specify the shared memory buffer pool cachesize,
	 * but everything else is the default.
	 */
	ret = dbenv->set_cachesize(dbenv, 0, 64 * 1024, 0);
	if (ret) {
		syslog(LOG_ERR, "db: set_cachesize: %s", db_strerror(ret));
		dbenv->close(dbenv, 0);
		syslog(LOG_ERR, "db: exit code %d", ret);
		exit(CTDLEXIT_DB);
	}

	if ((ret = dbenv->set_lk_detect(dbenv, DB_LOCK_DEFAULT))) {
		syslog(LOG_ERR, "db: set_lk_detect: %s", db_strerror(ret));
		dbenv->close(dbenv, 0);
		syslog(LOG_ERR, "db: exit code %d", ret);
		exit(CTDLEXIT_DB);
	}

	flags = DB_CREATE | DB_INIT_MPOOL | DB_PRIVATE | DB_INIT_TXN | DB_INIT_LOCK | DB_THREAD | DB_INIT_LOG;
	syslog(LOG_DEBUG, "db: dbenv->open(dbenv, %s, %d, 0)", ctdl_data_dir, flags);
	ret = dbenv->open(dbenv, ctdl_data_dir, flags, 0);				// try opening the database cleanly
	if (ret == DB_RUNRECOVERY) {
		syslog(LOG_ERR, "db: dbenv->open: %s", db_strerror(ret));
		syslog(LOG_ERR, "db: attempting recovery...");
		flags |= DB_RECOVER;
		ret = dbenv->open(dbenv, ctdl_data_dir, flags, 0);			// try recovery
	}
	if (ret == DB_RUNRECOVERY) {
		syslog(LOG_ERR, "db: dbenv->open: %s", db_strerror(ret));
		syslog(LOG_ERR, "db: attempting catastrophic recovery...");
		flags &= ~DB_RECOVER;
		flags |= DB_RECOVER_FATAL;
		ret = dbenv->open(dbenv, ctdl_data_dir, flags, 0);			// try catastrophic recovery
	}
	if (ret) {
		syslog(LOG_ERR, "db: dbenv->open: %s", db_strerror(ret));
		dbenv->close(dbenv, 0);
		syslog(LOG_ERR, "db: exit code %d", ret);
		exit(CTDLEXIT_DB);
	}

	syslog(LOG_INFO, "db: mounting databases");
	for (i = 0; i < MAXCDB; ++i) {
		ret = db_create(&dbp[i], dbenv, 0);					// Create a database handle
		if (ret) {
			syslog(LOG_ERR, "db: db_create: %s", db_strerror(ret));
			syslog(LOG_ERR, "db: exit code %d", ret);
			exit(CTDLEXIT_DB);
		}

		snprintf(dbfilename, sizeof dbfilename, "cdb.%02x", i);			// table names by number
		ret = dbp[i]->open(dbp[i], NULL, dbfilename, NULL, DB_BTREE, DB_CREATE | DB_AUTO_COMMIT | DB_THREAD, 0600);
		if (ret) {
			syslog(LOG_ERR, "db: db_open[%02x]: %s", i, db_strerror(ret));
			if (ret == ENOMEM) {
				syslog(LOG_ERR, "db: You may need to tune your database; please read http://www.citadel.org/doku.php?id=faq:troubleshooting:out_of_lock_entries for more information.");
			}
			syslog(LOG_ERR, "db: exit code %d", ret);
			exit(CTDLEXIT_DB);
		}
	}
}


/*
 * Make sure we own all the files, because in a few milliseconds we're going to drop root privs.
 */
void cdb_chmod_data(void)
{
	DIR *dp;
	struct dirent *d;
	char filename[PATH_MAX];

	dp = opendir(ctdl_data_dir);
	if (dp != NULL) {
		while (d = readdir(dp), d != NULL) {
			if (d->d_name[0] != '.') {
				snprintf(filename, sizeof filename, "%s/%s", ctdl_data_dir, d->d_name);
				syslog(LOG_DEBUG, "db: chmod(%s, 0600) returned %d", filename, chmod(filename, 0600));
				syslog(LOG_DEBUG, "db: chown(%s, CTDLUID, -1) returned %d",
					filename, chown(filename, CTDLUID, (-1))
				);
			}
		}
		closedir(dp);
	}
}


/*
 * Close all of the db database files we've opened.  This can be done
 * in a loop, since it's just a bunch of closes.
 */
void close_databases(void)
{
	int i;
	int ret;

	syslog(LOG_INFO, "db: performing final checkpoint");
	if ((ret = dbenv->txn_checkpoint(dbenv, 0, 0, 0))) {
		syslog(LOG_ERR, "txn_checkpoint: %s", db_strerror(ret));
	}

	syslog(LOG_INFO, "db: flushing the database logs");
	if ((ret = dbenv->log_flush(dbenv, NULL))) {
		syslog(LOG_ERR, "db: log_flush: %s", db_strerror(ret));
	}

#ifdef DB_STAT_ALL
	/* print some statistics... */
	dbenv->lock_stat_print(dbenv, DB_STAT_ALL);
#endif

	/* close the tables */
	syslog(LOG_INFO, "db: closing databases");
	for (i = 0; i < MAXCDB; ++i) {
		syslog(LOG_INFO, "db: closing database %02x", i);
		ret = dbp[i]->close(dbp[i], 0);
		if (ret) {
			syslog(LOG_ERR, "db: db_close: %s", db_strerror(ret));
		}

	}

	/* Close the handle. */
	ret = dbenv->close(dbenv, 0);
	if (ret) {
		syslog(LOG_EMERG, "db: DBENV->close: %s", db_strerror(ret));
	}
}


/*
 * Decompress a database item if it was compressed on disk
 */
void cdb_decompress_if_necessary(struct cdbdata *cdb)
{
	static int magic = COMPRESS_MAGIC;

	if ((cdb == NULL) || (cdb->ptr == NULL) || (cdb->len < sizeof(magic)) || (memcmp(cdb->ptr, &magic, sizeof(magic)))) {
		return;
	}

	/* At this point we know we're looking at a compressed item. */

	struct CtdlCompressHeader zheader;
	char *uncompressed_data;
	char *compressed_data;
	uLongf destLen, sourceLen;
	size_t cplen;

	memset(&zheader, 0, sizeof(struct CtdlCompressHeader));
	cplen = sizeof(struct CtdlCompressHeader);
	if (sizeof(struct CtdlCompressHeader) > cdb->len) {
		cplen = cdb->len;
	}
	memcpy(&zheader, cdb->ptr, cplen);

	compressed_data = cdb->ptr;
	compressed_data += sizeof(struct CtdlCompressHeader);

	sourceLen = (uLongf) zheader.compressed_len;
	destLen = (uLongf) zheader.uncompressed_len;
	uncompressed_data = malloc(zheader.uncompressed_len);

	if (uncompress((Bytef *) uncompressed_data,
		       (uLongf *) & destLen, (const Bytef *) compressed_data, (uLong) sourceLen) != Z_OK) {
		syslog(LOG_EMERG, "db: uncompress() error");
		cdb_abort();
	}

	free(cdb->ptr);
	cdb->len = (size_t) destLen;
	cdb->ptr = uncompressed_data;
}


/*
 * Store a piece of data.  Returns 0 if the operation was successful.  If a
 * key already exists it should be overwritten.
 */
int cdb_store(int cdb, const void *ckey, int ckeylen, void *cdata, int cdatalen)
{

	DBT dkey, ddata;
	DB_TXN *tid = NULL;
	int ret = 0;
	struct CtdlCompressHeader zheader;
	char *compressed_data = NULL;
	int compressing = 0;
	size_t buffer_len = 0;
	uLongf destLen = 0;

	memset(&dkey, 0, sizeof(DBT));
	memset(&ddata, 0, sizeof(DBT));
	dkey.size = ckeylen;
	dkey.data = (void *) ckey;
	ddata.size = cdatalen;
	ddata.data = cdata;

	/* Only compress Visit and UseTable records.  Everything else is uncompressed. */
	if ((cdb == CDB_VISIT) || (cdb == CDB_USETABLE)) {
		compressing = 1;
		zheader.magic = COMPRESS_MAGIC;
		zheader.uncompressed_len = cdatalen;
		buffer_len = ((cdatalen * 101) / 100) + 100 + sizeof(struct CtdlCompressHeader);
		destLen = (uLongf) buffer_len;
		compressed_data = malloc(buffer_len);
		if (compress2((Bytef *) (compressed_data + sizeof(struct CtdlCompressHeader)),
			      &destLen, (Bytef *) cdata, (uLongf) cdatalen, 1) != Z_OK) {
			syslog(LOG_EMERG, "db: compress2() error");
			cdb_abort();
		}
		zheader.compressed_len = (size_t) destLen;
		memcpy(compressed_data, &zheader, sizeof(struct CtdlCompressHeader));
		ddata.size = (size_t) (sizeof(struct CtdlCompressHeader) + zheader.compressed_len);
		ddata.data = compressed_data;
	}

	if (TSD->tid != NULL) {
		ret = dbp[cdb]->put(dbp[cdb],	// db
				    TSD->tid,	// transaction ID
				    &dkey,	// key
				    &ddata,	// data
				    0		// flags
		);
		if (ret) {
			syslog(LOG_EMERG, "db: cdb_store(%d): %s", cdb, db_strerror(ret));
			cdb_abort();
		}
		if (compressing) {
			free(compressed_data);
		}
		return ret;
	} else {
		bailIfCursor(TSD->cursors, "attempt to write during r/o cursor");

	      retry:
		txbegin(&tid);

		if ((ret = dbp[cdb]->put(dbp[cdb],	// db
					 tid,		// transaction ID
					 &dkey,		// key
					 &ddata,	// data
					 0))) {		// flags
			if (ret == DB_LOCK_DEADLOCK) {
				txabort(tid);
				goto retry;
			} else {
				syslog(LOG_EMERG, "db: cdb_store(%d): %s", cdb, db_strerror(ret));
				cdb_abort();
			}
		} else {
			txcommit(tid);
			if (compressing) {
				free(compressed_data);
			}
			return ret;
		}
	}
	return ret;
}


/*
 * Delete a piece of data.  Returns 0 if the operation was successful.
 */
int cdb_delete(int cdb, void *key, int keylen)
{

	DBT dkey;
	DB_TXN *tid;
	int ret;

	memset(&dkey, 0, sizeof dkey);
	dkey.size = keylen;
	dkey.data = key;

	if (TSD->tid != NULL) {
		ret = dbp[cdb]->del(dbp[cdb], TSD->tid, &dkey, 0);
		if (ret) {
			syslog(LOG_EMERG, "db: cdb_delete(%d): %s", cdb, db_strerror(ret));
			if (ret != DB_NOTFOUND) {
				cdb_abort();
			}
		}
	} else {
		bailIfCursor(TSD->cursors, "attempt to delete during r/o cursor");

	      retry:
		txbegin(&tid);

		if ((ret = dbp[cdb]->del(dbp[cdb], tid, &dkey, 0)) && ret != DB_NOTFOUND) {
			if (ret == DB_LOCK_DEADLOCK) {
				txabort(tid);
				goto retry;
			} else {
				syslog(LOG_EMERG, "db: cdb_delete(%d): %s", cdb, db_strerror(ret));
				cdb_abort();
			}
		} else {
			txcommit(tid);
		}
	}
	return ret;
}


static DBC *localcursor(int cdb)
{
	int ret;
	DBC *curs;

	if (TSD->cursors[cdb] == NULL) {
		ret = dbp[cdb]->cursor(dbp[cdb], TSD->tid, &curs, 0);
	}
	else {
		ret = TSD->cursors[cdb]->c_dup(TSD->cursors[cdb], &curs, DB_POSITION);
	}

	if (ret) {
		syslog(LOG_EMERG, "db: localcursor: %s", db_strerror(ret));
		cdb_abort();
	}

	return curs;
}


/*
 * Fetch a piece of data.  If not found, returns NULL.  Otherwise, it returns
 * a struct cdbdata which it is the caller's responsibility to free later on
 * using the cdb_free() routine.
 */
struct cdbdata *cdb_fetch(int cdb, const void *key, int keylen)
{
	struct cdbdata *tempcdb;
	DBT dkey, dret;
	int ret;

	memset(&dkey, 0, sizeof(DBT));
	dkey.size = keylen;
	dkey.data = (void *) key;

	if (TSD->tid != NULL) {
		memset(&dret, 0, sizeof(DBT));
		dret.flags = DB_DBT_MALLOC;
		ret = dbp[cdb]->get(dbp[cdb], TSD->tid, &dkey, &dret, 0);		// crashing here
	} else {
		DBC *curs;

		do {
			memset(&dret, 0, sizeof(DBT));
			dret.flags = DB_DBT_MALLOC;
			curs = localcursor(cdb);
			ret = curs->c_get(curs, &dkey, &dret, DB_SET);
			cclose(curs);
		}
		while (ret == DB_LOCK_DEADLOCK);
	}

	if ((ret != 0) && (ret != DB_NOTFOUND)) {
		syslog(LOG_EMERG, "db: cdb_fetch(%d): %s", cdb, db_strerror(ret));
		cdb_abort();
	}

	if (ret != 0) {
		return NULL;
	}

	tempcdb = (struct cdbdata *) malloc(sizeof(struct cdbdata));

	if (tempcdb == NULL) {
		syslog(LOG_EMERG, "db: cdb_fetch: Cannot allocate memory for tempcdb");
		cdb_abort();
		return NULL;	/* make it easier for static analysis... */
	} else {
		tempcdb->len = dret.size;
		tempcdb->ptr = dret.data;
		cdb_decompress_if_necessary(tempcdb);
		return (tempcdb);
	}
}


/*
 * Free a cdbdata item.
 *
 * Note that we only free the 'ptr' portion if it is not NULL.  This allows
 * other code to assume ownership of that memory simply by storing the
 * pointer elsewhere and then setting 'ptr' to NULL.  cdb_free() will then
 * avoid freeing it.
 */
void cdb_free(struct cdbdata *cdb)
{
	if (cdb->ptr) {
		free(cdb->ptr);
	}
	free(cdb);
}


void cdb_close_cursor(int cdb)
{
	if (TSD->cursors[cdb] != NULL) {
		cclose(TSD->cursors[cdb]);
	}

	TSD->cursors[cdb] = NULL;
}


/* 
 * Prepare for a sequential search of an entire database.
 * (There is guaranteed to be no more than one traversal in
 * progress per thread at any given time.)
 */
void cdb_rewind(int cdb)
{
	int ret = 0;

	if (TSD->cursors[cdb] != NULL) {
		syslog(LOG_EMERG, "db: cdb_rewind: must close cursor on database %d before reopening", cdb);
		cdb_abort();
		/* cclose(TSD->cursors[cdb]); */
	}

	/*
	 * Now initialize the cursor
	 */
	ret = dbp[cdb]->cursor(dbp[cdb], TSD->tid, &TSD->cursors[cdb], 0);
	if (ret) {
		syslog(LOG_EMERG, "db: cdb_rewind: db_cursor: %s", db_strerror(ret));
		cdb_abort();
	}
}


/*
 * Fetch the next item in a sequential search.  Returns a pointer to a 
 * cdbdata structure, or NULL if we've hit the end.
 */
struct cdbdata *cdb_next_item(int cdb)
{
	DBT key, data;
	struct cdbdata *cdbret;
	int ret = 0;

	/* Initialize the key/data pair so the flags aren't set. */
	memset(&key, 0, sizeof(key));
	memset(&data, 0, sizeof(data));
	data.flags = DB_DBT_MALLOC;

	ret = TSD->cursors[cdb]->c_get(TSD->cursors[cdb], &key, &data, DB_NEXT);

	if (ret) {
		if (ret != DB_NOTFOUND) {
			syslog(LOG_EMERG, "db: cdb_next_item(%d): %s", cdb, db_strerror(ret));
			cdb_abort();
		}
		cdb_close_cursor(cdb);
		return NULL;	/* presumably, end of file */
	}

	cdbret = (struct cdbdata *) malloc(sizeof(struct cdbdata));
	cdbret->len = data.size;
	cdbret->ptr = data.data;
	cdb_decompress_if_necessary(cdbret);

	return (cdbret);
}


/*
 * Transaction-based stuff.  I'm writing this as I bake cookies...
 */
void cdb_begin_transaction(void)
{

	bailIfCursor(TSD->cursors, "can't begin transaction during r/o cursor");

	if (TSD->tid != NULL) {
		syslog(LOG_EMERG, "db: cdb_begin_transaction: ERROR: nested transaction");
		cdb_abort();
	}

	txbegin(&TSD->tid);
}


void cdb_end_transaction(void)
{
	int i;

	for (i = 0; i < MAXCDB; i++)
		if (TSD->cursors[i] != NULL) {
			syslog(LOG_WARNING, "db: cdb_end_transaction: WARNING: cursor %d still open at transaction end", i);
			cclose(TSD->cursors[i]);
			TSD->cursors[i] = NULL;
		}

	if (TSD->tid == NULL) {
		syslog(LOG_ERR, "db: cdb_end_transaction: ERROR: txcommit(NULL) !!");
		cdb_abort();
	} else {
		txcommit(TSD->tid);
	}

	TSD->tid = NULL;
}


/*
 * Truncate (delete every record)
 */
void cdb_trunc(int cdb)
{
	/* DB_TXN *tid; */
	int ret;
	u_int32_t count;

	if (TSD->tid != NULL) {
		syslog(LOG_EMERG, "db: cdb_trunc must not be called in a transaction.");
		cdb_abort();
	} else {
		bailIfCursor(TSD->cursors, "attempt to write during r/o cursor");

	      retry:
		/* txbegin(&tid); */

		if ((ret = dbp[cdb]->truncate(dbp[cdb],	/* db */
					      NULL,	/* transaction ID */
					      &count,	/* #rows deleted */
					      0))) {	/* flags */
			if (ret == DB_LOCK_DEADLOCK) {
				/* txabort(tid); */
				goto retry;
			} else {
				syslog(LOG_EMERG, "db: cdb_truncate(%d): %s", cdb, db_strerror(ret));
				if (ret == ENOMEM) {
					syslog(LOG_EMERG, "db: You may need to tune your database; please read http://www.citadel.org/doku.php?id=faq:troubleshooting:out_of_lock_entries for more information.");
				}
				exit(CTDLEXIT_DB);
			}
		} else {
			/* txcommit(tid); */
		}
	}
}


/*
 * compact (defragment) the database , possibly returning space back to the underlying filesystem
 */
void cdb_compact(void)
{
	int ret;
	int i;

	syslog(LOG_DEBUG, "db: cdb_compact() started");
	for (i = 0; i < MAXCDB; i++) {
		syslog(LOG_DEBUG, "db: compacting database %d", i);
		ret = dbp[i]->compact(dbp[i], NULL, NULL, NULL, NULL, DB_FREE_SPACE, NULL);
		if (ret) {
			syslog(LOG_ERR, "db: compact: %s", db_strerror(ret));
		}
	}
	syslog(LOG_DEBUG, "db: cdb_compact() finished");
}


// Has an item already been seen (is it in the CDB_USETABLE) ?
// Returns 0 if it hasn't, 1 if it has
// In either case, writes the item to the database for next time.
int CheckIfAlreadySeen(StrBuf *guid)
{
	int found = 0;
	struct UseTable ut;
	struct cdbdata *cdbut;

	syslog(LOG_DEBUG, "db: CheckIfAlreadySeen(%s)", ChrPtr(guid));
	cdbut = cdb_fetch(CDB_USETABLE, SKEY(guid));
	if (cdbut != NULL) {
		found = 1;
		cdb_free(cdbut);
	}

	/* (Re)write the record, to update the timestamp.  Zeroing it out makes it compress better. */
	memset(&ut, 0, sizeof(struct UseTable));
	memcpy(ut.ut_msgid, SKEY(guid));
	ut.ut_timestamp = time(NULL);
	cdb_store(CDB_USETABLE, SKEY(guid), &ut, sizeof(struct UseTable));
	return (found);
}


void cmd_rsen(char *argbuf)
{
	char Token[SIZ];
	long TLen;
	char Time[SIZ];

	struct UseTable ut;
	struct cdbdata *cdbut;

	if (CtdlAccessCheck(ac_aide)) {
		return;
	}

	TLen = extract_token(Token, argbuf, 1, '|', sizeof Token);
	if (strncmp(argbuf, "GET", 3) == 0) {
		cdbut = cdb_fetch(CDB_USETABLE, Token, TLen);
		if (cdbut != NULL) {
			memcpy(&ut, cdbut->ptr, ((cdbut->len > sizeof(struct UseTable)) ? sizeof(struct UseTable) : cdbut->len));

			cprintf("%d %ld\n", CIT_OK, ut.ut_timestamp);
		} else {
			cprintf("%d not found\n", ERROR + NOT_HERE);
		}

	} else if (strncmp(argbuf, "SET", 3) == 0) {
		memcpy(ut.ut_msgid, Token, TLen);
		extract_token(Time, argbuf, 2, '|', sizeof Time);
		ut.ut_timestamp = atol(Time);
		cdb_store(CDB_USETABLE, Token, TLen, &ut, sizeof(struct UseTable));
		cprintf("%d token updated\n", CIT_OK);
	} else if (strncmp(argbuf, "DEL", 3) == 0) {
		if (cdb_delete(CDB_USETABLE, Token, TLen))
			cprintf("%d not found\n", ERROR + NOT_HERE);
		else
			cprintf("%d deleted.\n", CIT_OK);

	} else {
		cprintf("%d Usage: [GET|SET|DEL]|Token|timestamp\n", ERROR);
	}

}


CTDL_MODULE_INIT(database)
{
	if (!threading) {
		CtdlRegisterProtoHook(cmd_rsen, "RSEN", "manipulate Aggregators seen database");
	}

	/* return our module id for the log */
	return "database";
}
