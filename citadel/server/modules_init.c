// Module initialization hub
//
// Copyright (c) 1987-2022 by the citadel.org team
//
// This program is open source software.  Use, duplication, or disclosure
// is subject to the terms of the GNU General Public License, version 3.
//
// To add new modules to the server:
// 1. Write the module and place it in a server/modules/[module_name]/ directory
// 2. Add its initialization function to server/modules_init.h
// 3. Call its initialization function from server/modules_init.c

#include <stdlib.h>
#include <unistd.h>
#include <syslog.h>
#include <stdio.h>
#include "modules_init.h"

// Module initialization functions will be called TWICE during startup, once before
// the server has gone into multithreading mode, and once afterwards.  Most modules
// can be initialized while in multithreading mode, but your module code should be
// prepared for this kind of initialization.  Look at the existing modules to see what
// goes on there.
int threading = 0;

void initialize_modules(int is_threading) {
	threading = is_threading;
	syslog(LOG_DEBUG, "extensions: begin initializing modules (threading=%d)", threading);
	syslog(LOG_DEBUG, "extensions: init %s", ctdl_module_init_control());
	syslog(LOG_DEBUG, "extensions: init %s", ctdl_module_init_euidindex());
	syslog(LOG_DEBUG, "extensions: init %s", ctdl_module_init_msgbase());
	syslog(LOG_DEBUG, "extensions: init %s", ctdl_module_init_database());
	syslog(LOG_DEBUG, "extensions: init %s", ctdl_module_init_autocompletion());
	syslog(LOG_DEBUG, "extensions: init %s", ctdl_module_init_bio());
	syslog(LOG_DEBUG, "extensions: init %s", ctdl_module_init_blog());
	syslog(LOG_DEBUG, "extensions: init %s", ctdl_module_init_calendar());
	syslog(LOG_DEBUG, "extensions: init %s", ctdl_module_init_checkpoint());
	syslog(LOG_DEBUG, "extensions: init %s", ctdl_module_init_virus());
	syslog(LOG_DEBUG, "extensions: init %s", ctdl_module_init_file_ops());
	syslog(LOG_DEBUG, "extensions: init %s", ctdl_module_init_ctdl_message());
	syslog(LOG_DEBUG, "extensions: init %s", ctdl_module_init_rooms());
	syslog(LOG_DEBUG, "extensions: init %s", ctdl_module_init_serv_session());
	syslog(LOG_DEBUG, "extensions: init %s", ctdl_module_init_syscmd());
	syslog(LOG_DEBUG, "extensions: init %s", ctdl_module_init_serv_user());
	syslog(LOG_DEBUG, "extensions: init %s", ctdl_module_init_expire());
	syslog(LOG_DEBUG, "extensions: init %s", ctdl_module_init_fulltext());
	syslog(LOG_DEBUG, "extensions: init %s", ctdl_module_init_image());
	syslog(LOG_DEBUG, "extensions: init %s", ctdl_module_init_imap());
	syslog(LOG_DEBUG, "extensions: init %s", ctdl_module_init_sieve());
	syslog(LOG_DEBUG, "extensions: init %s", ctdl_module_init_inetcfg());
	syslog(LOG_DEBUG, "extensions: init %s", ctdl_module_init_instmsg());
	syslog(LOG_DEBUG, "extensions: init %s", ctdl_module_init_listdeliver());
	syslog(LOG_DEBUG, "extensions: init %s", ctdl_module_init_listsub());
	syslog(LOG_DEBUG, "extensions: init %s", ctdl_module_init_migrate());
	syslog(LOG_DEBUG, "extensions: init %s", ctdl_module_init_newuser());
	syslog(LOG_DEBUG, "extensions: init %s", ctdl_module_init_nntp());
	syslog(LOG_DEBUG, "extensions: init %s", ctdl_module_init_notes());
	syslog(LOG_DEBUG, "extensions: init %s", ctdl_module_init_openid_rp());
	syslog(LOG_DEBUG, "extensions: init %s", ctdl_module_init_pop3());
	syslog(LOG_DEBUG, "extensions: init %s", ctdl_module_init_pop3client());
	syslog(LOG_DEBUG, "extensions: init %s", ctdl_module_init_roomchat());
	syslog(LOG_DEBUG, "extensions: init %s", ctdl_module_init_rssclient());
	syslog(LOG_DEBUG, "extensions: init %s", ctdl_module_init_rwho());
	syslog(LOG_DEBUG, "extensions: init %s", ctdl_module_init_smtp());
	syslog(LOG_DEBUG, "extensions: init %s", ctdl_module_init_smtpclient());
	syslog(LOG_DEBUG, "extensions: init %s", ctdl_module_init_spam());
	syslog(LOG_DEBUG, "extensions: init %s", ctdl_module_init_test());
	syslog(LOG_DEBUG, "extensions: init %s", ctdl_module_init_upgrade());
	syslog(LOG_DEBUG, "extensions: init %s", ctdl_module_init_vcard());
	syslog(LOG_DEBUG, "extensions: init %s", ctdl_module_init_wiki());
	syslog(LOG_DEBUG, "extensions: init %s", ctdl_module_init_xmpp());
	syslog(LOG_DEBUG, "extensions: init %s", ctdl_module_init_netconfig());
	syslog(LOG_DEBUG, "extensions: finished initializing modules (threading=%d)", threading);
}
