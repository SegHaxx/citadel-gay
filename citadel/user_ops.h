/* 
 * Header file for server functions which perform operations on user objects.
 *
 * Copyright (c) 1987-2020 by the citadel.org team
 *
 * This program is open source software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License, version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef __USER_OPS_H__
#define __USER_OPS_H__

#include <ctype.h>
#include <syslog.h>

int hash (char *str);
int is_aide (void);
int is_room_aide (void);
int CtdlCheckInternetMailPermission(struct ctdluser *who);
void rebuild_usersbynumber(void);
void session_startup (void);
void logged_in_response(void);
int purge_user (char *pname);
int getuserbyuid(struct ctdluser *usbuf, uid_t number);

int create_user(char *newusername, int become_user, uid_t uid);
enum {
	CREATE_USER_DO_NOT_BECOME_USER,
	CREATE_USER_BECOME_USER
};
#define NATIVE_AUTH_UID (-1)

void do_login(void);
int CtdlInvtKick(char *iuser, int op);
void ForEachUser(void (*CallBack) (char *, void *out_data), void *in_data);
int NewMailCount(void);
int InitialMailCheck(void);
void put_visit(visit *newvisit);
/* MailboxName is deprecated us CtdlMailboxName instead */
void MailboxName(char *buf, size_t n, const struct ctdluser *who,
		 const char *prefix) __attribute__ ((deprecated));
int GenerateRelationshipIndex(  char *IndexBuf,
                                long RoomID,
                                long RoomGen,
                                long UserID);
int CtdlAssociateSystemUser(char *screenname, char *loginname);




void CtdlSetPassword(char *new_pw);

int CtdlForgetThisRoom(void);

void cmd_newu (char *cmdbuf);
void start_chkpwd_daemon(void);


#define RENAMEUSER_OK			0	/* Operation succeeded */
#define RENAMEUSER_LOGGED_IN		1	/* Cannot rename a user who is currently logged in */
#define RENAMEUSER_NOT_FOUND		2	/* The old user name does not exist */
#define RENAMEUSER_ALREADY_EXISTS	3	/* An account with the desired new name already exists */

int rename_user(char *oldname, char *newname);
void reindex_user_928(char *username, void *out_data);

void makeuserkey(char *key, const char *username);
int CtdlUserCmp(char *s1, char *s2);
int internal_create_user(char *username, struct ctdluser *usbuf, uid_t uid);

#endif
