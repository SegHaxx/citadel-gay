// Implements the message store.
//
// Copyright (c) 1987-2023 by the citadel.org team
//
// This program is open source software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License version 3.

#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <regex.h>
#include <sys/stat.h>
#include <assert.h>
#include <libcitadel.h>
#include "ctdl_module.h"
#include "citserver.h"
#include "control.h"
#include "config.h"
#include "clientsocket.h"
#include "genstamp.h"
#include "room_ops.h"
#include "user_ops.h"
#include "internet_addressing.h"
#include "euidindex.h"
#include "msgbase.h"
#include "journaling.h"

struct addresses_to_be_filed *atbf = NULL;

// These are the four-character field headers we use when outputting
// messages in Citadel format (as opposed to RFC822 format).
char *msgkeys[] = {
	NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, 
	NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, 
	NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, 
	NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, 
	NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, 
	NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, 
	NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, 
	NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, 
	NULL, 
	"from", // A -> eAuthor
	NULL,   // B -> eBig_message
	NULL,   // C (formerly used as eRemoteRoom)
	NULL,   // D (formerly used as eDestination)
	"exti", // E -> eXclusivID
	"rfca", // F -> erFc822Addr
	NULL,   // G
	"hnod", // H (formerly used as eHumanNode)
	"msgn", // I -> emessageId
	"jrnl", // J -> eJournal
	"rep2", // K -> eReplyTo
	"list", // L -> eListID
	"text", // M -> eMesageText
	NULL,	// N (formerly used as eNodename)
	"room", // O -> eOriginalRoom
	"path", // P -> eMessagePath
	NULL,   // Q
	"rcpt", // R -> eRecipient
	NULL,	// S (formerly used as eSpecialField)
	"time", // T -> eTimestamp
	"subj", // U -> eMsgSubject
	"nvto", // V -> eenVelopeTo
	"wefw", // W -> eWeferences
	NULL,   // X
	"cccc", // Y -> eCarbonCopY
	NULL	// Z
};


HashList *msgKeyLookup = NULL;

int GetFieldFromMnemonic(eMsgField *f, const char* c) {
	void *v = NULL;
	if (GetHash(msgKeyLookup, c, 4, &v)) {
		*f = (eMsgField) v;
		return 1;
	}
	return 0;
}

void FillMsgKeyLookupTable(void) {
	long i;

	msgKeyLookup = NewHash (1, FourHash);

	for (i=0; i < 91; i++) {
		if (msgkeys[i] != NULL) {
			Put(msgKeyLookup, msgkeys[i], 4, (void*)i, reference_free_handler);
		}
	}
}


eMsgField FieldOrder[]  = {
/* Important fields */
	emessageId   ,
	eMessagePath ,
	eTimestamp   ,
	eAuthor      ,
	erFc822Addr  ,
	eOriginalRoom,
	eRecipient   ,
/* Semi-important fields */
	eBig_message ,
	eExclusiveID ,
	eWeferences  ,
	eJournal     ,
/* G is not used yet */
	eReplyTo     ,
	eListID      ,
/* Q is not used yet */
	eenVelopeTo  ,
/* X is not used yet */
/* Z is not used yet */
	eCarbonCopY  ,
	eMsgSubject  ,
/* internal only */
	eErrorMsg    ,
	eSuppressIdx ,
	eExtnotify   ,
/* Message text (MUST be last) */
	eMesageText 
/* Not saved to disk: 
	eVltMsgNum
*/
};

static const long NDiskFields = sizeof(FieldOrder) / sizeof(eMsgField);


int CM_IsEmpty(struct CtdlMessage *Msg, eMsgField which) {
	return !((Msg->cm_fields[which] != NULL) && (Msg->cm_fields[which][0] != '\0'));
}


void CM_SetField(struct CtdlMessage *Msg, eMsgField which, const char *buf, long length) {
	if (Msg->cm_fields[which] != NULL) {
		free (Msg->cm_fields[which]);
	}
	if (length < 0) {			// You can set the length to -1 to have CM_SetField measure it for you
		length = strlen(buf);
	}
	Msg->cm_fields[which] = malloc(length + 1);
	memcpy(Msg->cm_fields[which], buf, length);
	Msg->cm_fields[which][length] = '\0';
	Msg->cm_lengths[which] = length;
}


void CM_SetFieldLONG(struct CtdlMessage *Msg, eMsgField which, long lvalue) {
	char buf[128];
	long len;
	len = snprintf(buf, sizeof(buf), "%ld", lvalue);
	CM_SetField(Msg, which, buf, len);
}


void CM_CutFieldAt(struct CtdlMessage *Msg, eMsgField WhichToCut, long maxlen) {
	if (Msg->cm_fields[WhichToCut] == NULL)
		return;

	if (Msg->cm_lengths[WhichToCut] > maxlen)
	{
		Msg->cm_fields[WhichToCut][maxlen] = '\0';
		Msg->cm_lengths[WhichToCut] = maxlen;
	}
}


void CM_FlushField(struct CtdlMessage *Msg, eMsgField which) {
	if (Msg->cm_fields[which] != NULL)
		free (Msg->cm_fields[which]);
	Msg->cm_fields[which] = NULL;
	Msg->cm_lengths[which] = 0;
}


void CM_Flush(struct CtdlMessage *Msg) {
	int i;

	if (CM_IsValidMsg(Msg) == 0) {
		return;
	}

	for (i = 0; i < 256; ++i) {
		CM_FlushField(Msg, i);
	}
}


void CM_CopyField(struct CtdlMessage *Msg, eMsgField WhichToPutTo, eMsgField WhichtToCopy) {
	long len;
	if (Msg->cm_fields[WhichToPutTo] != NULL) {
		free (Msg->cm_fields[WhichToPutTo]);
	}

	if (Msg->cm_fields[WhichtToCopy] != NULL) {
		len = Msg->cm_lengths[WhichtToCopy];
		Msg->cm_fields[WhichToPutTo] = malloc(len + 1);
		memcpy(Msg->cm_fields[WhichToPutTo], Msg->cm_fields[WhichtToCopy], len);
		Msg->cm_fields[WhichToPutTo][len] = '\0';
		Msg->cm_lengths[WhichToPutTo] = len;
	}
	else {
		Msg->cm_fields[WhichToPutTo] = NULL;
		Msg->cm_lengths[WhichToPutTo] = 0;
	}
}


void CM_PrependToField(struct CtdlMessage *Msg, eMsgField which, const char *buf, long length) {
	if (Msg->cm_fields[which] != NULL) {
		long oldmsgsize;
		long newmsgsize;
		char *new;

		oldmsgsize = Msg->cm_lengths[which] + 1;
		newmsgsize = length + oldmsgsize;

		new = malloc(newmsgsize);
		memcpy(new, buf, length);
		memcpy(new + length, Msg->cm_fields[which], oldmsgsize);
		free(Msg->cm_fields[which]);
		Msg->cm_fields[which] = new;
		Msg->cm_lengths[which] = newmsgsize - 1;
	}
	else {
		Msg->cm_fields[which] = malloc(length + 1);
		memcpy(Msg->cm_fields[which], buf, length);
		Msg->cm_fields[which][length] = '\0';
		Msg->cm_lengths[which] = length;
	}
}


void CM_SetAsField(struct CtdlMessage *Msg, eMsgField which, char **buf, long length) {
	if (Msg->cm_fields[which] != NULL) {
		free (Msg->cm_fields[which]);
	}

	Msg->cm_fields[which] = *buf;
	*buf = NULL;
	if (length < 0) {			// You can set the length to -1 to have CM_SetField measure it for you
		Msg->cm_lengths[which] = strlen(Msg->cm_fields[which]);
	}
	else {
		Msg->cm_lengths[which] = length;
	}
}


void CM_SetAsFieldSB(struct CtdlMessage *Msg, eMsgField which, StrBuf **buf) {
	if (Msg->cm_fields[which] != NULL) {
		free (Msg->cm_fields[which]);
	}

	Msg->cm_lengths[which] = StrLength(*buf);
	Msg->cm_fields[which] = SmashStrBuf(buf);
}


void CM_GetAsField(struct CtdlMessage *Msg, eMsgField which, char **ret, long *retlen) {
	if (Msg->cm_fields[which] != NULL) {
		*retlen = Msg->cm_lengths[which];
		*ret = Msg->cm_fields[which];
		Msg->cm_fields[which] = NULL;
		Msg->cm_lengths[which] = 0;
	}
	else {
		*ret = NULL;
		*retlen = 0;
	}
}


// Returns 1 if the supplied pointer points to a valid Citadel message.
// If the pointer is NULL or the magic number check fails, returns 0.
int CM_IsValidMsg(struct CtdlMessage *msg) {
	if (msg == NULL) {
		return 0;
	}
	if ((msg->cm_magic) != CTDLMESSAGE_MAGIC) {
		syslog(LOG_WARNING, "msgbase: CM_IsValidMsg() self-check failed");
		return 0;
	}
	return 1;
}


void CM_FreeContents(struct CtdlMessage *msg) {
	int i;

	for (i = 0; i < 256; ++i)
		if (msg->cm_fields[i] != NULL) {
			free(msg->cm_fields[i]);
			msg->cm_lengths[i] = 0;
		}

	msg->cm_magic = 0;	// just in case
}


// 'Destructor' for struct CtdlMessage
void CM_Free(struct CtdlMessage *msg) {
	if (CM_IsValidMsg(msg) == 0) {
		if (msg != NULL) free (msg);
		return;
	}
	CM_FreeContents(msg);
	free(msg);
}


int CM_DupField(eMsgField i, struct CtdlMessage *OrgMsg, struct CtdlMessage *NewMsg) {
	long len;
	len = OrgMsg->cm_lengths[i];
	NewMsg->cm_fields[i] = malloc(len + 1);
	if (NewMsg->cm_fields[i] == NULL) {
		return 0;
	}
	memcpy(NewMsg->cm_fields[i], OrgMsg->cm_fields[i], len);
	NewMsg->cm_fields[i][len] = '\0';
	NewMsg->cm_lengths[i] = len;
	return 1;
}


struct CtdlMessage *CM_Duplicate(struct CtdlMessage *OrgMsg) {
	int i;
	struct CtdlMessage *NewMsg;

	if (CM_IsValidMsg(OrgMsg) == 0) {
		return NULL;
	}
	NewMsg = (struct CtdlMessage *)malloc(sizeof(struct CtdlMessage));
	if (NewMsg == NULL) {
		return NULL;
	}

	memcpy(NewMsg, OrgMsg, sizeof(struct CtdlMessage));

	memset(&NewMsg->cm_fields, 0, sizeof(char*) * 256);
	
	for (i = 0; i < 256; ++i) {
		if (OrgMsg->cm_fields[i] != NULL) {
			if (!CM_DupField(i, OrgMsg, NewMsg)) {
				CM_Free(NewMsg);
				return NULL;
			}
		}
	}

	return NewMsg;
}


// Determine if a given message matches the fields in a message template.
// Return 0 for a successful match.
int CtdlMsgCmp(struct CtdlMessage *msg, struct CtdlMessage *template) {
	int i;

	// If there aren't any fields in the template, all messages will match.
	if (template == NULL) return(0);

	// Null messages are bogus.
	if (msg == NULL) return(1);

	for (i='A'; i<='Z'; ++i) {
		if (template->cm_fields[i] != NULL) {
			if (msg->cm_fields[i] == NULL) {
				// Considered equal if temmplate is empty string
				if (IsEmptyStr(template->cm_fields[i])) continue;
				return 1;
			}
			if ((template->cm_lengths[i] != msg->cm_lengths[i]) ||
			    (strcasecmp(msg->cm_fields[i], template->cm_fields[i])))
				return 1;
		}
	}

	// All compares succeeded: we have a match!
	return 0;
}


// Retrieve the "seen" message list for the current room.
void CtdlGetSeen(char *buf, int which_set) {
	struct visit vbuf;

	// Learn about the user and room in question
	CtdlGetRelationship(&vbuf, &CC->user, &CC->room);

	if (which_set == ctdlsetseen_seen) {
		safestrncpy(buf, vbuf.v_seen, SIZ);
	}
	if (which_set == ctdlsetseen_answered) {
		safestrncpy(buf, vbuf.v_answered, SIZ);
	}
}


