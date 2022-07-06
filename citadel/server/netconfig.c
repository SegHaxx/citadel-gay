// This module handles loading, saving, and parsing of room network configurations.
//
// Copyright (c) 2000-2022 by the citadel.org team
//
// This program is open source software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License, version 3.

#include "sysdep.h"
#include <stdio.h>
#include <sys/types.h>
#include <dirent.h>

#ifdef HAVE_SYSCALL_H
# include <syscall.h>
#else 
# if HAVE_SYS_SYSCALL_H
#  include <sys/syscall.h>
# endif
#endif
#include <dirent.h>
#include <assert.h>
#include <libcitadel.h>
#include "ctdl_module.h"
#include "serv_extensions.h"
#include "config.h"


// Create a config key for a room's netconfig entry
void netcfg_keyname(char *keybuf, long roomnum) {
	if (!keybuf) return;
	sprintf(keybuf, "c_netconfig_%010ld", roomnum);
}


// Given a room number and a textual netconfig, convert to base64 and write to the configdb
void SaveRoomNetConfigFile(long roomnum, const char *raw_netconfig) {
	char keyname[25];
	char *enc;
	int enc_len;
	int len;

	len = strlen(raw_netconfig);
	netcfg_keyname(keyname, roomnum);
	enc = malloc(len * 2);

	if (enc) {
		enc_len = CtdlEncodeBase64(enc, raw_netconfig, len, BASE64_NO_LINEBREAKS);
		if ((enc_len > 1) && (enc[enc_len-2] == 13)) enc[enc_len-2] = 0;
		if ((enc_len > 0) && (enc[enc_len-1] == 10)) enc[enc_len-1] = 0;
		enc[enc_len] = 0;
		syslog(LOG_DEBUG, "netconfig: writing key '%s' (length=%d)", keyname, enc_len);
		CtdlSetConfigStr(keyname, enc);
		free(enc);
	}
}


// Given a room number, attempt to load the netconfig configdb entry for that room.
// If it returns NULL, there is no netconfig.
// Otherwise the caller owns the returned memory and is responsible for freeing it.
char *LoadRoomNetConfigFile(long roomnum) {
	char keyname[25];
	char *encoded_netconfig = NULL;
	char *decoded_netconfig = NULL;

	netcfg_keyname(keyname, roomnum);
	encoded_netconfig = CtdlGetConfigStr(keyname);
	if (!encoded_netconfig) return NULL;

	decoded_netconfig = malloc(strlen(encoded_netconfig));	// yeah, way bigger than it needs to be, but safe
	CtdlDecodeBase64(decoded_netconfig, encoded_netconfig, strlen(encoded_netconfig));
	return decoded_netconfig;
}


//-----------------------------------------------------------------------------
//              Per room network configs : exchange with client                
//-----------------------------------------------------------------------------
void cmd_gnet(char *argbuf) {
	if ( (CC->room.QRflags & QR_MAILBOX) && (CC->user.usernum == atol(CC->room.QRname)) ) {
		// users can edit the netconfigs for their own mailbox rooms
	}
	else if (CtdlAccessCheck(ac_room_aide)) return;
	
	cprintf("%d Network settings for room #%ld <%s>\n", LISTING_FOLLOWS, CC->room.QRnumber, CC->room.QRname);

	char *c = LoadRoomNetConfigFile(CC->room.QRnumber);
	if (c) {
		int len = strlen(c);
		client_write(c, len);			// Can't use cprintf() here, it has a limit of 1024 bytes
		if (c[len] != '\n') {
			client_write(HKEY("\n"));
		}
		free(c);
	}
	cprintf("000\n");
}


void cmd_snet(char *argbuf) {
	StrBuf *Line = NULL;
	StrBuf *TheConfig = NULL;
	int rc;

	unbuffer_output();
	Line = NewStrBuf();
	TheConfig = NewStrBuf();
	cprintf("%d send new netconfig now\n", SEND_LISTING);

	while (rc = CtdlClientGetLine(Line), (rc >= 0)) {
		if ((rc == 3) && (strcmp(ChrPtr(Line), "000") == 0))
			break;

		StrBufAppendBuf(TheConfig, Line, 0);
		StrBufAppendBufPlain(TheConfig, HKEY("\n"), 0);
	}
	FreeStrBuf(&Line);

	begin_critical_section(S_NETCONFIGS);
	SaveRoomNetConfigFile(CC->room.QRnumber, ChrPtr(TheConfig));
	end_critical_section(S_NETCONFIGS);
	FreeStrBuf(&TheConfig);
}


// Convert any legacy configuration files in the "netconfigs" directory
void convert_legacy_netcfg_files(void) {
	DIR *dh = NULL;
	struct dirent *dit = NULL;
	char filename[PATH_MAX];
	long roomnum;
	FILE *fp;
	long len;
	char *v;

	dh = opendir(ctdl_netcfg_dir);
	if (!dh) return;

	syslog(LOG_INFO, "netconfig: legacy netconfig files exist - converting them!");

	while (dit = readdir(dh), dit != NULL) {	// yes, we use the non-reentrant version; we're not in threaded mode yet
		roomnum = atol(dit->d_name);
		if (roomnum > 0) {
			snprintf(filename, sizeof filename, "%s/%ld", ctdl_netcfg_dir, roomnum);
			fp = fopen(filename, "r");
			if (fp) {
				fseek(fp, 0L, SEEK_END);
				len = ftell(fp);
				if (len > 0) {
					v = malloc(len);
					if (v) {
						rewind(fp);
						if (fread(v, len, 1, fp)) {
							SaveRoomNetConfigFile(roomnum, v);
							unlink(filename);
						}
						free(v);
					}
				}
				else {
					unlink(filename);	// zero length netconfig, just delete it
				}
				fclose(fp);
			}
		}
	}

	closedir(dh);
	rmdir(ctdl_netcfg_dir);
}


/*
 * Module entry point
 */
char *ctdl_module_init_netconfig(void) {
	if (!threading) {
		convert_legacy_netcfg_files();
		CtdlRegisterProtoHook(cmd_gnet, "GNET", "Get network config");
		CtdlRegisterProtoHook(cmd_snet, "SNET", "Set network config");
	}
	return "netconfig";
}
