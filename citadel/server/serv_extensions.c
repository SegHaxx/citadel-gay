// This began as a framework written by Brian Costello (btx) that loaded server extensions as dynamic modules.
// We don't do it that way anymore but the concept lives on as a high degree of modularity in the server.
// The functions in this file handle registration and execution of the server hooks used by static linked modules.
//
// Copyright (c) 1987-2021 by the citadel.org team
//
// This program is open source software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License, version 3.

#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/stat.h>
#include <libcitadel.h>
#include "sysdep_decls.h"
#include "modules/crypto/serv_crypto.h"	/* Needed until a universal crypto startup hook is implimented for CtdlStartTLS */
#include "serv_extensions.h"
#include "ctdl_module.h"
#include "config.h"


/*
 * Structure defentitions for hook tables
 */

typedef struct FixedOutputHook FixedOutputHook;
struct FixedOutputHook {
	FixedOutputHook *next;
	char content_type[64];
	void (*h_function_pointer) (char *, int);
};
FixedOutputHook *FixedOutputTable = NULL;


/*
 * SessionFunctionHook extensions are used for any type of hook for which
 * the context in which it's being called (which is determined by the event
 * type) will make it obvious for the hook function to know where to look for
 * pertinent data.
 */
typedef struct SessionFunctionHook SessionFunctionHook;
struct SessionFunctionHook {
	SessionFunctionHook *next;
	int Priority;
	void (*h_function_pointer) (void);
	int eventtype;
};
SessionFunctionHook *SessionHookTable = NULL;


/*
 * UserFunctionHook extensions are used for any type of hook which implements
 * an operation on a user or username (potentially) other than the one
 * operating the current session.
 */
typedef struct UserFunctionHook UserFunctionHook;
struct UserFunctionHook {
	UserFunctionHook *next;
	void (*h_function_pointer) (struct ctdluser *usbuf);
	int eventtype;
};
UserFunctionHook *UserHookTable = NULL;


/*
 * MessageFunctionHook extensions are used for hooks which implement handlers
 * for various types of message operations (save, read, etc.)
 */
typedef struct MessageFunctionHook MessageFunctionHook;
struct MessageFunctionHook {
	MessageFunctionHook *next;
	int (*h_function_pointer) (struct CtdlMessage *msg, struct recptypes *recps);
	int eventtype;
};
MessageFunctionHook *MessageHookTable = NULL;


/*
 * DeleteFunctionHook extensions are used for hooks which get called when a
 * message is about to be deleted.
 */
typedef struct DeleteFunctionHook DeleteFunctionHook;
struct DeleteFunctionHook {
	DeleteFunctionHook *next;
	void (*h_function_pointer) (char *target_room, long msgnum);
};
DeleteFunctionHook *DeleteHookTable = NULL;


/*
 * ExpressMessageFunctionHook extensions are used for hooks which implement
 * the sending of an instant message through various channels.  Any function
 * registered should return the number of recipients to whom the message was
 * successfully transmitted.
 */
typedef struct XmsgFunctionHook XmsgFunctionHook;
struct XmsgFunctionHook {
	XmsgFunctionHook *next;
	int (*h_function_pointer) (char *, char *, char *, char *);
	int order;
};
XmsgFunctionHook *XmsgHookTable = NULL;


/*
 * RoomFunctionHook extensions are used for hooks which impliment room
 * processing functions when new messages are added.
 */
typedef struct RoomFunctionHook RoomFunctionHook;
struct RoomFunctionHook {
	RoomFunctionHook *next;
	int (*fcn_ptr) (struct ctdlroom *);
};
RoomFunctionHook *RoomHookTable = NULL;


typedef struct SearchFunctionHook SearchFunctionHook;
struct SearchFunctionHook {
	SearchFunctionHook *next;
	void (*fcn_ptr) (int *, long **, const char *);
	char *name;
};
SearchFunctionHook *SearchFunctionHookTable = NULL;

ServiceFunctionHook *ServiceHookTable = NULL;

