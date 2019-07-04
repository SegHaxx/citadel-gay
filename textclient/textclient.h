/*
 * Copyright (c) 1987-2019 by the citadel.org team
 *
 *  This program is open source software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 3.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 */

#define	UDS			"_UDS_"
#define DEFAULT_HOST		"localhost"
#define DEFAULT_PORT		"504"
#define CLIENT_VERSION          925
#define CLIENT_TYPE		0

/* commands we can send to the stty_ctdl() routine */
#define SB_NO_INTR     		0               // set to Citadel client mode, i/q disabled
#define SB_YES_INTR		1               // set to Citadel client mode, i/q enabled
#define SB_SAVE			2               // save settings
#define SB_RESTORE		3               // restore settings
#define SB_LAST			4               // redo the last command sent

#define UGLISTLEN		100		// you get a ungoto list of this size */
#define ROOMNAMELEN		128		// The size of a roomname string
#define USERNAME_SIZE		64      	// The size of a username string
#define MAX_EDITORS		5      		// number of external editors supported, must be at least 1
#define NONCE_SIZE		128		// Added by <bc> to allow for APOP auth 

#define S_KEEPALIVE		30		// How often (in seconds) to send keepalives to the server

#define READ_HEADER		2
#define READ_MSGBODY		3

#define NUM_CONFIGS		72

#define	NEXT_KEY		15
#define STOP_KEY		3

/* citadel.rc stuff */
#define RC_NO			0		/* always no */
#define RC_YES			1		/* always yes */
#define RC_DEFAULT		2		/* setting depends on user config */

/* keepalives */
enum {
	KA_NO,				/* no keepalives */
	KA_YES,				/* full keepalives */
	KA_HALF				/* half keepalives */
};

/* for <;G>oto and <;S>kip commands */
#define GF_GOTO		0		/* <;G>oto floor mode */
#define GF_SKIP		1		/* <;S>kip floor mode */
#define GF_ZAP		2		/* <;Z>ap floor mode */

/* Can messages be entered in this room? */
#define ENTMSG_OK_NO	0		/* You may not enter messages here */
#define ENTMSG_OK_YES	1		/* Go ahead! */
#define ENTMSG_OK_BLOG	2		/* Yes, but warn the user about how blog rooms work */

/*
 * Colors for color() command
 */
#define DIM_BLACK	0
#define DIM_RED		1
#define DIM_GREEN	2
#define DIM_YELLOW	3
#define DIM_BLUE	4
#define DIM_MAGENTA	5
#define DIM_CYAN	6
#define DIM_WHITE	7
#define BRIGHT_BLACK	8
#define BRIGHT_RED	9
#define BRIGHT_GREEN	10
#define BRIGHT_YELLOW	11
#define BRIGHT_BLUE	12
#define BRIGHT_MAGENTA	13
#define BRIGHT_CYAN	14
#define BRIGHT_WHITE	15
#define COLOR_PUSH	16	/* Save current color */
#define COLOR_POP	17	/* Restore saved color */
#define ORIGINAL_PAIR	-1	/* Default terminal colors */

typedef void (*sighandler_t)(int);



#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <time.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <termios.h>
//	#include <sgtty.h>	not needed if we have termios.h
#include <stdlib.h>
#include <ctype.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netdb.h>
#include <sys/un.h>
#include <errno.h>
#include <signal.h>
#include <pwd.h>
#include <stdarg.h>
#include <errno.h>
#include <signal.h>
#include <malloc.h>
#include <stdarg.h>
#include <sys/select.h>
#include <dirent.h>
#include <libcitadel.h>

#include <limits.h>
#ifdef HAVE_PTHREAD_H
#include <pthread.h>
#endif
#ifdef HAVE_OPENSSL
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/rand.h>
#endif


struct CtdlServInfo {
	int pid;
	char nodename[32];
	char humannode[64];
	char fqdn[64];
	char software[64];
	int rev_level;
	char site_location[64];
	char sysadm[64];
	char moreprompt[256];
	int ok_floors;
	int paging_level;
	int supports_qnop;
	int supports_ldap;
	int newuser_disabled;
	char default_cal_zone[256];
	double load_avg;
	double worker_avg;
	int thread_count;
	int has_sieve;
	int fulltext_enabled;
	char svn_revision[256];
	int guest_logins;
};


/*
 * This class is responsible for the server connection
 */
