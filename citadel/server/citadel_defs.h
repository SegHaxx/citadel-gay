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

#define REV_LEVEL 976		// This version
#define REV_MIN		591	// Oldest compatible database
#define EXPORT_REV_MIN	931	// Oldest compatible export files
#define LIBCITADEL_MIN	951	// Minimum required version of libcitadel
#define SERVER_TYPE	0	// zero for stock Citadel; other developers please obtain SERVER_TYPE codes for your implementations

#define TRACE	syslog(LOG_DEBUG, "\033[7m  Checkpoint: %s : %d  \033[0m", __FILE__, __LINE__)

#ifndef LONG_MAX
#define LONG_MAX 2147483647L
#endif

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

// Authentication modes
#define AUTHMODE_NATIVE		0	// Native (self-contained or "black box")
#define AUTHMODE_HOST		1	// Authenticate against the host OS user database
#define AUTHMODE_LDAP		2	// Authenticate using LDAP server with POSIX schema
#define AUTHMODE_LDAP_AD	3	// Authenticate using LDAP server with Active Directory schema

// Found in struct CtdlMessage
#define	CTDLMESSAGE_MAGIC	0x159d
#define	CM_SKIP_HOOKS		0x01	// Don't run server-side handlers

// Floors
#define F_INUSE			1	// floor is in use

// Found in struct expirepolicy
#define EXPIRE_NEXTLEVEL	0	// Inherit expiration policy
#define EXPIRE_MANUAL		1	// Don't expire messages at all
#define EXPIRE_NUMMSGS		2	// Keep only latest n messages
#define EXPIRE_AGE		3	// Expire messages after n days

#define RECPTYPES_MAGIC 0xfeeb

#define CTDLEXIT_SHUTDOWN	0	// Normal shutdown; do NOT auto-restart

// Exit codes 101-109  are used for conditions in which we
// deliberately do NOT want the service to automatically restart.
#define CTDLEXIT_CONFIG		101	// Could not read system configuration
#define CTDLEXIT_SANITY		102	// Internal sanity check failed
#define CTDLEXIT_HOME		103	// Citadel home directory not found
#define CTDLEXIT_DB		105	// Unable to initialize database
#define CTDLEXIT_LIBCITADEL	106	// Incorrect version of libcitadel
#define CTDL_EXIT_UNSUP_AUTH	107	// Unsupported auth mode configured
#define CTDLEXIT_UNUSER		108	// Could not determine uid to run as
#define CTDLEXIT_CRYPTO		109	// Problem initializing SSL or TLS

// Other exit codes are probably ok to try starting the server again.
#define CTDLEXIT_REDIRECT	110	// Redirect buffer failure
#define CTDLEXIT_CHKPWD		111	// chkpwd daemon failed
#define CTDLEXIT_THREAD		112	// Problem setting up multithreading
#define CTDLEXIT_BAD_MAGIC	113	// internet_addressing() magic number is wrong

// Any other exit is likely to be from an unexpected abort (segfault etc) and we want to try restarting.


// Reasons why a session would be terminated (set CC->kill_me to these values)
enum {
	KILLME_NOT,
	KILLME_UNKNOWN,
	KILLME_CLIENT_LOGGED_OUT,
	KILLME_IDLE,
	KILLME_CLIENT_DISCONNECTED,
	KILLME_AUTHFAILED,
	KILLME_SERVER_SHUTTING_DOWN,
	KILLME_MAX_SESSIONS_EXCEEDED,
	KILLME_ADMIN_TERMINATE,
	KILLME_SELECT_INTERRUPTED,
	KILLME_SELECT_FAILED,
	KILLME_WRITE_FAILED,
	KILLME_SIMULATION_WORKER,
	KILLME_NOLOGIN,
	KILLME_NO_CRYPTO,
	KILLME_READSTRING_FAILED,
	KILLME_MALLOC_FAILED,
	KILLME_QUOTA,
	KILLME_READ_FAILED,
	KILLME_SPAMMER,
	KILLME_XML_PARSER
};


// Flags that may appear in the "who is online" list
#define CS_STEALTH	1	// stealth mode
#define CS_CHAT		2	// chat mode
#define CS_POSTING	4	// posting


// Flags that may appear in an instant message
#define EM_BROADCAST	1	// Broadcast message
#define EM_GO_AWAY	2	// Server requests client log off
#define EM_CHAT		4	// Server requests client enter chat