typedef struct ProtoFunctionHook ProtoFunctionHook;
struct ProtoFunctionHook {
	void (*handler) (char *cmdbuf);
	const char *cmd;
	const char *desc;
};

HashList *ProtoHookList = NULL;


#define ERR_PORT (1 << 1)


static StrBuf *portlist = NULL;
static StrBuf *errormessages = NULL;


long   DetailErrorFlags;
ConstStr Empty = {HKEY("")};
char *ErrSubject = "Startup Problems";
ConstStr ErrGeneral[] = {
	{HKEY("Citadel had trouble on starting up. ")},
	{HKEY(" This means, Citadel won't be the service provider for a specific service you configured it to.\n\n"
	      "If you don't want Citadel to provide these services, turn them off in WebCit via: ")},
	{HKEY("To make both ways actualy take place restart the citserver with \"sendcommand down\"\n\n"
	      "The errors returned by the system were:\n")},
	{HKEY("You can recheck the above if you follow this faq item:\n"
	      "http://www.citadel.org/doku.php?id=faq:mastering_your_os:net#netstat")}
};

ConstStr ErrPortShort = { HKEY("We couldn't bind all ports you configured to be provided by Citadel Server.\n")};
ConstStr ErrPortWhere = { HKEY("\"Admin->System Preferences->Network\".\n\nThe failed ports and sockets are: ")};
ConstStr ErrPortHint  = { HKEY("If you want Citadel to provide you with that functionality, "
			       "check the output of \"netstat -lnp\" on Linux, or \"netstat -na\" on BSD"
			       " and disable the program that binds these ports.\n")};


void LogPrintMessages(long err) {
	StrBuf *Message;
	StrBuf *List, *DetailList;
	ConstStr *Short, *Where, *Hint; 

	
	Message = NewStrBufPlain(NULL, StrLength(portlist) + StrLength(errormessages));
	
	DetailErrorFlags = DetailErrorFlags & ~err;

	switch (err)
	{
	case ERR_PORT:
		Short = &ErrPortShort;
		Where = &ErrPortWhere;
		Hint  = &ErrPortHint;
		List  = portlist;
		DetailList = errormessages;
		break;
	default:
		Short = &Empty;
		Where = &Empty;
		Hint  = &Empty;
		List  = NULL;
		DetailList = NULL;
	}

	StrBufAppendBufPlain(Message, CKEY(ErrGeneral[0]), 0);
	StrBufAppendBufPlain(Message, CKEY(*Short), 0);	
	StrBufAppendBufPlain(Message, CKEY(ErrGeneral[1]), 0);
	StrBufAppendBufPlain(Message, CKEY(*Where), 0);
	StrBufAppendBuf(Message, List, 0);
	StrBufAppendBufPlain(Message, HKEY("\n\n"), 0);
	StrBufAppendBufPlain(Message, CKEY(*Hint), 0);
	StrBufAppendBufPlain(Message, HKEY("\n\n"), 0);
	StrBufAppendBufPlain(Message, CKEY(ErrGeneral[2]), 0);
	StrBufAppendBuf(Message, DetailList, 0);
	StrBufAppendBufPlain(Message, HKEY("\n\n"), 0);
	StrBufAppendBufPlain(Message, CKEY(ErrGeneral[3]), 0);

	syslog(LOG_ERR, "extensions: %s", ChrPtr(Message));
	syslog(LOG_ERR, "extensions: %s", ErrSubject);
	quickie_message("Citadel", NULL, NULL, AIDEROOM, ChrPtr(Message), FMT_FIXED, ErrSubject);

	FreeStrBuf(&Message);
	FreeStrBuf(&List);
	FreeStrBuf(&DetailList);
}