// Manipulate the "seen msgs" string (or other message set strings)
void CtdlSetSeen(long *target_msgnums, int num_target_msgnums,
		int target_setting, int which_set,
		struct ctdluser *which_user, struct ctdlroom *which_room) {
	struct cdbdata *cdbfr;
	int i, k;
	int is_seen = 0;
	int was_seen = 0;
	long lo = (-1L);
	long hi = (-1L);
	struct visit vbuf;
	long *msglist;
	int num_msgs = 0;
	StrBuf *vset;
	StrBuf *setstr;
	StrBuf *lostr;
	StrBuf *histr;
	const char *pvset;
	char *is_set;	// actually an array of booleans

	// Don't bother doing *anything* if we were passed a list of zero messages
	if (num_target_msgnums < 1) {
		return;
	}

	// If no room was specified, we go with the current room.
	if (!which_room) {
		which_room = &CC->room;
	}

	// If no user was specified, we go with the current user.
	if (!which_user) {
		which_user = &CC->user;
	}

	syslog(LOG_DEBUG, "msgbase: CtdlSetSeen(%d msgs starting with %ld, %s, %d) in <%s>",
		   num_target_msgnums, target_msgnums[0],
		   (target_setting ? "SET" : "CLEAR"),
		   which_set,
		   which_room->QRname);

	// Learn about the user and room in question
	CtdlGetRelationship(&vbuf, which_user, which_room);

	// Load the message list
	cdbfr = cdb_fetch(CDB_MSGLISTS, &which_room->QRnumber, sizeof(long));
	if (cdbfr != NULL) {
		msglist = (long *) cdbfr->ptr;
		cdbfr->ptr = NULL;	// CtdlSetSeen() now owns this memory
		num_msgs = cdbfr->len / sizeof(long);
		cdb_free(cdbfr);
	}
	else {
		return;	// No messages at all?  No further action.
	}

	is_set = malloc(num_msgs * sizeof(char));
	memset(is_set, 0, (num_msgs * sizeof(char)) );

	// Decide which message set we're manipulating
	switch(which_set) {
	case ctdlsetseen_seen:
		vset = NewStrBufPlain(vbuf.v_seen, -1);
		break;
	case ctdlsetseen_answered:
		vset = NewStrBufPlain(vbuf.v_answered, -1);
		break;
	default:
		vset = NewStrBuf();
	}


#if 0	// This is a special diagnostic section.  Do not allow it to run during normal operation.
	syslog(LOG_DEBUG, "There are %d messages in the room.\n", num_msgs);
	for (i=0; i<num_msgs; ++i) {
		if ((i > 0) && (msglist[i] <= msglist[i-1])) abort();
	}
	syslog(LOG_DEBUG, "We are twiddling %d of them.\n", num_target_msgnums);
	for (k=0; k<num_target_msgnums; ++k) {
		if ((k > 0) && (target_msgnums[k] <= target_msgnums[k-1])) abort();
	}
#endif

	// Translate the existing sequence set into an array of booleans
	setstr = NewStrBuf();
	lostr = NewStrBuf();
	histr = NewStrBuf();
	pvset = NULL;
	while (StrBufExtract_NextToken(setstr, vset, &pvset, ',') >= 0) {

		StrBufExtract_token(lostr, setstr, 0, ':');
		if (StrBufNum_tokens(setstr, ':') >= 2) {
			StrBufExtract_token(histr, setstr, 1, ':');
		}
		else {
			FlushStrBuf(histr);
			StrBufAppendBuf(histr, lostr, 0);
		}
		lo = StrTol(lostr);
		if (!strcmp(ChrPtr(histr), "*")) {
			hi = LONG_MAX;
		}
		else {
			hi = StrTol(histr);
		}

		for (i = 0; i < num_msgs; ++i) {
			if ((msglist[i] >= lo) && (msglist[i] <= hi)) {
				is_set[i] = 1;
			}
		}
	}
	FreeStrBuf(&setstr);
	FreeStrBuf(&lostr);
	FreeStrBuf(&histr);

	// Now translate the array of booleans back into a sequence set
	FlushStrBuf(vset);
	was_seen = 0;
	lo = (-1);
	hi = (-1);

	for (i=0; i<num_msgs; ++i) {
		is_seen = is_set[i];

		// Apply changes
		for (k=0; k<num_target_msgnums; ++k) {
			if (msglist[i] == target_msgnums[k]) {
				is_seen = target_setting;
			}
		}

		if ((was_seen == 0) && (is_seen == 1)) {
			lo = msglist[i];
		}
		else if ((was_seen == 1) && (is_seen == 0)) {
			hi = msglist[i-1];

			if (StrLength(vset) > 0) {
				StrBufAppendBufPlain(vset, HKEY(","), 0);
			}
			if (lo == hi) {
				StrBufAppendPrintf(vset, "%ld", hi);
			}
			else {
				StrBufAppendPrintf(vset, "%ld:%ld", lo, hi);
			}
		}

		if ((is_seen) && (i == num_msgs - 1)) {
			if (StrLength(vset) > 0) {
				StrBufAppendBufPlain(vset, HKEY(","), 0);
			}
			if ((i==0) || (was_seen == 0)) {
				StrBufAppendPrintf(vset, "%ld", msglist[i]);
			}
			else {
				StrBufAppendPrintf(vset, "%ld:%ld", lo, msglist[i]);
			}
		}

		was_seen = is_seen;
	}

	// We will have to stuff this string back into a 4096 byte buffer, so if it's
	// larger than that now, truncate it by removing tokens from the beginning.
	// The limit of 100 iterations is there to prevent an infinite loop in case
	// something unexpected happens.
	int number_of_truncations = 0;
	while ( (StrLength(vset) > SIZ) && (number_of_truncations < 100) ) {
		StrBufRemove_token(vset, 0, ',');
		++number_of_truncations;
	}

	// If we're truncating the sequence set of messages marked with the 'seen' flag,
	// we want the earliest messages (the truncated ones) to be marked, not unmarked.
	// Otherwise messages at the beginning will suddenly appear to be 'unseen'.
	if ( (which_set == ctdlsetseen_seen) && (number_of_truncations > 0) ) {
		StrBuf *first_tok;
		first_tok = NewStrBuf();
		StrBufExtract_token(first_tok, vset, 0, ',');
		StrBufRemove_token(vset, 0, ',');

		if (StrBufNum_tokens(first_tok, ':') > 1) {
			StrBufRemove_token(first_tok, 0, ':');
		}
		
		StrBuf *new_set;
		new_set = NewStrBuf();
		StrBufAppendBufPlain(new_set, HKEY("1:"), 0);
		StrBufAppendBuf(new_set, first_tok, 0);
		StrBufAppendBufPlain(new_set, HKEY(":"), 0);
		StrBufAppendBuf(new_set, vset, 0);

		FreeStrBuf(&vset);
		FreeStrBuf(&first_tok);
		vset = new_set;
	}

	// Decide which message set we're manipulating.  Zero the buffers so they compress well.
	switch (which_set) {
		case ctdlsetseen_seen:
			memset(vbuf.v_seen, 0, sizeof vbuf.v_seen);
			safestrncpy(vbuf.v_seen, ChrPtr(vset), sizeof vbuf.v_seen);
			break;
		case ctdlsetseen_answered:
			memset(vbuf.v_answered, 0, sizeof vbuf.v_seen);
			safestrncpy(vbuf.v_answered, ChrPtr(vset), sizeof vbuf.v_answered);
			break;
	}

	free(is_set);
	free(msglist);
	CtdlSetRelationship(&vbuf, which_user, which_room);
	FreeStrBuf(&vset);
}


// API function to perform an operation for each qualifying message in the
// current room.  (Returns the number of messages processed.)
int CtdlForEachMessage(int mode, long ref, char *search_string,
			char *content_type,
			struct CtdlMessage *compare,
                        ForEachMsgCallback CallBack,
			void *userdata)
{
	int a, i, j;
	struct visit vbuf;
	struct cdbdata *cdbfr;
	long *msglist = NULL;
	int num_msgs = 0;
	int num_processed = 0;
	long thismsg;
	struct MetaData smi;
	struct CtdlMessage *msg = NULL;
	int is_seen = 0;
	long lastold = 0L;
	int printed_lastold = 0;
	int num_search_msgs = 0;
	long *search_msgs = NULL;
	regex_t re;
	int need_to_free_re = 0;
	regmatch_t pm;

	if ((content_type) && (!IsEmptyStr(content_type))) {
		regcomp(&re, content_type, 0);
		need_to_free_re = 1;
	}

	// Learn about the user and room in question
	if (server_shutting_down) {
		if (need_to_free_re) regfree(&re);
		return -1;
	}
	CtdlGetUser(&CC->user, CC->curr_user);

	if (server_shutting_down) {
		if (need_to_free_re) regfree(&re);
		return -1;
	}
	CtdlGetRelationship(&vbuf, &CC->user, &CC->room);

	if (server_shutting_down) {
		if (need_to_free_re) regfree(&re);
		return -1;
	}

	// Load the message list
	cdbfr = cdb_fetch(CDB_MSGLISTS, &CC->room.QRnumber, sizeof(long));
	if (cdbfr == NULL) {
		if (need_to_free_re) regfree(&re);
		return 0;	// No messages at all?  No further action.
	}

	msglist = (long *) cdbfr->ptr;
	num_msgs = cdbfr->len / sizeof(long);

	cdbfr->ptr = NULL;	// clear this so that cdb_free() doesn't free it
	cdb_free(cdbfr);	// we own this memory now

	/*
	 * Now begin the traversal.
	 */
	if (num_msgs > 0) for (a = 0; a < num_msgs; ++a) {

		/* If the caller is looking for a specific MIME type, filter
		 * out all messages which are not of the type requested.
	 	 */
		if ((content_type != NULL) && (!IsEmptyStr(content_type))) {

			/* This call to GetMetaData() sits inside this loop
			 * so that we only do the extra database read per msg
			 * if we need to.  Doing the extra read all the time
			 * really kills the server.  If we ever need to use
			 * metadata for another search criterion, we need to
			 * move the read somewhere else -- but still be smart
			 * enough to only do the read if the caller has
			 * specified something that will need it.
			 */
			if (server_shutting_down) {
				if (need_to_free_re) regfree(&re);
				free(msglist);
				return -1;
			}
			GetMetaData(&smi, msglist[a]);

			/* if (strcasecmp(smi.meta_content_type, content_type)) { old non-regex way */
			if (regexec(&re, smi.meta_content_type, 1, &pm, 0) != 0) {
				msglist[a] = 0L;
			}
		}
	}

	num_msgs = sort_msglist(msglist, num_msgs);

	/* If a template was supplied, filter out the messages which
	 * don't match.  (This could induce some delays!)
	 */
	if (num_msgs > 0) {
		if (compare != NULL) {
			for (a = 0; a < num_msgs; ++a) {
				if (server_shutting_down) {
					if (need_to_free_re) regfree(&re);
					free(msglist);
					return -1;
				}
				msg = CtdlFetchMessage(msglist[a], 1);
				if (msg != NULL) {
					if (CtdlMsgCmp(msg, compare)) {
						msglist[a] = 0L;
					}
					CM_Free(msg);
				}
			}
		}
	}

	/* If a search string was specified, get a message list from
	 * the full text index and remove messages which aren't on both
	 * lists.
	 *
	 * How this works:
	 * Since the lists are sorted and strictly ascending, and the
	 * output list is guaranteed to be shorter than or equal to the
	 * input list, we overwrite the bottom of the input list.  This
	 * eliminates the need to memmove big chunks of the list over and
	 * over again.
	 */
	if ( (num_msgs > 0) && (mode == MSGS_SEARCH) && (search_string) ) {

		/* Call search module via hook mechanism.
		 * NULL means use any search function available.
		 * otherwise replace with a char * to name of search routine
		 */
		CtdlModuleDoSearch(&num_search_msgs, &search_msgs, search_string, "fulltext");

		if (num_search_msgs > 0) {
	
			int orig_num_msgs;

			orig_num_msgs = num_msgs;
			num_msgs = 0;
			for (i=0; i<orig_num_msgs; ++i) {
				for (j=0; j<num_search_msgs; ++j) {
					if (msglist[i] == search_msgs[j]) {
						msglist[num_msgs++] = msglist[i];
					}
				}
			}
		}
		else {
			num_msgs = 0;	/* No messages qualify */
		}
		if (search_msgs != NULL) free(search_msgs);

		/* Now that we've purged messages which don't contain the search
		 * string, treat a MSGS_SEARCH just like a MSGS_ALL from this
		 * point on.
		 */
		mode = MSGS_ALL;
	}

	/*
	 * Now iterate through the message list, according to the
	 * criteria supplied by the caller.
	 */
	if (num_msgs > 0)
		for (a = 0; a < num_msgs; ++a) {
			if (server_shutting_down) {
				if (need_to_free_re) regfree(&re);
				free(msglist);
				return num_processed;
			}
			thismsg = msglist[a];
			if (mode == MSGS_ALL) {
				is_seen = 0;
			}
			else {
				is_seen = is_msg_in_sequence_set(vbuf.v_seen, thismsg);
				if (is_seen) lastold = thismsg;
			}
			if (
				(thismsg > 0L)
			&& (
				(mode == MSGS_ALL)
				|| ((mode == MSGS_OLD) && (is_seen))
				|| ((mode == MSGS_NEW) && (!is_seen))
				|| ((mode == MSGS_LAST) && (a >= (num_msgs - ref)))
				|| ((mode == MSGS_FIRST) && (a < ref))
				|| ((mode == MSGS_GT) && (thismsg > ref))
				|| ((mode == MSGS_LT) && (thismsg < ref))
				|| ((mode == MSGS_EQ) && (thismsg == ref))
			    )
			    ) {
				if ((mode == MSGS_NEW) && (CC->user.flags & US_LASTOLD) && (lastold > 0L) && (printed_lastold == 0) && (!is_seen)) {
					if (CallBack) {
						CallBack(lastold, userdata);
					}
					printed_lastold = 1;
					++num_processed;
				}
				if (CallBack) {
					CallBack(thismsg, userdata);
				}
				++num_processed;
			}
		}
	if (need_to_free_re) regfree(&re);

	/*
	 * We cache the most recent msglist in order to do security checks later
	 */
	if (CC->client_socket > 0) {
		if (CC->cached_msglist != NULL) {
			free(CC->cached_msglist);
		}
		CC->cached_msglist = msglist;
		CC->cached_num_msgs = num_msgs;
	}
	else {
		free(msglist);
	}

	return num_processed;
}


/*
 * memfmout()  -  Citadel text formatter and paginator.
 *	     Although the original purpose of this routine was to format
 *	     text to the reader's screen width, all we're really using it
 *	     for here is to format text out to 80 columns before sending it
 *	     to the client.  The client software may reformat it again.
 */
void memfmout(
	char *mptr,		/* where are we going to get our text from? */
	const char *nl		/* string to terminate lines with */
) {
	int column = 0;
	unsigned char ch = 0;
	char outbuf[1024];
	int len = 0;
	int nllen = 0;

	if (!mptr) return;
	nllen = strlen(nl);
	while (ch=*(mptr++), ch != 0) {

		if (ch == '\n') {
			if (client_write(outbuf, len) == -1) {
				syslog(LOG_ERR, "msgbase: memfmout() aborting due to write failure");
				return;
			}
			len = 0;
			if (client_write(nl, nllen) == -1) {
				syslog(LOG_ERR, "msgbase: memfmout() aborting due to write failure");
				return;
			}
			column = 0;
		}
		else if (ch == '\r') {
			/* Ignore carriage returns.  Newlines are always LF or CRLF but never CR. */
		}
		else if (isspace(ch)) {
			if (column > 72) {		/* Beyond 72 columns, break on the next space */
				if (client_write(outbuf, len) == -1) {
					syslog(LOG_ERR, "msgbase: memfmout() aborting due to write failure");
					return;
				}
				len = 0;
				if (client_write(nl, nllen) == -1) {
					syslog(LOG_ERR, "msgbase: memfmout() aborting due to write failure");
					return;
				}
				column = 0;
			}
			else {
				outbuf[len++] = ch;
				++column;
			}
		}
		else {
			outbuf[len++] = ch;
			++column;
			if (column > 1000) {		/* Beyond 1000 columns, break anywhere */
				if (client_write(outbuf, len) == -1) {
					syslog(LOG_ERR, "msgbase: memfmout() aborting due to write failure");
					return;
				}
				len = 0;
				if (client_write(nl, nllen) == -1) {
					syslog(LOG_ERR, "msgbase: memfmout(): aborting due to write failure");
					return;
				}
				column = 0;
			}
		}
	}
	if (len) {
		if (client_write(outbuf, len) == -1) {
			syslog(LOG_ERR, "msgbase: memfmout() aborting due to write failure");
			return;
		}
		client_write(nl, nllen);
		column = 0;
	}
}


/*
 * Callback function for mime parser that simply lists the part
 */
