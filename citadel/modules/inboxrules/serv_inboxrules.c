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
#include <time.h>
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
			int t = 0;
			for (t=0; t<nt; ++t) {
				extract_token(rtoken, decoded_rule, t, '|', sizeof(rtoken));
				striplt(rtoken);
				switch(t) {
					case 1:								// field to compare
						for (i=0; i<=field_all; ++i) {
							if (!strcasecmp(rtoken, field_keys[i])) {
								new_rule->compared_field = i;
							}
						}
						break;
					case 2:								// field comparison operation
						for (i=0; i<=fcomp_notmatches; ++i) {
							if (!strcasecmp(rtoken, fcomp_keys[i])) {
								new_rule->field_compare_op = i;
							}
						}
						break;
					case 3:								// field comparison value
						safestrncpy(new_rule->compared_value, rtoken, sizeof(new_rule->compared_value));
						break;
					case 4:								// size comparison operation
						for (i=0; i<=scomp_smaller; ++i) {
							if (!strcasecmp(rtoken, scomp_keys[i])) {
								new_rule->size_compare_op = i;
							}
						}
						break;
					case 5:								// size comparison value
						new_rule->compared_size = atol(rtoken);
						break;
					case 6:								// action
						for (i=0; i<=action_vacation; ++i) {
							if (!strcasecmp(rtoken, action_keys[i])) {
								new_rule->action = i;
							}
						}
						break;
					case 7:								// file into (target room)
						safestrncpy(new_rule->file_into, rtoken, sizeof(new_rule->file_into));
						break;
					case 8:								// redirect to (target address)
						safestrncpy(new_rule->redirect_to, rtoken, sizeof(new_rule->redirect_to));
						break;
					case 9:								// autoreply message
						safestrncpy(new_rule->autoreply_message, rtoken, sizeof(new_rule->autoreply_message));
						break;
					case 10:							// final_action;
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
		}

		// "lastproc" indicates the newest message number in the inbox that was previously processed by our inbox rules.
		// This is a legacy location for this value and will only be used if it's the only one present.
		else if (!strncasecmp(token, "lastproc|", 5)) {
			ibr->lastproc = atol(&token[9]);
		}

		// Lines which do not contain a recognizable token must be IGNORED.  These lines may be left over
		// from a previous version and will disappear when we rewrite the config.

	}

	free(sr);		// free our copy of the source buffer that has now been trashed with null bytes...
	return(ibr);		// and return our complex data type to the caller.
}


// Perform the "fileinto" action (save the message in another room)
// Returns: 1 or 0 to tell the caller to keep (1) or delete (0) the inbox copy of the message.
//
int inbox_do_fileinto(struct irule *rule, long msgnum) {
	char *dest_folder = rule->file_into;
	char original_room_name[ROOMNAMELEN];
	char foldername[ROOMNAMELEN];
	int c;

	// Situations where we want to just keep the message in the inbox:
	if (
		(IsEmptyStr(dest_folder))			// no destination room was specified
		|| (!strcasecmp(dest_folder, "INBOX"))		// fileinto inbox is the same as keep
		|| (!strcasecmp(dest_folder, MAILROOM))		// fileinto "Mail" is the same as keep
	) {
		return(1);					// don't delete the inbox copy if this failed
	}

	// Remember what room we came from
	safestrncpy(original_room_name, CC->room.QRname, sizeof original_room_name);

	// First try a mailbox name match (check personal mail folders first)
	strcpy(foldername, original_room_name);					// This keeps the user namespace of the inbox
	snprintf(&foldername[10], sizeof(foldername)-10, ".%s", dest_folder);	// And this tacks on the target room name
	c = CtdlGetRoom(&CC->room, foldername);

	// Then a regular room name match (public and private rooms)
	if (c != 0) {
		safestrncpy(foldername, dest_folder, sizeof foldername);
		c = CtdlGetRoom(&CC->room, foldername);
	}

	if (c != 0) {
		syslog(LOG_WARNING, "inboxrules: target <%s> does not exist", dest_folder);
		return(1);					// don't delete the inbox copy if this failed
	}

	// Yes, we actually have to go there
	CtdlUserGoto(NULL, 0, 0, NULL, NULL, NULL, NULL);

	c = CtdlSaveMsgPointersInRoom(NULL, &msgnum, 1, 0, NULL, 0);

	// Go back to the room we came from
	if (strcasecmp(original_room_name, CC->room.QRname)) {
		CtdlUserGoto(original_room_name, 0, 0, NULL, NULL, NULL, NULL);
	}

	return(0);						// delete the inbox copy
}