void AddPortError(char *Port, char *ErrorMessage) {
	long len;

	DetailErrorFlags |= ERR_PORT;

	len = StrLength(errormessages);
	if (len > 0) StrBufAppendBufPlain(errormessages, HKEY("; "), 0);
	else errormessages = NewStrBuf();
	StrBufAppendBufPlain(errormessages, ErrorMessage, -1, 0);


	len = StrLength(portlist);
	if (len > 0) StrBufAppendBufPlain(portlist, HKEY(";"), 0);
	else portlist = NewStrBuf();
	StrBufAppendBufPlain(portlist, Port, -1, 0);
}


int DLoader_Exec_Cmd(char *cmdbuf) {
	void *vP;
	ProtoFunctionHook *p;

	if (GetHash(ProtoHookList, cmdbuf, 4, &vP) && (vP != NULL)) {
		p = (ProtoFunctionHook*) vP;
		p->handler(&cmdbuf[5]);
		return 1;
	}
	return 0;
}


void CtdlRegisterProtoHook(void (*handler) (char *), char *cmd, char *desc) {
	ProtoFunctionHook *p;

	if (ProtoHookList == NULL)
		ProtoHookList = NewHash (1, FourHash);


	p = (ProtoFunctionHook *)
		malloc(sizeof(ProtoFunctionHook));

	if (p == NULL) {
		fprintf(stderr, "can't malloc new ProtoFunctionHook\n");
		exit(EXIT_FAILURE);
	}
	p->handler = handler;
	p->cmd = cmd;
	p->desc = desc;

	Put(ProtoHookList, cmd, 4, p, NULL);
	syslog(LOG_DEBUG, "extensions: registered server command %s (%s)", cmd, desc);
}


void CtdlRegisterSessionHook(void (*fcn_ptr) (void), int EventType, int Priority)
{
	SessionFunctionHook *newfcn;

	newfcn = (SessionFunctionHook *)
	    malloc(sizeof(SessionFunctionHook));
	newfcn->Priority = Priority;
	newfcn->h_function_pointer = fcn_ptr;
	newfcn->eventtype = EventType;

	SessionFunctionHook **pfcn;
	pfcn = &SessionHookTable;
	while ((*pfcn != NULL) && 
	       ((*pfcn)->Priority < newfcn->Priority) &&
	       ((*pfcn)->next != NULL))
		pfcn = &(*pfcn)->next;
		
	newfcn->next = *pfcn;
	*pfcn = newfcn;
	
	syslog(LOG_DEBUG, "extensions: registered a new session function (type %d Priority %d)", EventType, Priority);
}


void CtdlUnregisterSessionHook(void (*fcn_ptr) (void), int EventType)
{
	SessionFunctionHook *cur, *p, *last;
	last = NULL;
	cur = SessionHookTable;
	while  (cur != NULL) {
		if ((fcn_ptr == cur->h_function_pointer) &&
		    (EventType == cur->eventtype))
		{
			syslog(LOG_DEBUG, "extensions: unregistered session function (type %d)", EventType);
			p = cur->next;

			free(cur);
			cur = NULL;

			if (last != NULL)
				last->next = p;
			else 
				SessionHookTable = p;
			cur = p;
		}
		else {
			last = cur;
			cur = cur->next;
		}
	}
}


void CtdlRegisterUserHook(void (*fcn_ptr) (ctdluser *), int EventType)
{

	UserFunctionHook *newfcn;

	newfcn = (UserFunctionHook *)
	    malloc(sizeof(UserFunctionHook));
	newfcn->next = UserHookTable;
	newfcn->h_function_pointer = fcn_ptr;
	newfcn->eventtype = EventType;
	UserHookTable = newfcn;

	syslog(LOG_DEBUG, "extensions: registered a new user function (type %d)",
		   EventType);
}


void CtdlUnregisterUserHook(void (*fcn_ptr) (struct ctdluser *), int EventType)
{
	UserFunctionHook *cur, *p, *last;
	last = NULL;
	cur = UserHookTable;
	while (cur != NULL) {
		if ((fcn_ptr == cur->h_function_pointer) &&
		    (EventType == cur->eventtype))
		{
			syslog(LOG_DEBUG, "extensions: unregistered user function (type %d)", EventType);
			p = cur->next;

			free(cur);
			cur = NULL;

			if (last != NULL)
				last->next = p;
			else 
				UserHookTable = p;
			cur = p;
		}
		else {
			last = cur;
			cur = cur->next;
		}
	}
}


