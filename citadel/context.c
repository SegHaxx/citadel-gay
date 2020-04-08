/*
 * Citadel context management stuff.
 * Here's where we (hopefully) have all the code that manipulates contexts.
 *
 * Copyright (c) 1987-2020 by the citadel.org team
 *
 * This program is open source software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include "ctdl_module.h"
#include "serv_extensions.h"
#include "citserver.h"
#include "user_ops.h"
#include "locate_host.h"
#include "context.h"
#include "control.h"
#include "config.h"

pthread_key_t MyConKey;				/* TSD key for MyContext() */
CitContext masterCC;
CitContext *ContextList = NULL;
time_t last_purge = 0;				/* Last dead session purge */
int num_sessions = 0;				/* Current number of sessions */
int next_pid = 0;

/* Flag for single user mode */
static int want_single_user = 0;

/* Try to go single user */

int CtdlTrySingleUser(void)
{
	int can_do = 0;
	
	begin_critical_section(S_SINGLE_USER);
	if (want_single_user)
		can_do = 0;
	else
	{
		can_do = 1;
		want_single_user = 1;
	}
	end_critical_section(S_SINGLE_USER);
	return can_do;
}


void CtdlEndSingleUser(void)
{
	begin_critical_section(S_SINGLE_USER);
	want_single_user = 0;
	end_critical_section(S_SINGLE_USER);
}


int CtdlWantSingleUser(void)
{
	return want_single_user;
}


int CtdlIsSingleUser(void)
{
	if (want_single_user)
	{
		/* check for only one context here */
		if (num_sessions == 1)
			return 1;
	}
	return 0;
}


/*
 * Locate a context by its session number and terminate it if the user is able.
 * User can NOT terminate their current session.
 * User CAN terminate any other session that has them logged in.
 * Aide CAN terminate any session except the current one.
 */
int CtdlTerminateOtherSession (int session_num)
{
	struct CitContext *CCC = CC;
	int ret = 0;
	CitContext *ccptr;
	int aide;

	if (session_num == CCC->cs_pid) return TERM_NOTALLOWED;

	aide = ( (CCC->user.axlevel >= AxAideU) || (CCC->internal_pgm) ) ;

	syslog(LOG_DEBUG, "context: locating session to kill");
	begin_critical_section(S_SESSION_TABLE);
	for (ccptr = ContextList; ccptr != NULL; ccptr = ccptr->next) {
		if (session_num == ccptr->cs_pid) {
			ret |= TERM_FOUND;
			if ((ccptr->user.usernum == CCC->user.usernum) || aide) {
				ret |= TERM_ALLOWED;
			}
			break;
		}
	}

	if (((ret & TERM_FOUND) != 0) && ((ret & TERM_ALLOWED) != 0))
	{
		if (ccptr->user.usernum == CCC->user.usernum)
			ccptr->kill_me = KILLME_ADMIN_TERMINATE;
		else
			ccptr->kill_me = KILLME_IDLE;
		end_critical_section(S_SESSION_TABLE);
	}
	else
		end_critical_section(S_SESSION_TABLE);

	return ret;
}



/*
 * Check to see if the user who we just sent mail to is logged in.  If yes,
 * bump the 'new mail' counter for their session.  That enables them to
 * receive a new mail notification without having to hit the database.
 */
void BumpNewMailCounter(long which_user) 
{
	CtdlBumpNewMailCounter(which_user);
}

void CtdlBumpNewMailCounter(long which_user)
{
	CitContext *ptr;

	begin_critical_section(S_SESSION_TABLE);

	for (ptr = ContextList; ptr != NULL; ptr = ptr->next) {
		if (ptr->user.usernum == which_user) {
			ptr->newmail += 1;
		}
	}

	end_critical_section(S_SESSION_TABLE);
}


/*
 * Check to see if a user is currently logged in
 * Take care with what you do as a result of this test.
 * The user may not have been logged in when this function was called BUT
 * because of threading the user might be logged in before you test the result.
 */
int CtdlIsUserLoggedIn (char *user_name)
{
	CitContext *cptr;
	int ret = 0;

	begin_critical_section (S_SESSION_TABLE);
	for (cptr = ContextList; cptr != NULL; cptr = cptr->next) {
		if (!strcasecmp(cptr->user.fullname, user_name)) {
			ret = 1;
			break;
		}
	}
	end_critical_section(S_SESSION_TABLE);
	return ret;
}



