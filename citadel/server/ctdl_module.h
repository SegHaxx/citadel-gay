
#ifndef CTDL_MODULE_H
#define CTDL_MODULE_H

#include "sysdep.h"

#ifdef HAVE_GC
#define GC_THREADS
#define GC_REDIRECT_TO_LOCAL
#include <gc/gc_local_alloc.h>
#else
#define GC_MALLOC malloc
#define GC_MALLOC_ATOMIC malloc
#define GC_FREE free
#define GC_REALLOC realloc
#endif


#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <ctype.h>
#include <signal.h>
#include <pwd.h>
#include <errno.h>
#include <syslog.h>
#include <sys/types.h>
#include <time.h>
#include <sys/wait.h>
#include <string.h>
#ifdef HAVE_STRINGS_H
#include <strings.h>
#endif
#include <limits.h>

#include <libcitadel.h>

#include "server.h"
#include "sysdep_decls.h"
#include "msgbase.h"
#include "threads.h"
#include "citadel_dirs.h"
#include "context.h"

extern int threading;


/*
 * define macros for module init stuff
 */
 
#define CTDL_MODULE_INIT(module_name) char *ctdl_module_##module_name##_init (int threading)

#define CTDL_INIT_CALL(module_name) ctdl_module_##module_name##_init (threading)

#define CTDL_MODULE_UPGRADE(module_name) char *ctdl_module_##module_name##_upgrade (void)

#define CTDL_UPGRADE_CALL(module_name) ctdl_module_##module_name##_upgrade ()

#define CtdlAideMessage(TEXT, SUBJECT)		\
	quickie_message(			\
		"Citadel",			\
		NULL,				\
		NULL,				\
		AIDEROOM,			\
		TEXT,				\
		FMT_CITADEL,			\
		SUBJECT) 

/*
 * Hook functions available to modules.
 */
/* Priorities for  */
#define PRIO_QUEUE 500
#define PRIO_AGGR 1000
#define PRIO_SEND 1500
#define PRIO_CLEANUP 2000
/* Priorities for EVT_HOUSE */
#define PRIO_HOUSE 3000
/* Priorities for EVT_LOGIN */
#define PRIO_CREATE 10000
/* Priorities for EVT_LOGOUT */
#define PRIO_LOGOUT 15000
/* Priorities for EVT_LOGIN */
#define PRIO_LOGIN 20000
/* Priorities for EVT_START */
#define PRIO_START 25000
/* Priorities for EVT_STOP */
#define PRIO_STOP 30000
/* Priorities for EVT_ASYNC */
#define PRIO_ASYNC 35000
/* Priorities for EVT_SHUTDOWN */
#define PRIO_SHUTDOWN 40000
/* Priorities for EVT_UNSTEALTH */
#define PRIO_UNSTEALTH 45000
/* Priorities for EVT_STEALTH */
#define PRIO_STEALTH 50000


void CtdlRegisterSessionHook(void (*fcn_ptr)(void), int EventType, int Priority);
void CtdlUnregisterSessionHook(void (*fcn_ptr)(void), int EventType);
void CtdlShutdownServiceHooks(void);

void CtdlRegisterUserHook(void (*fcn_ptr)(struct ctdluser *), int EventType);
void CtdlUnregisterUserHook(void (*fcn_ptr)(struct ctdluser *), int EventType);

void CtdlRegisterXmsgHook(int (*fcn_ptr)(char *, char *, char *, char *), int order);
void CtdlUnregisterXmsgHook(int (*fcn_ptr)(char *, char *, char *, char *), int order);

void CtdlRegisterMessageHook(int (*handler)(struct CtdlMessage *, struct recptypes *), int EventType);
void CtdlUnregisterMessageHook(int (*handler)(struct CtdlMessage *, struct recptypes *), int EventType);

void CtdlRegisterRoomHook(int (*fcn_ptr)(struct ctdlroom *) );
void CtdlUnregisterRoomHook(int (*fnc_ptr)(struct ctdlroom *) );

void CtdlRegisterDeleteHook(void (*handler)(char *, long) );
void CtdlUnregisterDeleteHook(void (*handler)(char *, long) );

