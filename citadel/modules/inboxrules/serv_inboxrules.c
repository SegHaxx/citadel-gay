/*
 * Inbox handling rules
 *
 * Copyright (c) 1987-2020 by the citadel.org team
 *
 * This program is open source software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include "sysdep.h"
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <ctype.h>
#include <pwd.h>
#include <errno.h>
#include <sys/types.h>

#if TIME_WITH_SYS_TIME
# include <sys/time.h>
# include <time.h>
#else
# if HAVE_SYS_TIME_H
#  include <sys/time.h>
# else
#  include <time.h>
# endif
#endif

#include <sys/wait.h>
#include <string.h>
#include <limits.h>
#include <libcitadel.h>
#include "citadel.h"
#include "server.h"
#include "citserver.h"
#include "support.h"
#include "config.h"
#include "database.h"
#include "msgbase.h"
#include "internet_addressing.h"
#include "ctdl_module.h"


#if 0

/*
 * Callback function to redirect a message to a different folder
 */
int ctdl_redirect(void)
{
	struct ctdl_sieve *cs = (struct ctdl_sieve *)my;
	struct CtdlMessage *msg = NULL;
	recptypes *valid = NULL;
	char recp[256];

	safestrncpy(recp, sieve2_getvalue_string(s, "address"), sizeof recp);

	syslog(LOG_DEBUG, "Action is REDIRECT, recipient <%s>", recp);

	valid = validate_recipients(recp, NULL, 0);
	if (valid == NULL) {
		syslog(LOG_WARNING, "REDIRECT failed: bad recipient <%s>", recp);
		return SIEVE2_ERROR_BADARGS;
	}
	if (valid->num_error > 0) {
		syslog(LOG_WARNING, "REDIRECT failed: bad recipient <%s>", recp);
		free_recipients(valid);
		return SIEVE2_ERROR_BADARGS;
	}

	msg = CtdlFetchMessage(cs->msgnum, 1);
	if (msg == NULL) {
		syslog(LOG_WARNING, "REDIRECT failed: unable to fetch msg %ld", cs->msgnum);
		free_recipients(valid);
		return SIEVE2_ERROR_BADARGS;
	}

	CtdlSubmitMsg(msg, valid, NULL, 0);
	cs->cancel_implicit_keep = 1;
	free_recipients(valid);
	CM_Free(msg);
	return SIEVE2_OK;
}




/*
 * Callback function to file a message into a different mailbox
 */
int ctdl_fileinto(sieve2_context_t *s, void *my)
{
	struct ctdl_sieve *cs = (struct ctdl_sieve *)my;
	const char *dest_folder = sieve2_getvalue_string(s, "mailbox");
	int c;
	char foldername[256];
	char original_room_name[ROOMNAMELEN];

	syslog(LOG_DEBUG, "Action is FILEINTO, destination is <%s>", dest_folder);

	/* FILEINTO 'INBOX' is the same thing as KEEP */
	if ( (!strcasecmp(dest_folder, "INBOX")) || (!strcasecmp(dest_folder, MAILROOM)) ) {
		cs->keep = 1;
		cs->cancel_implicit_keep = 1;
		return SIEVE2_OK;
	}

	/* Remember what room we came from */
	safestrncpy(original_room_name, CC->room.QRname, sizeof original_room_name);

	/* First try a mailbox name match (check personal mail folders first) */
	snprintf(foldername, sizeof foldername, "%010ld.%s", cs->usernum, dest_folder);
	c = CtdlGetRoom(&CC->room, foldername);

	/* Then a regular room name match (public and private rooms) */
	if (c != 0) {
		safestrncpy(foldername, dest_folder, sizeof foldername);
		c = CtdlGetRoom(&CC->room, foldername);
	}

	if (c != 0) {
		syslog(LOG_WARNING, "FILEINTO failed: target <%s> does not exist", dest_folder);
		return SIEVE2_ERROR_BADARGS;
	}

	/* Yes, we actually have to go there */
	CtdlUserGoto(NULL, 0, 0, NULL, NULL, NULL, NULL);

	c = CtdlSaveMsgPointersInRoom(NULL, &cs->msgnum, 1, 0, NULL, 0);

	/* Go back to the room we came from */
	if (strcasecmp(original_room_name, CC->room.QRname)) {
		CtdlUserGoto(original_room_name, 0, 0, NULL, NULL, NULL, NULL);
	}

	if (c == 0) {
		cs->cancel_implicit_keep = 1;
		return SIEVE2_OK;
	}
	else {
		return SIEVE2_ERROR_BADARGS;
	}
}


/*
 * Callback function to indicate that a message should be discarded.
 */
int ctdl_discard(sieve2_context_t *s, void *my)
{
	struct ctdl_sieve *cs = (struct ctdl_sieve *)my;

	syslog(LOG_DEBUG, "Action is DISCARD");

	/* Cancel the implicit keep.  That's all there is to it. */
	cs->cancel_implicit_keep = 1;
	return SIEVE2_OK;
}


