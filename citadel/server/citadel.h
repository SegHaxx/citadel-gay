// Main Citadel header file
//
// Copyright (c) 1987-2022 by the citadel.org team
//
// This program is open source software.  Use, duplication, or disclosure
// is subject to the terms of the GNU General Public License, version 3.
// The program is distributed without any warranty, expressed or implied.

#ifndef CITADEL_H
#define CITADEL_H

// Suppress these compiler warnings
#pragma GCC diagnostic ignored "-Wcast-qual"
#pragma GCC diagnostic ignored "-Wdiscarded-qualifiers"
#pragma GCC diagnostic ignored "-Wformat-truncation"

#include "sysdep.h"
#include <limits.h>
#include "sysconfig.h"
#include "typesize.h"
#include "ipcdef.h"

#ifdef __cplusplus
extern "C" {
#endif


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
#define MAX_EDITORS	5		// number of external editors supported ; must be at least 1

// Message expiration policy stuff
typedef struct ExpirePolicy ExpirePolicy;
struct ExpirePolicy {
	int expire_mode;
	int expire_value;
};

#define EXPIRE_NEXTLEVEL	0	// Inherit expiration policy
#define EXPIRE_MANUAL		1	// Don't expire messages at all
#define EXPIRE_NUMMSGS		2	// Keep only latest n messages
#define EXPIRE_AGE		3	// Expire messages after n days

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

#ifdef __cplusplus
}
#endif

#endif // CITADEL_H
