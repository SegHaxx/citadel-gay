// Data types for the Citadel Server
//
// Copyright (c) 1987-2023 by the citadel.org team
//
// This program is open source software.  Use, duplication, or disclosure
// is subject to the terms of the GNU General Public License, version 3.

#ifndef SERVER_H
#define SERVER_H

#ifdef __GNUC__
#define INLINE __inline__
#else
#define INLINE
#endif

#include "citadel_defs.h"
#ifdef HAVE_OPENSSL
#define OPENSSL_NO_KRB5			// work around redhat b0rken ssl headers
#include <openssl/ssl.h>
#endif

// New format for a message in memory
struct CtdlMessage {
	int cm_magic;			// Self-check (NOT SAVED TO DISK)
	char cm_anon_type;		// Anonymous or author-visible
	char cm_format_type;		// Format type
	char *cm_fields[256];		// Data fields
	long cm_lengths[256];		// size of datafields
	unsigned int cm_flags;		// How to handle (NOT SAVED TO DISK)
};

#define	CTDLMESSAGE_MAGIC	0x159d
#define	CM_SKIP_HOOKS		0x01	// Don't run server-side handlers


// Data structure returned by validate_recipients()
struct recptypes {
	int recptypes_magic;
        int num_local;
        int num_internet;
	int num_room;
        int num_error;
	char *errormsg;
	char *recp_local;
	char *recp_internet;
	char *recp_room;
	char *recp_orgroom;
	char *display_recp;
	char *bounce_to;
	char *envelope_from;
	char *sending_room;
};

#define RECPTYPES_MAGIC 0xfeeb

#define CTDLEXIT_SHUTDOWN	0	// Normal shutdown; do NOT auto-restart

// Exit codes 101 through 109 are used for conditions in which
// we deliberately do NOT want the service to automatically
// restart.
#define CTDLEXIT_CONFIG		101	// Could not read system configuration
#define CTDLEXIT_HOME		103	// Citadel home directory not found
#define CTDLEXIT_DB		105	// Unable to initialize database
#define CTDLEXIT_LIBCITADEL	106	// Incorrect version of libcitadel
#define CTDL_EXIT_UNSUP_AUTH	107	// Unsupported auth mode configured
#define CTDLEXIT_UNUSER		108	// Could not determine uid to run as
#define CTDLEXIT_CRYPTO		109	// Problem initializing SSL or TLS

// Any other exit is likely to be from an unexpected abort (segfault etc)
// and we want to try restarting.



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


#define CS_STEALTH	1	// stealth mode
#define CS_CHAT		2	// chat mode
#define CS_POSTING	4	// posting

extern int ScheduledShutdown;
extern uid_t ctdluid;
extern int sanity_diag_mode;

struct ExpressMessage {
	struct ExpressMessage *next;
	time_t timestamp;	// When this message was sent
	unsigned flags;		// Special instructions
	char sender[256];	// Name of sending user
	char sender_email[256];	// Email or JID of sending user
	char *text;		// Message text (if applicable)
};

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
	CDB_EXTAUTH,		// associates OpenIDs with users
	CDB_CONFIG,		// system configuration database
	MAXCDB			// total number of CDB's defined
};