/*
 * Callback function to indicate that a message should be rejected
 */
int ctdl_reject(sieve2_context_t *s, void *my)
{
	struct ctdl_sieve *cs = (struct ctdl_sieve *)my;
	char *reject_text = NULL;

	syslog(LOG_DEBUG, "Action is REJECT");

	/* If we don't know who sent the message, do a DISCARD instead. */
	if (IsEmptyStr(cs->sender)) {
		syslog(LOG_INFO, "Unknown sender.  Doing DISCARD instead of REJECT.");
		return ctdl_discard(s, my);
	}

	/* Assemble the reject message. */
	reject_text = malloc(strlen(sieve2_getvalue_string(s, "message")) + 1024);
	if (reject_text == NULL) {
		return SIEVE2_ERROR_FAIL;
	}

	sprintf(reject_text, 
		"Content-type: text/plain\n"
		"\n"
		"The message was refused by the recipient's mail filtering program.\n"
		"The reason given was as follows:\n"
		"\n"
		"%s\n"
		"\n"
	,
		sieve2_getvalue_string(s, "message")
	);

	quickie_message(	/* This delivers the message */
		NULL,
		cs->envelope_to,
		cs->sender,
		NULL,
		reject_text,
		FMT_RFC822,
		"Delivery status notification"
	);

	free(reject_text);
	cs->cancel_implicit_keep = 1;
	return SIEVE2_OK;
}


/*
 * Callback function to indicate that a vacation message should be generated
 */
int ctdl_vacation(sieve2_context_t *s, void *my)
{
	struct ctdl_sieve *cs = (struct ctdl_sieve *)my;
	struct sdm_vacation *vptr;
	int days = 1;
	const char *message;
	char *vacamsg_text = NULL;
	char vacamsg_subject[1024];

	syslog(LOG_DEBUG, "Action is VACATION");

	message = sieve2_getvalue_string(s, "message");
	if (message == NULL) return SIEVE2_ERROR_BADARGS;

	if (sieve2_getvalue_string(s, "subject") != NULL) {
		safestrncpy(vacamsg_subject, sieve2_getvalue_string(s, "subject"), sizeof vacamsg_subject);
	}
	else {
		snprintf(vacamsg_subject, sizeof vacamsg_subject, "Re: %s", cs->subject);
	}

	days = sieve2_getvalue_int(s, "days");
	if (days < 1) days = 1;
	if (days > MAX_VACATION) days = MAX_VACATION;

	/* Check to see whether we've already alerted this sender that we're on vacation. */
	for (vptr = cs->u->first_vacation; vptr != NULL; vptr = vptr->next) {
		if (!strcasecmp(vptr->fromaddr, cs->sender)) {
			if ( (time(NULL) - vptr->timestamp) < (days * 86400) ) {
				syslog(LOG_DEBUG, "Already alerted <%s> recently.", cs->sender);
				return SIEVE2_OK;
			}
		}
	}

	/* Assemble the reject message. */
	vacamsg_text = malloc(strlen(message) + 1024);
	if (vacamsg_text == NULL) {
		return SIEVE2_ERROR_FAIL;
	}

	sprintf(vacamsg_text, 
		"Content-type: text/plain charset=utf-8\n"
		"\n"
		"%s\n"
		"\n"
	,
		message
	);

	quickie_message(	/* This delivers the message */
		NULL,
		cs->envelope_to,
		cs->sender,
		NULL,
		vacamsg_text,
		FMT_RFC822,
		vacamsg_subject
	);

	free(vacamsg_text);

	/* Now update the list to reflect the fact that we've alerted this sender.
	 * If they're already in the list, just update the timestamp.
	 */
	for (vptr = cs->u->first_vacation; vptr != NULL; vptr = vptr->next) {
		if (!strcasecmp(vptr->fromaddr, cs->sender)) {
			vptr->timestamp = time(NULL);
			return SIEVE2_OK;
		}
	}

	/* If we get to this point, create a new record.
	 */
	vptr = malloc(sizeof(struct sdm_vacation));
	memset(vptr, 0, sizeof(struct sdm_vacation));
	vptr->timestamp = time(NULL);
	safestrncpy(vptr->fromaddr, cs->sender, sizeof vptr->fromaddr);
	vptr->next = cs->u->first_vacation;
	cs->u->first_vacation = vptr;

	return SIEVE2_OK;
}


/*
 * Callback function to parse message envelope
 */