void CtdlRegisterMessageHook(int (*handler)(struct CtdlMessage *, struct recptypes *), int EventType)
{

	MessageFunctionHook *newfcn;

	newfcn = (MessageFunctionHook *)
	    malloc(sizeof(MessageFunctionHook));
	newfcn->next = MessageHookTable;
	newfcn->h_function_pointer = handler;
	newfcn->eventtype = EventType;
	MessageHookTable = newfcn;

	syslog(LOG_DEBUG, "extensions: registered a new message function (type %d)", EventType);
}


void CtdlUnregisterMessageHook(int (*handler)(struct CtdlMessage *, struct recptypes *), int EventType)
{
	MessageFunctionHook *cur, *p, *last;
	last = NULL;
	cur = MessageHookTable;
	while (cur != NULL) {
		if ((handler == cur->h_function_pointer) &&
		    (EventType == cur->eventtype))
		{
			syslog(LOG_DEBUG, "extensions: unregistered message function (type %d)", EventType);
			p = cur->next;
			free(cur);
			cur = NULL;

			if (last != NULL)
				last->next = p;
			else 
				MessageHookTable = p;
			cur = p;
		}
		else {
			last = cur;
			cur = cur->next;
		}
	}
}


void CtdlRegisterRoomHook(int (*fcn_ptr)(struct ctdlroom *))
{
	RoomFunctionHook *newfcn;

	newfcn = (RoomFunctionHook *)
	    malloc(sizeof(RoomFunctionHook));
	newfcn->next = RoomHookTable;
	newfcn->fcn_ptr = fcn_ptr;
	RoomHookTable = newfcn;

	syslog(LOG_DEBUG, "extensions: registered a new room function");
}


void CtdlUnregisterRoomHook(int (*fcn_ptr)(struct ctdlroom *))
{
	RoomFunctionHook *cur, *p, *last;
	last = NULL;
	cur = RoomHookTable;
	while (cur != NULL)
	{
		if (fcn_ptr == cur->fcn_ptr) {
			syslog(LOG_DEBUG, "extensions: unregistered room function");
			p = cur->next;

			free(cur);
			cur = NULL;

			if (last != NULL)
				last->next = p;
			else 
				RoomHookTable = p;
			cur = p;
		}
		else {
			last = cur;
			cur = cur->next;
		}
	}
}


void CtdlRegisterDeleteHook(void (*handler)(char *, long) )
{
	DeleteFunctionHook *newfcn;

	newfcn = (DeleteFunctionHook *)
	    malloc(sizeof(DeleteFunctionHook));
	newfcn->next = DeleteHookTable;
	newfcn->h_function_pointer = handler;
	DeleteHookTable = newfcn;

	syslog(LOG_DEBUG, "extensions: registered a new delete function");
}


void CtdlUnregisterDeleteHook(void (*handler)(char *, long) )
{
	DeleteFunctionHook *cur, *p, *last;

	last = NULL;
	cur = DeleteHookTable;
	while (cur != NULL) {
		if (handler == cur->h_function_pointer )
		{
			syslog(LOG_DEBUG, "extensions: unregistered delete function");
			p = cur->next;
			free(cur);

			if (last != NULL)
				last->next = p;
			else
				DeleteHookTable = p;

			cur = p;
		}
		else {
			last = cur;
			cur = cur->next;
		}
	}
}


void CtdlRegisterFixedOutputHook(char *content_type, void (*handler)(char *, int) )
{
	FixedOutputHook *newfcn;

	newfcn = (FixedOutputHook *)
	    malloc(sizeof(FixedOutputHook));
	newfcn->next = FixedOutputTable;
	newfcn->h_function_pointer = handler;
	safestrncpy(newfcn->content_type, content_type, sizeof newfcn->content_type);
	FixedOutputTable = newfcn;

	syslog(LOG_DEBUG, "extensions: registered a new fixed output function for %s", newfcn->content_type);
}


