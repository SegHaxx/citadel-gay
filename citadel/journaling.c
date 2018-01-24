/*
 * Message journaling functions.
 *
 * Copyright (c) 1987-2018 by the citadel.org team
 *
 * This program is open source software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <stdio.h>
#include <libcitadel.h>
#include "ctdl_module.h"
#include "citserver.h"
#include "config.h"
#include "user_ops.h"
#include "serv_vcard.h"			/* Needed for vcard_getuser and extract_inet_email_addrs */
#include "internet_addressing.h"
#include "journaling.h"

struct jnlq *jnlq = NULL;	/* journal queue */

/*
 * Hand off a copy of a message to be journalized.
 */
void JournalBackgroundSubmit(struct CtdlMessage *msg,
			StrBuf *saved_rfc822_version,
			recptypes *recps) {

	struct jnlq *jptr = NULL;

	/* Avoid double journaling! */
	if (!CM_IsEmpty(msg, eJournal)) {
		FreeStrBuf(&saved_rfc822_version);
		return;
	}

	jptr = (struct jnlq *)malloc(sizeof(struct jnlq));
	if (jptr == NULL) {
		FreeStrBuf(&saved_rfc822_version);
		return;
	}
	memset(jptr, 0, sizeof(struct jnlq));
	if (recps != NULL) memcpy(&jptr->recps, recps, sizeof(recptypes));
	if (!CM_IsEmpty(msg, eAuthor)) jptr->from = strdup(msg->cm_fields[eAuthor]);
	if (!CM_IsEmpty(msg, erFc822Addr)) jptr->rfca = strdup(msg->cm_fields[erFc822Addr]);
	if (!CM_IsEmpty(msg, eMsgSubject)) jptr->subj = strdup(msg->cm_fields[eMsgSubject]);
	if (!CM_IsEmpty(msg, emessageId)) jptr->msgn = strdup(msg->cm_fields[emessageId]);
	jptr->rfc822 = SmashStrBuf(&saved_rfc822_version);

	/* Add to the queue */
	begin_critical_section(S_JOURNAL_QUEUE);
	jptr->next = jnlq;
	jnlq = jptr;
	end_critical_section(S_JOURNAL_QUEUE);
}


/*
 * Convert a local user name to an internet email address for the journal
 * FIXME - grab the user's Internet email address from the user record, not from vCard !!!!
 */
void local_to_inetemail(char *inetemail, char *localuser, size_t inetemail_len) {
	struct ctdluser us;
	struct vCard *v;

	strcpy(inetemail, "");
	if (CtdlGetUser(&us, localuser) != 0) {
		return;
	}

	v = vcard_get_user(&us);
	if (v == NULL) {
		return;
	}

	extract_inet_email_addrs(inetemail, inetemail_len, NULL, 0, v, 1);
	vcard_free(v);
}


/*
 * Called by JournalRunQueue() to send an individual message.
 */
