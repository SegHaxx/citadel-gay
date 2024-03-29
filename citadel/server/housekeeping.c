// This file contains miscellaneous housekeeping tasks.
//
// Copyright (c) 1987-2021 by the citadel.org team
//
// This program is open source software.  Use, duplication, or disclosure
// is subject to the terms of the GNU General Public License, version 3.


#include <stdio.h>
#include <libcitadel.h>

#include "ctdl_module.h"
#include "serv_extensions.h"
#include "room_ops.h"
#include "internet_addressing.h"
#include "config.h"
#include "journaling.h"
#include "citadel_ldap.h"

void check_sched_shutdown(void) {
	if ((ScheduledShutdown == 1) && (ContextList == NULL)) {
		syslog(LOG_NOTICE, "housekeeping: scheduled shutdown initiating");
		server_shutting_down = 1;
	}
}


// Check (and fix) floor reference counts.  This doesn't need to be done
// very often, since the counts should remain correct during normal operation.
void check_ref_counts_backend(struct ctdlroom *qrbuf, void *data) {

	int *new_refcounts;

	new_refcounts = (int *) data;

	++new_refcounts[(int)qrbuf->QRfloor];
}


void check_ref_counts(void) {
	struct floor flbuf;
	int a;

	int new_refcounts[MAXFLOORS];

	syslog(LOG_DEBUG, "housekeeping: checking floor reference counts");
	for (a=0; a<MAXFLOORS; ++a) {
		new_refcounts[a] = 0;
	}

	cdb_begin_transaction();
	CtdlForEachRoom(check_ref_counts_backend, (void *)new_refcounts );
	cdb_end_transaction();

	for (a=0; a<MAXFLOORS; ++a) {
		lgetfloor(&flbuf, a);
		flbuf.f_ref_count = new_refcounts[a];
		if (new_refcounts[a] > 0) {
			flbuf.f_flags = flbuf.f_flags | QR_INUSE;
		}
		else {
			flbuf.f_flags = flbuf.f_flags & ~QR_INUSE;
		}
		lputfloor(&flbuf, a);
		syslog(LOG_DEBUG, "housekeeping: floor %d has %d rooms", a, new_refcounts[a]);
	}
}


// Provide hints as to whether we have any memory leaks
void keep_an_eye_on_memory_usage(void) {
	static void *original_brk = NULL;
	if (!original_brk) original_brk = sbrk(0);	// Remember the original program break so we can test for leaks
	syslog(LOG_DEBUG, "original_brk=%lx, current_brk=%lx, addl=%ld", (long)original_brk, (long)sbrk(0), (long)(sbrk(0)-original_brk));
}


// This is the housekeeping loop.  Worker threads come through here after
// processing client requests but before jumping back into the pool.  We
// only allow housekeeping to execute once per minute, and we only allow one
// instance to run at a time.
static int housekeeping_in_progress = 0;
static int housekeeping_disabled = 0;
static time_t last_timer = 0L;

void do_housekeeping(void) {
	int do_housekeeping_now = 0;
	int do_perminute_housekeeping_now = 0;
	time_t now;

	if (housekeeping_disabled) {
		return;
	}

	// We do it this way instead of wrapping the whole loop in an
	// S_HOUSEKEEPING critical section because it eliminates the need to
	// potentially have multiple concurrent mutexes in progress.
	begin_critical_section(S_HOUSEKEEPING);
	if (housekeeping_in_progress == 0) {
		do_housekeeping_now = 1;
		housekeeping_in_progress = 1;
	}
	end_critical_section(S_HOUSEKEEPING);

	now = time(NULL);
	if ( (do_housekeeping_now == 0) && (!CtdlIsSingleUser()) && ((now - last_timer) > (time_t)3600) ) {
		if ( (now - last_timer) > (time_t)3600 ) {
			syslog(LOG_WARNING,
				"housekeeping: WARNING: housekeeping loop has not run for %ld minutes.  Is something stuck?",
				((now - last_timer) / 60)
			);
		}
		return;
	}

	// Ok, at this point we've made the decision to run the housekeeping
	// loop.  Everything below this point is real work.

	if ( (now - last_timer) > (time_t)60 ) {
		do_perminute_housekeeping_now = 1;
		last_timer = time(NULL);
	}

	// First, do the "as often as needed" stuff...
	JournalRunQueue();
	PerformSessionHooks(EVT_HOUSE);

	// Then, do the "once per minute" stuff...
	if (do_perminute_housekeeping_now) {
		cdb_check_handles();
		PerformSessionHooks(EVT_TIMER);		// Run all registered TIMER hooks

		// LDAP sync isn't in a module so we can put it here
		static time_t last_ldap_sync = 0L;
		if ( (now - last_ldap_sync) > (time_t)CtdlGetConfigLong("c_ldap_sync_freq") ) {
			CtdlSynchronizeUsersFromLDAP();
			last_ldap_sync = time(NULL);
		}

	keep_an_eye_on_memory_usage();
	}

	// All done.
	begin_critical_section(S_HOUSEKEEPING);
	housekeeping_in_progress = 0;
	end_critical_section(S_HOUSEKEEPING);
}


void CtdlDisableHouseKeeping(void) {
	syslog(LOG_INFO, "housekeeping: trying to disable");
	while ( (!housekeeping_disabled) && (!server_shutting_down) && (!housekeeping_in_progress) ) {

		if (housekeeping_in_progress) {
			sleep(1);
		}
		else {
			begin_critical_section(S_HOUSEKEEPING);
			if (!housekeeping_in_progress) {
				housekeeping_disabled = 1;
			}
			end_critical_section(S_HOUSEKEEPING);
		}
	}
	syslog(LOG_INFO, "housekeeping: disabled now");
}


void CtdlEnableHouseKeeping(void) {
	begin_critical_section(S_HOUSEKEEPING);
	housekeeping_in_progress = 0;
	end_critical_section(S_HOUSEKEEPING);
}