typedef struct _CtdlIPC {
	struct CtdlServInfo ServInfo;	/* The server info for this connection */
#if defined(HAVE_OPENSSL)
	SSL *ssl;			/* NULL if not encrypted, non-NULL otherwise */
#endif
	int sock;			/* Socket for connection to server, or -1 if not connected */
	int isLocal;			/* 1 if server is local, 0 otherwise or if not connected */
	int downloading;		/* 1 if a download is open on the server, 0 otherwise */
	int uploading;			/* 1 if an upload is open on the server, 0 otherwise */
	time_t last_command_sent;	/* Time the last command was sent to the server */
	char *Buf;			/* Our buffer for linebuffered read. */
	size_t BufSize;
	size_t BufUsed;
	char *BufPtr;
	void (*network_status_cb)(int state);	/* Callback for update on whether the IPC is locked */
	char ip_hostname[256];		/* host name of server to which we are connected (if network) */
	char ip_address[64];		/* IP address of server to which we are connected (if network) */
} CtdlIPC;

extern char *axdefs[];
extern char *viewdefs[];
extern char fullname[USERNAME_SIZE];
extern unsigned room_flags;
extern char room_name[ROOMNAMELEN];
extern struct CtdlServInfo serv_info;
extern char axlevel;
extern char is_room_aide;
extern unsigned userflags;
extern char sigcaught;
extern char editor_paths[MAX_EDITORS][SIZ];
extern char printcmd[SIZ];
extern char imagecmd[SIZ];
extern char have_xterm;
extern char rc_username[USERNAME_SIZE];
extern char rc_password[32];
extern char rc_floor_mode;
extern time_t rc_idle_threshold;
#ifdef HAVE_OPENSSL
extern char rc_encrypt;			/* from the citadel.rc file */
extern char arg_encrypt;		/* from the command line */
#endif
#if defined(HAVE_CURSES_H) && !defined(DISABLE_CURSES)
extern char rc_screen;
extern char arg_screen;
#endif
extern char rc_alt_semantics;
extern char instant_msgs;
void ctdl_logoff(char *file, int line, CtdlIPC *ipc, int code);
#define logoff(ipc, code)	ctdl_logoff(__FILE__, __LINE__, (ipc), (code))
void formout(CtdlIPC *ipc, char *name);
void sighandler(int which_sig);
extern int secure;
void remove_march(char *roomname, int floornum);
void calc_dirs_n_files(int relh, int home, const char *relhome, char  *ctdldir, int dbg);

/*
 * This struct stores a list of rooms with new messages which the client
 * fetches from the server.  This allows the client to "march" through
 * relevant rooms without having to ask the server each time where to go next.
 */
typedef struct ExpirePolicy ExpirePolicy;
struct ExpirePolicy {
	int expire_mode;
	int expire_value;
};

typedef struct march march;
struct march {
	struct march *next;
	char march_name[ROOMNAMELEN];
	unsigned int march_flags;
	char march_floor;
	char march_order;
	unsigned int march_flags2;
	int march_access;
};

/*
 * This is NOT the same 'struct ctdluser' from the server.
 */
typedef struct ctdluser ctdluser;
struct ctdluser {			// User record
	int version;			// Cit vers. which created this rec
	uid_t uid;			// Associate with a unix account?
	char password[32];		// password
	unsigned flags;			// See US_ flags below
	long timescalled;		// Total number of logins
	long posted;			// Number of messages ever submitted
	uint8_t axlevel;		// Access level
	long usernum;			// User number (never recycled)
	time_t lastcall;		// Date/time of most recent login
	int USuserpurge;		// Purge time (in days) for user
	char fullname[64];		// Display name (primary identifier)
	char emailaddrs[512];		// Internet email addresses
};

typedef struct ctdlroom ctdlroom;
struct ctdlroom {
 	char QRname[ROOMNAMELEN];	/* Name of room                     */
 	char QRpasswd[10];		/* Only valid if it's a private rm  */
 	long QRroomaide;		/* User number of room aide         */
 	long QRhighest;			/* Highest message NUMBER in room   */
 	time_t QRgen;			/* Generation number of room        */
 	unsigned QRflags;		/* See flag values below            */
 	char QRdirname[15];		/* Directory name, if applicable    */
 	long QRinfo;			/* Info file update relative to msgs*/
 	char QRfloor;			/* Which floor this room is on      */
 	time_t QRmtime;			/* Date/time of last post           */
 	struct ExpirePolicy QRep;	/* Message expiration policy        */
 	long QRnumber;			/* Globally unique room number      */
 	char QRorder;			/* Sort key for room listing order  */
 	unsigned QRflags2;		/* Additional flags                 */
 	int QRdefaultview;		/* How to display the contents      */
};