void JournalRunQueueMsg(struct jnlq *jmsg) {

	struct CtdlMessage *journal_msg = NULL;
	recptypes *journal_recps = NULL;
	StrBuf *message_text = NULL;
	char mime_boundary[256];
	long mblen;
	long rfc822len;
	char recipient[256];
	char inetemail[256];
	static int seq = 0;
	int i;

	if (jmsg == NULL)
		return;
	journal_recps = validate_recipients(CtdlGetConfigStr("c_journal_dest"), NULL, 0);
	if (journal_recps != NULL) {

		if (  (journal_recps->num_local > 0)
		   || (journal_recps->num_internet > 0)
		   || (journal_recps->num_room > 0)
		) {

			/*
			 * Construct journal message.
			 * Note that we are transferring ownership of some of the memory here.
			 */
			journal_msg = malloc(sizeof(struct CtdlMessage));
			memset(journal_msg, 0, sizeof(struct CtdlMessage));
			journal_msg->cm_magic = CTDLMESSAGE_MAGIC;
			journal_msg->cm_anon_type = MES_NORMAL;
			journal_msg->cm_format_type = FMT_RFC822;
			CM_SetField(journal_msg, eJournal, HKEY("is journal"));

			if (!IsEmptyStr(jmsg->from)) {
				CM_SetField(journal_msg, eAuthor, jmsg->from, strlen(jmsg->from));
			}

			if (!IsEmptyStr(jmsg->rfca)) {
				CM_SetField(journal_msg, erFc822Addr, jmsg->rfca, strlen(jmsg->rfca));
			}

			if (!IsEmptyStr(jmsg->subj)) {
				CM_SetField(journal_msg, eMsgSubject, jmsg->subj, strlen(jmsg->subj));
			}

			mblen = snprintf(mime_boundary, sizeof(mime_boundary),
					 "--Citadel-Journal-%08lx-%04x--", time(NULL), ++seq);

			if (!IsEmptyStr(jmsg->rfc822)) {
				rfc822len = strlen(jmsg->rfc822);
			}
			else {
				rfc822len = 0;
			}

			message_text = NewStrBufPlain(NULL, rfc822len + sizeof(recptypes) + 1024);

			/*
			 * Here is where we begin to compose the journalized message.
			 * (The "ExJournalReport" header is consumed by some email retention services which assume the journaling agent is Exchange.)
			 */
			StrBufAppendBufPlain(
				message_text, 
				HKEY("Content-type: multipart/mixed; boundary=\""),
				0
			);

			StrBufAppendBufPlain(message_text, mime_boundary, mblen, 0);

			StrBufAppendBufPlain(
				message_text, 
				HKEY("\"\r\n"
				     "Content-Identifier: ExJournalReport\r\n"
				     "MIME-Version: 1.0\r\n"
				     "\n"
				     "--"),
				0
			);

			StrBufAppendBufPlain(message_text, mime_boundary, mblen, 0);

			StrBufAppendBufPlain(
				message_text, 
				HKEY("\r\n"
				     "Content-type: text/plain\r\n"
				     "\r\n"
				     "Sender: "), 0);

			if (CM_IsEmpty(journal_msg, eAuthor))
				StrBufAppendBufPlain(
					message_text, 
					journal_msg->cm_fields[eAuthor], -1, 0);
			else
				StrBufAppendBufPlain(
					message_text, 
					HKEY("(null)"), 0);

			if (!CM_IsEmpty(journal_msg, erFc822Addr)) {
				StrBufAppendPrintf(message_text, " <%s>",
						   journal_msg->cm_fields[erFc822Addr]);
			}

			StrBufAppendBufPlain(message_text, HKEY("\r\nMessage-ID: <"), 0);
			StrBufAppendBufPlain(message_text, jmsg->msgn, -1, 0);
			StrBufAppendBufPlain(message_text, HKEY(">\r\nRecipients:\r\n"), 0);

			if (jmsg->recps.num_local > 0) {
				for (i=0; i<jmsg->recps.num_local; ++i) {
					extract_token(recipient, jmsg->recps.recp_local, i, '|', sizeof recipient);
					local_to_inetemail(inetemail, recipient, sizeof inetemail);
					StrBufAppendPrintf(message_text, "	%s <%s>\r\n", recipient, inetemail);
				}
			}

			if (jmsg->recps.num_internet > 0) {
				for (i=0; i<jmsg->recps.num_internet; ++i) {
					extract_token(recipient, jmsg->recps.recp_internet, i, '|', sizeof recipient);
					StrBufAppendPrintf(message_text, "	%s\r\n", recipient);
				}
			}

			StrBufAppendBufPlain(message_text, HKEY("\r\n" "--"), 0);
			StrBufAppendBufPlain(message_text, mime_boundary, mblen, 0);
			StrBufAppendBufPlain(message_text, HKEY("\r\nContent-type: message/rfc822\r\n\r\n"), 0);
			StrBufAppendBufPlain(message_text, jmsg->rfc822, rfc822len, 0);
			StrBufAppendBufPlain(message_text, HKEY("--"), 0);
			StrBufAppendBufPlain(message_text, mime_boundary, mblen, 0);
			StrBufAppendBufPlain(message_text, HKEY("--\r\n"), 0);

			CM_SetAsFieldSB(journal_msg, eMesageText, &message_text);
			free(jmsg->rfc822);
			free(jmsg->msgn);
			jmsg->rfc822 = NULL;
			jmsg->msgn = NULL;
			
			/* Submit journal message */
			CtdlSubmitMsg(journal_msg, journal_recps, "", 0);
			CM_Free(journal_msg);
		}

		free_recipients(journal_recps);
	}

	/* We are responsible for freeing this memory. */
	free(jmsg);
}


/*
 * Run the queue.
 */
void JournalRunQueue(void) {
	struct jnlq *jptr = NULL;

	while (jnlq != NULL) {
		begin_critical_section(S_JOURNAL_QUEUE);
		if (jnlq != NULL) {
			jptr = jnlq;
			jnlq = jnlq->next;
		}
		end_critical_section(S_JOURNAL_QUEUE);
		JournalRunQueueMsg(jptr);
	}
}