void list_this_part(char *name, char *filename, char *partnum, char *disp,
		    void *content, char *cbtype, char *cbcharset, size_t length, char *encoding,
		    char *cbid, void *cbuserdata)
{
	struct ma_info *ma;
	
	ma = (struct ma_info *)cbuserdata;
	if (ma->is_ma == 0) {
		cprintf("part=%s|%s|%s|%s|%s|%ld|%s|%s\n",
			name, 
			filename, 
			partnum, 
			disp, 
			cbtype, 
			(long)length, 
			cbid, 
			cbcharset);
	}
}


/* 
 * Callback function for multipart prefix
 */
void list_this_pref(char *name, char *filename, char *partnum, char *disp,
		    void *content, char *cbtype, char *cbcharset, size_t length, char *encoding,
		    char *cbid, void *cbuserdata)
{
	struct ma_info *ma;
	
	ma = (struct ma_info *)cbuserdata;
	if (!strcasecmp(cbtype, "multipart/alternative")) {
		++ma->is_ma;
	}

	if (ma->is_ma == 0) {
		cprintf("pref=%s|%s\n", partnum, cbtype);
	}
}


/* 
 * Callback function for multipart sufffix
 */
void list_this_suff(char *name, char *filename, char *partnum, char *disp,
		    void *content, char *cbtype, char *cbcharset, size_t length, char *encoding,
		    char *cbid, void *cbuserdata)
{
	struct ma_info *ma;
	
	ma = (struct ma_info *)cbuserdata;
	if (ma->is_ma == 0) {
		cprintf("suff=%s|%s\n", partnum, cbtype);
	}
	if (!strcasecmp(cbtype, "multipart/alternative")) {
		--ma->is_ma;
	}
}


/*
 * Callback function for mime parser that opens a section for downloading
 * we use serv_files function here: 
 */
extern void OpenCmdResult(char *filename, const char *mime_type);
void mime_download(char *name, char *filename, char *partnum, char *disp,
		   void *content, char *cbtype, char *cbcharset, size_t length,
		   char *encoding, char *cbid, void *cbuserdata)
{
	int rv = 0;

	/* Silently go away if there's already a download open. */
	if (CC->download_fp != NULL)
		return;

	if (
		(!IsEmptyStr(partnum) && (!strcasecmp(CC->download_desired_section, partnum)))
	||	(!IsEmptyStr(cbid) && (!strcasecmp(CC->download_desired_section, cbid)))
	) {
		CC->download_fp = tmpfile();
		if (CC->download_fp == NULL) {
			syslog(LOG_ERR, "msgbase: mime_download() couldn't write: %m");
			cprintf("%d cannot open temporary file: %s\n", ERROR + INTERNAL_ERROR, strerror(errno));
			return;
		}
	
		rv = fwrite(content, length, 1, CC->download_fp);
		if (rv <= 0) {
			syslog(LOG_ERR, "msgbase: mime_download() Couldn't write: %m");
			cprintf("%d unable to write tempfile.\n", ERROR + TOO_BIG);
			fclose(CC->download_fp);
			CC->download_fp = NULL;
			return;
		}
		fflush(CC->download_fp);
		rewind(CC->download_fp);
	
		OpenCmdResult(filename, cbtype);
	}
}


/*
 * Callback function for mime parser that outputs a section all at once.
 * We can specify the desired section by part number *or* content-id.
 */
void mime_spew_section(char *name, char *filename, char *partnum, char *disp,
		   void *content, char *cbtype, char *cbcharset, size_t length,
		   char *encoding, char *cbid, void *cbuserdata)
{
	int *found_it = (int *)cbuserdata;

	if (
		(!IsEmptyStr(partnum) && (!strcasecmp(CC->download_desired_section, partnum)))
	||	(!IsEmptyStr(cbid) && (!strcasecmp(CC->download_desired_section, cbid)))
	) {
		*found_it = 1;
		cprintf("%d %d|-1|%s|%s|%s\n",
			BINARY_FOLLOWS,
			(int)length,
			filename,
			cbtype,
			cbcharset
		);
		client_write(content, length);
	}
}


struct CtdlMessage *CtdlDeserializeMessage(long msgnum, int with_body, const char *Buffer, long Length) {
	struct CtdlMessage *ret = NULL;
	const char *mptr;
	const char *upper_bound;
	cit_uint8_t ch;
	cit_uint8_t field_header;
	eMsgField which;

	mptr = Buffer;
	upper_bound = Buffer + Length;
	if (msgnum <= 0) {
		return NULL;
	}

	// Parse the three bytes that begin EVERY message on disk.
	// The first is always 0xFF, the on-disk magic number.
	// The second is the anonymous/public type byte.
	// The third is the format type byte (vari, fixed, or MIME).
	//
	ch = *mptr++;
	if (ch != 255) {
		syslog(LOG_ERR, "msgbase: message %ld appears to be corrupted", msgnum);
		return NULL;
	}
	ret = (struct CtdlMessage *) malloc(sizeof(struct CtdlMessage));
	memset(ret, 0, sizeof(struct CtdlMessage));

	ret->cm_magic = CTDLMESSAGE_MAGIC;
	ret->cm_anon_type = *mptr++;				// Anon type byte
	ret->cm_format_type = *mptr++;				// Format type byte

	// The rest is zero or more arbitrary fields.  Load them in.
	// We're done when we encounter either a zero-length field or
	// have just processed the 'M' (message text) field.
	//
	do {
		field_header = '\0';
		long len;

		while (field_header == '\0') {			// work around possibly buggy messages
			if (mptr >= upper_bound) {
				break;
			}
			field_header = *mptr++;
		}
		if (mptr >= upper_bound) {
			break;
		}
		which = field_header;
		len = strlen(mptr);

		CM_SetField(ret, which, mptr, len);

		mptr += len + 1;				// advance to next field

	} while ((mptr < upper_bound) && (field_header != 'M'));
	return (ret);
}


// Load a message from disk into memory.
// This is used by CtdlOutputMsg() and other fetch functions.
//
// NOTE: Caller is responsible for freeing the returned CtdlMessage struct
//       using the CM_Free(); function.
//
struct CtdlMessage *CtdlFetchMessage(long msgnum, int with_body) {
	struct cdbdata *dmsgtext;
	struct CtdlMessage *ret = NULL;

	syslog(LOG_DEBUG, "msgbase: CtdlFetchMessage(%ld, %d)", msgnum, with_body);
	dmsgtext = cdb_fetch(CDB_MSGMAIN, &msgnum, sizeof(long));
	if (dmsgtext == NULL) {
		syslog(LOG_ERR, "msgbase: message #%ld was not found", msgnum);
		return NULL;
	}

	if (dmsgtext->ptr[dmsgtext->len - 1] != '\0') {
		syslog(LOG_ERR, "msgbase: CtdlFetchMessage(%ld, %d) Forcefully terminating message!!", msgnum, with_body);
		dmsgtext->ptr[dmsgtext->len - 1] = '\0';
	}

	ret = CtdlDeserializeMessage(msgnum, with_body, dmsgtext->ptr, dmsgtext->len);

	cdb_free(dmsgtext);

	if (ret == NULL) {
		return NULL;
	}

	// Always make sure there's something in the msg text field.  If
	// it's NULL, the message text is most likely stored separately,
	// so go ahead and fetch that.  Failing that, just set a dummy
	// body so other code doesn't barf.
	//
	if ( (CM_IsEmpty(ret, eMesageText)) && (with_body) ) {
		dmsgtext = cdb_fetch(CDB_BIGMSGS, &msgnum, sizeof(long));
		if (dmsgtext != NULL) {
			CM_SetAsField(ret, eMesageText, &dmsgtext->ptr, dmsgtext->len - 1);
			cdb_free(dmsgtext);
		}
	}
	if (CM_IsEmpty(ret, eMesageText)) {
		CM_SetField(ret, eMesageText, HKEY("\r\n\r\n (no text)\r\n"));
	}

	return (ret);
}


// Pre callback function for multipart/alternative
//
// NOTE: this differs from the standard behavior for a reason.  Normally when
//       displaying multipart/alternative you want to show the _last_ usable
//       format in the message.  Here we show the _first_ one, because it's
//       usually text/plain.  Since this set of functions is designed for text
//       output to non-MIME-aware clients, this is the desired behavior.
//
void fixed_output_pre(char *name, char *filename, char *partnum, char *disp,
	  	void *content, char *cbtype, char *cbcharset, size_t length, char *encoding,
		char *cbid, void *cbuserdata)
{
	struct ma_info *ma;
	
	ma = (struct ma_info *)cbuserdata;
	syslog(LOG_DEBUG, "msgbase: fixed_output_pre() type=<%s>", cbtype);	
	if (!strcasecmp(cbtype, "multipart/alternative")) {
		++ma->is_ma;
		ma->did_print = 0;
	}
	if (!strcasecmp(cbtype, "message/rfc822")) {
		++ma->freeze;
	}
}


//
// Post callback function for multipart/alternative
//
void fixed_output_post(char *name, char *filename, char *partnum, char *disp,
	  	void *content, char *cbtype, char *cbcharset, size_t length,
		char *encoding, char *cbid, void *cbuserdata)
{
	struct ma_info *ma;
	
	ma = (struct ma_info *)cbuserdata;
	syslog(LOG_DEBUG, "msgbase: fixed_output_post() type=<%s>", cbtype);	
	if (!strcasecmp(cbtype, "multipart/alternative")) {
		--ma->is_ma;
		ma->did_print = 0;
	}
	if (!strcasecmp(cbtype, "message/rfc822")) {
		--ma->freeze;
	}
}


// Inline callback function for mime parser that wants to display text
void fixed_output(char *name, char *filename, char *partnum, char *disp,
	  	void *content, char *cbtype, char *cbcharset, size_t length,
		char *encoding, char *cbid, void *cbuserdata)
{
	char *ptr;
	char *wptr;
	size_t wlen;
	struct ma_info *ma;

	ma = (struct ma_info *)cbuserdata;

	syslog(LOG_DEBUG,
		"msgbase: fixed_output() part %s: %s (%s) (%ld bytes)",
		partnum, filename, cbtype, (long)length
	);

	// If we're in the middle of a multipart/alternative scope and
	// we've already printed another section, skip this one.
   	if ( (ma->is_ma) && (ma->did_print) ) {
		syslog(LOG_DEBUG, "msgbase: skipping part %s (%s)", partnum, cbtype);
		return;
	}
	ma->did_print = 1;

	if ( (!strcasecmp(cbtype, "text/plain")) 
	   || (IsEmptyStr(cbtype)) ) {
		wptr = content;
		if (length > 0) {
			client_write(wptr, length);
			if (wptr[length-1] != '\n') {
				cprintf("\n");
			}
		}
		return;
	}

	if (!strcasecmp(cbtype, "text/html")) {
		ptr = html_to_ascii(content, length, 80, 0);
		wlen = strlen(ptr);
		client_write(ptr, wlen);
		if ((wlen > 0) && (ptr[wlen-1] != '\n')) {
			cprintf("\n");
		}
		free(ptr);
		return;
	}

	if (ma->use_fo_hooks) {
		if (PerformFixedOutputHooks(cbtype, content, length)) {		// returns nonzero if it handled the part
			return;
		}
	}

	if (strncasecmp(cbtype, "multipart/", 10)) {
		cprintf("Part %s: %s (%s) (%ld bytes)\r\n",
			partnum, filename, cbtype, (long)length);
		return;
	}
}


// The client is elegant and sophisticated and wants to be choosy about
// MIME content types, so figure out which multipart/alternative part
// we're going to send.
//
// We use a system of weights.  When we find a part that matches one of the
// MIME types we've declared as preferential, we can store it in ma->chosen_part
// and then set ma->chosen_pref to that MIME type's position in our preference
// list.  If we then hit another match, we only replace the first match if
// the preference value is lower.
void choose_preferred(char *name, char *filename, char *partnum, char *disp,
	  	void *content, char *cbtype, char *cbcharset, size_t length,
		char *encoding, char *cbid, void *cbuserdata)
{
	char buf[1024];
	int i;
	struct ma_info *ma;
	
	ma = (struct ma_info *)cbuserdata;

	for (i=0; i<num_tokens(CC->preferred_formats, '|'); ++i) {
		extract_token(buf, CC->preferred_formats, i, '|', sizeof buf);
		if ( (!strcasecmp(buf, cbtype)) && (!ma->freeze) ) {
			if (i < ma->chosen_pref) {
				syslog(LOG_DEBUG, "msgbase: setting chosen part to <%s>", partnum);
				safestrncpy(ma->chosen_part, partnum, sizeof ma->chosen_part);
				ma->chosen_pref = i;
			}
		}
	}
}


// Now that we've chosen our preferred part, output it.
void output_preferred(char *name, 
		      char *filename, 
		      char *partnum, 
		      char *disp,
		      void *content, 
		      char *cbtype, 
		      char *cbcharset, 
		      size_t length,
		      char *encoding, 
		      char *cbid, 
		      void *cbuserdata)
{
	int i;
	char buf[128];
	int add_newline = 0;
	char *text_content;
	struct ma_info *ma;
	char *decoded = NULL;
	size_t bytes_decoded;
	int rc = 0;

	ma = (struct ma_info *)cbuserdata;

	// This is not the MIME part you're looking for...
	if (strcasecmp(partnum, ma->chosen_part)) return;

	// If the content-type of this part is in our preferred formats
	// list, we can simply output it verbatim.
	for (i=0; i<num_tokens(CC->preferred_formats, '|'); ++i) {
		extract_token(buf, CC->preferred_formats, i, '|', sizeof buf);
		if (!strcasecmp(buf, cbtype)) {
			/* Yeah!  Go!  W00t!! */
			if (ma->dont_decode == 0) 
				rc = mime_decode_now (content, 
						      length,
						      encoding,
						      &decoded,
						      &bytes_decoded);
			if (rc < 0)
				break; // Give us the chance, maybe theres another one.

			if (rc == 0) text_content = (char *)content;
			else {
				text_content = decoded;
				length = bytes_decoded;
			}

			if (text_content[length-1] != '\n') {
				++add_newline;
			}
			cprintf("Content-type: %s", cbtype);
			if (!IsEmptyStr(cbcharset)) {
				cprintf("; charset=%s", cbcharset);
			}
			cprintf("\nContent-length: %d\n",
				(int)(length + add_newline) );
			if (!IsEmptyStr(encoding)) {
				cprintf("Content-transfer-encoding: %s\n", encoding);
			}
			else {
				cprintf("Content-transfer-encoding: 7bit\n");
			}
			cprintf("X-Citadel-MSG4-Partnum: %s\n", partnum);
			cprintf("\n");
			if (client_write(text_content, length) == -1)
			{
				syslog(LOG_ERR, "msgbase: output_preferred() aborting due to write failure");
				return;
			}
			if (add_newline) cprintf("\n");
			if (decoded != NULL) free(decoded);
			return;
		}
	}

	// No translations required or possible: output as text/plain
	cprintf("Content-type: text/plain\n\n");
	rc = 0;
	if (ma->dont_decode == 0)
		rc = mime_decode_now (content, 
				      length,
				      encoding,
				      &decoded,
				      &bytes_decoded);
	if (rc < 0)
		return; // Give us the chance, maybe theres another one.
	
	if (rc == 0) text_content = (char *)content;
	else {
		text_content = decoded;
		length = bytes_decoded;
	}

	fixed_output(name, filename, partnum, disp, text_content, cbtype, cbcharset, length, encoding, cbid, cbuserdata);
	if (decoded != NULL) free(decoded);
}