// Various things we need to lock and unlock
enum {
	S_USERS,
	S_ROOMS,
	S_SESSION_TABLE,
	S_FLOORTAB,
	S_CHATQUEUE,
	S_CONTROL,
	S_SUPPMSGMAIN,
	S_CONFIG,
	S_HOUSEKEEPING,
	S_NETCONFIGS,
	S_FLOORCACHE,
	S_ATBF,
	S_JOURNAL_QUEUE,
	S_CHKPWD,
	S_XMPP_QUEUE,
	S_SINGLE_USER,
	S_IM_LOGS,
	S_OPENSSL,
	S_SMTPQUEUE,
	MAX_SEMAPHORES
};


// message transfer formats
enum {
	MT_CITADEL,		// Citadel proprietary
	MT_RFC822,		// RFC822
	MT_MIME,		// MIME-formatted message
	MT_DOWNLOAD,		// Download a component
	MT_SPEW_SECTION		// Download a component in a single operation
};


// Message format types in the database
#define FMT_CITADEL	0	// Citadel vari-format (proprietary)
#define FMT_FIXED	1	// Fixed format (proprietary)
#define FMT_RFC822	4	// Standard (headers are in M field)


// citadel database tables (define one for each cdb we need to open)
enum {
	CDB_MSGMAIN,		// message base
	CDB_USERS,		// user file
	CDB_ROOMS,		// room index
	CDB_FLOORTAB,		// floor index
	CDB_MSGLISTS,		// room message lists
	CDB_VISIT,		// user/room relationships
	CDB_DIRECTORY,		// address book directory
	CDB_USETABLE,		// network use table
	CDB_BIGMSGS,		// larger message bodies
	CDB_FULLTEXT,		// full text search index
	CDB_EUIDINDEX,		// locate msgs by EUID
	CDB_USERSBYNUMBER,	// index of users by number
	CDB_UNUSED1,		// this used to be the EXTAUTH table but is no longer used
	CDB_CONFIG,		// system configuration database
	MAXCDB			// total number of CDB's defined
};


// Event types for hooks
enum {
	EVT_STOP,		// Session is terminating
	EVT_START,		// Session is starting
	EVT_LOGIN,		// A user is logging in
	EVT_NEWROOM,		// Changing rooms
	EVT_LOGOUT,		// A user is logging out
	EVT_SETPASS,		// Setting or changing password
	EVT_CMD,		// Called after each server command
	EVT_RWHO,		// An RWHO command is being executed
	EVT_ASYNC,		// Doing asynchronous messages
	EVT_STEALTH,		// Entering stealth mode
	EVT_UNSTEALTH,		// Exiting stealth mode
	EVT_TIMER,		// Timer events are called once per minute and are not tied to any session
	EVT_HOUSE,		// as needed houskeeping stuff
	EVT_SHUTDOWN,		// Server is shutting down
	EVT_PURGEUSER,		// Deleting a user
	EVT_NEWUSER,		// Creating a user
	EVT_BEFORESAVE,
	EVT_AFTERSAVE,
	EVT_SMTPSCAN,		// called before submitting a msg from SMTP
	EVT_AFTERUSRMBOXSAVE	// called afte a message was saved into a users inbox
};


// Priority levels for paging functions (lower is better)
enum {
	XMSG_PRI_LOCAL,		// Other users on -this- server
	XMSG_PRI_REMOTE,	// Other users on a Citadel network
	XMSG_PRI_FOREIGN,	// Contacts on foreign instant message hosts
	MAX_XMSG_PRI
};


// Flags that may appear in a 'struct visit'
#define V_FORGET	1	// User has zapped this room
#define V_LOCKOUT	2	// User is locked out of this room
#define V_ACCESS	4	// Access is granted to this room


// These one-byte field headers are found in the Citadel message store.
typedef enum _MsgField {
	eAuthor       = 'A',
	eBig_message  = 'B',
	eExclusiveID  = 'E',
	erFc822Addr   = 'F',
	emessageId    = 'I',
	eJournal      = 'J',
	eReplyTo      = 'K',
	eListID       = 'L',
	eMesageText   = 'M',
	eOriginalRoom = 'O',
	eMessagePath  = 'P',
	eRecipient    = 'R',
	eTimestamp    = 'T',
	eMsgSubject   = 'U',
	eenVelopeTo   = 'V',
	eWeferences   = 'W',
	eCarbonCopY   = 'Y',
	eErrorMsg     = '0',
	eSuppressIdx  = '1',
	eExtnotify    = '2',
	eVltMsgNum    = '3'
} eMsgField;


// Private rooms are always flagged with QR_PRIVATE.  If neither QR_PASSWORDED
// or QR_GUESSNAME is set, then it is invitation-only.  Passworded rooms are
// flagged with both QR_PRIVATE and QR_PASSWORDED while guess-name rooms are
// flagged with both QR_PRIVATE and QR_GUESSNAME.  NEVER set all three flags.


#endif // CITADEL_DEFS_H
