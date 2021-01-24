
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

void initialize_server_extensions(void);
int DLoader_Exec_Cmd(char *cmdbuf);
char *Dynamic_Module_Init(void);

void PerformSessionHooks(int EventType);

void PerformUserHooks(struct ctdluser *usbuf, int EventType);

int PerformXmsgHooks(char *, char *, char *, char *);

int PerformMessageHooks(struct CtdlMessage *, recptypes *recps, int EventType);

int PerformRoomHooks(struct ctdlroom *);

void PerformDeleteHooks(char *, long);





int PerformFixedOutputHooks(char *, char *, int);

void netcfg_keyname(char *keybuf, long roomnum);

#endif /* SERV_EXTENSIONS_H */