/*
 * Check to see if a user is currently logged in.
 * Basically same as CtdlIsUserLoggedIn() but uses the user number instead.
 * Take care with what you do as a result of this test.
 * The user may not have been logged in when this function was called BUT
 * because of threading the user might be logged in before you test the result.
 */
int CtdlIsUserLoggedInByNum (long usernum)
{
	CitContext *cptr;
	int ret = 0;

	begin_critical_section(S_SESSION_TABLE);
	for (cptr = ContextList; cptr != NULL; cptr = cptr->next) {
		if (cptr->user.usernum == usernum) {
			ret = 1;
		}
	}
	end_critical_section(S_SESSION_TABLE);
	return ret;
}



/*
 * Return a pointer to the CitContext structure bound to the thread which
 * called this function.  If there's no such binding (for example, if it's
 * called by the housekeeper thread) then a generic 'master' CC is returned.
 *
 * This function is used *VERY* frequently and must be kept small.
 */
CitContext *MyContext(void) {
	register CitContext *c;
	return ((c = (CitContext *) pthread_getspecific(MyConKey), c == NULL) ? &masterCC : c);
}




/*
 * Terminate idle sessions.  This function pounds through the session table
 * comparing the current time to each session's time-of-last-command.  If an
 * idle session is found it is terminated, then the search restarts at the
 * beginning because the pointer to our place in the list becomes invalid.
 */
void terminate_idle_sessions(void)
{
	CitContext *ccptr;
	time_t now;
	int killed = 0;
	int longrunners = 0;

	now = time(NULL);
	begin_critical_section(S_SESSION_TABLE);
	for (ccptr = ContextList; ccptr != NULL; ccptr = ccptr->next) {
		if (
			(ccptr != CC)
	   		&& (CtdlGetConfigLong("c_sleeping") > 0)
	   		&& (now - (ccptr->lastcmd) > CtdlGetConfigLong("c_sleeping"))
		) {
			if (!ccptr->dont_term) {
				ccptr->kill_me = KILLME_IDLE;
				++killed;
			}
			else {
				++longrunners;
			}
		}
	}
	end_critical_section(S_SESSION_TABLE);
	if (killed > 0) {
		syslog(LOG_INFO, "context: scheduled %d idle sessions for termination", killed);
	}
	if (longrunners > 0) {
		syslog(LOG_INFO, "context: did not terminate %d protected idle sessions", longrunners);
	}
}


/*
 * During shutdown, close the sockets of any sessions still connected.
 */
void terminate_all_sessions(void)
{
	CitContext *ccptr;
	int killed = 0;

	begin_critical_section(S_SESSION_TABLE);
	for (ccptr = ContextList; ccptr != NULL; ccptr = ccptr->next) {
		if (ccptr->client_socket != -1)
		{
			syslog(LOG_INFO, "context: terminate_all_sessions() is murdering %s CC[%d]", ccptr->curr_user, ccptr->cs_pid);
			close(ccptr->client_socket);
			ccptr->client_socket = -1;
			killed++;
		}
	}
	end_critical_section(S_SESSION_TABLE);
	if (killed > 0) {
		syslog(LOG_INFO, "context: flushed %d stuck sessions", killed);
	}
}



/*
 * Terminate a session.
 */
void RemoveContext (CitContext *con)
{
	const char *c;
	if (con == NULL) {
		syslog(LOG_ERR, "context: RemoveContext() called with NULL, this should not happen");
		return;
	}
	c = con->ServiceName;
	if (c == NULL) {
		c = "WTF?";
	}
	syslog(LOG_DEBUG, "context: RemoveContext(%s) session %d", c, con->cs_pid);

	/* Run any cleanup routines registered by loadable modules.
	 * Note: We have to "become_session()" because the cleanup functions
	 *       might make references to "CC" assuming it's the right one.
	 */
	become_session(con);
	CtdlUserLogout();
	PerformSessionHooks(EVT_STOP);
	client_close();				/* If the client is still connected, blow 'em away. */
	become_session(NULL);

	syslog(LOG_INFO, "context: [%3d]SRV[%s] Session ended.", con->cs_pid, c);

	/* 
	 * If the client is still connected, blow 'em away. 
	 * if the socket is 0 or -1, its already gone or was never there.
	 */
	if (con->client_socket > 0)
	{
		syslog(LOG_INFO, "context: closing socket %d", con->client_socket);
		close(con->client_socket);
	}

	/* If using AUTHMODE_LDAP, free the DN */
	if (con->ldap_dn) {
		free(con->ldap_dn);
		con->ldap_dn = NULL;
	}
	FreeStrBuf(&con->StatusMessage);
	FreeStrBuf(&con->MigrateBuf);
	FreeStrBuf(&con->RecvBuf.Buf);
	if (con->cached_msglist) {
		free(con->cached_msglist);
	}

	syslog(LOG_DEBUG, "context: done with RemoveContext()");
}