struct encapmsg {
	char desired_section[64];
	char *msg;
	size_t msglen;
};


// Callback function
void extract_encapsulated_message(char *name, char *filename, char *partnum, char *disp,
		   void *content, char *cbtype, char *cbcharset, size_t length,
		   char *encoding, char *cbid, void *cbuserdata)
{
	struct encapmsg *encap;

	encap = (struct encapmsg *)cbuserdata;

	// Only proceed if this is the desired section...
	if (!strcasecmp(encap->desired_section, partnum)) {
		encap->msglen = length;
		encap->msg = malloc(length + 2);
		memcpy(encap->msg, content, length);
		return;
	}
}


// Determine whether the specified message exists in the cached_msglist
// (This is a security check)
int check_cached_msglist(long msgnum) {

	// cases in which we skip the check
	if (!CC) return om_ok;						// not a session
	if (CC->client_socket <= 0) return om_ok;			// not a client session
	if (CC->cached_msglist == NULL) return om_access_denied;	// no msglist fetched
	if (CC->cached_num_msgs == 0) return om_access_denied;		// nothing to check 

	// Do a binary search within the cached_msglist for the requested msgnum
	int min = 0;
	int max = (CC->cached_num_msgs - 1);

	while (max >= min) {
		int middle = min + (max-min) / 2 ;
		if (msgnum == CC->cached_msglist[middle]) {
			return om_ok;
		}
		if (msgnum > CC->cached_msglist[middle]) {
			min = middle + 1;
		}
		else {
			max = middle - 1;
		}
	}

	return om_access_denied;
}


// Get a message off disk.  (returns om_* values found in msgbase.h)
int CtdlOutputMsg(long msg_num,		// message number (local) to fetch
		int mode,		// how would you like that message?
		int headers_only,	// eschew the message body?
		int do_proto,		// do Citadel protocol responses?
		int crlf,		// Use CRLF newlines instead of LF?
		char *section,	 	// NULL or a message/rfc822 section
		int flags,		// various flags; see msgbase.h
		char **Author,
		char **Address,
		char **MessageID
) {
	struct CtdlMessage *TheMessage = NULL;
	int retcode = CIT_OK;
	struct encapmsg encap;
	int r;

	syslog(LOG_DEBUG, "msgbase: CtdlOutputMsg(msgnum=%ld, mode=%d, section=%s)", 
		msg_num, mode,
		(section ? section : "<>")
	);

	r = CtdlDoIHavePermissionToReadMessagesInThisRoom();
	if (r != om_ok) {
		if (do_proto) {
			if (r == om_not_logged_in) {
				cprintf("%d Not logged in.\n", ERROR + NOT_LOGGED_IN);
			}
			else {
				cprintf("%d An unknown error has occurred.\n", ERROR);
			}
		}
		return(r);
	}

	/*
	 * Check to make sure the message is actually IN this room
	 */
	r = check_cached_msglist(msg_num);
	if (r == om_access_denied) {
		/* Not in the cache?  We get ONE shot to check it again. */
		CtdlForEachMessage(MSGS_ALL, 0L, NULL, NULL, NULL, NULL, NULL);
		r = check_cached_msglist(msg_num);
	}
	if (r != om_ok) {
		syslog(LOG_DEBUG, "msgbase: security check fail; message %ld is not in %s",
			   msg_num, CC->room.QRname
		);
		if (do_proto) {
			if (r == om_access_denied) {
				cprintf("%d message %ld was not found in this room\n",
					ERROR + HIGHER_ACCESS_REQUIRED,
					msg_num
				);
			}
		}
		return(r);
	}

	/*
	 * Fetch the message from disk.  If we're in HEADERS_FAST mode,
	 * request that we don't even bother loading the body into memory.
	 */
	if (headers_only == HEADERS_FAST) {
		TheMessage = CtdlFetchMessage(msg_num, 0);
	}
	else {
		TheMessage = CtdlFetchMessage(msg_num, 1);
	}

	if (TheMessage == NULL) {
		if (do_proto) cprintf("%d Can't locate msg %ld on disk\n",
			ERROR + MESSAGE_NOT_FOUND, msg_num);
		return(om_no_such_msg);
	}

	/* Here is the weird form of this command, to process only an
	 * encapsulated message/rfc822 section.
	 */
	if (section) if (!IsEmptyStr(section)) if (strcmp(section, "0")) {
		memset(&encap, 0, sizeof encap);
		safestrncpy(encap.desired_section, section, sizeof encap.desired_section);
		mime_parser(CM_RANGE(TheMessage, eMesageText),
			    *extract_encapsulated_message,
			    NULL, NULL, (void *)&encap, 0
			);

		if ((Author != NULL) && (*Author == NULL))
		{
			long len;
			CM_GetAsField(TheMessage, eAuthor, Author, &len);
		}
		if ((Address != NULL) && (*Address == NULL))
		{	
			long len;
			CM_GetAsField(TheMessage, erFc822Addr, Address, &len);
		}
		if ((MessageID != NULL) && (*MessageID == NULL))
		{	
			long len;
			CM_GetAsField(TheMessage, emessageId, MessageID, &len);
		}
		CM_Free(TheMessage);
		TheMessage = NULL;

		if (encap.msg) {
			encap.msg[encap.msglen] = 0;
			TheMessage = convert_internet_message(encap.msg);
			encap.msg = NULL;	/* no free() here, TheMessage owns it now */

			/* Now we let it fall through to the bottom of this
			 * function, because TheMessage now contains the
			 * encapsulated message instead of the top-level
			 * message.  Isn't that neat?
			 */
		}
		else {
			if (do_proto) {
				cprintf("%d msg %ld has no part %s\n",
					ERROR + MESSAGE_NOT_FOUND,
					msg_num,
					section);
			}
			retcode = om_no_such_msg;
		}

	}

	/* Ok, output the message now */
	if (retcode == CIT_OK)
		retcode = CtdlOutputPreLoadedMsg(TheMessage, mode, headers_only, do_proto, crlf, flags);
	if ((Author != NULL) && (*Author == NULL))
	{
		long len;
		CM_GetAsField(TheMessage, eAuthor, Author, &len);
	}
	if ((Address != NULL) && (*Address == NULL))
	{	
		long len;
		CM_GetAsField(TheMessage, erFc822Addr, Address, &len);
	}
	if ((MessageID != NULL) && (*MessageID == NULL))
	{	
		long len;
		CM_GetAsField(TheMessage, emessageId, MessageID, &len);
	}

	CM_Free(TheMessage);

	return(retcode);
}


void OutputCtdlMsgHeaders(struct CtdlMessage *TheMessage, int do_proto) {
	int i;
	char buf[SIZ];
	char display_name[256];

	/* begin header processing loop for Citadel message format */
	safestrncpy(display_name, "<unknown>", sizeof display_name);
	if (!CM_IsEmpty(TheMessage, eAuthor)) {
		strcpy(buf, TheMessage->cm_fields[eAuthor]);
		if (TheMessage->cm_anon_type == MES_ANONONLY) {
			safestrncpy(display_name, "****", sizeof display_name);
		}
		else if (TheMessage->cm_anon_type == MES_ANONOPT) {
			safestrncpy(display_name, "anonymous", sizeof display_name);
		}
		else {
			safestrncpy(display_name, buf, sizeof display_name);
		}
		if ((is_room_aide())
		    && ((TheMessage->cm_anon_type == MES_ANONONLY)
			|| (TheMessage->cm_anon_type == MES_ANONOPT))) {
			size_t tmp = strlen(display_name);
			snprintf(&display_name[tmp],
				 sizeof display_name - tmp,
				 " [%s]", buf);
		}
	}

	/* Now spew the header fields in the order we like them. */
	for (i=0; i< NDiskFields; ++i) {
		eMsgField Field;
		Field = FieldOrder[i];
		if (Field != eMesageText) {
			if ( (!CM_IsEmpty(TheMessage, Field)) && (msgkeys[Field] != NULL) ) {
				if ((Field == eenVelopeTo) || (Field == eRecipient) || (Field == eCarbonCopY)) {
					sanitize_truncated_recipient(TheMessage->cm_fields[Field]);
				}
				if (Field == eAuthor) {
					if (do_proto) {
						cprintf("%s=%s\n", msgkeys[Field], display_name);
					}
				}
				/* Masquerade display name if needed */
				else {
					if (do_proto) {
						cprintf("%s=%s\n", msgkeys[Field], TheMessage->cm_fields[Field]);
					}
				}
				/* Give the client a hint about whether the message originated locally */
				if (Field == erFc822Addr) {
					if (IsDirectory(TheMessage->cm_fields[Field] ,0)) {
						cprintf("locl=yes\n");				// message originated locally.
					}



				}
			}
		}
	}
}


void OutputRFC822MsgHeaders(
	struct CtdlMessage *TheMessage,
	int flags,		/* should the message be exported clean	*/
	const char *nl, int nlen,
	char *mid, long sizeof_mid,
	char *suser, long sizeof_suser,
	char *luser, long sizeof_luser,
	char *fuser, long sizeof_fuser,
	char *snode, long sizeof_snode)
{
	char datestamp[100];
	int subject_found = 0;
	char buf[SIZ];
	int i, j, k;
	char *mptr = NULL;
	char *mpptr = NULL;
	char *hptr;

	for (i = 0; i < NDiskFields; ++i) {
		if (TheMessage->cm_fields[FieldOrder[i]]) {
			mptr = mpptr = TheMessage->cm_fields[FieldOrder[i]];
			switch (FieldOrder[i]) {
			case eAuthor:
				safestrncpy(luser, mptr, sizeof_luser);
				safestrncpy(suser, mptr, sizeof_suser);
				break;
			case eCarbonCopY:
				if ((flags & QP_EADDR) != 0) {
					mptr = qp_encode_email_addrs(mptr);
				}
				sanitize_truncated_recipient(mptr);
				cprintf("CC: %s%s", mptr, nl);
				break;
			case eMessagePath:
				cprintf("Return-Path: %s%s", mptr, nl);
				break;
			case eListID:
				cprintf("List-ID: %s%s", mptr, nl);
				break;
			case eenVelopeTo:
				if ((flags & QP_EADDR) != 0) 
					mptr = qp_encode_email_addrs(mptr);
				hptr = mptr;
				while ((*hptr != '\0') && isspace(*hptr))
					hptr ++;
				if (!IsEmptyStr(hptr))
					cprintf("Envelope-To: %s%s", hptr, nl);
				break;
			case eMsgSubject:
				cprintf("Subject: %s%s", mptr, nl);
				subject_found = 1;
				break;
			case emessageId:
				safestrncpy(mid, mptr, sizeof_mid);
				break;
			case erFc822Addr:
				safestrncpy(fuser, mptr, sizeof_fuser);
				break;
			case eRecipient:
				if (haschar(mptr, '@') == 0) {
					sanitize_truncated_recipient(mptr);
					cprintf("To: %s@%s", mptr, CtdlGetConfigStr("c_fqdn"));
					cprintf("%s", nl);
				}
				else {
					if ((flags & QP_EADDR) != 0) {
						mptr = qp_encode_email_addrs(mptr);
					}
					sanitize_truncated_recipient(mptr);
					cprintf("To: %s", mptr);
					cprintf("%s", nl);
				}
				break;
			case eTimestamp:
				datestring(datestamp, sizeof datestamp, atol(mptr), DATESTRING_RFC822);
				cprintf("Date: %s%s", datestamp, nl);
				break;
			case eWeferences:
				cprintf("References: ");
				k = num_tokens(mptr, '|');
				for (j=0; j<k; ++j) {
					extract_token(buf, mptr, j, '|', sizeof buf);
					cprintf("<%s>", buf);
					if (j == (k-1)) {
						cprintf("%s", nl);
					}
					else {
						cprintf(" ");
					}
				}
				break;
			case eReplyTo:
				hptr = mptr;
				while ((*hptr != '\0') && isspace(*hptr))
					hptr ++;
				if (!IsEmptyStr(hptr))
					cprintf("Reply-To: %s%s", mptr, nl);
				break;

			case eExclusiveID:
			case eJournal:
			case eMesageText:
			case eBig_message:
			case eOriginalRoom:
			case eErrorMsg:
			case eSuppressIdx:
			case eExtnotify:
			case eVltMsgNum:
				/* these don't map to mime message headers. */
				break;
			}
			if (mptr != mpptr) {
				free (mptr);
			}
		}
	}
	if (subject_found == 0) {
		cprintf("Subject: (no subject)%s", nl);
	}
}


void Dump_RFC822HeadersBody(
	struct CtdlMessage *TheMessage,
	int headers_only,	/* eschew the message body? */
	int flags,		/* should the bessage be exported clean? */
	const char *nl, int nlen)
{
	cit_uint8_t prev_ch;
	int eoh = 0;
	const char *StartOfText = StrBufNOTNULL;
	char outbuf[1024];
	int outlen = 0;
	int nllen = strlen(nl);
	char *mptr;
	int lfSent = 0;

	mptr = TheMessage->cm_fields[eMesageText];

	prev_ch = '\0';
	while (*mptr != '\0') {
		if (*mptr == '\r') {
			/* do nothing */
		}
		else {
			if ((!eoh) &&
			    (*mptr == '\n'))
			{
				eoh = (*(mptr+1) == '\r') && (*(mptr+2) == '\n');
				if (!eoh)
					eoh = *(mptr+1) == '\n';
				if (eoh)
				{
					StartOfText = mptr;
					StartOfText = strchr(StartOfText, '\n');
					StartOfText = strchr(StartOfText, '\n');
				}
			}
			if (((headers_only == HEADERS_NONE) && (mptr >= StartOfText)) ||
			    ((headers_only == HEADERS_ONLY) && (mptr < StartOfText)) ||
			    ((headers_only != HEADERS_NONE) && 
			     (headers_only != HEADERS_ONLY))
			) {
				if (*mptr == '\n') {
					memcpy(&outbuf[outlen], nl, nllen);
					outlen += nllen;
					outbuf[outlen] = '\0';
				}
				else {
					outbuf[outlen++] = *mptr;
				}
			}
		}
		if (flags & ESC_DOT) {
			if ((prev_ch == '\n') && (*mptr == '.') && ((*(mptr+1) == '\r') || (*(mptr+1) == '\n'))) {
				outbuf[outlen++] = '.';
			}
			prev_ch = *mptr;
		}
		++mptr;
		if (outlen > 1000) {
			if (client_write(outbuf, outlen) == -1) {
				syslog(LOG_ERR, "msgbase: Dump_RFC822HeadersBody() aborting due to write failure");
				return;
			}
			lfSent =  (outbuf[outlen - 1] == '\n');
			outlen = 0;
		}
	}
	if (outlen > 0) {
		client_write(outbuf, outlen);
		lfSent =  (outbuf[outlen - 1] == '\n');
	}
	if (!lfSent)
		client_write(nl, nlen);
}