int ctdl_getenvelope(sieve2_context_t *s, void *my)
{
	struct ctdl_sieve *cs = (struct ctdl_sieve *)my;

	syslog(LOG_DEBUG, "Action is GETENVELOPE");
	syslog(LOG_DEBUG, "EnvFrom: %s", cs->envelope_from);
	syslog(LOG_DEBUG, "EnvTo: %s", cs->envelope_to);

	if (cs->envelope_from != NULL) {
		if ((cs->envelope_from[0] != '@')&&(cs->envelope_from[strlen(cs->envelope_from)-1] != '@')) {
			sieve2_setvalue_string(s, "from", cs->envelope_from);
		}
		else {
			sieve2_setvalue_string(s, "from", "invalid_envelope_from@example.org");
		}
	}
	else {
		sieve2_setvalue_string(s, "from", "null_envelope_from@example.org");
	}


	if (cs->envelope_to != NULL) {
		if ((cs->envelope_to[0] != '@') && (cs->envelope_to[strlen(cs->envelope_to)-1] != '@')) {
			sieve2_setvalue_string(s, "to", cs->envelope_to);
		}
		else {
			sieve2_setvalue_string(s, "to", "invalid_envelope_to@example.org");
		}
	}
	else {
		sieve2_setvalue_string(s, "to", "null_envelope_to@example.org");
	}

	return SIEVE2_OK;
}




/*
 * Given the on-disk representation of our Sieve config, load
 * it into an in-memory data structure.
 */
void parse_sieve_config(char *conf, struct sdm_userdata *u) {
	char *ptr;
	char *c, *vacrec;
	char keyword[256];
	struct sdm_script *sptr;
	struct sdm_vacation *vptr;

	ptr = conf;
	while (c = ptr, ptr = bmstrcasestr(ptr, CTDLSIEVECONFIGSEPARATOR), ptr != NULL) {
		*ptr = 0;
		ptr += strlen(CTDLSIEVECONFIGSEPARATOR);

		extract_token(keyword, c, 0, '|', sizeof keyword);

		if (!strcasecmp(keyword, "lastproc")) {
			u->lastproc = extract_long(c, 1);
		}

		else if (!strcasecmp(keyword, "script")) {
			sptr = malloc(sizeof(struct sdm_script));
			extract_token(sptr->script_name, c, 1, '|', sizeof sptr->script_name);
			sptr->script_active = extract_int(c, 2);
			remove_token(c, 0, '|');
			remove_token(c, 0, '|');
			remove_token(c, 0, '|');
			sptr->script_content = strdup(c);
			sptr->next = u->first_script;
			u->first_script = sptr;
		}

		else if (!strcasecmp(keyword, "vacation")) {

			if (c != NULL) while (vacrec=c, c=strchr(c, '\n'), (c != NULL)) {

				*c = 0;
				++c;

				if (strncasecmp(vacrec, "vacation|", 9)) {
					vptr = malloc(sizeof(struct sdm_vacation));
					extract_token(vptr->fromaddr, vacrec, 0, '|', sizeof vptr->fromaddr);
					vptr->timestamp = extract_long(vacrec, 1);
					vptr->next = u->first_vacation;
					u->first_vacation = vptr;
				}
			}
		}

		/* ignore unknown keywords */
	}
}





/* 
 * Write our citadel sieve config back to disk
 * 
 * (Set yes_write_to_disk to nonzero to make it actually write the config;
 * otherwise it just frees the data structures.)
 */
void rewrite_ctdl_sieve_config(struct sdm_userdata *u, int yes_write_to_disk) {
	StrBuf *text;
	struct sdm_script *sptr;
	struct sdm_vacation *vptr;
	
	text = NewStrBufPlain(NULL, SIZ);
	StrBufPrintf(text,
		     "Content-type: application/x-citadel-sieve-config\n"
		     "\n"
		     CTDLSIEVECONFIGSEPARATOR
		     "lastproc|%ld"
		     CTDLSIEVECONFIGSEPARATOR
		     ,
		     u->lastproc
		);

	while (u->first_script != NULL) {
		StrBufAppendPrintf(text,
				   "script|%s|%d|%s" CTDLSIEVECONFIGSEPARATOR,
				   u->first_script->script_name,
				   u->first_script->script_active,
				   u->first_script->script_content
			);
		sptr = u->first_script;
		u->first_script = u->first_script->next;
		free(sptr->script_content);
		free(sptr);
	}

	if (u->first_vacation != NULL) {

		StrBufAppendPrintf(text, "vacation|\n");
		while (u->first_vacation != NULL) {
			if ( (time(NULL) - u->first_vacation->timestamp) < (MAX_VACATION * 86400)) {
				StrBufAppendPrintf(text, "%s|%ld\n",
						   u->first_vacation->fromaddr,
						   u->first_vacation->timestamp
					);
			}
			vptr = u->first_vacation;
			u->first_vacation = u->first_vacation->next;
			free(vptr);
		}
		StrBufAppendPrintf(text, CTDLSIEVECONFIGSEPARATOR);
	}

	if (yes_write_to_disk)
	{
		/* Save the config */
		quickie_message("Citadel", NULL, NULL, u->config_roomname,
				ChrPtr(text),
				4,
				"Sieve configuration"
		);
		
		/* And delete the old one */
		if (u->config_msgnum > 0) {
			CtdlDeleteMessages(u->config_roomname, &u->config_msgnum, 1, "");
		}
	}

	FreeStrBuf (&text);

}