// Perform the "redirect" action (divert the message to another email address)
// Returns: 1 or 0 to tell the caller to keep (1) or delete (0) the inbox copy of the message.
//
int inbox_do_redirect(struct irule *rule, long msgnum) {
	if (IsEmptyStr(rule->redirect_to)) {
		syslog(LOG_WARNING, "inboxrules: inbox_do_redirect() invalid recipient <%s>", rule->redirect_to);
		return(1);					// don't delete the inbox copy if this failed
	}

	struct recptypes *valid = validate_recipients(rule->redirect_to, NULL, 0);
	if (valid == NULL) {
		syslog(LOG_WARNING, "inboxrules: inbox_do_redirect() invalid recipient <%s>", rule->redirect_to);
		return(1);					// don't delete the inbox copy if this failed
	}
	if (valid->num_error > 0) {
		free_recipients(valid);
		syslog(LOG_WARNING, "inboxrules: inbox_do_redirect() invalid recipient <%s>", rule->redirect_to);
		return(1);					// don't delete the inbox copy if this failed
	}

	struct CtdlMessage *msg = CtdlFetchMessage(msgnum, 1);
	if (msg == NULL) {
		free_recipients(valid);
		syslog(LOG_WARNING, "inboxrules: cannot reload message %ld for forwarding", msgnum);
		return(1);					// don't delete the inbox copy if this failed
	}

	CtdlSubmitMsg(msg, valid, NULL);			// send the message to the new recipient
	free_recipients(valid);
	CM_Free(msg);
	return(0);						// delete the inbox copy
}


/*
 * Perform the "reject" action (delete the message, and tell the sender we deleted it)
 */
void inbox_do_reject(struct irule *rule, struct CtdlMessage *msg) {
	syslog(LOG_DEBUG, "inbox_do_reject: sender: <%s>, reject", msg->cm_fields[erFc822Addr]);

	// If we can't determine who sent the message, reject silently.
	char *sender;
	if (!IsEmptyStr(msg->cm_fields[eMessagePath])) {
		sender = msg->cm_fields[eMessagePath];
	}
	else if (!IsEmptyStr(msg->cm_fields[erFc822Addr])) {
		sender = msg->cm_fields[erFc822Addr];
	}
	else {
		return;
	}

	// Assemble the reject message.
	char *reject_text = malloc(strlen(rule->autoreply_message) + 1024);
	if (reject_text == NULL) {
		return;
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
		rule->autoreply_message
	);

	// Deliver the message
	quickie_message(
		" ",
		msg->cm_fields[eenVelopeTo],
		sender,
		MAILROOM,
		reject_text,
		FMT_RFC822,
		"Delivery status notification"
	);
	free(reject_text);
}


/*
 * Perform the "vacation" action (send an automatic response)
 */