/* If the format type on disk is 1 (fixed-format), then we want
 * everything to be output completely literally ... regardless of
 * what message transfer format is in use.
 */
void DumpFormatFixed(
	struct CtdlMessage *TheMessage,
	int mode,		/* how would you like that message? */
	const char *nl, int nllen)
{
	cit_uint8_t ch;
	char buf[SIZ];
	int buflen;
	int xlline = 0;
	char *mptr;

	mptr = TheMessage->cm_fields[eMesageText];
	
	if (mode == MT_MIME) {
		cprintf("Content-type: text/plain\n\n");
	}
	*buf = '\0';
	buflen = 0;
	while (ch = *mptr++, ch > 0) {
		if (ch == '\n')
			ch = '\r';

		if ((buflen > 250) && (!xlline)){
			int tbuflen;
			tbuflen = buflen;

			while ((buflen > 0) && 
			       (!isspace(buf[buflen])))
				buflen --;
			if (buflen == 0) {
				xlline = 1;
			}
			else {
				mptr -= tbuflen - buflen;
				buf[buflen] = '\0';
				ch = '\r';
			}
		}

		/* if we reach the outer bounds of our buffer, abort without respect for what we purge. */
		if (xlline && ((isspace(ch)) || (buflen > SIZ - nllen - 2))) {
			ch = '\r';
		}

		if (ch == '\r') {
			memcpy (&buf[buflen], nl, nllen);
			buflen += nllen;
			buf[buflen] = '\0';

			if (client_write(buf, buflen) == -1) {
				syslog(LOG_ERR, "msgbase: DumpFormatFixed() aborting due to write failure");
				return;
			}
			*buf = '\0';
			buflen = 0;
			xlline = 0;
		} else {
			buf[buflen] = ch;
			buflen++;
		}
	}
	buf[buflen] = '\0';
	if (!IsEmptyStr(buf)) {
		cprintf("%s%s", buf, nl);
	}
}


/*
 * Get a message off disk.  (returns om_* values found in msgbase.h)
 */
int CtdlOutputPreLoadedMsg(
		struct CtdlMessage *TheMessage,
		int mode,		/* how would you like that message? */
		int headers_only,	/* eschew the message body? */
		int do_proto,		/* do Citadel protocol responses? */
		int crlf,		/* Use CRLF newlines instead of LF? */
		int flags		/* should the bessage be exported clean? */
) {
	int i;
	const char *nl;	/* newline string */
	int nlen;
	struct ma_info ma;

	/* Buffers needed for RFC822 translation.  These are all filled
	 * using functions that are bounds-checked, and therefore we can
	 * make them substantially smaller than SIZ.
	 */
	char suser[1024];
	char luser[1024];
	char fuser[1024];
	char snode[1024];
	char mid[1024];

	syslog(LOG_DEBUG, "msgbase: CtdlOutputPreLoadedMsg(TheMessage=%s, %d, %d, %d, %d",
		   ((TheMessage == NULL) ? "NULL" : "not null"),
		   mode, headers_only, do_proto, crlf
	);

	strcpy(mid, "unknown");
	nl = (crlf ? "\r\n" : "\n");
	nlen = crlf ? 2 : 1;

	if (!CM_IsValidMsg(TheMessage)) {
		syslog(LOG_ERR, "msgbase: error; invalid preloaded message for output");
	 	return(om_no_such_msg);
	}

	/* Suppress envelope recipients if required to avoid disclosing BCC addresses.
	 * Pad it with spaces in order to avoid changing the RFC822 length of the message.
	 */
	if ( (flags & SUPPRESS_ENV_TO) && (!CM_IsEmpty(TheMessage, eenVelopeTo)) ) {
		memset(TheMessage->cm_fields[eenVelopeTo], ' ', TheMessage->cm_lengths[eenVelopeTo]);
	}
		
	/* Are we downloading a MIME component? */
	if (mode == MT_DOWNLOAD) {
		if (TheMessage->cm_format_type != FMT_RFC822) {
			if (do_proto)
				cprintf("%d This is not a MIME message.\n",
				ERROR + ILLEGAL_VALUE);
		} else if (CC->download_fp != NULL) {
			if (do_proto) cprintf(
				"%d You already have a download open.\n",
				ERROR + RESOURCE_BUSY);
		} else {
			/* Parse the message text component */
			mime_parser(CM_RANGE(TheMessage, eMesageText),
				    *mime_download, NULL, NULL, NULL, 0);
			/* If there's no file open by this time, the requested
			 * section wasn't found, so print an error
			 */
			if (CC->download_fp == NULL) {
				if (do_proto) cprintf(
					"%d Section %s not found.\n",
					ERROR + FILE_NOT_FOUND,
					CC->download_desired_section);
			}
		}
		return((CC->download_fp != NULL) ? om_ok : om_mime_error);
	}

	// MT_SPEW_SECTION is like MT_DOWNLOAD except it outputs the whole MIME part
	// in a single server operation instead of opening a download file.
	if (mode == MT_SPEW_SECTION) {
		if (TheMessage->cm_format_type != FMT_RFC822) {
			if (do_proto)
				cprintf("%d This is not a MIME message.\n",
				ERROR + ILLEGAL_VALUE);
		}
		else {
			// Locate and parse the component specified by the caller
			int found_it = 0;
			mime_parser(CM_RANGE(TheMessage, eMesageText), *mime_spew_section, NULL, NULL, (void *)&found_it, 0);

			// If section wasn't found, print an error
			if (!found_it) {
				if (do_proto) {
					cprintf( "%d Section %s not found.\n", ERROR + FILE_NOT_FOUND, CC->download_desired_section);
				}
			}
		}
		return((CC->download_fp != NULL) ? om_ok : om_mime_error);
	}

	// now for the user-mode message reading loops
	if (do_proto) cprintf("%d msg:\n", LISTING_FOLLOWS);

	// Does the caller want to skip the headers?
	if (headers_only == HEADERS_NONE) goto START_TEXT;

	// Tell the client which format type we're using.
	if ( (mode == MT_CITADEL) && (do_proto) ) {
		cprintf("type=%d\n", TheMessage->cm_format_type);	// Tell the client which format type we're using.
	}

	// nhdr=yes means that we're only displaying headers, no body
	if ( (TheMessage->cm_anon_type == MES_ANONONLY)
	   && ((mode == MT_CITADEL) || (mode == MT_MIME))
	   && (do_proto)
	   ) {
		cprintf("nhdr=yes\n");
	}

	if ((mode == MT_CITADEL) || (mode == MT_MIME)) {
		OutputCtdlMsgHeaders(TheMessage, do_proto);
	}

	// begin header processing loop for RFC822 transfer format
	strcpy(suser, "");
	strcpy(luser, "");
	strcpy(fuser, "");
	strcpy(snode, "");
	if (mode == MT_RFC822) 
		OutputRFC822MsgHeaders(
			TheMessage,
			flags,
			nl, nlen,
			mid, sizeof(mid),
			suser, sizeof(suser),
			luser, sizeof(luser),
			fuser, sizeof(fuser),
			snode, sizeof(snode)
			);


	for (i=0; !IsEmptyStr(&suser[i]); ++i) {
		suser[i] = tolower(suser[i]);
		if (!isalnum(suser[i])) suser[i]='_';
	}

	if (mode == MT_RFC822) {
		/* Construct a fun message id */
		cprintf("Message-ID: <%s", mid);
		if (strchr(mid, '@')==NULL) {
			cprintf("@%s", snode);
		}
		cprintf(">%s", nl);

		if (!is_room_aide() && (TheMessage->cm_anon_type == MES_ANONONLY)) {
			cprintf("From: \"----\" <x@x.org>%s", nl);
		}
		else if (!is_room_aide() && (TheMessage->cm_anon_type == MES_ANONOPT)) {
			cprintf("From: \"anonymous\" <x@x.org>%s", nl);
		}
		else if (!IsEmptyStr(fuser)) {
			cprintf("From: \"%s\" <%s>%s", luser, fuser, nl);
		}
		else {
			cprintf("From: \"%s\" <%s@%s>%s", luser, suser, snode, nl);
		}

		/* Blank line signifying RFC822 end-of-headers */
		if (TheMessage->cm_format_type != FMT_RFC822) {
			cprintf("%s", nl);
		}
	}

	// end header processing loop ... at this point, we're in the text
START_TEXT:
	if (headers_only == HEADERS_FAST) goto DONE;

	// Tell the client about the MIME parts in this message
	if (TheMessage->cm_format_type == FMT_RFC822) {
		if ( (mode == MT_CITADEL) || (mode == MT_MIME) ) {
			memset(&ma, 0, sizeof(struct ma_info));
			mime_parser(CM_RANGE(TheMessage, eMesageText),
				(do_proto ? *list_this_part : NULL),
				(do_proto ? *list_this_pref : NULL),
				(do_proto ? *list_this_suff : NULL),
				(void *)&ma, 1);
		}
		else if (mode == MT_RFC822) {	// unparsed RFC822 dump
			Dump_RFC822HeadersBody(
				TheMessage,
				headers_only,
				flags,
				nl, nlen);
			goto DONE;
		}
	}

	if (headers_only == HEADERS_ONLY) {
		goto DONE;
	}

	// signify start of msg text
	if ( (mode == MT_CITADEL) || (mode == MT_MIME) ) {
		if (do_proto) cprintf("text\n");
	}

	if (TheMessage->cm_format_type == FMT_FIXED) 
		DumpFormatFixed(
			TheMessage,
			mode,		// how would you like that message?
			nl, nlen);

	// If the message on disk is format 0 (Citadel vari-format), we
	// output using the formatter at 80 columns.  This is the final output
	// form if the transfer format is RFC822, but if the transfer format
	// is Citadel proprietary, it'll still work, because the indentation
	// for new paragraphs is correct and the client will reformat the
	// message to the reader's screen width.
	//
	if (TheMessage->cm_format_type == FMT_CITADEL) {
		if (mode == MT_MIME) {
			cprintf("Content-type: text/x-citadel-variformat\n\n");
		}
		memfmout(TheMessage->cm_fields[eMesageText], nl);
	}

	// If the message on disk is format 4 (MIME), we've gotta hand it
	// off to the MIME parser.  The client has already been told that
	// this message is format 1 (fixed format), so the callback function
	// we use will display those parts as-is.
	//
	if (TheMessage->cm_format_type == FMT_RFC822) {
		memset(&ma, 0, sizeof(struct ma_info));

		if (mode == MT_MIME) {
			ma.use_fo_hooks = 0;
			strcpy(ma.chosen_part, "1");
			ma.chosen_pref = 9999;
			ma.dont_decode = CC->msg4_dont_decode;
			mime_parser(CM_RANGE(TheMessage, eMesageText),
				    *choose_preferred, *fixed_output_pre,
				    *fixed_output_post, (void *)&ma, 1);
			mime_parser(CM_RANGE(TheMessage, eMesageText),
				    *output_preferred, NULL, NULL, (void *)&ma, 1);
		}
		else {
			ma.use_fo_hooks = 1;
			mime_parser(CM_RANGE(TheMessage, eMesageText),
				    *fixed_output, *fixed_output_pre,
				    *fixed_output_post, (void *)&ma, 0);
		}

	}

DONE:	/* now we're done */
	if (do_proto) cprintf("000\n");
	return(om_ok);
}