void CtdlRegisterCleanupHook(void (*fcn_ptr)(void));
void CtdlUnregisterCleanupHook(void (*fcn_ptr)(void));

void CtdlRegisterEVCleanupHook(void (*fcn_ptr)(void));
void CtdlUnregisterEVCleanupHook(void (*fcn_ptr)(void));

void CtdlRegisterProtoHook(void (*handler)(char *), char *cmd, char *desc);

void CtdlRegisterServiceHook(int tcp_port,
			     char *sockpath,
			     void (*h_greeting_function) (void),
			     void (*h_command_function) (void),
			     void (*h_async_function) (void),
			     const char *ServiceName
);
void CtdlUnregisterServiceHook(int tcp_port,
			char *sockpath,
                        void (*h_greeting_function) (void),
                        void (*h_command_function) (void),
                        void (*h_async_function) (void)
);

void CtdlRegisterFixedOutputHook(char *content_type, void (*output_function) (char *supplied_data, int len));
void CtdlUnRegisterFixedOutputHook(char *content_type);
void CtdlRegisterMaintenanceThread(char *name, void *(*thread_proc) (void *arg));
void CtdlRegisterSearchFuncHook(void (*fcn_ptr)(int *, long **, const char *), char *name);

/*
 * if you say a) (which may take a while)
 * don't forget to say b)
 */
void CtdlDisableHouseKeeping(void);
void CtdlEnableHouseKeeping(void);

/* TODODRW: This needs to be changed into a hook type interface
 * for now we have this horrible hack
 */
void CtdlModuleStartCryptoMsgs(char *ok_response, char *nosup_response, char *error_response);

/* return the current context list as an array and do it in a safe manner
 * The returned data is a copy so only reading is useful
 * The number of contexts is returned in count.
 * Beware, this does not copy any of the data pointed to by the context.
 * This means that you can not rely on things like the redirect buffer being valid.
 * You must free the returned pointer when done.
 */
struct CitContext *CtdlGetContextArray (int *count);
void CtdlFillSystemContext(struct CitContext *context, char *name);
int CtdlTrySingleUser(void);
void CtdlEndSingleUser(void);
int CtdlWantSingleUser(void);
int CtdlIsSingleUser(void);


int CtdlIsUserLoggedIn (char *user_name);
int CtdlIsUserLoggedInByNum (long usernum);
void CtdlBumpNewMailCounter(long which_user);


/*
 * CtdlGetCurrentMessageNumber()  -  Obtain the current highest message number in the system
 * This provides a quick way to initialize a variable that might be used to indicate
 * messages that should not be processed.  For example, a new inbox script will use this
 * to record determine that messages older than this should not be processed.
 * This function is defined in control.c
 */
long CtdlGetCurrentMessageNumber(void);



/*
 * Expose various room operation functions from room_ops.c to the modules API
 */

unsigned CtdlCreateRoom(char *new_room_name,
			int new_room_type,
			char *new_room_pass,
			int new_room_floor,
			int really_create,
			int avoid_access,
			int new_room_view);
int CtdlGetRoom(struct ctdlroom *qrbuf, const char *room_name);
int CtdlGetRoomLock(struct ctdlroom *qrbuf, const char *room_name);
int CtdlDoIHavePermissionToDeleteThisRoom(struct ctdlroom *qr);
void CtdlRoomAccess(struct ctdlroom *roombuf, struct ctdluser *userbuf, int *result, int *view);
void CtdlPutRoomLock(struct ctdlroom *qrbuf);
typedef void (*ForEachRoomCallBack)(struct ctdlroom *EachRoom, void *out_data);
void CtdlForEachRoom(ForEachRoomCallBack CB, void *in_data);
char *LoadRoomNetConfigFile(long roomnum);
void SaveRoomNetConfigFile(long roomnum, const char *raw_netconfig);
void CtdlDeleteRoom(struct ctdlroom *qrbuf);
int CtdlRenameRoom(char *old_name, char *new_name, int new_floor);
void CtdlUserGoto (char *where, int display_result, int transiently, int *msgs, int *new, long *oldest, long *newest);
struct floor *CtdlGetCachedFloor(int floor_num);
void CtdlScheduleRoomForDeletion(struct ctdlroom *qrbuf);
void CtdlGetFloor (struct floor *flbuf, int floor_num);
void CtdlPutFloor (struct floor *flbuf, int floor_num);
void CtdlPutFloorLock(struct floor *flbuf, int floor_num);
int CtdlGetFloorByName(const char *floor_name);
int CtdlGetFloorByNameLock(const char *floor_name);
int CtdlGetAvailableFloor(void);
int CtdlIsNonEditable(struct ctdlroom *qrbuf);
void CtdlPutRoom(struct ctdlroom *);