/*
 * Initialize a new context and place it in the list.  The session number
 * used to be the PID (which is why it's called cs_pid), but that was when we
 * had one process per session.  Now we just assign them sequentially, starting
 * at 1 (don't change it to 0 because masterCC uses 0).
 */
CitContext *CreateNewContext(void) {
	CitContext *me;

	me = (CitContext *) malloc(sizeof(CitContext));
	if (me == NULL) {
		syslog(LOG_ERR, "citserver: malloc() failed: %m");
		return NULL;
	}
	memset(me, 0, sizeof(CitContext));

	/* Give the context a name. Hopefully makes it easier to track */
	strcpy (me->user.fullname, "SYS_notauth");
	
	/* The new context will be created already in the CON_EXECUTING state
	 * in order to prevent another thread from grabbing it while it's
	 * being set up.
	 */
	me->state = CON_EXECUTING;
	/*
	 * Generate a unique session number and insert this context into
	 * the list.
	 */
	me->MigrateBuf = NewStrBuf();

	me->RecvBuf.Buf = NewStrBuf();

	me->lastcmd = time(NULL);	/* set lastcmd to now to prevent idle timer infanticide TODO: if we have a valid IO, use that to set the timer. */


	begin_critical_section(S_SESSION_TABLE);
	me->cs_pid = ++next_pid;
	me->prev = NULL;
	me->next = ContextList;
	ContextList = me;
	if (me->next != NULL) {
		me->next->prev = me;
	}
	++num_sessions;
	end_critical_section(S_SESSION_TABLE);
	return (me);
}


/*
 * Initialize a new context and place it in the list.  The session number
 * used to be the PID (which is why it's called cs_pid), but that was when we
 * had one process per session.  Now we just assign them sequentially, starting
 * at 1 (don't change it to 0 because masterCC uses 0).
 */
CitContext *CloneContext(CitContext *CloneMe) {
	CitContext *me;

	me = (CitContext *) malloc(sizeof(CitContext));
	if (me == NULL) {
		syslog(LOG_ERR, "citserver: malloc() failed: %m");
		return NULL;
	}
	memcpy(me, CloneMe, sizeof(CitContext));

	memset(&me->RecvBuf, 0, sizeof(IOBuffer));
	memset(&me->SendBuf, 0, sizeof(IOBuffer));
	memset(&me->SBuf, 0, sizeof(IOBuffer));
	me->MigrateBuf = NULL;
	me->sMigrateBuf = NULL;
	me->redirect_buffer = NULL;
#ifdef HAVE_OPENSSL
	me->ssl = NULL;
#endif

	me->download_fp = NULL;
	me->upload_fp = NULL;
	/// TODO: what about the room/user?
	me->ma = NULL;
	me->openid_data = NULL;
	me->ldap_dn = NULL;
	me->session_specific_data = NULL;
	
	me->CIT_ICAL = NULL;

	me->cached_msglist = NULL;
	me->download_fp = NULL;
	me->upload_fp = NULL;
	me->client_socket = 0;

	me->MigrateBuf = NewStrBuf();
	me->RecvBuf.Buf = NewStrBuf();
	
	begin_critical_section(S_SESSION_TABLE);
	{
		me->cs_pid = ++next_pid;
		me->prev = NULL;
		me->next = ContextList;
		me->lastcmd = time(NULL);	/* set lastcmd to now to prevent idle timer infanticide */
		ContextList = me;
		if (me->next != NULL) {
			me->next->prev = me;
		}
		++num_sessions;
	}
	end_critical_section(S_SESSION_TABLE);
	return (me);
}


/*
 * Return an array containing a copy of the context list.
 * This allows worker threads to perform "for each context" operations without
 * having to lock and traverse the live list.
 */