#endif


/*
 * The next sections are enums and keys that drive the serialize/deserialize functions for the inbox rules/state configuration.
 */

// Fields to be compared
enum {
	field_from,		
	field_tocc,		
	field_subject,	
	field_replyto,	
	field_sender,	
	field_resentfrom,	
	field_resentto,	
	field_envfrom,	
	field_envto,	
	field_xmailer,	
	field_xspamflag,	
	field_xspamstatus,	
	field_listid,	
	field_size,		
	field_all
};
char *field_keys[] = {
	"from",
	"tocc",
	"subject",
	"replyto",
	"sender",
	"resentfrom",
	"resentto",
	"envfrom",
	"envto",
	"xmailer",
	"xspamflag",
	"xspamstatus",
	"listid",
	"size",
	"all"
};

// Field comparison operators
enum {
	fcomp_contains,
	fcomp_notcontains,
	fcomp_is,
	fcomp_isnot,
	fcomp_matches,
	fcomp_notmatches
};
char *fcomp_keys[] = {
	"contains",
	"notcontains",
	"is",
	"isnot",
	"matches",
	"notmatches"
};

// Actions
enum {
	action_keep,
	action_discard,
	action_reject,
	action_fileinto,
	action_redirect,
	action_vacation
};
char *action_keys[] = {
	"keep",
	"discard",
	"reject",
	"fileinto",
	"redirect",
	"vacation"
};

// Size comparison operators
enum {
	scomp_larger,
	scomp_smaller
};
char *scomp_keys[] = {
	"larger",
	"smaller"
};

// Final actions
enum {
	final_continue,
	final_stop
};
char *final_keys[] = {
	"continue",
	"stop"
};

// This data structure represents ONE inbox rule within the configuration.
struct irule {
	int compared_field;
	int field_compare_op;
	char compared_value[128];
	int size_compare_op;
	long compared_size;
	int action;
	char file_into[ROOMNAMELEN];
	char redirect_to[1024];
	char autoreply_message[SIZ];
	int final_action;
};

// This data structure represents the entire inbox rules configuration AND current state for a single user.
struct inboxrules {
	long lastproc;
	int num_rules;
	struct irule *rules;
};


// Destructor for 'struct inboxrules'
void free_inbox_rules(struct inboxrules *ibr) {
	free(ibr->rules);
	free(ibr);
}


// Constructor for 'struct inboxrules' that deserializes the configuration from text input.
struct inboxrules *deserialize_inbox_rules(char *serialized_rules) {
	int i;

	if (!serialized_rules) {
		return NULL;
	}

	/* Make a copy of the supplied buffer because we're going to shit all over it with strtok_r() */
	char *sr = strdup(serialized_rules);
	if (!sr) {
		return NULL;
	}

	struct inboxrules *ibr = malloc(sizeof(struct inboxrules));
	if (ibr == NULL) {
		return NULL;
	}
	memset(ibr, 0, sizeof(struct inboxrules));