void inbox_do_vacation(struct irule *rule, struct CtdlMessage *msg) {
	syslog(LOG_DEBUG, "inbox_do_vacation: sender: <%s>, vacation", msg->cm_fields[erFc822Addr]);

	// If we can't determine who sent the message, no auto-reply can be sent.
	char *sender;
	if (!IsEmptyStr(msg->cm_fields[eMessagePath])) {
		sender = msg->cm_fields[eMessagePath];
	}
	else if (!IsEmptyStr(msg->cm_fields[erFc822Addr])) {
		sender = msg->cm_fields[erFc822Addr];
	}
	else {
		return;
	}

	// Avoid repeatedly sending auto-replies to the same correspondent over and over again by creating
	// a hash of the user, correspondent, and reply text, and hitting the S_USETABLE database.
	StrBuf *u = NewStrBuf();
	StrBufPrintf(u, "vacation/%x/%x/%x",
		HashLittle(sender, strlen(sender)),
		HashLittle(msg->cm_fields[eenVelopeTo], msg->cm_lengths[eenVelopeTo]),
		HashLittle(rule->autoreply_message, strlen(rule->autoreply_message))
	);
	int already_seen = CheckIfAlreadySeen(u);
	FreeStrBuf(&u);

	if (!already_seen) {
		// Assemble the auto-reply message.
		StrBuf *reject_text = NewStrBuf();
		if (reject_text == NULL) {
			return;
		}

		StrBufPrintf(reject_text, 
			"Content-type: text/plain\n"
			"\n"
			"%s\n"
			"\n"
		,
			rule->autoreply_message
		);
	
		// Deliver the auto-reply.
		quickie_message(
			"",
			msg->cm_fields[eenVelopeTo],
			sender,
			MAILROOM,
			ChrPtr(reject_text),
			FMT_RFC822,
			"Delivery status notification"
		);
		FreeStrBuf(&reject_text);
	}
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
	int compare_compound = 0;		// Set to 1 when we are comparing both display name and email address
	int keep_message = 1;			// Nonzero to keep the message in the inbox after processing, 0 to delete it.
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
		compare_compound = 0;
		switch(ii->rules[i].compared_field) {
			case field_from:		// From:
				if ( (!IsEmptyStr(msg->cm_fields[erFc822Addr])) && (!IsEmptyStr(msg->cm_fields[erFc822Addr])) ) {
					snprintf(compare_me, sizeof compare_me, "%s|%s",
						msg->cm_fields[eAuthor],
						msg->cm_fields[erFc822Addr]
					);
					compare_compound = 1;		// there will be two fields to compare "name|address"
				}
				else if (!IsEmptyStr(msg->cm_fields[erFc822Addr])) {
					safestrncpy(compare_me, msg->cm_fields[erFc822Addr], sizeof compare_me);
				}
				else if (!IsEmptyStr(msg->cm_fields[eAuthor])) {
					safestrncpy(compare_me, msg->cm_fields[eAuthor], sizeof compare_me);
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
				int substring_match = (bmstrcasestr(compare_me, ii->rules[i].compared_value) ? 1 : 0);
				int exact_match = 0;
				if (compare_compound) {
					char *sep = strchr(compare_me, '|');
					if (sep) {
						*sep = 0;
						exact_match =
							(strcasecmp(compare_me, ii->rules[i].compared_value) ? 0 : 1)
							+ (strcasecmp(++sep, ii->rules[i].compared_value) ? 0 : 1)
						;
					}
				}
				else {
					exact_match = (strcasecmp(compare_me, ii->rules[i].compared_value) ? 0 : 1);
				}
				syslog(LOG_DEBUG, "substring match: %d", substring_match);
				syslog(LOG_DEBUG, "exact match: %d", exact_match);
				switch(ii->rules[i].field_compare_op) {
					case fcomp_contains:
					case fcomp_matches:
						rule_activated = substring_match;
						break;
					case fcomp_notcontains:
					case fcomp_notmatches:
						rule_activated = !substring_match;
						break;
					case fcomp_is:
						rule_activated = exact_match;
						break;
					case fcomp_isnot:
						rule_activated = !exact_match;
						break;
				}
				break;

			case field_size:
				rule_activated = 0;
				switch(ii->rules[i].field_compare_op) {
					case scomp_larger:
						rule_activated = ((smi.meta_rfc822_length > ii->rules[i].compared_size) ? 1 : 0);
						break;
					case scomp_smaller:
						rule_activated = ((smi.meta_rfc822_length < ii->rules[i].compared_size) ? 1 : 0);
						break;
				}
				break;
			case field_all:			// The "all messages" rule ALWAYS triggers
				rule_activated = 1;
				break;
			default:			// no matches, fall through and do nothing
				syslog(LOG_WARNING, "inboxrules: an unknown field comparison was encountered");
				rule_activated = 0;
				break;
		}

		// If the rule matched, perform the requested action.
		if (rule_activated) {
			syslog(LOG_DEBUG, "inboxrules: rule activated");

			// Perform the requested action
			switch(ii->rules[i].action) {
				case action_keep:
					keep_message = 1;
					break;
				case action_discard:
					keep_message = 0;
					break;
				case action_reject:
					inbox_do_reject(&ii->rules[i], msg);
					keep_message = 0;
					break;
				case action_fileinto:
					keep_message = inbox_do_fileinto(&ii->rules[i], msgnum);
					break;
				case action_redirect:
					keep_message = inbox_do_redirect(&ii->rules[i], msgnum);
					break;
				case action_vacation:
					inbox_do_vacation(&ii->rules[i], msg);
					keep_message = 1;
					break;
			}

			// Now perform the "final" action (anything other than "stop" means continue)
			if (ii->rules[i].final_action == final_stop) {
				syslog(LOG_DEBUG, "inboxrules: stop processing");
				i = ii->num_rules + 1;					// throw us out of scope to stop
			}


		}
		else {
			syslog(LOG_DEBUG, "inboxrules: rule not activated");
		}
	}

	if (msg != NULL) {		// Delete the copy of the message that is currently in memory.  We don't need it anymore.
		CM_Free(msg);
	}

	if (!keep_message) {		// Delete the copy of the message that is currently in the inbox, if rules dictated that.
		syslog(LOG_DEBUG, "inboxrules: delete %ld from inbox", msgnum);
		CtdlDeleteMessages(CC->room.QRname, &msgnum, 1, "");			// we're in the inbox already
	}

	ii->lastproc = msgnum;		// make note of the last message we processed, so we don't scan the whole inbox again
}


