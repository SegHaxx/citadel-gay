
#ifndef MSGBASE_H
#define MSGBASE_H

enum {
	MSGS_ALL,
	MSGS_OLD,
	MSGS_NEW,
	MSGS_FIRST,
	MSGS_LAST,
	MSGS_GT,
	MSGS_EQ,
	MSGS_SEARCH,
	MSGS_LT
};

enum {
	MSG_HDRS_BRIEF = 0,
	MSG_HDRS_ALL = 1,
	MSG_HDRS_EUID = 4,
	MSG_HDRS_BRIEFFILTER = 8,
	MSG_HDRS_THREADS = 9
};

/*
 * Possible return codes from CtdlOutputMsg()
 */
enum {
	om_ok,
	om_not_logged_in,
	om_no_such_msg,
	om_mime_error,
	om_access_denied
};

/*
 * Values of "headers_only" when calling message output routines
 */
#define HEADERS_ALL	0	/* Headers and body */
#define	HEADERS_ONLY	1	/* Headers only */
#define	HEADERS_NONE	2	/* Body only */
#define HEADERS_FAST	3	/* Headers only with no MIME info */


struct ma_info {
	int is_ma;		/* Set to 1 if we are using this stuff */
	int freeze;		/* Freeze the replacement chain because we're
				 * digging through a subsection */
	int did_print;		/* One alternative has been displayed */
	char chosen_part[128];	/* Which part of a m/a did we choose? */
	int chosen_pref;	/* Chosen part preference level (lower is better) */
	int use_fo_hooks;	/* Use fixed output hooks */
	int dont_decode;        /* should we call the decoder or not? */
};


struct repl {			/* Info for replication checking */
	char exclusive_id[SIZ];
	time_t highest;
};


/*
 * This is a list of "harvested" email addresses that we might want to
 * stick into someone's address book.  But we defer this operaiton so
 * it can be done asynchronously.
 */
struct addresses_to_be_filed {
	struct addresses_to_be_filed *next;
	char *roomname;
	char *collected_addresses;
};

extern struct addresses_to_be_filed *atbf;

int GetFieldFromMnemonic(eMsgField *f, const char* c);
void memfmout (char *mptr, const char *nl);
void output_mime_parts(char *);
long send_message (struct CtdlMessage *);
void loadtroom (void);
long CtdlSubmitMsg(struct CtdlMessage *, struct recptypes *, const char *);
long quickie_message(char *from, char *fromaddr, char *to, char *room, char *text, int format_type, char *subject);
void GetMetaData(struct MetaData *, long);
void PutMetaData(struct MetaData *);
void AdjRefCount(long, int);
void TDAP_AdjRefCount(long, int);
int TDAP_ProcessAdjRefCountQueue(void);
void simple_listing(long, void *);
int CtdlMsgCmp(struct CtdlMessage *msg, struct CtdlMessage *template);
typedef void (*ForEachMsgCallback)(long MsgNumber, void *UserData);
int CtdlForEachMessage(int mode,
			long ref,
			char *searchstring,
			char *content_type,
			struct CtdlMessage *compare,
                        ForEachMsgCallback CallBack,
			void *userdata);
int CtdlDeleteMessages(const char *, long *, int, char *);
long CtdlWriteObject(char *req_room,			/* Room to stuff it in */
			char *content_type,		/* MIME type of this object */
			char *raw_message,		/* Data to be written */
			off_t raw_length,		/* Size of raw_message */
			struct ctdluser *is_mailbox,	/* Mailbox room? */
			int is_binary,			/* Is encoding necessary? */
			unsigned int flags		/* Internal save flags */
);
struct CtdlMessage *CtdlFetchMessage(long msgnum, int with_body);
struct CtdlMessage * CM_Duplicate
                       (struct CtdlMessage *OrgMsg);
int  CM_IsEmpty        (struct CtdlMessage *Msg, eMsgField which);
void CM_SetField       (struct CtdlMessage *Msg, eMsgField which, const char *buf, long length);
void CM_SetFieldLONG   (struct CtdlMessage *Msg, eMsgField which, long lvalue);
void CM_CopyField      (struct CtdlMessage *Msg, eMsgField WhichToPutTo, eMsgField WhichtToCopy);
void CM_CutFieldAt     (struct CtdlMessage *Msg, eMsgField WhichToCut, long maxlen);
void CM_FlushField     (struct CtdlMessage *Msg, eMsgField which);
void CM_Flush          (struct CtdlMessage *Msg);
void CM_SetAsField     (struct CtdlMessage *Msg, eMsgField which, char **buf, long length);
void CM_SetAsFieldSB   (struct CtdlMessage *Msg, eMsgField which, StrBuf **buf);
void CM_GetAsField     (struct CtdlMessage *Msg, eMsgField which, char **ret, long *retlen);
void CM_PrependToField (struct CtdlMessage *Msg, eMsgField which, const char *buf, long length);

