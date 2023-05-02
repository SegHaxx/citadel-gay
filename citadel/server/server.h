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

extern int ScheduledShutdown;
extern uid_t ctdluid;
extern int sanity_diag_mode;


// Instant message in transit on the system (not used in the database)
struct ExpressMessage {
	struct ExpressMessage *next;
	time_t timestamp;	// When this message was sent
	unsigned flags;		// Special instructions
	char sender[256];	// Name of sending user
	char sender_email[256];	// Email or JID of sending user
	char *text;		// Message text (if applicable)
};


// Row being stored or fetched in the database
struct cdbdata {
	size_t len;
	char *ptr;
};


// Defines the relationship of a user to a particular room
// NOTE: if you add fields to this, you have to also write export/import code in server/modules/migrate/serv_migrate.c
// NOTE: if you add fields to this, you have to also write conversion code in utils/ctdl3264/*
struct visit {
	long v_roomnum;		//
	long v_roomgen;		// The first three fields , sizeof(long)*3 , are the index format.
	long v_usernum;		//
	long v_lastseen;
	unsigned v_flags;
	char v_seen[SIZ];
	char v_answered[SIZ];
	int v_view;
};


// This is the db index format for "visit" records, which describe the relationship between one user and one room.
struct visit_index {
	long iRoomID;
	long iRoomGen;
	long iUserID;
};


// Supplementary data for a message on disk
// These are kept separate from the message itself for one of two reasons:
// 1. Either their values may change at some point after initial save, or
// 2. They are merely caches of data which exist somewhere else, for speed.
// DO NOT PUT BIG DATA IN HERE ... we need this struct to be tiny for lots of quick r/w
// NOTE: if you add fields to this, you have to also write export/import code in server/modules/migrate/serv_migrate.c
// NOTE: if you add fields to this, you have to also write conversion code in utils/ctdl3264/*
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


// User records.
// NOTE: if you add fields to this, you have to also write export/import code in server/modules/migrate/serv_migrate.c
// NOTE: if you add fields to this, you have to also write conversion code in utils/ctdl3264/*
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
// NOTE: if you add fields to this, you have to also write export/import code in server/modules/migrate/serv_migrate.c
// NOTE: if you add fields to this, you have to also write conversion code in utils/ctdl3264/*
typedef struct ExpirePolicy ExpirePolicy;
struct ExpirePolicy {
	int expire_mode;
	int expire_value;
};


// Room records.
// NOTE: if you add fields to this, you have to also write export/import code in server/modules/migrate/serv_migrate.c
// NOTE: if you add fields to this, you have to also write conversion code in utils/ctdl3264/*
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


// Floor record.  The floor number is implicit in its location in the file.
// NOTE: if you add fields to this, you have to also write export/import code in server/modules/migrate/serv_migrate.c
// NOTE: if you add fields to this, you have to also write conversion code in utils/ctdl3264/*
struct floor {
	unsigned short f_flags;		// flags
	char f_name[256];		// name of floor
	int f_ref_count;		// reference count
	struct ExpirePolicy f_ep;	// default expiration policy
};


// Database records beginning with this magic number are assumed to
// be compressed.  In the event that a database record actually begins with
// this magic number, we *must* compress it whether we want to or not,
// because the fetch function will try to uncompress it anyway.
// 
// (No need to #ifdef this stuff; it compiles ok even if zlib is not present
// and doesn't declare anything so it won't bloat the code)
#define COMPRESS_MAGIC	0xc0ffeeee

struct CtdlCompressHeader {
	int magic;
	size_t uncompressed_len;
	size_t compressed_len;
};


#endif // SERVER_H
