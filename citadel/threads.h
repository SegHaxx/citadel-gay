
#ifndef THREADS_H
#define THREADS_H

#include "sysdep.h"

#ifdef HAVE_PTHREAD_H
#include <pthread.h>
#endif

#include <sys/time.h>
#include <string.h>

#include <db.h>

#include "server.h"
#include "sysdep_decls.h"

/*
 * Things we need to keep track of per-thread instead of per-session
 */
struct thread_tsd {
	DB_TXN *tid;            /* Transaction handle */
	DBC *cursors[MAXCDB];   /* Cursors, for traversals... */
};

extern pthread_key_t ThreadKey;
extern struct thread_tsd masterTSD;
#define TSD MyThread()

extern int num_workers;
extern int active_workers;
extern int server_shutting_down;

struct thread_tsd *MyThread(void);
int try_critical_section (int which_one);
void begin_critical_section (int which_one);
void end_critical_section (int which_one);
void go_threading(void);
void InitializeMasterTSD(void);
void CtdlThreadCreate(void *(*start_routine)(void*));


extern pthread_mutex_t ThreadCountMutex;;

#endif // THREADS_H