// Save one or more message pointers into a specified room
// (Returns 0 for success, nonzero for failure)
// roomname may be NULL to use the current room
//
// Note that the 'supplied_msg' field may be set to NULL, in which case
// the message will be fetched from disk, by number, if we need to perform
// replication checks.  This adds an additional database read, so if the
// caller already has the message in memory then it should be supplied.  (Obviously
// this mode of operation only works if we're saving a single message.)
//
int CtdlSaveMsgPointersInRoom(char *roomname, long newmsgidlist[], int num_newmsgs,
			int do_repl_check, struct CtdlMessage *supplied_msg, int suppress_refcount_adj
) {
	int i, j, unique;
	char hold_rm[ROOMNAMELEN];
	struct cdbdata *cdbfr;
	int num_msgs;
	long *msglist;
	long highest_msg = 0L;

	long msgid = 0;
	struct CtdlMessage *msg = NULL;

	long *msgs_to_be_merged = NULL;
	int num_msgs_to_be_merged = 0;

	syslog(LOG_DEBUG,
		"msgbase: CtdlSaveMsgPointersInRoom(room=%s, num_msgs=%d, repl=%d, suppress_rca=%d)",
		roomname, num_newmsgs, do_repl_check, suppress_refcount_adj
	);

	strcpy(hold_rm, CC->room.QRname);

	/* Sanity checks */
	if (newmsgidlist == NULL) return(ERROR + INTERNAL_ERROR);
	if (num_newmsgs < 1) return(ERROR + INTERNAL_ERROR);
	if (num_newmsgs > 1) supplied_msg = NULL;

	/* Now the regular stuff */
	if (CtdlGetRoomLock(&CC->room,
	   ((roomname != NULL) ? roomname : CC->room.QRname) )
	   != 0) {
		syslog(LOG_ERR, "msgbase: no such room <%s>", roomname);
		return(ERROR + ROOM_NOT_FOUND);
	}


	msgs_to_be_merged = malloc(sizeof(long) * num_newmsgs);
	num_msgs_to_be_merged = 0;


	cdbfr = cdb_fetch(CDB_MSGLISTS, &CC->room.QRnumber, sizeof(long));
	if (cdbfr == NULL) {
		msglist = NULL;
		num_msgs = 0;
	} else {
		msglist = (long *) cdbfr->ptr;
		cdbfr->ptr = NULL;	/* CtdlSaveMsgPointerInRoom() now owns this memory */
		num_msgs = cdbfr->len / sizeof(long);
		cdb_free(cdbfr);
	}


	/* Create a list of msgid's which were supplied by the caller, but do
	 * not already exist in the target room.  It is absolutely taboo to
	 * have more than one reference to the same message in a room.
	 */
	for (i=0; i<num_newmsgs; ++i) {
		unique = 1;
		if (num_msgs > 0) for (j=0; j<num_msgs; ++j) {
			if (msglist[j] == newmsgidlist[i]) {
				unique = 0;
			}
		}
		if (unique) {
			msgs_to_be_merged[num_msgs_to_be_merged++] = newmsgidlist[i];
		}
	}

	syslog(LOG_DEBUG, "msgbase: %d unique messages to be merged", num_msgs_to_be_merged);

	/*
	 * Now merge the new messages
	 */
	msglist = realloc(msglist, (sizeof(long) * (num_msgs + num_msgs_to_be_merged)) );
	if (msglist == NULL) {
		syslog(LOG_ALERT, "msgbase: ERROR; can't realloc message list!");
		free(msgs_to_be_merged);
		return (ERROR + INTERNAL_ERROR);
	}
	memcpy(&msglist[num_msgs], msgs_to_be_merged, (sizeof(long) * num_msgs_to_be_merged) );
	num_msgs += num_msgs_to_be_merged;

	/* Sort the message list, so all the msgid's are in order */
	num_msgs = sort_msglist(msglist, num_msgs);

	/* Determine the highest message number */
	highest_msg = msglist[num_msgs - 1];

	/* Write it back to disk. */
	cdb_store(CDB_MSGLISTS, &CC->room.QRnumber, (int)sizeof(long),
		  msglist, (int)(num_msgs * sizeof(long)));

	/* Free up the memory we used. */
	free(msglist);

	/* Update the highest-message pointer and unlock the room. */
	CC->room.QRhighest = highest_msg;
	CtdlPutRoomLock(&CC->room);

	/* Perform replication checks if necessary */
	if ( (DoesThisRoomNeedEuidIndexing(&CC->room)) && (do_repl_check) ) {
		syslog(LOG_DEBUG, "msgbase: CtdlSaveMsgPointerInRoom() doing repl checks");

		for (i=0; i<num_msgs_to_be_merged; ++i) {
			msgid = msgs_to_be_merged[i];
	
			if (supplied_msg != NULL) {
				msg = supplied_msg;
			}
			else {
				msg = CtdlFetchMessage(msgid, 0);
			}
	
			if (msg != NULL) {
				ReplicationChecks(msg);
		
				/* If the message has an Exclusive ID, index that... */
				if (!CM_IsEmpty(msg, eExclusiveID)) {
					index_message_by_euid(msg->cm_fields[eExclusiveID], &CC->room, msgid);
				}

				/* Free up the memory we may have allocated */
				if (msg != supplied_msg) {
					CM_Free(msg);
				}
			}
	
		}
	}

	else {
		syslog(LOG_DEBUG, "msgbase: CtdlSaveMsgPointerInRoom() skips repl checks");
	}

	/* Submit this room for processing by hooks */
	int total_roomhook_errors = PerformRoomHooks(&CC->room);
	if (total_roomhook_errors) {
		syslog(LOG_WARNING, "msgbase: room hooks returned %d errors", total_roomhook_errors);
	}

	/* Go back to the room we were in before we wandered here... */
	CtdlGetRoom(&CC->room, hold_rm);

	/* Bump the reference count for all messages which were merged */
	if (!suppress_refcount_adj) {
		AdjRefCountList(msgs_to_be_merged, num_msgs_to_be_merged, +1);
	}

	/* Free up memory... */
	if (msgs_to_be_merged != NULL) {
		free(msgs_to_be_merged);
	}

	/* Return success. */
	return (0);
}


/*
 * This is the same as CtdlSaveMsgPointersInRoom() but it only accepts
 * a single message.
 */
int CtdlSaveMsgPointerInRoom(char *roomname, long msgid,
			     int do_repl_check, struct CtdlMessage *supplied_msg)
{
	return CtdlSaveMsgPointersInRoom(roomname, &msgid, 1, do_repl_check, supplied_msg, 0);
}


/*
 * Message base operation to save a new message to the message store
 * (returns new message number)
 *
 * This is the back end for CtdlSubmitMsg() and should not be directly
 * called by server-side modules.
 *
 */
long CtdlSaveThisMessage(struct CtdlMessage *msg, long msgid, int Reply) {
	long retval;
	struct ser_ret smr;
	int is_bigmsg = 0;
	char *holdM = NULL;
	long holdMLen = 0;

	/*
	 * If the message is big, set its body aside for storage elsewhere
	 * and we hide the message body from the serializer
	 */
	if (!CM_IsEmpty(msg, eMesageText) && msg->cm_lengths[eMesageText] > BIGMSG) {
		is_bigmsg = 1;
		holdM = msg->cm_fields[eMesageText];
		msg->cm_fields[eMesageText] = NULL;
		holdMLen = msg->cm_lengths[eMesageText];
		msg->cm_lengths[eMesageText] = 0;
	}

	/* Serialize our data structure for storage in the database */	
	CtdlSerializeMessage(&smr, msg);

	if (is_bigmsg) {
		/* put the message body back into the message */
		msg->cm_fields[eMesageText] = holdM;
		msg->cm_lengths[eMesageText] = holdMLen;
	}

	if (smr.len == 0) {
		if (Reply) {
			cprintf("%d Unable to serialize message\n",
				ERROR + INTERNAL_ERROR);
		}
		else {
			syslog(LOG_ERR, "msgbase: CtdlSaveMessage() unable to serialize message");

		}
		return (-1L);
	}

	/* Write our little bundle of joy into the message base */
	retval = cdb_store(CDB_MSGMAIN, &msgid, (int)sizeof(long), smr.ser, smr.len);
	if (retval < 0) {
		syslog(LOG_ERR, "msgbase: can't store message %ld: %ld", msgid, retval);
	}
	else {
		if (is_bigmsg) {
			retval = cdb_store(CDB_BIGMSGS,
					   &msgid,
					   (int)sizeof(long),
					   holdM,
					   (holdMLen + 1)
				);
			if (retval < 0) {
				syslog(LOG_ERR, "msgbase: failed to store message body for msgid %ld: %ld", msgid, retval);
			}
		}
	}

	/* Free the memory we used for the serialized message */
	free(smr.ser);

	return(retval);
}


long send_message(struct CtdlMessage *msg) {
	long newmsgid;
	long retval;
	char msgidbuf[256];
	long msgidbuflen;

	/* Get a new message number */
	newmsgid = get_new_message_number();

	/* Generate an ID if we don't have one already */
	if (CM_IsEmpty(msg, emessageId)) {
		msgidbuflen = snprintf(msgidbuf, sizeof msgidbuf, "%08lX-%08lX@%s",
				       (long unsigned int) time(NULL),
				       (long unsigned int) newmsgid,
				       CtdlGetConfigStr("c_fqdn")
			);

		CM_SetField(msg, emessageId, msgidbuf, msgidbuflen);
	}

	retval = CtdlSaveThisMessage(msg, newmsgid, 1);

	if (retval == 0) {
		retval = newmsgid;
	}

	/* Return the *local* message ID to the caller
	 * (even if we're storing an incoming network message)
	 */
	return(retval);
}


/*
 * Serialize a struct CtdlMessage into the format used on disk.
 * 
 * This function loads up a "struct ser_ret" (defined in server.h) which
 * contains the length of the serialized message and a pointer to the
 * serialized message in memory.  THE LATTER MUST BE FREED BY THE CALLER.
 */
void CtdlSerializeMessage(struct ser_ret *ret,		/* return values */
			  struct CtdlMessage *msg)	/* unserialized msg */
{
	size_t wlen;
	int i;

	/*
	 * Check for valid message format
	 */
	if (CM_IsValidMsg(msg) == 0) {
		syslog(LOG_ERR, "msgbase: CtdlSerializeMessage() aborting due to invalid message");
		ret->len = 0;
		ret->ser = NULL;
		return;
	}

	ret->len = 3;
	for (i=0; i < NDiskFields; ++i)
		if (msg->cm_fields[FieldOrder[i]] != NULL)
			ret->len += msg->cm_lengths[FieldOrder[i]] + 2;

	ret->ser = malloc(ret->len);
	if (ret->ser == NULL) {
		syslog(LOG_ERR, "msgbase: CtdlSerializeMessage() malloc(%ld) failed: %m", (long)ret->len);
		ret->len = 0;
		ret->ser = NULL;
		return;
	}

	ret->ser[0] = 0xFF;
	ret->ser[1] = msg->cm_anon_type;
	ret->ser[2] = msg->cm_format_type;
	wlen = 3;

	for (i=0; i < NDiskFields; ++i) {
		if (msg->cm_fields[FieldOrder[i]] != NULL) {
			ret->ser[wlen++] = (char)FieldOrder[i];

			memcpy(&ret->ser[wlen],
			       msg->cm_fields[FieldOrder[i]],
			       msg->cm_lengths[FieldOrder[i]] + 1);

			wlen = wlen + msg->cm_lengths[FieldOrder[i]] + 1;
		}
	}

	if (ret->len != wlen) {
		syslog(LOG_ERR, "msgbase: ERROR; len=%ld wlen=%ld", (long)ret->len, (long)wlen);
	}

	return;
}


/*
 * Check to see if any messages already exist in the current room which
 * carry the same Exclusive ID as this one.  If any are found, delete them.
 */
void ReplicationChecks(struct CtdlMessage *msg) {
	long old_msgnum = (-1L);

	if (DoesThisRoomNeedEuidIndexing(&CC->room) == 0) return;

	syslog(LOG_DEBUG, "msgbase: performing replication checks in <%s>", CC->room.QRname);

	/* No exclusive id?  Don't do anything. */
	if (msg == NULL) return;
	if (CM_IsEmpty(msg, eExclusiveID)) return;

	/*syslog(LOG_DEBUG, "msgbase: exclusive ID: <%s> for room <%s>",
	  msg->cm_fields[eExclusiveID], CC->room.QRname);*/

	old_msgnum = CtdlLocateMessageByEuid(msg->cm_fields[eExclusiveID], &CC->room);
	if (old_msgnum > 0L) {
		syslog(LOG_DEBUG, "msgbase: ReplicationChecks() replacing message %ld", old_msgnum);
		CtdlDeleteMessages(CC->room.QRname, &old_msgnum, 1, "");
	}
}


/*
 * Save a message to disk and submit it into the delivery system.
 */