/* C constructor */
CtdlIPC* CtdlIPC_new(int argc, char **argv, char *hostbuf, char *portbuf);
/* C destructor */
void CtdlIPC_delete(CtdlIPC* ipc);
/* Convenience destructor; also nulls out caller's pointer */
void CtdlIPC_delete_ptr(CtdlIPC** pipc);
/* Read a line from server, discarding newline, for chat, will go away */
void CtdlIPC_chat_recv(CtdlIPC* ipc, char *buf);
/* Write a line to server, adding newline, for chat, will go away */
void CtdlIPC_chat_send(CtdlIPC* ipc, const char *buf);

struct ctdlipcroom {
	char RRname[ROOMNAMELEN];	/* Name of room */
	long RRunread;			/* Number of unread messages */
	long RRtotal;			/* Total number of messages in room */
	char RRinfoupdated;		/* Nonzero if info was updated */
	unsigned RRflags;		/* Various flags (see LKRN) */
	unsigned RRflags2;		/* Various flags (see LKRN) */
	long RRhighest;			/* Highest message number in room */
	long RRlastread;		/* Highest message user has read */
	char RRismailbox;		/* Is this room a mailbox room? */
	char RRaide;			/* User can do aide commands in room */
	long RRnewmail;			/* Number of new mail messages */
	char RRfloor;			/* Which floor this room is on */
	char RRcurrentview;		/* The user's current view for this room */
	char RRdefaultview;		/* The default view for this room */
};


struct parts {
	struct parts *next;
	char number[16];		/* part number */
	char name[PATH_MAX];		/* Name */
	char filename[PATH_MAX];	/* Suggested filename */
	char mimetype[SIZ];		/* MIME type */
	char disposition[SIZ];		/* Content disposition */
	unsigned long length;		/* Content length */
};


struct ctdlipcmessage {
	char msgid[SIZ];		/* Original message ID */
	char path[SIZ];			/* Return path to sender */
	char zaps[SIZ];			/* Message ID that this supersedes */
	char subject[SIZ];		/* Message subject */
	char email[SIZ];		/* Email address of sender */
	char author[SIZ];		/* Sender of message */
	char recipient[SIZ];		/* Recipient of message */
	char room[SIZ];			/* Originating room */
	struct parts *attachments;	/* Available attachments */
	char *text;			/* Message text */
	int type;			/* Message type */
	time_t time;			/* Time message was posted */
	char nhdr;			/* Suppress message header? */
	char anonymous;			/* An anonymous message */
	char mime_chosen[SIZ];		/* Chosen MIME part to output */
	char content_type[SIZ];		/* How would you like that? */
	char references[SIZ];		/* Thread references */
};


struct ctdlipcfile {
	char remote_name[PATH_MAX];	/* Filename on server */
	char local_name[PATH_MAX];	/* Filename on client */
	char description[80];		/* Description on server */
	FILE *local_fd;			/* Open file on client */
	size_t size;			/* Size of file in octets */
	unsigned int upload:1;		/* uploading? 0 if downloading */
	unsigned int complete:1;	/* Transfer has finished? */
};


struct ctdlipcmisc {
	long newmail;			/* Number of new Mail messages */
	char needregis;			/* Nonzero if user needs to register */
	char needvalid;			/* Nonzero if users need validation */
};

enum RoomList {
	SubscribedRooms,
	SubscribedRoomsWithNewMessages,
	SubscribedRoomsWithNoNewMessages,
	UnsubscribedRooms,
	AllAccessibleRooms,
	AllPublicRooms
};
#define AllFloors -1
enum MessageList {
	AllMessages,
	OldMessages,
	NewMessages,
	LastMessages,
	FirstMessages,
	MessagesGreaterThan,
	MessagesLessThan
};
enum MessageDirection {
	ReadReverse = -1,
	ReadForward = 1
};
extern char file_citadel_rc[PATH_MAX];
extern char file_citadel_config[PATH_MAX];

