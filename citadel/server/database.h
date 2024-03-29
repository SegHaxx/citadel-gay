/*
 * Copyright (c) 1987-2017 by the citadel.org team
 *
 *  This program is open source software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 3.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 */

#ifndef DATABASE_H
#define DATABASE_H


void open_databases (void);
void close_databases (void);
int cdb_store (int cdb, const void *key, int keylen, void *data, int datalen);
int cdb_delete (int cdb, void *key, int keylen);
struct cdbdata *cdb_fetch (int cdb, const void *key, int keylen);
void cdb_free (struct cdbdata *cdb);
void cdb_rewind (int cdb);
struct cdbdata *cdb_next_item (int cdb);
void cdb_close_cursor(int cdb);
void cdb_begin_transaction(void);
void cdb_end_transaction(void);
void cdb_allocate_tsd(void);
void cdb_free_tsd(void);
void cdb_check_handles(void);
void cdb_trunc(int cdb);
void *checkpoint_thread(void *arg);
void cdb_chmod_data(void);
void cdb_checkpoint(void);
void check_handles(void *arg);
void cdb_cull_logs(void);
void cdb_compact(void);


int CheckIfAlreadySeen(StrBuf *guid);

#endif /* DATABASE_H */