/*
 * A user account is identified as requring inbox processing.
 * Do it.
 */
void do_inbox_processing_for_user(long usernum) {
	struct CtdlMessage *msg;
	struct inboxrules *ii;
	char roomname[ROOMNAMELEN];
	char username[64];

	if (CtdlGetUserByNumber(&CC->user, usernum) != 0) {	// grab the user record
		return;						// and bail out if we were given an invalid user
	}

	strcpy(username, CC->user.fullname);			// save the user name so we can fetch it later and lock it

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

	if (ii->lastproc > CC->user.lastproc_inboxrules) {	// There might be a "last message processed" number left over
		CC->user.lastproc_inboxrules = ii->lastproc;	// in the ruleset from a previous version.  Use this if it is
	}							// a higher number.
	else {
		ii->lastproc = CC->user.lastproc_inboxrules;
	}

	long original_lastproc = ii->lastproc;
	syslog(LOG_DEBUG, "inboxrules: for %s, messages newer than %ld", CC->user.fullname, original_lastproc);

	// Go to the user's inbox room and process all new messages
	snprintf(roomname, sizeof roomname, "%010ld.%s", usernum, MAILROOM);
	if (CtdlGetRoom(&CC->room, roomname) == 0) {
		CtdlForEachMessage(MSGS_GT, ii->lastproc, NULL, NULL, NULL, inbox_do_msg, (void *) ii);
	}

	// Record the number of the last message we processed
	if (ii->lastproc > original_lastproc) {
		CtdlGetUserLock(&CC->user, username);
		CC->user.lastproc_inboxrules = ii->lastproc;	// Avoid processing the entire inbox next time
		CtdlPutUserLock(&CC->user);
	}

	// And free the memory.
	free_inbox_rules(ii);
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
	int i = 0;

	if (num_urip == 0) {
		return;											// no action required
	}

	for (i=0; i<num_urip; ++i) {
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
	int i = 0;

	// Is this someone's inbox?
	if (!strcasecmp(&room->QRname[11], MAILROOM)) {
		long usernum = atol(room->QRname);
		if (usernum > 0) {

			// first check to see if this user is already on the list
			if (num_urip > 0) {
				for (i=0; i<=num_urip; ++i) {
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
				else {
					cprintf("# invalid rule found : %s\n", token);
				}
			}
		}
		CM_Free(msg);
	}
	cprintf("000\n");
}


/*
 * Rewrite the rule set to disk after it has been modified
 * Called by cmd_pibr()
 * Returns the msgnum of the new rules message
 */
void rewrite_rules_to_disk(const char *new_config) {
	long old_msgnum = CC->user.msgnum_inboxrules;
	char userconfigroomname[ROOMNAMELEN];
	CtdlMailboxName(userconfigroomname, sizeof userconfigroomname, &CC->user, USERCONFIGROOM);
	long new_msgnum = quickie_message("Citadel", NULL, NULL, userconfigroomname, new_config, FMT_RFC822, "inbox rules configuration");
	CtdlGetUserLock(&CC->user, CC->curr_user);
	CC->user.msgnum_inboxrules = new_msgnum;		// Now we know where to get the rules next time
	CC->user.lastproc_inboxrules = new_msgnum;		// Avoid processing the entire inbox next time
	CtdlPutUserLock(&CC->user);
	if (old_msgnum > 0) {
		syslog(LOG_DEBUG, "Deleting old message %ld from %s", old_msgnum, userconfigroomname);
		CtdlDeleteMessages(userconfigroomname, &old_msgnum, 1, "");
	}
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
	rewrite_rules_to_disk(ChrPtr(NewConfig));
	FreeStrBuf(&NewConfig);
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