/* Shared Diffie-Hellman parameters */
#define DH_P		"F6E33BD70D475906ABCFB368DA2D1E5611D57DFDAC6A10CD78F406D6952519C74E21FFDCC5A780B9359722AACC8036E4CD24D5F5165EAC9EF226DBD9BBCF678F8DDEE86386F1BC20E291A9854A513A2CA326B411DC92E38F2ED2FEB6A3B792F13DB6550371FDBAC5ECA373BE5050CA4905431CA86088737D52B36C8D13CE9CB4EEF4C910285035E8329DD07551A80B87775676DD1067395CCEE9040C9B8BF998C528B3772B4C590A2CF18C5E58929BFCB538A62638B7437A9C68572D15287E97692B0B1EC5444D9DAB6EB062D20B79CA005EC5035065567AFD1FEF9B251D74747C6065D8C8B6B0862D1EE03F3A244C429EADE0CCC5C3A4196F5CBF5AA01A9026EFB20AA90E462BD64620278F271905EB604F38E6CFAE412EAA6C468E3B58170909BC18662FE2053224F30BE4FDB93BF9FBF969D91A5427A0665AC7BD1C43701B991094C92F7A935063055617142164F02973EB4ED86DD74D2BBAB3CD3B28F7BBD8D9F925B0FE92F7F7D0568D783F9ECE7AF96FB5AF274B586924B64639733A73ACA8F2BD1E970DF51ADDD983F7F6361A2B0DC4F086DE26D8656EC8813DE4B74D6D57BC1E690AC2FF1682B7E16938565A41D1DC64C75ADB81DA4582613FC68C0FDD327D35E2CDF20D009465303773EF3870FBDB0985EE7002A95D7912BBCC78187C29DB046763B7FABFF44EABE820F8ED0D7230AA0AF24F428F82448345BC099B"
#define DH_G		"2"
#define DH_L		4096
#define CIT_CIPHERS	"ALL:RC4+RSA:+SSLv2:+TLSv1:!MD5:@STRENGTH"	/* see ciphers(1) */

int CtdlIPCNoop(CtdlIPC *ipc);
int CtdlIPCEcho(CtdlIPC *ipc, const char *arg, char *cret);
int CtdlIPCQuit(CtdlIPC *ipc);
int CtdlIPCLogout(CtdlIPC *ipc);
int CtdlIPCTryLogin(CtdlIPC *ipc, const char *username, char *cret);
int CtdlIPCTryPassword(CtdlIPC *ipc, const char *passwd, char *cret);
int CtdlIPCTryApopPassword(CtdlIPC *ipc, const char *response, char *cret);
int CtdlIPCCreateUser(CtdlIPC *ipc, const char *username, int selfservice,
		char *cret);
int CtdlIPCChangePassword(CtdlIPC *ipc, const char *passwd, char *cret);
int CtdlIPCKnownRooms(CtdlIPC *ipc, enum RoomList which, int floor,
		struct march **listing, char *cret);
int CtdlIPCGetConfig(CtdlIPC *ipc, struct ctdluser **uret, char *cret);
int CtdlIPCSetConfig(CtdlIPC *ipc, struct ctdluser *uret, char *cret);
int CtdlIPCGotoRoom(CtdlIPC *ipc, const char *room, const char *passwd,
		struct ctdlipcroom **rret, char *cret);
int CtdlIPCGetMessages(CtdlIPC *ipc, enum MessageList which, int whicharg,
		const char *mtemplate, unsigned long **mret, char *cret);
int CtdlIPCGetSingleMessage(CtdlIPC *ipc, long msgnum, int headers, int as_mime,
		struct ctdlipcmessage **mret, char *cret);
int CtdlIPCWhoKnowsRoom(CtdlIPC *ipc, char **listing, char *cret);
int CtdlIPCServerInfo(CtdlIPC *ipc, char *cret);
/* int CtdlIPCReadDirectory(CtdlIPC *ipc, struct ctdlipcfile **files, char *cret); */
int CtdlIPCReadDirectory(CtdlIPC *ipc, char **listing, char *cret);
int CtdlIPCSetLastRead(CtdlIPC *ipc, long msgnum, char *cret);
int CtdlIPCInviteUserToRoom(CtdlIPC *ipc, const char *username, char *cret);
int CtdlIPCKickoutUserFromRoom(CtdlIPC *ipc, const char *username, char *cret);
int CtdlIPCGetRoomAttributes(CtdlIPC *ipc, struct ctdlroom **qret, char *cret);
int CtdlIPCSetRoomAttributes(CtdlIPC *ipc, int forget, struct ctdlroom *qret,
		char *cret);