	char *token; 
	char *rest = sr;
	while ((token = strtok_r(rest, "\n", &rest))) {

		// For backwards compatibility, "# WEBCIT_RULE" is an alias for "rule".
		// Prior to version 930, WebCit converted its rules to Sieve scripts, but saved the rules as comments for later re-editing.
		// Now, the rules hidden in the comments become the real rules.
		if (!strncasecmp(token, "# WEBCIT_RULE|", 14)) {
			strcpy(token, "rule|");	
			strcpy(&token[5], &token[14]);
		}

		// Lines containing actual rules are double-serialized with Base64.  It seemed like a good idea at the time :(
		if (!strncasecmp(token, "rule|", 5)) {
			remove_token(&token[5], 0, '|');
			char *decoded_rule = malloc(strlen(token));
			CtdlDecodeBase64(decoded_rule, &token[5], strlen(&token[5]));
			ibr->num_rules++;
			ibr->rules = realloc(ibr->rules, (sizeof(struct irule) * ibr->num_rules));
			struct irule *new_rule = &ibr->rules[ibr->num_rules - 1];
			memset(new_rule, 0, sizeof(struct irule));

			// We have a rule , now parse it
			char rtoken[SIZ];
			int nt = num_tokens(decoded_rule, '|');
			for (int t=0; t<nt; ++t) {
				extract_token(rtoken, decoded_rule, t, '|', sizeof(rtoken));
				striplt(rtoken);
				switch(t) {
					case 1:									// field to compare
						for (i=0; i<=field_all; ++i) {
							if (!strcasecmp(rtoken, field_keys[i])) {
								new_rule->compared_field = i;
							}
						}
						break;
					case 2:									// field comparison operation
						for (i=0; i<=fcomp_notmatches; ++i) {
							if (!strcasecmp(rtoken, fcomp_keys[i])) {
								new_rule->field_compare_op = i;
							}
						}
						break;
					case 3:									// field comparison value
						safestrncpy(new_rule->compared_value, rtoken, sizeof(new_rule->compared_value));
						break;
					case 4:									// size comparison operation
						for (i=0; i<=scomp_smaller; ++i) {
							if (!strcasecmp(rtoken, scomp_keys[i])) {
								new_rule->size_compare_op = i;
							}
						}
						break;
					case 5:									// size comparison value
						new_rule->compared_size = atol(rtoken);
						break;
					case 6:									// action
						for (i=0; i<=action_vacation; ++i) {
							if (!strcasecmp(rtoken, action_keys[i])) {
								new_rule->action = i;
							}
						}
						break;
					case 7:									// file into (target room)
						safestrncpy(new_rule->file_into, rtoken, sizeof(new_rule->file_into));
						break;
					case 8:									// redirect to (target address)
						safestrncpy(new_rule->redirect_to, rtoken, sizeof(new_rule->redirect_to));
						break;
					case 9:									// autoreply message
						safestrncpy(new_rule->autoreply_message, rtoken, sizeof(new_rule->autoreply_message));
						break;
					case 10:								// final_action;
						for (i=0; i<=final_stop; ++i) {
							if (!strcasecmp(rtoken, final_keys[i])) {
								new_rule->final_action = i;
							}
						}
						break;
					default:
						break;
				}
			}
			free(decoded_rule);

			// if we re-serialized this now, what would it look like?
			syslog(LOG_DEBUG, "test reserialize: 0|%s|%s|%s|%s|%ld|%s|%s|%s|%s|%s",
				field_keys[new_rule->compared_field],
				fcomp_keys[new_rule->field_compare_op],
				new_rule->compared_value,
				scomp_keys[new_rule->size_compare_op],
				new_rule->compared_size,
				action_keys[new_rule->action],
				new_rule->file_into,
				new_rule->redirect_to,
				new_rule->autoreply_message,
				final_keys[new_rule->final_action]
			);
			// delete the above after moving it to a reserialize function

		}

		// "lastproc" indicates the newest message number in the inbox that was previously processed by our inbox rules.
		else if (!strncasecmp(token, "lastproc|", 5)) {
			ibr->lastproc = atol(&token[9]);
		}

	}

	free(sr);		// free our copy of the source buffer that has now been trashed with null bytes...
	return(ibr);		// and return our complex data type to the caller.
}


/*
 * Process a single message.  We know the room, the user, the rules, the message number, etc.
 */