struct cdbdata {
	size_t len;
	char *ptr;
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


/* Priority levels for paging functions (lower is better) */
enum {
	XMSG_PRI_LOCAL,		// Other users on -this- server
	XMSG_PRI_REMOTE,	// Other users on a Citadel network
	XMSG_PRI_FOREIGN,	// Contacts on foreign instant message hosts
	MAX_XMSG_PRI
};


// Defines the relationship of a user to a particular room
typedef struct __visit {
	long v_roomnum;
	long v_roomgen;
	long v_usernum;
	long v_lastseen;
	unsigned int v_flags;
	char v_seen[SIZ];
	char v_answered[SIZ];
	int v_view;
} visit;

#define V_FORGET	1	// User has zapped this room
#define V_LOCKOUT	2	// User is locked out of this room
#define V_ACCESS	4	// Access is granted to this room


// Supplementary data for a message on disk
// These are kept separate from the message itself for one of two reasons:
// 1. Either their values may change at some point after initial save, or
// 2. They are merely caches of data which exist somewhere else, for speed.
// DO NOT PUT BIG DATA IN HERE ... we need this struct to be tiny for lots of quick r/w
struct MetaData {
	long meta_msgnum;		// Message number in *local* message base
	int meta_refcount;		// Number of rooms pointing to this msg
	char meta_content_type[64];	// Cached MIME content-type
	long meta_rfc822_length;	// Cache of RFC822-translated msg length
};


// Calls to AdjRefCount() are queued and deferred, so the user doesn't
// have to wait for various disk-intensive operations to complete synchronously.
// This is the record format.
struct arcq {
	long arcq_msgnum;		// Message number being adjusted
	int arcq_delta;			// Adjustment ( usually 1 or -1 )
};


// Serialization routines use this struct to return a pointer and a length
struct ser_ret {
	size_t len;
	unsigned char *ser;
};


// The S_USETABLE database is used in several modules now, so we define its format here.
struct UseTable {
	char ut_msgid[SIZ];
	time_t ut_timestamp;
};


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


// User records.
typedef struct ctdluser ctdluser;
struct ctdluser {			// User record
	int version;			// Citadel version which created this record
	uid_t uid;			// Associate with a unix account?
	char password[32];		// password
	unsigned flags;			// See US_ flags below
	long timescalled;		// Total number of logins
	long posted;			// Number of messages ever submitted
	cit_uint8_t axlevel;		// Access level
	long usernum;			// User number (never recycled)
	time_t lastcall;		// Date/time of most recent login
	int USuserpurge;		// Purge time (in days) for user
	char fullname[64];		// Display name (primary identifier)
	long msgnum_bio;		// msgnum of user's profile (bio)
	long msgnum_pic;		// msgnum of user's avatar (photo)
	char emailaddrs[512];		// Internet email addresses
	long msgnum_inboxrules;		// msgnum of user's inbox filtering rules
	long lastproc_inboxrules;	// msgnum of last message filtered
};


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

// Room records.
struct ctdlroom {
 	char QRname[ROOMNAMELEN];	// Name of room
 	char QRpasswd[10];		// Only valid if it's a private rm
 	long QRroomaide;		// User number of room aide
 	long QRhighest;			// Highest message NUMBER in room
 	time_t QRgen;			// Generation number of room
 	unsigned QRflags;		// See flag values below
 	char QRdirname[15];		// Directory name, if applicable
 	long msgnum_info;		// msgnum of room banner (info file)
 	char QRfloor;			// Which floor this room is on
 	time_t QRmtime;			// Date/time of last post
 	struct ExpirePolicy QRep;	// Message expiration policy
 	long QRnumber;			// Globally unique room number
 	char QRorder;			// Sort key for room listing order
 	unsigned QRflags2;		// Additional flags
 	int QRdefaultview;		// How to display the contents
	long msgnum_pic;		// msgnum of room picture or icon
};

// Private rooms are always flagged with QR_PRIVATE.  If neither QR_PASSWORDED
// or QR_GUESSNAME is set, then it is invitation-only.  Passworded rooms are
// flagged with both QR_PRIVATE and QR_PASSWORDED while guess-name rooms are
// flagged with both QR_PRIVATE and QR_GUESSNAME.  NEVER set all three flags.

// Floor record.  The floor number is implicit in its location in the file.
struct floor {
	unsigned short f_flags;		// flags
	char f_name[256];		// name of floor
	int f_ref_count;		// reference count
	struct ExpirePolicy f_ep;	// default expiration policy
};

#define F_INUSE		1		// floor is in use


#endif // SERVER_H