int CtdlIPCGetRoomAide(CtdlIPC *ipc, char *cret);
int CtdlIPCSetRoomAide(CtdlIPC *ipc, const char *username, char *cret);
int CtdlIPCPostMessage(CtdlIPC *ipc, int flag, int *subject_required, 
					   struct ctdlipcmessage *mr,
					   char *cret);
int CtdlIPCRoomInfo(CtdlIPC *ipc, char **iret, char *cret);
int CtdlIPCDeleteMessage(CtdlIPC *ipc, long msgnum, char *cret);
int CtdlIPCMoveMessage(CtdlIPC *ipc, int copy, long msgnum,
		const char *destroom, char *cret);
int CtdlIPCDeleteRoom(CtdlIPC *ipc, int for_real, char *cret);
int CtdlIPCCreateRoom(CtdlIPC *ipc, int for_real, const char *roomname,
		int type, const char *password, int floor, char *cret);
int CtdlIPCForgetRoom(CtdlIPC *ipc, char *cret);
int CtdlIPCSystemMessage(CtdlIPC *ipc, const char *message, char **mret,
		char *cret);
int CtdlIPCNextUnvalidatedUser(CtdlIPC *ipc, char *cret);
int CtdlIPCGetUserRegistration(CtdlIPC *ipc, const char *username, char **rret,
		char *cret);
int CtdlIPCValidateUser(CtdlIPC *ipc, const char *username, int axlevel,
		char *cret);
int CtdlIPCSetRoomInfo(CtdlIPC *ipc, int for_real, const char *info,
		char *cret);
int CtdlIPCUserListing(CtdlIPC *ipc, char *searchstring, char **list, char *cret);
int CtdlIPCSetRegistration(CtdlIPC *ipc, const char *info, char *cret);
int CtdlIPCMiscCheck(CtdlIPC *ipc, struct ctdlipcmisc *chek, char *cret);
int CtdlIPCDeleteFile(CtdlIPC *ipc, const char *filename, char *cret);
int CtdlIPCMoveFile(CtdlIPC *ipc, const char *filename, const char *destroom, char *cret);
int CtdlIPCNetSendFile(CtdlIPC *ipc, const char *filename, const char *destnode, char *cret);
int CtdlIPCOnlineUsers(CtdlIPC *ipc, char **listing, time_t *stamp, char *cret);
int CtdlIPCFileDownload(CtdlIPC *ipc, const char *filename, void **buf,
		size_t resume,
		void (*progress_gauge_callback)(CtdlIPC*, unsigned long, unsigned long),
		char *cret);
int CtdlIPCAttachmentDownload(CtdlIPC *ipc, long msgnum, const char *part,
		void **buf,
		void (*progress_gauge_callback)(CtdlIPC*, unsigned long, unsigned long),
		char *cret);
int CtdlIPCImageDownload(CtdlIPC *ipc, const char *filename, void **buf,
		void (*progress_gauge_callback)(CtdlIPC*, unsigned long, unsigned long),
		char *cret);
int CtdlIPCFileUpload(CtdlIPC *ipc, const char *save_as, const char *comment,
		const char *path, 
		void (*progress_gauge_callback)(CtdlIPC*, unsigned long, unsigned long),
		char *cret);
int CtdlIPCImageUpload(CtdlIPC *ipc, int for_real, const char *path,
		const char *save_as,
		void (*progress_gauge_callback)(CtdlIPC*, unsigned long, unsigned long),
		char *cret);
int CtdlIPCQueryUsername(CtdlIPC *ipc, const char *username, char *cret);
int CtdlIPCFloorListing(CtdlIPC *ipc, char **listing, char *cret);
int CtdlIPCCreateFloor(CtdlIPC *ipc, int for_real, const char *name, char *cret);
int CtdlIPCDeleteFloor(CtdlIPC *ipc, int for_real, int floornum, char *cret);
int CtdlIPCEditFloor(CtdlIPC *ipc, int floornum, const char *floorname, char *cret);
int CtdlIPCIdentifySoftware(CtdlIPC *ipc, int developerid, int clientid,
		int revision, const char *software_name, const char *hostname,
		char *cret);