void inbox_do_msg(long msgnum, void *userdata) {
	struct inboxrules *ii = (struct inboxrules *) userdata;
	struct CtdlMessage *msg = NULL;		// If we are loading a message to process, put it here.
	int headers_loaded = 0;			// Did we load the headers yet?  Do it only once.
	int body_loaded = 0;			// Did we load the message body yet?  Do it only once.
	int metadata_loaded = 0;		// Did we load the metadata yet?  Do it only once.
	struct MetaData smi;			// If we are loading the metadata to compare, put it here.
	int rule_activated = 0;			// On each rule, this is set if the compare succeeds and the rule activates.
	char compare_me[SIZ];			// On each rule, we will store the field to be compared here.
	int i;

	syslog(LOG_DEBUG, "inboxrules: processing message #%ld which is higher than %ld, we are in %s", msgnum, ii->lastproc, CC->room.QRname);

	if (ii->num_rules <= 0) {
		syslog(LOG_DEBUG, "inboxrules: rule set is empty");
		return;
	}

	for (i=0; i<ii->num_rules; ++i) {
		syslog(LOG_DEBUG, "inboxrules: processing rule %d is %s", i, field_keys[ ii->rules[i].compared_field ]);
		rule_activated = 0;

		// Before doing a field compare, check to see if we have the correct parts of the message in memory.

		switch(ii->rules[i].compared_field) {
			// These fields require loading only the top-level headers
			case field_from:		// From:
			case field_tocc:		// To: or Cc:
			case field_subject:		// Subject:
			case field_replyto:		// Reply-to:
			case field_listid:		// List-ID:
			case field_envto:		// Envelope-to:
			case field_envfrom:		// Return-path:
				if (!headers_loaded) {
					syslog(LOG_DEBUG, "inboxrules: loading headers for message %ld", msgnum);
					msg = CtdlFetchMessage(msgnum, 0);
					if (!msg) {
						return;
					}
					headers_loaded = 1;
				}
				break;
			// These fields are not stored as Citadel headers, and therefore require a full message load.
			case field_sender:
			case field_resentfrom:
			case field_resentto:
			case field_xmailer:
			case field_xspamflag:
			case field_xspamstatus:
				if (!body_loaded) {
					syslog(LOG_DEBUG, "inboxrules: loading all of message %ld", msgnum);
					if (msg != NULL) {
						CM_Free(msg);
					}
					msg = CtdlFetchMessage(msgnum, 1);
					if (!msg) {
						return;
					}
					headers_loaded = 1;
					body_loaded = 1;
				}
				break;
			case field_size:
				if (!metadata_loaded) {
					syslog(LOG_DEBUG, "inboxrules: loading metadata for message %ld", msgnum);
					GetMetaData(&smi, msgnum);
					metadata_loaded = 1;
				}
				break;
			case field_all:
				syslog(LOG_DEBUG, "inboxrules: this is an always-on rule");
				break;
			default:
				syslog(LOG_DEBUG, "inboxrules: unknown rule key");
		}

		// If the rule involves a field comparison, load the field to be compared.
		compare_me[0] = 0;
		switch(ii->rules[i].compared_field) {

			case field_from:		// From:
				if (!IsEmptyStr(msg->cm_fields[erFc822Addr])) {
					safestrncpy(compare_me, msg->cm_fields[erFc822Addr], sizeof compare_me);
				}
				break;
			case field_tocc:		// To: or Cc:
				if (!IsEmptyStr(msg->cm_fields[eRecipient])) {
					safestrncpy(compare_me, msg->cm_fields[eRecipient], sizeof compare_me);
				}
				if (!IsEmptyStr(msg->cm_fields[eCarbonCopY])) {
					if (!IsEmptyStr(compare_me)) {
						strcat(compare_me, ",");
					}
					safestrncpy(&compare_me[strlen(compare_me)], msg->cm_fields[eCarbonCopY], (sizeof compare_me - strlen(compare_me)));
				}
				break;
			case field_subject:		// Subject:
				if (!IsEmptyStr(msg->cm_fields[eMsgSubject])) {
					safestrncpy(compare_me, msg->cm_fields[eMsgSubject], sizeof compare_me);
				}
				break;
			case field_replyto:		// Reply-to:
				if (!IsEmptyStr(msg->cm_fields[eReplyTo])) {
					safestrncpy(compare_me, msg->cm_fields[eReplyTo], sizeof compare_me);
				}
				break;
			case field_listid:		// List-ID:
				if (!IsEmptyStr(msg->cm_fields[eListID])) {
					safestrncpy(compare_me, msg->cm_fields[eListID], sizeof compare_me);
				}
				break;
			case field_envto:		// Envelope-to:
				if (!IsEmptyStr(msg->cm_fields[eenVelopeTo])) {
					safestrncpy(compare_me, msg->cm_fields[eenVelopeTo], sizeof compare_me);
				}
				break;
			case field_envfrom:		// Return-path:
				if (!IsEmptyStr(msg->cm_fields[eMessagePath])) {
					safestrncpy(compare_me, msg->cm_fields[eMessagePath], sizeof compare_me);
				}
				break;

			case field_sender:
			case field_resentfrom:
			case field_resentto:
			case field_xmailer:
			case field_xspamflag:
			case field_xspamstatus:

			default:
				break;
		}

		// Message data to compare is loaded, now do something.
		switch(ii->rules[i].compared_field) {
			case field_from:		// From:
			case field_tocc:		// To: or Cc:
			case field_subject:		// Subject:
			case field_replyto:		// Reply-to:
			case field_listid:		// List-ID:
			case field_envto:		// Envelope-to:
			case field_envfrom:		// Return-path:
			case field_sender:
			case field_resentfrom:
			case field_resentto:
			case field_xmailer:
			case field_xspamflag:
			case field_xspamstatus:

				// For all of the above fields, we can compare the field we've loaded into the buffer.
				syslog(LOG_DEBUG, "Value of field to compare is: <%s>", compare_me);
				switch(ii->rules[i].field_compare_op) {
					case fcomp_contains:
					case fcomp_matches:
						rule_activated = (bmstrcasestr(compare_me, ii->rules[i].compared_value) ? 1 : 0);
						syslog(LOG_DEBUG, "Does %s contain %s? %s", compare_me, ii->rules[i].compared_value, rule_activated?"yes":"no");
						break;
					case fcomp_notcontains:
					case fcomp_notmatches:
						rule_activated = (bmstrcasestr(compare_me, ii->rules[i].compared_value) ? 0 : 1);
						syslog(LOG_DEBUG, "Does %s contain %s? %s", compare_me, ii->rules[i].compared_value, rule_activated?"yes":"no");
						break;
					case fcomp_is:
						rule_activated = (strcasecmp(compare_me, ii->rules[i].compared_value) ? 0 : 1);
						syslog(LOG_DEBUG, "Does %s equal %s? %s", compare_me, ii->rules[i].compared_value, rule_activated?"yes":"no");
						break;
					case fcomp_isnot:
						rule_activated = (strcasecmp(compare_me, ii->rules[i].compared_value) ? 1 : 0);
						syslog(LOG_DEBUG, "Does %s equal %s? %s", compare_me, ii->rules[i].compared_value, rule_activated?"yes":"no");
						break;
				}
				break;

			case field_size:
				rule_activated = 0;
				syslog(LOG_DEBUG, "comparing actual message size %ld to rule message size %ld", smi.meta_rfc822_length, ii->rules[i].compared_size);
				switch(ii->rules[i].field_compare_op) {
					case scomp_larger:
						rule_activated = ((smi.meta_rfc822_length > ii->rules[i].compared_size) ? 1 : 0);
						syslog(LOG_DEBUG, "Is %ld larger than %ld? %s", smi.meta_rfc822_length, ii->rules[i].compared_size, (smi.meta_rfc822_length > ii->rules[i].compared_size) ? "yes":"no");
						break;
					case scomp_smaller:
						rule_activated = ((smi.meta_rfc822_length < ii->rules[i].compared_size) ? 1 : 0);
						syslog(LOG_DEBUG, "Is %ld smaller than %ld? %s", smi.meta_rfc822_length, ii->rules[i].compared_size, (smi.meta_rfc822_length < ii->rules[i].compared_size) ? "yes":"no");
						break;
				}
				break;
			case field_all:			// The "all messages" rule ALWAYS triggers
				rule_activated = 1;
				break;
			default:			// no matches, fall through and do nothing
				syslog(LOG_DEBUG, "inboxrules: an unknown field comparison was encountered");
				rule_activated = 0;
				break;
		}

		// FIXME you are here YOU ARE HERE next write the code to take action
		if (rule_activated) {
			syslog(LOG_DEBUG, "\033[32m\033[7mrule activated\033[0m");		// FIXME remove color


			// do action



			// do final action (anything other than "stop" means continue)
			if (ii->rules[i].final_action == final_stop) {
				syslog(LOG_DEBUG, "\033[33m\033[7mSTOP\033[0m");		// FIXME remove color
				i = ii->num_rules + 1;
			}


		}
		else {
			syslog(LOG_DEBUG, "\033[31m\033[7mrule not activated\033[0m");		// FIXME remove color
		}
	
	}

	if (msg != NULL) {
		CM_Free(msg);
	}
}


