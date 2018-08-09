/*
 * Tuning of various parameters of the system.
 * Normally you don't want to mess with any of this.
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

/*
 * NLI is the string that shows up in a <W>ho's online listing for sessions
 * that are active, but for which no user has yet authenticated.
 */
#define NLI	"(not logged in)"

/*
 * Maximum number of floors on the system.
 * WARNING!  *Never* change this value once your system is up; THINGS WILL DIE!
 * Also, do not set it higher than 127.
 */
#define MAXFLOORS	16

/*
 * Standard buffer size for string datatypes.  DO NOT CHANGE!  Not only does
 * there exist a minimum buffer size for certain protocols (such as IMAP), but
 * fixed-length buffers are now stored in some of the data structures on disk,
 * so if you change the buffer size you'll fux0r your database.
 */
#define SIZ		4096

/*
 * If the body of a message is beyond this size, it will be stored in
 * a separate table.
 */
#define BIGMSG		1024

/*
 * SMTP delivery timeouts (measured in seconds)
 * If outbound SMTP deliveries cannot be completed due to transient errors
 * within SMTP_DELIVER_WARN seconds, the sender will receive a warning message
 * indicating that the message has not yet been delivered but Citadel will
 * keep trying.  After SMTP_DELIVER_FAIL seconds, Citadel will advise the
 * sender that the deliveries have failed.
 */
#define SMTP_DELIVER_WARN	14400		// warn after four hours
#define SMTP_DELIVER_FAIL	432000		// fail after five days

/*
 * Who bounced messages appear to be from
 */
#define BOUNCESOURCE		"Citadel Mail Delivery Subsystem"

/*
 * The names of rooms which are automatically created by the system
 */
#define BASEROOM		"Lobby"
#define MAILROOM		"Mail"
#define SENTITEMS		"Sent Items"
#define AIDEROOM		"Aide"
#define USERCONFIGROOM		"My Citadel Config"
#define USERCALENDARROOM	"Calendar"
#define USERTASKSROOM		"Tasks"
#define USERCONTACTSROOM	"Contacts"
#define USERNOTESROOM		"Notes"
#define USERDRAFTROOM		"Drafts"
#define USERTRASHROOM		"Trash"
#define PAGELOGROOM		"Sent/Received Pages"
#define SYSCONFIGROOM		"Local System Configuration"
#define SMTP_SPOOLOUT_ROOM	"__CitadelSMTPspoolout__"

/*
 * Where we keep messages containing the vCards that source our directory.  It
 * makes no sense to change this, because you'd have to change it on every
 * system on the network.  That would be stupid.
 */
#define ADDRESS_BOOK_ROOM	"Global Address Book"

/*
 * How long (in seconds) to retain message entries in the use table
 */
#define USETABLE_RETAIN		864000L		/* 10 days */

/*
 * The size of per-thread stacks.  If set too low, citserver will randomly crash.
 */
#define THREADSTACKSIZE		0x100000

/*
 * How many messages may the full text indexer scan before flushing its
 * tables to disk?
 */
#define FT_MAX_CACHE		25
