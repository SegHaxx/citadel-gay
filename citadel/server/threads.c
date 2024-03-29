// Thread handling stuff for the Citadel server
//
// Copyright (c) 1987-2023 by the citadel.org team
//
// This program is open source software.  Use, duplication, or disclosure
// is subject to the terms of the GNU General Public License, version 3.

#include <errno.h>
#include <stdio.h>
#include <syslog.h>
#include <libcitadel.h>
#include "modules_init.h"
#include "serv_extensions.h"
#include "ctdl_module.h"
#include "config.h"
#include "context.h"
#include "threads.h"

int num_workers = 0;				// Current number of worker threads
int active_workers = 0;				// Number of ACTIVE worker threads
pthread_key_t ThreadKey;
pthread_mutex_t Critters[MAX_SEMAPHORES];	// Things needing locking
struct thread_tsd masterTSD;
int server_shutting_down = 0;			// set to nonzero during shutdown
pthread_mutex_t ThreadCountMutex;

void InitializeSemaphores(void) {
	int i;

	// Set up a bunch of semaphores to be used for critical sections
	for (i=0; i<MAX_SEMAPHORES; ++i) {
		pthread_mutex_init(&Critters[i], NULL);
	}
}


// Obtain a semaphore lock to begin a critical section, but only if no one else has one
int try_critical_section(int which_one) {
	// For all types of critical sections except those listed here,
	// ensure nobody ever tries to do a critical section within a
	// transaction; this could lead to deadlock.
	if (	(which_one != S_FLOORCACHE)
		&& (which_one != S_NETCONFIGS)
	) {
		cdb_check_handles();
	}
	return (pthread_mutex_trylock(&Critters[which_one]));
}


// Obtain a semaphore lock to begin a critical section.
void begin_critical_section(int which_one) {
	// For all types of critical sections except those listed here,
	// ensure nobody ever tries to do a critical section within a
	// transaction; this could lead to deadlock.
	if (	(which_one != S_FLOORCACHE)
		&& (which_one != S_NETCONFIGS)
	) {
		cdb_check_handles();
	}
	pthread_mutex_lock(&Critters[which_one]);
}


// Release a semaphore lock to end a critical section.
void end_critical_section(int which_one) {
	pthread_mutex_unlock(&Critters[which_one]);
}


// Return a pointer to our thread-specific (not session-specific) data.
struct thread_tsd *MyThread(void) {
        struct thread_tsd *c = (struct thread_tsd *) pthread_getspecific(ThreadKey) ;
	if (!c) {
		return &masterTSD;
	}
	return c;
}


// Called by CtdlThreadCreate()
// We have to pass through here before starting our thread in order to create a set of data
// that is thread-specific rather than session-specific.
void *CTC_backend(void *supplied_start_routine) {
	struct thread_tsd *mytsd;
	void *(*start_routine)(void*) = supplied_start_routine;

	mytsd = (struct thread_tsd *) malloc(sizeof(struct thread_tsd));
	memset(mytsd, 0, sizeof(struct thread_tsd));
	pthread_setspecific(ThreadKey, (const void *) mytsd);

	start_routine(NULL);
	// free(mytsd);
	return(NULL);
}

 
// Function to create a thread.
void CtdlThreadCreate(void *(*start_routine)(void*)) {
	pthread_t thread;
	pthread_attr_t attr;
	int ret = 0;

	ret = pthread_attr_init(&attr);
	ret = pthread_attr_setstacksize(&attr, THREADSTACKSIZE);
	ret = pthread_create(&thread, &attr, CTC_backend, (void *)start_routine);
	if (ret != 0) syslog(LOG_ERR, "pthread_create() : %m");
}


void InitializeMasterTSD(void) {
	memset(&masterTSD, 0, sizeof(struct thread_tsd));
}


// Initialize the thread system
void go_threading(void) {
	pthread_mutex_init(&ThreadCountMutex, NULL);

	// Second call to module init functions now that threading is up
	initialize_modules(1);

	// Begin with one worker thread.  We will expand the pool if necessary
	CtdlThreadCreate(worker_thread);

	// The supervisor thread monitors worker threads and spawns more of them if it finds that they are all in use.
	while (!server_shutting_down) {
		if ((active_workers == num_workers) && (num_workers < CtdlGetConfigInt("c_max_workers"))) {
			CtdlThreadCreate(worker_thread);
		}
		usleep(1000000);
	}

	// When we get to this point we are getting ready to shut down our Citadel server
	terminate_all_sessions();		// close all client sockets
	CtdlShutdownServiceHooks();		// close all listener sockets to prevent new connections
	PerformSessionHooks(EVT_SHUTDOWN);	// run any registered shutdown hooks

	// We used to wait for all threads to exit.  Fuck that.  The only thing important is that the databases are
	// cleanly unmounted.  After that, exit the whole program.
}