void CtdlUnregisterFixedOutputHook(char *content_type)
{
	FixedOutputHook *cur, *p, *last;

	last = NULL;
	cur = FixedOutputTable;
	while (cur != NULL) {
		/* This will also remove duplicates if any */
		if (!strcasecmp(content_type, cur->content_type)) {
			syslog(LOG_DEBUG, "extensions: unregistered fixed output function for %s", content_type);
			p = cur->next;
			free(cur);

			if (last != NULL)
				last->next = p;
			else
				FixedOutputTable = p;
			
			cur = p;
		}
		else
		{
			last = cur;
			cur = cur->next;
		}
	}
}


/* returns nonzero if we found a hook and used it */
int PerformFixedOutputHooks(char *content_type, char *content, int content_length)
{
	FixedOutputHook *fcn;

	for (fcn = FixedOutputTable; fcn != NULL; fcn = fcn->next) {
		if (!strcasecmp(content_type, fcn->content_type)) {
			(*fcn->h_function_pointer) (content, content_length);
			return(1);
		}
	}
	return(0);
}


void CtdlRegisterXmsgHook(int (*fcn_ptr) (char *, char *, char *, char *), int order)
{

	XmsgFunctionHook *newfcn;

	newfcn = (XmsgFunctionHook *) malloc(sizeof(XmsgFunctionHook));
	newfcn->next = XmsgHookTable;
	newfcn->order = order;
	newfcn->h_function_pointer = fcn_ptr;
	XmsgHookTable = newfcn;
	syslog(LOG_DEBUG, "extensions: registered a new x-msg function (priority %d)", order);
}


void CtdlUnregisterXmsgHook(int (*fcn_ptr) (char *, char *, char *, char *), int order)
{
	XmsgFunctionHook *cur, *p, *last;

	last = NULL;
	cur = XmsgHookTable;
	while (cur != NULL) {
		/* This will also remove duplicates if any */
		if (fcn_ptr == cur->h_function_pointer &&
		    order == cur->order) {
			syslog(LOG_DEBUG, "extensions: unregistered x-msg function (priority %d)", order);
			p = cur->next;
			free(cur);

			if (last != NULL) {
				last->next = p;
			}
			else {
				XmsgHookTable = p;
			}
			cur = p;
		}
		else {
			last = cur;
			cur = cur->next;
		}
	}
}


void CtdlRegisterServiceHook(int tcp_port,
			     char *sockpath,
			     void (*h_greeting_function) (void),
			     void (*h_command_function) (void),
			     void (*h_async_function) (void),
			     const char *ServiceName)
{
	ServiceFunctionHook *newfcn;
	char *message;

	newfcn = (ServiceFunctionHook *) malloc(sizeof(ServiceFunctionHook));
	message = (char*) malloc (SIZ + SIZ);
	
	newfcn->next = ServiceHookTable;
	newfcn->tcp_port = tcp_port;
	newfcn->sockpath = sockpath;
	newfcn->h_greeting_function = h_greeting_function;
	newfcn->h_command_function = h_command_function;
	newfcn->h_async_function = h_async_function;
	newfcn->ServiceName = ServiceName;

	if (sockpath != NULL) {
		newfcn->msock = ctdl_uds_server(sockpath, CtdlGetConfigInt("c_maxsessions"));
		snprintf(message, SIZ, "extensions: unix domain socket '%s': ", sockpath);
	}
	else if (tcp_port <= 0) {	/* port -1 to disable */
		syslog(LOG_INFO, "extensions: service %s has been manually disabled, skipping", ServiceName);
		free (message);
		free(newfcn);
		return;
	}
	else {
		newfcn->msock = ctdl_tcp_server(CtdlGetConfigStr("c_ip_addr"), tcp_port, CtdlGetConfigInt("c_maxsessions"));
		snprintf(message, SIZ, "extensions: TCP port %s:%d: (%s) ", 
			 CtdlGetConfigStr("c_ip_addr"), tcp_port, ServiceName);
	}

	if (newfcn->msock > 0) {
		ServiceHookTable = newfcn;
		strcat(message, "registered.");
		syslog(LOG_INFO, "%s", message);
	}
	else {
		AddPortError(message, "failed");
		strcat(message, "FAILED.");
		syslog(LOG_ERR, "%s", message);
		free(newfcn);
	}
	free(message);
}