/*
 * A user account is identified as requring inbox processing.
 * Do it.
 */
void do_inbox_processing_for_user(long usernum) {
	struct CtdlMessage *msg;
	struct inboxrules *ii;
	char roomname[ROOMNAMELEN];

	if (CtdlGetUserByNumber(&CC->user, usernum) == 0) {
		if (CC->user.msgnum_inboxrules <= 0) {
			return;						// this user has no inbox rules
		}

		msg = CtdlFetchMessage(CC->user.msgnum_inboxrules, 1);
		if (msg == NULL) {
			return;						// config msgnum is set but that message does not exist
		}
	
		ii = deserialize_inbox_rules(msg->cm_fields[eMesageText]);
		CM_Free(msg);
	
		if (ii == NULL) {
			return;						// config message exists but body is null
		}

		syslog(LOG_DEBUG, "inboxrules: for %s", CC->user.fullname);

		// Go to the user's inbox room and process all new messages
		snprintf(roomname, sizeof roomname, "%010ld.%s", usernum, MAILROOM);
		if (CtdlGetRoom(&CC->room, roomname) == 0) {
			CtdlForEachMessage(MSGS_GT, ii->lastproc, NULL, NULL, NULL, inbox_do_msg, (void *) ii);
		}

		// FIXME reserialize our inbox rules/state and write changes back to the config room
		free_inbox_rules(ii);
	}
}


/*
 * Here is an array of users (by number) who have received messages in their inbox and may require processing.
 */
long *users_requiring_inbox_processing = NULL;
int num_urip = 0;
int num_urip_alloc = 0;


/*
 * Perform inbox processing for all rooms which require it
 */
void perform_inbox_processing(void) {
	if (num_urip == 0) {
		return;											// no action required
	}

	for (int i=0; i<num_urip; ++i) {
		do_inbox_processing_for_user(users_requiring_inbox_processing[i]);
	}

	free(users_requiring_inbox_processing);
	users_requiring_inbox_processing = NULL;
	num_urip = 0;
	num_urip_alloc = 0;
}


/*
 * This function is called after a message is saved to a room.
 * If it's someone's inbox, we have to check for inbox rules
 */
int serv_inboxrules_roomhook(struct ctdlroom *room) {

	// Is this someone's inbox?
	if (!strcasecmp(&room->QRname[11], MAILROOM)) {
		long usernum = atol(room->QRname);
		if (usernum > 0) {

			// first check to see if this user is already on the list
			if (num_urip > 0) {
				for (int i=0; i<=num_urip; ++i) {
					if (users_requiring_inbox_processing[i] == usernum) {		// already on the list!
						return(0);
					}
				}
			}

			// make room if we need to
			if (num_urip_alloc == 0) {
				num_urip_alloc = 100;
				users_requiring_inbox_processing = malloc(sizeof(long) * num_urip_alloc);
			}
			else if (num_urip >= num_urip_alloc) {
				num_urip_alloc += 100;
				users_requiring_inbox_processing = realloc(users_requiring_inbox_processing, (sizeof(long) * num_urip_alloc));
			}
			
			// now add the user to the list
			users_requiring_inbox_processing[num_urip++] = usernum;
		}
	}

	// No errors are possible from this function.
	return(0);
}



