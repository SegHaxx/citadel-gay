
#ifndef SERV_EXTENSIONS_H
#define SERV_EXTENSIONS_H

#include "server.h"

/*
 * ServiceFunctionHook extensions are used for hooks which implement various
 * protocols (either on TCP or on unix domain sockets) directly in the Citadel server.
 */
typedef struct ServiceFunctionHook ServiceFunctionHook;
struct ServiceFunctionHook {
	ServiceFunctionHook *next;
	int tcp_port;
	char *sockpath;
	void (*h_greeting_function) (void) ;
	void (*h_command_function) (void) ;
	void (*h_async_function) (void) ;
	int msock;
	const char* ServiceName; /* this is just for debugging and logging purposes. */
};
extern ServiceFunctionHook *ServiceHookTable;

typedef struct CleanupFunctionHook CleanupFunctionHook;
struct CleanupFunctionHook {
	CleanupFunctionHook *next;
	void (*h_function_pointer) (void);
};
extern CleanupFunctionHook *CleanupHookTable;

void initialize_server_extensions(void);
int DLoader_Exec_Cmd(char *cmdbuf);
char *Dynamic_Module_Init(void);

void CtdlDestroySessionHooks(void);
void PerformSessionHooks(int EventType);

void CtdlDestroyUserHooks(void);
void PerformUserHooks(struct ctdluser *usbuf, int EventType);

int PerformXmsgHooks(char *, char *, char *, char *);
void CtdlDestroyXmsgHooks(void);

void CtdlDestroyMessageHook(void);
int PerformMessageHooks(struct CtdlMessage *, recptypes *recps, int EventType);

void CtdlDestroyRoomHooks(void);
int PerformRoomHooks(struct ctdlroom *);

void CtdlDestroyDeleteHooks(void);
void PerformDeleteHooks(char *, long);

void CtdlDestroyCleanupHooks(void);

void CtdlDestroyProtoHooks(void);

void CtdlDestroyServiceHook(void);

void CtdlDestroySearchHooks(void);

void CtdlDestroyFixedOutputHooks(void);
int PerformFixedOutputHooks(char *, char *, int);

void netcfg_keyname(char *keybuf, long roomnum);

#endif /* SERV_EXTENSIONS_H */
