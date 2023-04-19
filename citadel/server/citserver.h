// Copyright (c) 1987-2019 by the citadel.org team
//
// This program is open source software.  Use, duplication, or disclosure
// is subject to the terms of the GNU General Public License, version 3.

#include "serv_extensions.h"
#include "context.h"
#include "ctdl_module.h"

/* Simple linked list structures ... used in a bunch of different places. */
typedef struct RoomProcList RoomProcList;
struct RoomProcList {
        struct RoomProcList *next;
        char name[ROOMNAMELEN];
        char lcname[ROOMNAMELEN];
	long namelen;
	long lastsent;
	long key;
	long QRNum;
};
struct UserProcList {
	struct UserProcList *next;
	char user[USERNAME_SIZE];
};

#define CTDLUSERIP      (IsEmptyStr(CC->cs_addr) ?  CC->cs_clientinfo: CC->cs_addr)

void master_startup (void);
int master_cleanup (int exitcode);
void set_wtmpsupp (char *newtext);
void set_wtmpsupp_to_current_room(void);
void do_command_loop(void);
void do_async_loop(void);
void begin_session(struct CitContext *con);
void citproto_begin_session(void);
void citproto_begin_admin_session(void);
void help_subst (char *strbuf, char *source, char *dest);
char CtdlCheckExpress(void);

extern int panic_fd;
extern time_t server_startup_time;