int CtdlIPCSendInstantMessage(CtdlIPC *ipc, const char *username, const char *text, char *cret);
int CtdlIPCGetInstantMessage(CtdlIPC *ipc, char **listing, char *cret);
int CtdlIPCEnableInstantMessageReceipt(CtdlIPC *ipc, int mode, char *cret);
int CtdlIPCSetBio(CtdlIPC *ipc, char *bio, char *cret);
int CtdlIPCGetBio(CtdlIPC *ipc, const char *username, char **listing, char *cret);
int CtdlIPCListUsersWithBios(CtdlIPC *ipc, char **listing, char *cret);
int CtdlIPCStealthMode(CtdlIPC *ipc, int mode, char *cret);
int CtdlIPCTerminateSession(CtdlIPC *ipc, int sid, char *cret);
int CtdlIPCTerminateServerNow(CtdlIPC *ipc, char *cret);
int CtdlIPCTerminateServerScheduled(CtdlIPC *ipc, int mode, char *cret);
int CtdlIPCEnterSystemMessage(CtdlIPC *ipc, const char *filename, const char *text, char *cret);
time_t CtdlIPCServerTime(CtdlIPC *ipc, char *crert);
int CtdlIPCAideGetUserParameters(CtdlIPC *ipc, const char *who, struct ctdluser **uret, char *cret);
int CtdlIPCAideGetEmailAddresses(CtdlIPC *ipc, const char *who, char *, char *cret);
int CtdlIPCAideSetUserParameters(CtdlIPC *ipc, const struct ctdluser *uret, char *cret);
int CtdlIPCAideSetEmailAddresses(CtdlIPC *ipc, const char *who, char *emailaddrs, char *cret);
int CtdlIPCRenameUser(CtdlIPC *ipc, char *oldname, char *newname, char *cret);
int CtdlIPCGetMessageExpirationPolicy(CtdlIPC *ipc, GPEXWhichPolicy which, struct ExpirePolicy **policy, char *cret);
int CtdlIPCSetMessageExpirationPolicy(CtdlIPC *ipc, int which, struct ExpirePolicy *policy, char *cret);
int CtdlIPCGetSystemConfig(CtdlIPC *ipc, char **listing, char *cret);
int CtdlIPCSetSystemConfig(CtdlIPC *ipc, const char *listing, char *cret);
int CtdlIPCGetSystemConfigByType(CtdlIPC *ipc, const char *mimetype, char **listing, char *cret);
int CtdlIPCSetSystemConfigByType(CtdlIPC *ipc, const char *mimetype, const char *listing, char *cret);
int CtdlIPCGetRoomNetworkConfig(CtdlIPC *ipc, char **listing, char *cret);
int CtdlIPCSetRoomNetworkConfig(CtdlIPC *ipc, const char *listing, char *cret);
int CtdlIPCRequestClientLogout(CtdlIPC *ipc, int session, char *cret);
int CtdlIPCSetMessageSeen(CtdlIPC *ipc, long msgnum, int seen, char *cret);
int CtdlIPCStartEncryption(CtdlIPC *ipc, char *cret);
int CtdlIPCDirectoryLookup(CtdlIPC *ipc, const char *address, char *cret);
int CtdlIPCSpecifyPreferredFormats(CtdlIPC *ipc, char *cret, char *formats);
int CtdlIPCInternalProgram(CtdlIPC *ipc, int secret, char *cret);

/* ************************************************************************** */
/*             Stuff below this line is not for public consumption            */
/* ************************************************************************** */

char *CtdlIPCReadListing(CtdlIPC *ipc, char *dest);
int CtdlIPCSendListing(CtdlIPC *ipc, const char *listing);
size_t CtdlIPCPartialRead(CtdlIPC *ipc, void **buf, size_t offset, size_t bytes, char *cret);
int CtdlIPCEndUpload(CtdlIPC *ipc, int discard, char *cret);
int CtdlIPCWriteUpload(CtdlIPC *ipc, FILE *uploadFP,
		void (*progress_gauge_callback)(CtdlIPC*, unsigned long, unsigned long),
		char *cret);
int CtdlIPCEndDownload(CtdlIPC *ipc, char *cret);
int CtdlIPCReadDownload(CtdlIPC *ipc, void **buf, size_t bytes, size_t resume,
		void (*progress_gauge_callback)(CtdlIPC*, unsigned long, unsigned long),
		char *cret);
int CtdlIPCHighSpeedReadDownload(CtdlIPC *ipc, void **buf, size_t bytes,
		size_t resume,
		void (*progress_gauge_callback)(CtdlIPC*, unsigned long, unsigned long),
		char *cret);