/*
 * Get InBox Rules
 *
 * This is a client-facing function which fetches the user's inbox rules -- it omits all lines containing anything other than a rule.
 * 
 * hmmmmm ... should we try to rebuild this in terms of deserialize_inbox_rules() instread?
 */
void cmd_gibr(char *argbuf) {

	if (CtdlAccessCheck(ac_logged_in)) return;

	cprintf("%d inbox rules for %s\n", LISTING_FOLLOWS, CC->user.fullname);

	struct CtdlMessage *msg = CtdlFetchMessage(CC->user.msgnum_inboxrules, 1);
	if (msg != NULL) {
		if (!CM_IsEmpty(msg, eMesageText)) {
			char *token; 
			char *rest = msg->cm_fields[eMesageText];
			while ((token = strtok_r(rest, "\n", &rest))) {

				// for backwards compatibility, "# WEBCIT_RULE" is an alias for "rule" 
				if (!strncasecmp(token, "# WEBCIT_RULE|", 14)) {
					strcpy(token, "rule|");	
					strcpy(&token[5], &token[14]);
				}

				// Output only lines containing rules.
				if (!strncasecmp(token, "rule|", 5)) {
        				cprintf("%s\n", token); 
				}
			}
		}
		CM_Free(msg);
	}
	cprintf("000\n");
}


/*
 * Put InBox Rules
 *
 * User transmits the new inbox rules for the account.  They are inserted into the account, replacing the ones already there.
 */
void cmd_pibr(char *argbuf) {
	if (CtdlAccessCheck(ac_logged_in)) return;

	unbuffer_output();
	cprintf("%d send new rules\n", SEND_LISTING);
	char *newrules = CtdlReadMessageBody(HKEY("000"), CtdlGetConfigLong("c_maxmsglen"), NULL, 0);
	StrBuf *NewConfig = NewStrBufPlain("Content-type: application/x-citadel-sieve-config; charset=UTF-8\nContent-transfer-encoding: 8bit\n\n", -1);

	char *token; 
	char *rest = newrules;
	while ((token = strtok_r(rest, "\n", &rest))) {
		// Accept only lines containing rules
		if (!strncasecmp(token, "rule|", 5)) {
			StrBufAppendBufPlain(NewConfig, token, -1, 0);
			StrBufAppendBufPlain(NewConfig, HKEY("\n"), 0);
		}
	}
	free(newrules);

	// Fetch the existing config so we can merge in anything that is NOT a rule 
	// (Does not start with "rule|" but has at least one vertical bar)
	struct CtdlMessage *msg = CtdlFetchMessage(CC->user.msgnum_inboxrules, 1);
	if (msg != NULL) {
		if (!CM_IsEmpty(msg, eMesageText)) {
			rest = msg->cm_fields[eMesageText];
			while ((token = strtok_r(rest, "\n", &rest))) {
				// for backwards compatibility, "# WEBCIT_RULE" is an alias for "rule" 
				if ((strncasecmp(token, "# WEBCIT_RULE|", 14)) && (strncasecmp(token, "rule|", 5)) && (haschar(token, '|'))) {
					StrBufAppendBufPlain(NewConfig, token, -1, 0);
					StrBufAppendBufPlain(NewConfig, HKEY("\n"), 0);
				}
			}
		}
		CM_Free(msg);
	}

	/* we have composed the new configuration , now save it */
	long old_msgnum = CC->user.msgnum_inboxrules;
	char userconfigroomname[ROOMNAMELEN];
	CtdlMailboxName(userconfigroomname, sizeof userconfigroomname, &CC->user, USERCONFIGROOM);
	long new_msgnum = quickie_message("Citadel", NULL, NULL, userconfigroomname, ChrPtr(NewConfig), FMT_RFC822, "inbox rules configuration");
	FreeStrBuf(&NewConfig);
	CtdlGetUserLock(&CC->user, CC->curr_user);
	CC->user.msgnum_inboxrules = new_msgnum;
	CtdlPutUserLock(&CC->user);
	if (old_msgnum > 0) {
		syslog(LOG_DEBUG, "Deleting old message %ld from %s", old_msgnum, userconfigroomname);
		CtdlDeleteMessages(userconfigroomname, &old_msgnum, 1, "");
	}
}


CTDL_MODULE_INIT(sieve)
{
	if (!threading)
	{
		CtdlRegisterProtoHook(cmd_gibr, "GIBR", "Get InBox Rules");
		CtdlRegisterProtoHook(cmd_pibr, "PIBR", "Put InBox Rules");
		CtdlRegisterRoomHook(serv_inboxrules_roomhook);
		CtdlRegisterSessionHook(perform_inbox_processing, EVT_HOUSE, PRIO_HOUSE + 10);
	}
	
        /* return our module name for the log */
	return "inboxrules";
}