long CtdlSubmitMsg(struct CtdlMessage *msg,	/* message to save */
		   struct recptypes *recps,	/* recipients (if mail) */
		   const char *force		/* force a particular room? */
) {
	char hold_rm[ROOMNAMELEN];
	char actual_rm[ROOMNAMELEN];
	char force_room[ROOMNAMELEN];
	char content_type[SIZ];			/* We have to learn this */
	char recipient[SIZ];
	char bounce_to[1024];
	const char *room;
	long newmsgid;
	const char *mptr = NULL;
	struct ctdluser userbuf;
	int a, i;
	struct MetaData smi;
	char *collected_addresses = NULL;
	struct addresses_to_be_filed *aptr = NULL;
	StrBuf *saved_rfc822_version = NULL;
	int qualified_for_journaling = 0;

	syslog(LOG_DEBUG, "msgbase: CtdlSubmitMsg() called");
	if (CM_IsValidMsg(msg) == 0) return(-1);	/* self check */

	/* If this message has no timestamp, we take the liberty of
	 * giving it one, right now.
	 */
	if (CM_IsEmpty(msg, eTimestamp)) {
		CM_SetFieldLONG(msg, eTimestamp, time(NULL));
	}

	/* If this message has no path, we generate one.
	 */
	if (CM_IsEmpty(msg, eMessagePath)) {
		if (!CM_IsEmpty(msg, eAuthor)) {
			CM_CopyField(msg, eMessagePath, eAuthor);
			for (a=0; !IsEmptyStr(&msg->cm_fields[eMessagePath][a]); ++a) {
				if (isspace(msg->cm_fields[eMessagePath][a])) {
					msg->cm_fields[eMessagePath][a] = ' ';
				}
			}
		}
		else {
			CM_SetField(msg, eMessagePath, HKEY("unknown"));
		}
	}

	if (force == NULL) {
		force_room[0] = '\0';
	}
	else {
		strcpy(force_room, force);
	}

	/* Learn about what's inside, because it's what's inside that counts */
	if (CM_IsEmpty(msg, eMesageText)) {
		syslog(LOG_ERR, "msgbase: ERROR; attempt to save message with NULL body");
		return(-2);
	}

	switch (msg->cm_format_type) {
	case 0:
		strcpy(content_type, "text/x-citadel-variformat");
		break;
	case 1:
		strcpy(content_type, "text/plain");
		break;
	case 4:
		strcpy(content_type, "text/plain");
		mptr = bmstrcasestr(msg->cm_fields[eMesageText], "Content-type:");
		if (mptr != NULL) {
			char *aptr;
			safestrncpy(content_type, &mptr[13], sizeof content_type);
			string_trim(content_type);
			aptr = content_type;
			while (!IsEmptyStr(aptr)) {
				if ((*aptr == ';')
				    || (*aptr == ' ')
				    || (*aptr == 13)
				    || (*aptr == 10)) {
					*aptr = 0;
				}
				else aptr++;
			}
		}
	}

	/* Goto the correct room */
	room = (recps) ? CC->room.QRname : SENTITEMS;
	syslog(LOG_DEBUG, "msgbase: selected room %s", room);
	strcpy(hold_rm, CC->room.QRname);
	strcpy(actual_rm, CC->room.QRname);
	if (recps != NULL) {
		strcpy(actual_rm, SENTITEMS);
	}

	/* If the user is a twit, move to the twit room for posting */
	if (TWITDETECT) {
		if (CC->user.axlevel == AxProbU) {
			strcpy(hold_rm, actual_rm);
			strcpy(actual_rm, CtdlGetConfigStr("c_twitroom"));
			syslog(LOG_DEBUG, "msgbase: diverting to twit room");
		}
	}

	/* ...or if this message is destined for Aide> then go there. */
	if (!IsEmptyStr(force_room)) {
		strcpy(actual_rm, force_room);
	}

	syslog(LOG_DEBUG, "msgbase: final selection: %s (%s)", actual_rm, room);
	if (strcasecmp(actual_rm, CC->room.QRname)) {
		CtdlUserGoto(actual_rm, 0, 1, NULL, NULL, NULL, NULL);
	}

	/*
	 * If this message has no O (room) field, generate one.
	 */
	if (CM_IsEmpty(msg, eOriginalRoom) && !IsEmptyStr(CC->room.QRname)) {
		CM_SetField(msg, eOriginalRoom, CC->room.QRname, -1);
	}

	/* Perform "before save" hooks (aborting if any return nonzero) */
	syslog(LOG_DEBUG, "msgbase: performing before-save hooks");
	if (PerformMessageHooks(msg, recps, EVT_BEFORESAVE) > 0) return(-3);

	/*
	 * If this message has an Exclusive ID, and the room is replication
	 * checking enabled, then do replication checks.
	 */
	if (DoesThisRoomNeedEuidIndexing(&CC->room)) {
		ReplicationChecks(msg);
	}

	/* Save it to disk */
	syslog(LOG_DEBUG, "msgbase: saving to disk");
	newmsgid = send_message(msg);
	if (newmsgid <= 0L) return(-5);

	/* Write a supplemental message info record.  This doesn't have to
	 * be a critical section because nobody else knows about this message
	 * yet.
	 */
	syslog(LOG_DEBUG, "msgbase: creating metadata record");
	memset(&smi, 0, sizeof(struct MetaData));
	smi.meta_msgnum = newmsgid;
	smi.meta_refcount = 0;
	safestrncpy(smi.meta_content_type, content_type, sizeof smi.meta_content_type);

	/*
	 * Measure how big this message will be when rendered as RFC822.
	 * We do this for two reasons:
	 * 1. We need the RFC822 length for the new metadata record, so the
	 *    POP and IMAP services don't have to calculate message lengths
	 *    while the user is waiting (multiplied by potentially hundreds
	 *    or thousands of messages).
	 * 2. If journaling is enabled, we will need an RFC822 version of the
	 *    message to attach to the journalized copy.
	 */
	if (CC->redirect_buffer != NULL) {
		syslog(LOG_ALERT, "msgbase: CC->redirect_buffer is not NULL during message submission!");
		exit(CTDLEXIT_REDIRECT);
	}
	CC->redirect_buffer = NewStrBufPlain(NULL, SIZ);
	CtdlOutputPreLoadedMsg(msg, MT_RFC822, HEADERS_ALL, 0, 1, QP_EADDR);
	smi.meta_rfc822_length = StrLength(CC->redirect_buffer);
	saved_rfc822_version = CC->redirect_buffer;
	CC->redirect_buffer = NULL;

	PutMetaData(&smi);

	/* Now figure out where to store the pointers */
	syslog(LOG_DEBUG, "msgbase: storing pointers");

	/* If this is being done by the networker delivering a private
	 * message, we want to BYPASS saving the sender's copy (because there
	 * is no local sender; it would otherwise go to the Trashcan).
	 */
	if ((!CC->internal_pgm) || (recps == NULL)) {
		if (CtdlSaveMsgPointerInRoom(actual_rm, newmsgid, 1, msg) != 0) {
			syslog(LOG_ERR, "msgbase: ERROR saving message pointer %ld in %s", newmsgid, actual_rm);
			CtdlSaveMsgPointerInRoom(CtdlGetConfigStr("c_aideroom"), newmsgid, 0, msg);
		}
	}

	/* For internet mail, drop a copy in the outbound queue room */
	if ((recps != NULL) && (recps->num_internet > 0)) {
		CtdlSaveMsgPointerInRoom(SMTP_SPOOLOUT_ROOM, newmsgid, 0, msg);
	}

	/* If other rooms are specified, drop them there too. */
	if ((recps != NULL) && (recps->num_room > 0)) {
		for (i=0; i<num_tokens(recps->recp_room, '|'); ++i) {
			extract_token(recipient, recps->recp_room, i, '|', sizeof recipient);
			syslog(LOG_DEBUG, "msgbase: delivering to room <%s>", recipient);
			CtdlSaveMsgPointerInRoom(recipient, newmsgid, 0, msg);
		}
	}

	/* Decide where bounces need to be delivered */
	if ((recps != NULL) && (recps->bounce_to == NULL)) {
		if (CC->logged_in) {
			strcpy(bounce_to, CC->user.fullname);
		}
		else if (!IsEmptyStr(msg->cm_fields[eAuthor])){
			strcpy(bounce_to, msg->cm_fields[eAuthor]);
		}
		recps->bounce_to = bounce_to;
	}
		
	CM_SetFieldLONG(msg, eVltMsgNum, newmsgid);

	// If this is private, local mail, make a copy in the recipient's mailbox and bump the reference count.
	if ((recps != NULL) && (recps->num_local > 0)) {
		char *pch;
		int ntokens;

		pch = recps->recp_local;
		recps->recp_local = recipient;
		ntokens = num_tokens(pch, '|');
		for (i=0; i<ntokens; ++i) {
			extract_token(recipient, pch, i, '|', sizeof recipient);
			syslog(LOG_DEBUG, "msgbase: delivering private local mail to <%s>", recipient);
			if (CtdlGetUser(&userbuf, recipient) == 0) {
				CtdlMailboxName(actual_rm, sizeof actual_rm, &userbuf, MAILROOM);
				CtdlSaveMsgPointerInRoom(actual_rm, newmsgid, 0, msg);
				CtdlBumpNewMailCounter(userbuf.usernum);	// if this user is logged in, tell them they have new mail.
				PerformMessageHooks(msg, recps, EVT_AFTERUSRMBOXSAVE);
			}
			else {
				syslog(LOG_DEBUG, "msgbase: no user <%s>", recipient);
				CtdlSaveMsgPointerInRoom(CtdlGetConfigStr("c_aideroom"), newmsgid, 0, msg);
			}
		}
		recps->recp_local = pch;
	}

	/* Perform "after save" hooks */
	syslog(LOG_DEBUG, "msgbase: performing after-save hooks");

	PerformMessageHooks(msg, recps, EVT_AFTERSAVE);
	CM_FlushField(msg, eVltMsgNum);

	/* Go back to the room we started from */
	syslog(LOG_DEBUG, "msgbase: returning to original room %s", hold_rm);
	if (strcasecmp(hold_rm, CC->room.QRname)) {
		CtdlUserGoto(hold_rm, 0, 1, NULL, NULL, NULL, NULL);
	}

	/*
	 * Any addresses to harvest for someone's address book?
	 */
	if ( (CC->logged_in) && (recps != NULL) ) {
		collected_addresses = harvest_collected_addresses(msg);
	}

	if (collected_addresses != NULL) {
		aptr = (struct addresses_to_be_filed *) malloc(sizeof(struct addresses_to_be_filed));
		CtdlMailboxName(actual_rm, sizeof actual_rm, &CC->user, USERCONTACTSROOM);
		aptr->roomname = strdup(actual_rm);
		aptr->collected_addresses = collected_addresses;
		begin_critical_section(S_ATBF);
		aptr->next = atbf;
		atbf = aptr;
		end_critical_section(S_ATBF);
	}

	/*
	 * Determine whether this message qualifies for journaling.
	 */
	if (!CM_IsEmpty(msg, eJournal)) {
		qualified_for_journaling = 0;
	}
	else {
		if (recps == NULL) {
			qualified_for_journaling = CtdlGetConfigInt("c_journal_pubmsgs");
		}
		else if (recps->num_local + recps->num_internet > 0) {
			qualified_for_journaling = CtdlGetConfigInt("c_journal_email");
		}
		else {
			qualified_for_journaling = CtdlGetConfigInt("c_journal_pubmsgs");
		}
	}

	/*
	 * Do we have to perform journaling?  If so, hand off the saved
	 * RFC822 version will be handed off to the journaler for background
	 * submit.  Otherwise, we have to free the memory ourselves.
	 */
	if (saved_rfc822_version != NULL) {
		if (qualified_for_journaling) {
			JournalBackgroundSubmit(msg, saved_rfc822_version, recps);
		}
		else {
			FreeStrBuf(&saved_rfc822_version);
		}
	}

	if ((recps != NULL) && (recps->bounce_to == bounce_to))
		recps->bounce_to = NULL;

	/* Done. */
	return(newmsgid);
}


/*
 * Convenience function for generating small administrative messages.
 */
long quickie_message(char *from,
		     char *fromaddr,
		     char *to,
		     char *room,
		     char *text, 
		     int format_type,
		     char *subject)
{
	struct CtdlMessage *msg;
	struct recptypes *recp = NULL;

	msg = malloc(sizeof(struct CtdlMessage));
	memset(msg, 0, sizeof(struct CtdlMessage));
	msg->cm_magic = CTDLMESSAGE_MAGIC;
	msg->cm_anon_type = MES_NORMAL;
	msg->cm_format_type = format_type;

	if (!IsEmptyStr(from)) {
		CM_SetField(msg, eAuthor, from, -1);
	}
	else if (!IsEmptyStr(fromaddr)) {
		char *pAt;
		CM_SetField(msg, eAuthor, fromaddr, -1);
		pAt = strchr(msg->cm_fields[eAuthor], '@');
		if (pAt != NULL) {
			CM_CutFieldAt(msg, eAuthor, pAt - msg->cm_fields[eAuthor]);
		}
	}
	else {
		msg->cm_fields[eAuthor] = strdup("Citadel");
	}

	if (!IsEmptyStr(fromaddr)) CM_SetField(msg, erFc822Addr, fromaddr, -1);
	if (!IsEmptyStr(room)) CM_SetField(msg, eOriginalRoom, room, -1);
	if (!IsEmptyStr(to)) {
		CM_SetField(msg, eRecipient, to, -1);
		recp = validate_recipients(to, NULL, 0);
	}
	if (!IsEmptyStr(subject)) {
		CM_SetField(msg, eMsgSubject, subject, -1);
	}
	if (!IsEmptyStr(text)) {
		CM_SetField(msg, eMesageText, text, -1);
	}

	long msgnum = CtdlSubmitMsg(msg, recp, room);
	CM_Free(msg);
	if (recp != NULL) free_recipients(recp);
	return msgnum;
}


/*
 * Back end function used by CtdlMakeMessage() and similar functions
 */
StrBuf *CtdlReadMessageBodyBuf(char *terminator,	// token signalling EOT
			       long tlen,
			       size_t maxlen,		// maximum message length
			       StrBuf *exist,		// if non-null, append to it; exist is ALWAYS freed
			       int crlf			// CRLF newlines instead of LF
) {
	StrBuf *Message;
	StrBuf *LineBuf;
	int flushing = 0;
	int finished = 0;
	int dotdot = 0;

	LineBuf = NewStrBufPlain(NULL, SIZ);
	if (exist == NULL) {
		Message = NewStrBufPlain(NULL, 4 * SIZ);
	}
	else {
		Message = NewStrBufDup(exist);
	}

	/* Do we need to change leading ".." to "." for SMTP escaping? */
	if ((tlen == 1) && (*terminator == '.')) {
		dotdot = 1;
	}

	/* read in the lines of message text one by one */
	do {
		if (CtdlClientGetLine(LineBuf) < 0) {
			finished = 1;
		}
		if ((StrLength(LineBuf) == tlen) && (!strcmp(ChrPtr(LineBuf), terminator))) {
			finished = 1;
		}
		if ( (!flushing) && (!finished) ) {
			if (crlf) {
				StrBufAppendBufPlain(LineBuf, HKEY("\r\n"), 0);
			}
			else {
				StrBufAppendBufPlain(LineBuf, HKEY("\n"), 0);
			}
			
			/* Unescape SMTP-style input of two dots at the beginning of the line */
			if ((dotdot) && (StrLength(LineBuf) > 1) && (ChrPtr(LineBuf)[0] == '.')) {
				StrBufCutLeft(LineBuf, 1);
			}
			StrBufAppendBuf(Message, LineBuf, 0);
		}

		/* if we've hit the max msg length, flush the rest */
		if (StrLength(Message) >= maxlen) {
			flushing = 1;
		}

	} while (!finished);
	FreeStrBuf(&LineBuf);

	if (flushing) {
		syslog(LOG_ERR, "msgbase: exceeded maximum message length of %ld - message was truncated", maxlen);
	}

	return Message;
}


// Back end function used by CtdlMakeMessage() and similar functions
char *CtdlReadMessageBody(char *terminator,	// token signalling EOT
			  long tlen,
			  size_t maxlen,	// maximum message length
			  StrBuf *exist,	// if non-null, append to it; exist is ALWAYS freed
			  int crlf		// CRLF newlines instead of LF
) {
	StrBuf *Message;

	Message = CtdlReadMessageBodyBuf(terminator,
					 tlen,
					 maxlen,
					 exist,
					 crlf
	);
	if (Message == NULL) {
		return NULL;
	}
	else {
		return SmashStrBuf(&Message);
	}
}


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
) {
	return CtdlMakeMessageLen(
		author,	/* author's user structure */
		recipient,		/* NULL if it's not mail */
		(recipient)?strlen(recipient) : 0,
		recp_cc,			/* NULL if it's not mail */
		(recp_cc)?strlen(recp_cc): 0,
		room,			/* room where it's going */
		(room)?strlen(room): 0,
		type,			/* see MES_ types in header file */
		format_type,		/* variformat, plain text, MIME... */
		fake_name,		/* who we're masquerading as */
		(fake_name)?strlen(fake_name): 0,
		my_email,			/* which of my email addresses to use (empty is ok) */
		(my_email)?strlen(my_email): 0,
		subject,			/* Subject (optional) */
		(subject)?strlen(subject): 0,
		supplied_euid,		/* ...or NULL if this is irrelevant */
		(supplied_euid)?strlen(supplied_euid):0,
		preformatted_text,	/* ...or NULL to read text from client */
		(preformatted_text)?strlen(preformatted_text) : 0,
		references,		/* Thread references */
		(references)?strlen(references):0);

}