CitContext *CtdlGetContextArray(int *count)
{
	int nContexts, i;
	CitContext *nptr, *cptr;
	
	nContexts = num_sessions;
	nptr = malloc(sizeof(CitContext) * nContexts);
	if (!nptr) {
		*count = 0;
		return NULL;
	}
	begin_critical_section(S_SESSION_TABLE);
	for (cptr = ContextList, i=0; cptr != NULL && i < nContexts; cptr = cptr->next, i++) {
		memcpy(&nptr[i], cptr, sizeof (CitContext));
	}
	end_critical_section (S_SESSION_TABLE);
	
	*count = i;
	return nptr;
}



/*
 * Back-end function for starting a session
 */
void begin_session(CitContext *con)
{
	/* 
	 * Initialize some variables specific to our context.
	 */
	con->logged_in = 0;
	con->internal_pgm = 0;
	con->download_fp = NULL;
	con->upload_fp = NULL;
	con->cached_msglist = NULL;
	con->cached_num_msgs = 0;
	con->FirstExpressMessage = NULL;
	time(&con->lastcmd);
	time(&con->lastidle);
	strcpy(con->lastcmdname, "    ");
	strcpy(con->cs_clientname, "(unknown)");
	strcpy(con->curr_user, NLI);
	*con->cs_clientinfo = '\0';
	safestrncpy(con->cs_host, CtdlGetConfigStr("c_fqdn"), sizeof con->cs_host);
	safestrncpy(con->cs_addr, "", sizeof con->cs_addr);
	con->cs_UDSclientUID = -1;
	con->cs_host[sizeof con->cs_host - 1] = 0;
	if (!CC->is_local_client) {
		locate_host(con->cs_host, sizeof con->cs_host,
			con->cs_addr, sizeof con->cs_addr,
			con->client_socket
		);
	}
	else {
		con->cs_host[0] = 0;
		con->cs_addr[0] = 0;
#ifdef HAVE_STRUCT_UCRED
		{
			/* as http://www.wsinnovations.com/softeng/articles/uds.html told us... */
			struct ucred credentials;
			socklen_t ucred_length = sizeof(struct ucred);
			
			/*fill in the user data structure */
			if(getsockopt(con->client_socket, SOL_SOCKET, SO_PEERCRED, &credentials, &ucred_length)) {
				syslog(LOG_ERR, "context: could obtain credentials from unix domain socket");
				
			}
			else {		
				/* the process ID of the process on the other side of the socket */
				/* credentials.pid; */
				
				/* the effective UID of the process on the other side of the socket  */
				con->cs_UDSclientUID = credentials.uid;
				
				/* the effective primary GID of the process on the other side of the socket */
				/* credentials.gid; */
				
				/* To get supplemental groups, we will have to look them up in our account
				   database, after a reverse lookup on the UID to get the account name.
				   We can take this opportunity to check to see if this is a legit account.
				*/
				snprintf(con->cs_clientinfo, sizeof(con->cs_clientinfo),
					 "PID: "F_PID_T"; UID: "F_UID_T"; GID: "F_XPID_T" ", 
					 credentials.pid,
					 credentials.uid,
					 credentials.gid);
			}
		}
#endif
	}
	con->cs_flags = 0;

	con->nologin = 0;
	if (((CtdlGetConfigInt("c_maxsessions") > 0)&&(num_sessions > CtdlGetConfigInt("c_maxsessions"))) || CtdlWantSingleUser()) {
		con->nologin = 1;
	}

	syslog(LOG_INFO, "context: session (%s) started from %s (%s) uid=%d",
		con->ServiceName, con->cs_host, con->cs_addr, con->cs_UDSclientUID
	);

	/* Run any session startup routines registered by loadable modules */
	PerformSessionHooks(EVT_START);
}


/*
 * This function fills in a context and its user field correctly
 * Then creates/loads that user
 */
