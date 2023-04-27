// Headers for module initialization hub
//
// Copyright (c) 1987-2022 by the citadel.org team
//
// This program is open source software.  Use, duplication, or disclosure
// is subject to the terms of the GNU General Public License, version 3.
// The program is distributed without any warranty, expressed or implied.
//
// To add new modules to the server:
// 1. Write the module and place it in a server/modules/[module_name]/ directory
// 2. Add its initialization function to server/modules_init.h
// 3. Call its initialization function from server/modules_init.c

#ifndef MODULES_INIT_H
#define MODULES_INIT_H
#include "ctdl_module.h"
extern size_t nSizErrmsg;
void initialize_modules (int threading);
void pre_startup_upgrades(void);
char *ctdl_module_init_control(void);
char *ctdl_module_init_euidindex(void);
char *ctdl_module_init_msgbase(void);
char *ctdl_module_init_database(void);
char *ctdl_module_init_autocompletion(void);
char *ctdl_module_init_bio(void);
char *ctdl_module_init_blog(void);
char *ctdl_module_init_calendar(void);
char *ctdl_module_init_checkpoint(void);
char *ctdl_module_init_virus(void);
char *ctdl_module_init_file_ops(void);
char *ctdl_module_init_ctdl_message(void);
char *ctdl_module_init_rooms(void);
char *ctdl_module_init_serv_session(void);
char *ctdl_module_init_syscmd(void);
char *ctdl_module_init_serv_user(void);
char *ctdl_module_init_expire(void);
char *ctdl_module_init_fulltext(void);
char *ctdl_module_init_image(void);
char *ctdl_module_init_imap(void);
char *ctdl_module_init_sieve(void);
char *ctdl_module_init_inetcfg(void);
char *ctdl_module_init_instmsg(void);
char *ctdl_module_init_listdeliver(void);
char *ctdl_module_init_listsub(void);
char *ctdl_module_init_migrate(void);
char *ctdl_module_init_newuser(void);
char *ctdl_module_init_nntp(void);
char *ctdl_module_init_notes(void);
char *ctdl_module_init_pop3(void);
char *ctdl_module_init_pop3client(void);
char *ctdl_module_init_roomchat(void);
char *ctdl_module_init_rssclient(void);
char *ctdl_module_init_rwho(void);
char *ctdl_module_init_smtp(void);
char *ctdl_module_init_smtpclient(void);
char *ctdl_module_init_spam(void);
char *ctdl_module_init_test(void);
char *ctdl_module_init_upgrade(void);
char *ctdl_module_init_vcard(void);
char *ctdl_module_init_wiki(void);
char *ctdl_module_init_xmpp(void);
char *ctdl_module_init_netconfig(void);
#endif // MODULES_INIT_H