int CtdlIPCGenericCommand(CtdlIPC *ipc, const char *command,
		const char *to_send, size_t bytes_to_send, char **to_receive,
		size_t *bytes_to_receive, char *proto_response);

/* Internals */
int starttls(CtdlIPC *ipc);
void setCryptoStatusHook(void (*hook)(char *s));
void CtdlIPC_SetNetworkStatusCallback(CtdlIPC *ipc, void (*hook)(int state));
/* This is all Ford's doing.  FIXME: figure out what it's doing */
extern int (*error_printf)(char *s, ...);
void setIPCDeathHook(void (*hook)(void));
void setIPCErrorPrintf(int (*func)(char *s, ...));
void connection_died(CtdlIPC* ipc, int using_ssl);
int CtdlIPC_getsockfd(CtdlIPC* ipc);
char CtdlIPC_get(CtdlIPC* ipc);
void CtdlIPC_lock(CtdlIPC *ipc);
void CtdlIPC_unlock(CtdlIPC *ipc);
char *libcitadelclient_version_string(void);
void chatmode(CtdlIPC *ipc);
void page_user(CtdlIPC *ipc);
void quiet_mode(CtdlIPC *ipc);
void stealth_mode(CtdlIPC *ipc);
extern char last_paged[];

void determine_pwfilename(char *pwfile, size_t n);
void get_stored_password(
		char *host,
		char *port,
		char *username,
		char *password);
void set_stored_password(
		char *host,
		char *port,
		char *username,
		char *password);
void offer_to_remember_password(CtdlIPC *ipc,
		char *host,
		char *port,
		char *username,
		char *password);

void load_command_set(void);
void stty_ctdl(int cmd);
void newprompt(char *prompt, char *str, int len);
void strprompt(char *prompt, char *str, int len);
int boolprompt(char *prompt, int prev_val);
int intprompt(char *prompt, int ival, int imin, int imax);
int fmout(int width, FILE *fpin, char *text, FILE *fpout, int subst);
int getcmd(CtdlIPC *ipc, char *argbuf);
void display_help(CtdlIPC *ipc, char *name);
void color(int colornum);
void cls(int colornum);
void send_ansi_detect(void);
void look_for_ansi(void);
int inkey(void);
void set_keepalives(int s);
extern int enable_color;
int yesno(void);
int yesno_d(int d);
void keyopt(char *);
char keymenu(char *menuprompt, char *menustring);
void async_ka_start(void);
void async_ka_end(void);
int checkpagin(int lp, unsigned int pagin, unsigned int height);
char was_a_key_pressed(void);

#ifdef __GNUC__
void pprintf(const char *format, ...) __attribute__((__format__(__printf__,1,2)));
#else
void pprintf(const char *format, ...);
#endif


extern char rc_url_cmd[SIZ];
extern char rc_open_cmd[SIZ];
extern char rc_gotmail_cmd[SIZ];
extern int lines_printed;
extern int rc_remember_passwords;

#ifndef MD5_H
#define MD5_H

struct MD5Context {
	uint32_t buf[4];
	uint32_t bits[2];
	uint32_t in[16];
};

void MD5Init(struct MD5Context *context);
void MD5Update(struct MD5Context *context, unsigned char const *buf,
	       unsigned len);
void MD5Final(unsigned char digest[16], struct MD5Context *context);
void MD5Transform(uint32_t buf[4], uint32_t const in[16]);
char *make_apop_string(char *realpass, char *nonce, char *buffer, size_t n);

/*
 * This is needed to make RSAREF happy on some MS-DOS compilers.
 */
#ifndef HAVE_OPENSSL
typedef struct MD5Context MD5_CTX;
#endif

#define MD5_DIGEST_LEN		16
#define MD5_HEXSTRING_SIZE	33

#endif /* !MD5_H */


#define MAXURLS		50	/* Max embedded URL's per message */
extern int num_urls;
extern char urls[MAXURLS][SIZ];

