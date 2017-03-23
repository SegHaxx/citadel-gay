/*
 * Copyright (c) 1998-2017 by the citadel.org team
 *
 * This program is open source software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

const char *smtp_get_Recipients(void);

typedef struct _citsmtp {		/* Information about the current session */
	int command_state;
	StrBuf *Cmd;
	StrBuf *helo_node;
	StrBuf *from;
	StrBuf *recipients;
	StrBuf *OneRcpt;
	int number_of_recipients;
	int delivery_mode;
	int message_originated_locally;
	int is_lmtp;
	int is_unfiltered;
	int is_msa;
	StrBuf *preferred_sender_email;
	StrBuf *preferred_sender_name;
} citsmtp;

#define SMTP		((citsmtp *)CC->session_specific_data)

// These are all the values that can be passed to the is_final parameter of smtp_do_bounce()
enum {
	SDB_BOUNCE_FATALS,
	SDB_BOUNCE_ALL,
	SDB_WARN
};

void smtp_do_bounce(const char *instr, int is_final);
char *smtpstatus(int code);