/*
 * Possible return values for CtdlRenameRoom()
 */
enum {
	crr_ok,				/* success */
	crr_room_not_found,		/* room not found */
	crr_already_exists,		/* new name already exists */
	crr_noneditable,		/* cannot edit this room */
	crr_invalid_floor,		/* target floor does not exist */
	crr_access_denied		/* not allowed to edit this room */
};



/*
 * API declarations from citserver.h
 */
int CtdlAccessCheck(int);
/* 'required access level' values which may be passed to CtdlAccessCheck()
 */
enum {
	ac_none,
	ac_logged_in_or_guest,
	ac_logged_in,
	ac_room_aide,
	ac_aide,
	ac_internal,
};



/*
 * API declarations from serv_extensions.h
 */
void CtdlModuleDoSearch(int *num_msgs, long **search_msgs, const char *search_string, const char *func_name);

#define NODENAME		CtdlGetConfigStr("c_nodename")
#define FQDN			CtdlGetConfigStr("c_fqdn")
#define CTDLUID			ctdluid
#define CREATAIDE		CtdlGetConfigInt("c_creataide")
#define REGISCALL		CtdlGetConfigInt("c_regiscall")
#define TWITDETECT		CtdlGetConfigInt("c_twitdetect")
#define TWITROOM		CtdlGetConfigStr("c_twitroom")
#define RESTRICT_INTERNET	CtdlGetConfigInt("c_restrict")

#define CtdlREGISTERRoomCfgType(a, p, uniq, nSegs, s, d) RegisterRoomCfgType(#a, sizeof(#a) - 1, a, p, uniq, nSegs, s, d);



/*
 * Expose API calls from user_ops.c
 */
int CtdlGetUser(struct ctdluser *usbuf, char *name);
int CtdlGetUserLen(struct ctdluser *usbuf, const char *name, long len);
int CtdlGetUserLock(struct ctdluser *usbuf, char *name);
void CtdlPutUser(struct ctdluser *usbuf);
void CtdlPutUserLock(struct ctdluser *usbuf);
int CtdlLockGetCurrentUser(void);
void CtdlPutCurrentUserLock(void);
int CtdlGetUserByNumber(struct ctdluser *usbuf, long number);
void CtdlGetRelationship(struct visit *vbuf, struct ctdluser *rel_user, struct ctdlroom *rel_room);
void CtdlSetRelationship(struct visit *newvisit, struct ctdluser *rel_user, struct ctdlroom *rel_room);
void CtdlMailboxName(char *buf, size_t n, const struct ctdluser *who, const char *prefix);
int CtdlLoginExistingUser(const char *username);

/*
 * Values which may be returned by CtdlLoginExistingUser()
 */
enum {
	pass_ok,
	pass_already_logged_in,
	pass_no_user,
	pass_internal_error,
	pass_wrong_password
};

int CtdlTryPassword(const char *password, long len);
/*
 * Values which may be returned by CtdlTryPassword()
 */
enum {
	login_ok,
	login_already_logged_in,
	login_too_many_users,
	login_not_found
};

void CtdlUserLogout(void);

/*
 * Expose API calls from msgbase.c
 */


/*
 * Expose API calls from euidindex.c
 */
long CtdlLocateMessageByEuid(char *euid, struct ctdlroom *qrbuf);


/*
 * Expose API calls from external authentication driver
 */
int attach_extauth(struct ctdluser *who, StrBuf *claimed_id);

#endif /* CTDL_MODULE_H */