void CM_Free           (struct CtdlMessage *msg);
void CM_FreeContents   (struct CtdlMessage *msg);
int  CM_IsValidMsg     (struct CtdlMessage *msg);

#define CM_KEY(Message, Which) Message->cm_fields[Which], Message->cm_lengths[Which]
#define CM_RANGE(Message, Which) Message->cm_fields[Which], \
		Message->cm_fields[Which] + Message->cm_lengths[Which]

void CtdlSerializeMessage(struct ser_ret *, struct CtdlMessage *);
struct CtdlMessage *CtdlDeserializeMessage(long msgnum, int with_body, const char *Buffer, long Length);
void ReplicationChecks(struct CtdlMessage *);
int CtdlSaveMsgPointersInRoom(char *roomname, long newmsgidlist[], int num_newmsgs,
			      int do_repl_check, struct CtdlMessage *supplied_msg, int suppress_refcount_adj
);
int CtdlSaveMsgPointerInRoom(char *roomname, long msgid, int do_repl_check, struct CtdlMessage *msg);
long CtdlSaveThisMessage(struct CtdlMessage *msg, long msgid, int Reply);
char *CtdlReadMessageBody(char *terminator, long tlen, size_t maxlen, StrBuf *exist, int crlf);
StrBuf *CtdlReadMessageBodyBuf(
		char *terminator,	/* token signalling EOT */
		long tlen,
		size_t maxlen,		/* maximum message length */
		StrBuf *exist,		/* if non-null, append to it; exist is ALWAYS freed  */
		int crlf		/* CRLF newlines instead of LF */
);

int CtdlOutputMsg(long msg_num,		/* message number (local) to fetch */
		int mode,		/* how would you like that message? */
		int headers_only,	/* eschew the message body? */
		int do_proto,		/* do Citadel protocol responses? */
		int crlf,		/* 0=LF, 1=CRLF */
		char *section,		/* output a message/rfc822 section */
		int flags,		/* should the bessage be exported clean? */
		char **Author,		/* if you want to know the author of the message... */
		char **Address,		/* if you want to know the sender address of the message... */
		char **MessageID	/* if you want to know the Message-ID of the message... */
);

/* Flags which may be passed to CtdlOutputMsg() and CtdlOutputPreLoadedMsg() */
#define QP_EADDR	(1<<0)		/* quoted-printable encode email addresses */
#define CRLF		(1<<1)
#define ESC_DOT		(1<<2)		/* output a line containing only "." as ".." instead */
#define SUPPRESS_ENV_TO	(1<<3)		/* suppress Envelope-to: header (warning: destructive!) */

int CtdlOutputPreLoadedMsg(struct CtdlMessage *,
			   int mode,		/* how would you like that message? */
			   int headers_only,	/* eschew the message body? */
			   int do_proto,	/* do Citadel protocol responses? */
			   int crlf,		/* 0=LF, 1=CRLF */
			   int flags		/* should the bessage be exported clean? */
);


/* values for which_set */
enum {
	ctdlsetseen_seen,
	ctdlsetseen_answered
};

void CtdlSetSeen(long *target_msgnums, int num_target_msgnums,
	int target_setting, int which_set,
	struct ctdluser *which_user, struct ctdlroom *which_room
);

void CtdlGetSeen(char *buf, int which_set);


struct CtdlMessage *CtdlMakeMessage(
        struct ctdluser *author,        /* author's user structure */
        char *recipient,                /* NULL if it's not mail */
        char *recp_cc,	                /* NULL if it's not mail */
        char *room,                     /* room where it's going */
        int type,                       /* see MES_ types in header file */
        int format_type,                /* variformat, plain text, MIME... */
        char *fake_name,                /* who we're masquerading as */
	char *my_email,			/* which of my email addresses to use (empty is ok) */
        char *subject,                  /* Subject (optional) */
	char *supplied_euid,		/* ...or NULL if this is irrelevant */
        char *preformatted_text,        /* ...or NULL to read text from client */
	char *references		/* Thread references */
);

struct CtdlMessage *CtdlMakeMessageLen(
	struct ctdluser *author,	/* author's user structure */
	char *recipient,		/* NULL if it's not mail */
	long rcplen,
	char *recp_cc,			/* NULL if it's not mail */
	long cclen,
	char *room,			/* room where it's going */
	long roomlen,
	int type,			/* see MES_ types in header file */
	int format_type,		/* variformat, plain text, MIME... */
	char *fake_name,		/* who we're masquerading as */
	long fnlen,
	char *my_email,			/* which of my email addresses to use (empty is ok) */
	long myelen,
	char *subject,			/* Subject (optional) */
	long subjlen,
	char *supplied_euid,		/* ...or NULL if this is irrelevant */
	long euidlen,
	char *preformatted_text,	/* ...or NULL to read text from client */
	long textlen,
	char *references,		/* Thread references */
	long reflen
);

void AdjRefCountList(long *msgnum, long nmsg, int incr);

#endif /* MSGBASE_H */