void CtdlFillSystemContext(CitContext *context, char *name)
{
	char sysname[SIZ];
	long len;

	memset(context, 0, sizeof(CitContext));
	context->internal_pgm = 1;
	context->cs_pid = 0;
	strcpy (sysname, "SYS_");
	strcat (sysname, name);
	len = strlen(sysname);
	memcpy(context->curr_user, sysname, len + 1);
	context->client_socket = (-1);
	context->state = CON_SYS;
	context->ServiceName = name;

	/* internal_create_user has the side effect of loading the user regardless of whether they
	 * already existed or needed to be created
	 */
	internal_create_user(sysname, &(context->user), -1) ;
	
	/* Check to see if the system user needs upgrading */
	if (context->user.usernum == 0)
	{	/* old system user with number 0, upgrade it */
		context->user.usernum = get_new_user_number();
		syslog(LOG_INFO, "context: upgrading system user \"%s\" from user number 0 to user number %ld", context->user.fullname, context->user.usernum);
		/* add user to the database */
		CtdlPutUser(&(context->user));
		cdb_store(CDB_USERSBYNUMBER, &(context->user.usernum), sizeof(long), context->user.fullname, strlen(context->user.fullname)+1);
	}
}


/*
 * Cleanup any contexts that are left lying around
 */
void context_cleanup(void)
{
	CitContext *ptr = NULL;
	CitContext *rem = NULL;

	/*
	 * Clean up the contexts.
	 * There are no threads so no critical_section stuff is needed.
	 */
	ptr = ContextList;
	
	/* We need to update the ContextList because some modules may want to itterate it
	 * Question is should we NULL it before iterating here or should we just keep updating it
	 * as we remove items?
	 *
	 * Answer is to NULL it first to prevent modules from doing any actions on the list at all
	 */
	ContextList=NULL;
	while (ptr != NULL){
		/* Remove the session from the active list */
		rem = ptr->next;
		--num_sessions;

		syslog(LOG_DEBUG, "context: context_cleanup() purging session %d", ptr->cs_pid);
		RemoveContext(ptr);
		free (ptr);
		ptr = rem;
	}
}



/*
 * Purge all sessions which have the 'kill_me' flag set.
 * This function has code to prevent it from running more than once every
 * few seconds, because running it after every single unbind would waste a lot
 * of CPU time and keep the context list locked too much.  To force it to run
 * anyway, set "force" to nonzero.
 */
void dead_session_purge(int force) {
	CitContext *ptr, *ptr2;		/* general-purpose utility pointer */
	CitContext *rem = NULL;		/* list of sessions to be destroyed */
	
	if (force == 0) {
		if ( (time(NULL) - last_purge) < 5 ) {
			return;	/* Too soon, go away */
		}
	}
	time(&last_purge);

	if (try_critical_section(S_SESSION_TABLE))
		return;
		
	ptr = ContextList;
	while (ptr) {
		ptr2 = ptr;
		ptr = ptr->next;
		
		if ( (ptr2->state == CON_IDLE) && (ptr2->kill_me) ) {
			/* Remove the session from the active list */
			if (ptr2->prev) {
				ptr2->prev->next = ptr2->next;
			}
			else {
				ContextList = ptr2->next;
			}
			if (ptr2->next) {
				ptr2->next->prev = ptr2->prev;
			}

			--num_sessions;
			/* And put it on our to-be-destroyed list */
			ptr2->next = rem;
			rem = ptr2;
		}
	}
	end_critical_section(S_SESSION_TABLE);

	/* Now that we no longer have the session list locked, we can take
	 * our time and destroy any sessions on the to-be-killed list, which
	 * is allocated privately on this thread's stack.
	 */
	while (rem != NULL) {
		syslog(LOG_DEBUG, "context: dead_session_purge() purging session %d, reason=%d", rem->cs_pid, rem->kill_me);
		RemoveContext(rem);
		ptr = rem;
		rem = rem->next;
		free(ptr);
	}
}





/*
 * masterCC is the context we use when not attached to a session.  This
 * function initializes it.
 */
void InitializeMasterCC(void) {
	memset(&masterCC, 0, sizeof(struct CitContext));
	masterCC.internal_pgm = 1;
	masterCC.cs_pid = 0;
}





/*
 * Set the "async waiting" flag for a session, if applicable
 */
void set_async_waiting(struct CitContext *ccptr)
{
	syslog(LOG_DEBUG, "context: setting async_waiting flag for session %d", ccptr->cs_pid);
	if (ccptr->is_async) {
		ccptr->async_waiting++;
		if (ccptr->state == CON_IDLE) {
			ccptr->state = CON_READY;
		}
	}
}


CTDL_MODULE_INIT(session)
{
	if (!threading) {
	}
	return "session";
}
