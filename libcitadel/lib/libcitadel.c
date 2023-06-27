//
// Main stuff for libcitadel
//
// Copyright (c) 1987-2013 by the citadel.org team
//
// This program is open source software.  Use, duplication, or disclosure
// is subject to the terms of the GNU General Public License, version 3.

#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <signal.h>
#include <sys/types.h>
#include <ctype.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>
#include "libcitadel.h"
#include "xdgmime/xdgmime.h"
#include "libcitadellocal.h"

ConstStr RoomNetCfgStrs[maxRoomNetCfg] = {
	{HKEY(strof(subpending))},
	{HKEY(strof(unsubpending))},
	{HKEY(strof(lastsent))}, /* Server internal use only */
	{HKEY(strof(ignet_push_share))},
	{HKEY(strof(listrecp))},
	{HKEY(strof(digestrecp))},
	{HKEY(strof(pop3client))},
	{HKEY(strof(rssclient))},
	{HKEY(strof(participate))}
// No, not one of..	{HKEY(strof(maxRoomNetCfg))}
};

extern int EnableSplice;
extern int ZLibCompressionRatio;

char *libcitadel_version_string(void) {
	return "libcitadel(unnumbered)";
}

int libcitadel_version_number(void) {
	return LIBCITADEL_VERSION_NUMBER;
}

void ShutDownLibCitadel(void) {
	ShutDownLibCitadelMime();
	WildFireShutdown();
	xdg_mime_shutdown();
}