/*
 * Build a binary message to be saved on disk.
 * (NOTE: if you supply 'preformatted_text', the buffer you give it
 * will become part of the message.  This means you are no longer
 * responsible for managing that memory -- it will be freed along with
 * the rest of the fields when CM_Free() is called.)
 */
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
) {
	long blen;
	char buf[1024];
	struct CtdlMessage *msg;
	StrBuf *FakeAuthor;
	StrBuf *FakeEncAuthor = NULL;

	msg = malloc(sizeof(struct CtdlMessage));
	memset(msg, 0, sizeof(struct CtdlMessage));
	msg->cm_magic = CTDLMESSAGE_MAGIC;
	msg->cm_anon_type = type;
	msg->cm_format_type = format_type;

	if (recipient != NULL) rcplen = string_trim(recipient);
	if (recp_cc != NULL) cclen = string_trim(recp_cc);

	/* Path or Return-Path */
	if (myelen > 0) {
		CM_SetField(msg, eMessagePath, my_email, myelen);
	}
	else if (!IsEmptyStr(author->fullname)) {
		CM_SetField(msg, eMessagePath, author->fullname, -1);
	}
	convert_spaces_to_underscores(msg->cm_fields[eMessagePath]);

	blen = snprintf(buf, sizeof buf, "%ld", (long)time(NULL));
	CM_SetField(msg, eTimestamp, buf, blen);

	if (fnlen > 0) {
		FakeAuthor = NewStrBufPlain (fake_name, fnlen);
	}
	else {
		FakeAuthor = NewStrBufPlain (author->fullname, -1);
	}
	StrBufRFC2047encode(&FakeEncAuthor, FakeAuthor);
	CM_SetAsFieldSB(msg, eAuthor, &FakeEncAuthor);
	FreeStrBuf(&FakeAuthor);

	if (!!IsEmptyStr(CC->room.QRname)) {
		if (CC->room.QRflags & QR_MAILBOX) {		/* room */
			CM_SetField(msg, eOriginalRoom, &CC->room.QRname[11], -1);
		}
		else {
			CM_SetField(msg, eOriginalRoom, CC->room.QRname, -1);
		}
	}

	if (rcplen > 0) {
		CM_SetField(msg, eRecipient, recipient, rcplen);
	}
	if (cclen > 0) {
		CM_SetField(msg, eCarbonCopY, recp_cc, cclen);
	}

	if (myelen > 0) {
		CM_SetField(msg, erFc822Addr, my_email, myelen);
	}
	else if ( (author == &CC->user) && (!IsEmptyStr(CC->cs_inet_email)) ) {
		CM_SetField(msg, erFc822Addr, CC->cs_inet_email, -1);
	}

	if (subject != NULL) {
		long length;
		length = string_trim(subject);
		if (length > 0) {
			long i;
			long IsAscii;
			IsAscii = -1;
			i = 0;
			while ((subject[i] != '\0') &&
			       (IsAscii = isascii(subject[i]) != 0 ))
				i++;
			if (IsAscii != 0)
				CM_SetField(msg, eMsgSubject, subject, subjlen);
			else /* ok, we've got utf8 in the string. */
			{
				char *rfc2047Subj;
				rfc2047Subj = rfc2047encode(subject, length);
				CM_SetAsField(msg, eMsgSubject, &rfc2047Subj, strlen(rfc2047Subj));
			}

		}
	}

	if (euidlen > 0) {
		CM_SetField(msg, eExclusiveID, supplied_euid, euidlen);
	}

	if (reflen > 0) {
		CM_SetField(msg, eWeferences, references, reflen);
	}

	if (preformatted_text != NULL) {
		CM_SetField(msg, eMesageText, preformatted_text, textlen);
	}
	else {
		StrBuf *MsgBody;
		MsgBody = CtdlReadMessageBodyBuf(HKEY("000"), CtdlGetConfigLong("c_maxmsglen"), NULL, 0);
		if (MsgBody != NULL) {
			CM_SetAsFieldSB(msg, eMesageText, &MsgBody);
		}
	}

	return(msg);
}


/*
 * API function to delete messages which match a set of criteria
 * (returns the actual number of messages deleted)
 */
int CtdlDeleteMessages(const char *room_name,	// which room
		       long *dmsgnums,		// array of msg numbers to be deleted
		       int num_dmsgnums,	// number of msgs to be deleted, or 0 for "any"
		       char *content_type	// or "" for any.  regular expressions expected.
) {
	struct ctdlroom qrbuf;
	struct cdbdata *cdbfr;
	long *msglist = NULL;
	long *dellist = NULL;
	int num_msgs = 0;
	int i, j;
	int num_deleted = 0;
	int delete_this;
	struct MetaData smi;
	regex_t re;
	regmatch_t pm;
	int need_to_free_re = 0;

	if (content_type) if (!IsEmptyStr(content_type)) {
			regcomp(&re, content_type, 0);
			need_to_free_re = 1;
		}
	syslog(LOG_DEBUG, "msgbase: CtdlDeleteMessages(%s, %d msgs, %s)", room_name, num_dmsgnums, content_type);

	/* get room record, obtaining a lock... */
	if (CtdlGetRoomLock(&qrbuf, room_name) != 0) {
		syslog(LOG_ERR, "msgbase: CtdlDeleteMessages(): Room <%s> not found", room_name);
		if (need_to_free_re) regfree(&re);
		return(0);	/* room not found */
	}
	cdbfr = cdb_fetch(CDB_MSGLISTS, &qrbuf.QRnumber, sizeof(long));

	if (cdbfr != NULL) {
		dellist = malloc(cdbfr->len);
		msglist = (long *) cdbfr->ptr;
		cdbfr->ptr = NULL;	/* CtdlDeleteMessages() now owns this memory */
		num_msgs = cdbfr->len / sizeof(long);
		cdb_free(cdbfr);
	}
	if (num_msgs > 0) {
		int have_contenttype = (content_type != NULL) && !IsEmptyStr(content_type);
		int have_delmsgs = (num_dmsgnums == 0) || (dmsgnums == NULL);
		int have_more_del = 1;

		num_msgs = sort_msglist(msglist, num_msgs);
		if (num_dmsgnums > 1)
			num_dmsgnums = sort_msglist(dmsgnums, num_dmsgnums);
/*
		{
			StrBuf *dbg = NewStrBuf();
			for (i = 0; i < num_dmsgnums; i++)
				StrBufAppendPrintf(dbg, ", %ld", dmsgnums[i]);
			syslog(LOG_DEBUG, "msgbase: Deleting before: %s", ChrPtr(dbg));
			FreeStrBuf(&dbg);
		}
*/
		i = 0; j = 0;
		while ((i < num_msgs) && (have_more_del)) {
			delete_this = 0x00;

			/* Set/clear a bit for each criterion */

			/* 0 messages in the list or a null list means that we are
			 * interested in deleting any messages which meet the other criteria.
			 */
			if (have_delmsgs) {
				delete_this |= 0x01;
			}
			else {
				while ((i < num_msgs) && (msglist[i] < dmsgnums[j])) i++;

				if (i >= num_msgs)
					continue;

				if (msglist[i] == dmsgnums[j]) {
					delete_this |= 0x01;
				}
				j++;
				have_more_del = (j < num_dmsgnums);
			}

			if (have_contenttype) {
				GetMetaData(&smi, msglist[i]);
				if (regexec(&re, smi.meta_content_type, 1, &pm, 0) == 0) {
					delete_this |= 0x02;
				}
			} else {
				delete_this |= 0x02;
			}

			/* Delete message only if all bits are set */
			if (delete_this == 0x03) {
				dellist[num_deleted++] = msglist[i];
				msglist[i] = 0L;
			}
			i++;
		}
/*
		{
			StrBuf *dbg = NewStrBuf();
			for (i = 0; i < num_deleted; i++)
				StrBufAppendPrintf(dbg, ", %ld", dellist[i]);
			syslog(LOG_DEBUG, "msgbase: Deleting: %s", ChrPtr(dbg));
			FreeStrBuf(&dbg);
		}
*/
		num_msgs = sort_msglist(msglist, num_msgs);
		cdb_store(CDB_MSGLISTS, &qrbuf.QRnumber, (int)sizeof(long),
			  msglist, (int)(num_msgs * sizeof(long)));

		if (num_msgs > 0)
			qrbuf.QRhighest = msglist[num_msgs - 1];
		else
			qrbuf.QRhighest = 0;
	}
	CtdlPutRoomLock(&qrbuf);

	/* Go through the messages we pulled out of the index, and decrement
	 * their reference counts by 1.  If this is the only room the message
	 * was in, the reference count will reach zero and the message will
	 * automatically be deleted from the database.  We do this in a
	 * separate pass because there might be plug-in hooks getting called,
	 * and we don't want that happening during an S_ROOMS critical
	 * section.
	 */
	if (num_deleted) {
		for (i=0; i<num_deleted; ++i) {
			PerformDeleteHooks(qrbuf.QRname, dellist[i]);
		}
		AdjRefCountList(dellist, num_deleted, -1);
	}
	/* Now free the memory we used, and go away. */
	if (msglist != NULL) free(msglist);
	if (dellist != NULL) free(dellist);
	syslog(LOG_DEBUG, "msgbase: %d message(s) deleted", num_deleted);
	if (need_to_free_re) regfree(&re);
	return (num_deleted);
}


/*
 * GetMetaData()  -  Get the supplementary record for a message
 */
void GetMetaData(struct MetaData *smibuf, long msgnum)
{
	struct cdbdata *cdbsmi;
	long TheIndex;

	memset(smibuf, 0, sizeof(struct MetaData));
	smibuf->meta_msgnum = msgnum;
	smibuf->meta_refcount = 1;	/* Default reference count is 1 */

	/* Use the negative of the message number for its supp record index */
	TheIndex = (0L - msgnum);

	cdbsmi = cdb_fetch(CDB_MSGMAIN, &TheIndex, sizeof(long));
	if (cdbsmi == NULL) {
		return;			/* record not found; leave it alone */
	}
	memcpy(smibuf, cdbsmi->ptr,
	       ((cdbsmi->len > sizeof(struct MetaData)) ?
		sizeof(struct MetaData) : cdbsmi->len)
	);
	cdb_free(cdbsmi);
	return;
}


/*
 * PutMetaData()  -  (re)write supplementary record for a message
 */
void PutMetaData(struct MetaData *smibuf)
{
	long TheIndex;

	/* Use the negative of the message number for the metadata db index */
	TheIndex = (0L - smibuf->meta_msgnum);

	cdb_store(CDB_MSGMAIN,
		  &TheIndex, (int)sizeof(long),
		  smibuf, (int)sizeof(struct MetaData)
	);
}


/*
 * Convenience function to process a big block of AdjRefCount() operations
 */
void AdjRefCountList(long *msgnum, long nmsg, int incr)
{
	long i;

	for (i = 0; i < nmsg; i++) {
		AdjRefCount(msgnum[i], incr);
	}
}


/*
 * AdjRefCount - adjust the reference count for a message.  We need to delete from disk any message whose reference count reaches zero.
 */
void AdjRefCount(long msgnum, int incr)
{
	struct MetaData smi;
	long delnum;

	/* This is a *tight* critical section; please keep it that way, as
	 * it may get called while nested in other critical sections.  
	 * Complicating this any further will surely cause deadlock!
	 */
	begin_critical_section(S_SUPPMSGMAIN);
	GetMetaData(&smi, msgnum);
	smi.meta_refcount += incr;
	PutMetaData(&smi);
	end_critical_section(S_SUPPMSGMAIN);
	syslog(LOG_DEBUG, "msgbase: AdjRefCount() msg %ld ref count delta %+d, is now %d", msgnum, incr, smi.meta_refcount);

	/* If the reference count is now zero, delete both the message and its metadata record.
	 */
	if (smi.meta_refcount == 0) {
		syslog(LOG_DEBUG, "msgbase: deleting message <%ld>", msgnum);
		
		/* Call delete hooks with NULL room to show it has gone altogether */
		PerformDeleteHooks(NULL, msgnum);

		/* Remove from message base */
		delnum = msgnum;
		cdb_delete(CDB_MSGMAIN, &delnum, (int)sizeof(long));
		cdb_delete(CDB_BIGMSGS, &delnum, (int)sizeof(long));

		/* Remove metadata record */
		delnum = (0L - msgnum);
		cdb_delete(CDB_MSGMAIN, &delnum, (int)sizeof(long));
	}
}


/*
 * Write a generic object to this room
 *
 * Returns the message number of the written object, in case you need it.
 */
long CtdlWriteObject(char *req_room,			/* Room to stuff it in */
		     char *content_type,		/* MIME type of this object */
		     char *raw_message,			/* Data to be written */
		     off_t raw_length,			/* Size of raw_message */
		     struct ctdluser *is_mailbox,	/* Mailbox room? */
		     int is_binary,			/* Is encoding necessary? */
		     unsigned int flags			/* Internal save flags */
) {
	struct ctdlroom qrbuf;
	char roomname[ROOMNAMELEN];
	struct CtdlMessage *msg;
	StrBuf *encoded_message = NULL;

	if (is_mailbox != NULL) {
		CtdlMailboxName(roomname, sizeof roomname, is_mailbox, req_room);
	}
	else {
		safestrncpy(roomname, req_room, sizeof(roomname));
	}

	syslog(LOG_DEBUG, "msfbase: raw length is %ld", (long)raw_length);

	if (is_binary) {
		encoded_message = NewStrBufPlain(NULL, (size_t) (((raw_length * 134) / 100) + 4096 ) );
	}
	else {
		encoded_message = NewStrBufPlain(NULL, (size_t)(raw_length + 4096));
	}

	StrBufAppendBufPlain(encoded_message, HKEY("Content-type: "), 0);
	StrBufAppendBufPlain(encoded_message, content_type, -1, 0);
	StrBufAppendBufPlain(encoded_message, HKEY("\n"), 0);

	if (is_binary) {
		StrBufAppendBufPlain(encoded_message, HKEY("Content-transfer-encoding: base64\n\n"), 0);
	}
	else {
		StrBufAppendBufPlain(encoded_message, HKEY("Content-transfer-encoding: 7bit\n\n"), 0);
	}

	if (is_binary) {
		StrBufBase64Append(encoded_message, NULL, raw_message, raw_length, 0);
	}
	else {
		StrBufAppendBufPlain(encoded_message, raw_message, raw_length, 0);
	}

	syslog(LOG_DEBUG, "msgbase: allocating");
	msg = malloc(sizeof(struct CtdlMessage));
	memset(msg, 0, sizeof(struct CtdlMessage));
	msg->cm_magic = CTDLMESSAGE_MAGIC;
	msg->cm_anon_type = MES_NORMAL;
	msg->cm_format_type = 4;
	CM_SetField(msg, eAuthor, CC->user.fullname, -1);
	CM_SetField(msg, eOriginalRoom, req_room, -1);
	msg->cm_flags = flags;
	
	CM_SetAsFieldSB(msg, eMesageText, &encoded_message);

	/* Create the requested room if we have to. */
	if (CtdlGetRoom(&qrbuf, roomname) != 0) {
		CtdlCreateRoom(roomname, ( (is_mailbox != NULL) ? 5 : 3 ), "", 0, 1, 0, VIEW_BBS);
	}

	/* Now write the data */
	long new_msgnum = CtdlSubmitMsg(msg, NULL, roomname);
	CM_Free(msg);
	return new_msgnum;
}


/************************************************************************/
/*                      MODULE INITIALIZATION                           */
/************************************************************************/

char *ctdl_module_init_msgbase(void) {
	if (!threading) {
		FillMsgKeyLookupTable();
	}

        /* return our module id for the log */
	return "msgbase";
}