void CtdlUnregisterServiceHook(int tcp_port, char *sockpath,
			void (*h_greeting_function) (void),
			void (*h_command_function) (void),
			void (*h_async_function) (void)
			)
{
	ServiceFunctionHook *cur, *p, *last;

	last = NULL;
	cur = ServiceHookTable;
	while (cur != NULL) {
		/* This will also remove duplicates if any */
		if (h_greeting_function == cur->h_greeting_function &&
		    h_command_function == cur->h_command_function &&
		    h_async_function == cur->h_async_function &&
		    tcp_port == cur->tcp_port && 
		    !(sockpath && cur->sockpath && strcmp(sockpath, cur->sockpath)) )
		{
			if (cur->msock > 0)
				close(cur->msock);
			if (sockpath) {
				syslog(LOG_INFO, "extensions: closed UNIX domain socket %s", sockpath);
				unlink(sockpath);
			} else if (tcp_port) {
				syslog(LOG_INFO, "extensions: closed TCP port %d", tcp_port);
			} else {
				syslog(LOG_INFO, "extensions: unregistered service \"%s\"", cur->ServiceName);
			}
			p = cur->next;
			free(cur);
			if (last != NULL)
				last->next = p;
			else
				ServiceHookTable = p;
			cur = p;
		}
		else {
			last = cur;
			cur = cur->next;
		}
	}
}


// During shutdown we can close all of the listening sockets.
void CtdlShutdownServiceHooks(void) {
	ServiceFunctionHook *cur;

	cur = ServiceHookTable;
	while (cur != NULL) {
		if (cur->msock != -1) {
			close(cur->msock);
			cur->msock = -1;
			if (cur->sockpath != NULL){
				syslog(LOG_INFO, "extensions: [%s] closed unix domain socket %s", cur->ServiceName, cur->sockpath);
				unlink(cur->sockpath);
			} else {
				syslog(LOG_INFO, "extensions: [%s] closing service", cur->ServiceName);
			}
		}
		cur = cur->next;
	}
}


void CtdlRegisterSearchFuncHook(void (*fcn_ptr)(int *, long **, const char *), char *name) {
	SearchFunctionHook *newfcn;

	if (!name || !fcn_ptr) {
		return;
	}
	
	newfcn = (SearchFunctionHook *)
	    malloc(sizeof(SearchFunctionHook));
	newfcn->next = SearchFunctionHookTable;
	newfcn->name = name;
	newfcn->fcn_ptr = fcn_ptr;
	SearchFunctionHookTable = newfcn;

	syslog(LOG_DEBUG, "extensions: registered a new search function (%s)", name);
}


void CtdlUnregisterSearchFuncHook(void (*fcn_ptr)(int *, long **, const char *), char *name) {
	SearchFunctionHook *cur, *p, *last;
	
	last = NULL;
	cur = SearchFunctionHookTable;
	while (cur != NULL) {
		if (fcn_ptr &&
		    (cur->fcn_ptr == fcn_ptr) &&
		    name && !strcmp(name, cur->name))
		{
			syslog(LOG_DEBUG, "extensions: unregistered search function(%s)", name);
			p = cur->next;
			free (cur);
			if (last != NULL)
				last->next = p;
			else
				SearchFunctionHookTable = p;
			cur = p;
		}
		else {
			last = cur;
			cur = cur->next;
		}
	}
}