int ka_system(char *shc);
int entmsg(CtdlIPC *ipc, int is_reply, int c, int masquerade);
void readmsgs(CtdlIPC *ipc, enum MessageList c, enum MessageDirection rdir, int q);
void edit_system_message(CtdlIPC *ipc, char *which_message);
pid_t ka_wait(int *kstatus);
void list_urls(CtdlIPC *ipc);
int client_make_message(CtdlIPC *ipc,
			char *filename,		/* temporary file name */
			char *recipient,	/* NULL if it's not mail */
			int anon_type,		/* see MES_ types in header file */
			int format_type,
			int mode,
			char *subject,
			int subject_required
);
void citedit(FILE *);
char *load_message_from_file(FILE *src);
int file_checksum(char *filename);
void listzrooms(CtdlIPC *ipc);
void readinfo(CtdlIPC *ipc);
void forget(CtdlIPC *ipc);
void entroom(CtdlIPC *ipc);
void killroom(CtdlIPC *ipc);
void invite(CtdlIPC *ipc);
void kickout(CtdlIPC *ipc);
void editthisroom(CtdlIPC *ipc);
void roomdir(CtdlIPC *ipc);
void download(CtdlIPC *ipc, int proto);
void ungoto(CtdlIPC *ipc);
void dotungoto(CtdlIPC *ipc, char *towhere);
void whoknows(CtdlIPC *ipc);
void enterinfo(CtdlIPC *ipc);
void knrooms(CtdlIPC *ipc, int kn_floor_mode);
void dotknown(CtdlIPC *ipc, int what, char *match);
void load_floorlist(CtdlIPC *ipc);
void create_floor(CtdlIPC *ipc);
void edit_floor(CtdlIPC *ipc);
void kill_floor(CtdlIPC *ipc);
void enter_bio(CtdlIPC *ipc);
int save_buffer(void *file, size_t filelen, const char *pathname);
void destination_directory(char *dest, const char *supplied_filename);
void do_edit(CtdlIPC *ipc, char *desc, char *read_cmd, char *check_cmd, char *write_cmd);


/* 
 * This struct holds a list of rooms for client display.
 * (oooh, a tree!)
 */
struct ctdlroomlisting {
        struct ctdlroomlisting *lnext;
	struct ctdlroomlisting *rnext;
        char rlname[ROOMNAMELEN];
        unsigned rlflags;
	int rlfloor;
        int rlorder;
        };


enum {
        LISTRMS_NEW_ONLY,
        LISTRMS_OLD_ONLY,
        LISTRMS_ALL
};


void updatels(CtdlIPC *ipc);
void updatelsa(CtdlIPC *ipc);
void movefile(CtdlIPC *ipc);
void deletefile(CtdlIPC *ipc);
void netsendfile(CtdlIPC *ipc);
void entregis(CtdlIPC *ipc);
void subshell(void);
void upload(CtdlIPC *ipc, int c);
void cli_upload(CtdlIPC *ipc);
void validate(CtdlIPC *ipc);
void read_bio(CtdlIPC *ipc);
void cli_image_upload(CtdlIPC *ipc, char *keyname);
int room_prompt(unsigned int qrflags);
int val_user(CtdlIPC *ipc, char *user, int do_validate);

void edituser(CtdlIPC *ipc, int cmd);
void interr(int errnum);
int struncmp(char *lstr, char *rstr, int len);
int pattern(char *search, char *patn);
void enter_config(CtdlIPC* ipc, int mode);
void locate_host(CtdlIPC* ipc, char *hbuf);
void misc_server_cmd(CtdlIPC *ipc, char *cmd);
int nukedir(char *dirname);
void strproc(char *string);
void back(int spaces);
void progress(CtdlIPC* ipc, unsigned long curr, unsigned long cmax);
int set_attr(CtdlIPC *ipc, unsigned int sval, char *prompt, unsigned int sbit, int backwards);

void screen_new(void);
int scr_printf(char *fmt, ...);
#define SCR_NOBLOCK 0
#define SCR_BLOCK -1
int scr_getc(int delay);
int scr_putc(int c);
void scr_flush(void);
int scr_blockread(void);
sighandler_t scr_winch(int signum);
void wait_indicator(int state);
void ctdl_beep(void);
void scr_wait_indicator(int);
extern char status_line[];
extern void check_screen_dims(void);
extern int screenwidth;
extern int screenheight;
void do_internet_configuration(CtdlIPC *ipc);
void network_config_management(CtdlIPC *ipc, char *entrytype, char *comment);
void do_pop3client_configuration(CtdlIPC *ipc);
void do_rssclient_configuration(CtdlIPC *ipc);
void do_system_configuration(CtdlIPC *ipc);
extern char editor_path[PATH_MAX];
extern int enable_status_line;
