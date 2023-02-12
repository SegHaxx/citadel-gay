// Preprocessor definitions for the Citadel Server
//
// Copyright (c) 1987-2023 by the citadel.org team
//
// This program is open source software.  Use, duplication, or disclosure
// is subject to the terms of the GNU General Public License, version 3.
// The program is distributed without any warranty, expressed or implied.

#ifndef CITADEL_DEFS_H
#define CITADEL_DEFS_H

// Suppress these compiler warnings
#pragma GCC diagnostic ignored "-Wcast-qual"
#pragma GCC diagnostic ignored "-Wdiscarded-qualifiers"
#pragma GCC diagnostic ignored "-Wformat-truncation"
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

#include "sysdep.h"
#include <limits.h>
#include "sysconfig.h"
#include "typesize.h"
#include "ipcdef.h"

#define REV_LEVEL 972		// This version
#define REV_MIN		591		// Oldest compatible database
#define EXPORT_REV_MIN	931		// Oldest compatible export files
#define LIBCITADEL_MIN	951		// Minimum required version of libcitadel
#define SERVER_TYPE	0		// zero for stock Citadel; other developers please obtain SERVER_TYPE codes for your implementations

// hats off to https://stackoverflow.com/questions/5459868/concatenate-int-to-string-using-c-preprocessor
#define STR_HELPER(x) #x
#define STR(x) STR_HELPER(x)
#define CITADEL	"Citadel Server " STR(REV_LEVEL)

#ifdef LIBCITADEL_VERSION_NUMBER
#if LIBCITADEL_VERSION_NUMBER < LIBCITADEL_MIN
#error libcitadel is too old.  Please upgrade it before continuing.
#endif
#endif

// This is the user name and password for the default administrator account
// that is created when Citadel Server is started with an empty database.
#define DEFAULT_ADMIN_USERNAME	"admin"
#define DEFAULT_ADMIN_PASSWORD	"citadel"

// Various length constants
#define ROOMNAMELEN	128		// The size of a roomname string
#define USERNAME_SIZE	64		// The size of a username string

// Bits which may appear in MMflags.
#define MM_VALID	4		// New users need validating

// Miscellaneous
#define MES_NORMAL	65		// Normal message
#define MES_ANONONLY	66		// "****" header
#define MES_ANONOPT	67		// "Anonymous" header

// Values used internally for function call returns, etc.
#define NEWREGISTER	0		// new user to register
#define REREGISTER	1		// existing user reregistering

// number of items which may be handled by the CONF command
#define NUM_CONFIGS 71

#define TRACE	syslog(LOG_DEBUG, "\033[7m  Checkpoint: %s : %d  \033[0m", __FILE__, __LINE__)

#ifndef LONG_MAX
#define LONG_MAX 2147483647L
#endif

// Authentication modes
#define AUTHMODE_NATIVE		0	// Native (self-contained or "black box")
#define AUTHMODE_HOST		1	// Authenticate against the host OS user database
#define AUTHMODE_LDAP		2	// Authenticate using LDAP server with POSIX schema
#define AUTHMODE_LDAP_AD	3	// Authenticate using LDAP server with Active Directory schema

// Found in struct CtdlMessage
#define	CTDLMESSAGE_MAGIC	0x159d
#define	CM_SKIP_HOOKS		0x01	// Don't run server-side handlers

// Floors
#define F_INUSE		1		// floor is in use

#endif // CITADEL_DEFS_H