void CtdlModuleDoSearch(int *num_msgs, long **search_msgs, const char *search_string, const char *func_name) {
	SearchFunctionHook *fcn = NULL;

	for (fcn = SearchFunctionHookTable; fcn != NULL; fcn = fcn->next) {
		if (!func_name || !strcmp(func_name, fcn->name)) {
			(*fcn->fcn_ptr) (num_msgs, search_msgs, search_string);
			return;
		}
	}
	*num_msgs = 0;
}


void PerformSessionHooks(int EventType) {
	SessionFunctionHook *fcn = NULL;

	for (fcn = SessionHookTable; fcn != NULL; fcn = fcn->next) {
		if (fcn->eventtype == EventType) {
			if (EventType == EVT_TIMER) {
				pthread_setspecific(MyConKey, NULL);	/* for every hook */
			}
			(*fcn->h_function_pointer) ();
		}
	}
}


void PerformUserHooks(ctdluser *usbuf, int EventType) {
	UserFunctionHook *fcn = NULL;

	for (fcn = UserHookTable; fcn != NULL; fcn = fcn->next) {
		if (fcn->eventtype == EventType) {
			(*fcn->h_function_pointer) (usbuf);
		}
	}
}


int PerformMessageHooks(struct CtdlMessage *msg, struct recptypes *recps, int EventType) {
	MessageFunctionHook *fcn = NULL;
	int total_retval = 0;

	/* Other code may elect to protect this message from server-side
	 * handlers; if this is the case, don't do anything.
	 */
	if (msg->cm_flags & CM_SKIP_HOOKS) {
		return(0);
	}

	/* Otherwise, run all the hooks appropriate to this event type.
	 */
	for (fcn = MessageHookTable; fcn != NULL; fcn = fcn->next) {
		if (fcn->eventtype == EventType) {
			total_retval = total_retval + (*fcn->h_function_pointer) (msg, recps);
		}
	}

	/* Return the sum of the return codes from the hook functions.  If
	 * this is an EVT_BEFORESAVE event, a nonzero return code will cause
	 * the save operation to abort.
	 */
	return total_retval;
}


int PerformRoomHooks(struct ctdlroom *target_room) {
	RoomFunctionHook *fcn;
	int total_retval = 0;

	syslog(LOG_DEBUG, "extensions: performing room hooks for <%s>", target_room->QRname);

	for (fcn = RoomHookTable; fcn != NULL; fcn = fcn->next) {
		total_retval = total_retval + (*fcn->fcn_ptr) (target_room);
	}

	/* Return the sum of the return codes from the hook functions.
	 */
	return total_retval;
}


void PerformDeleteHooks(char *room, long msgnum) {
	DeleteFunctionHook *fcn;

	for (fcn = DeleteHookTable; fcn != NULL; fcn = fcn->next) {
		(*fcn->h_function_pointer) (room, msgnum);
	}
}


int PerformXmsgHooks(char *sender, char *sender_email, char *recp, char *msg) {
	XmsgFunctionHook *fcn;
	int total_sent = 0;
	int p;

	for (p=0; p<MAX_XMSG_PRI; ++p) {
		for (fcn = XmsgHookTable; fcn != NULL; fcn = fcn->next) {
			if (fcn->order == p) {
				total_sent +=
					(*fcn->h_function_pointer)
						(sender, sender_email, recp, msg);
			}
		}
		/* Break out of the loop if a higher-priority function
		 * successfully delivered the message.  This prevents duplicate
		 * deliveries to local users simultaneously signed onto
		 * remote services.
		 */
		if (total_sent) break;
	}
	return total_sent;
}


/*
 * "Start TLS" function that is (hopefully) adaptable for any protocol
 */
void CtdlModuleStartCryptoMsgs(char *ok_response, char *nosup_response, char *error_response) {
#ifdef HAVE_OPENSSL
	CtdlStartTLS (ok_response, nosup_response, error_response);
#endif
}


CTDL_MODULE_INIT(modules) {
	return "modules";
}
