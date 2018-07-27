/*
 * (c) 1987-2017 by Art Cancro and citadel.org
 * This program is open source software, released under the terms of the GNU General Public License v3.
 * It runs really well on the Linux operating system.
 * We love open source software but reject Richard Stallman's linguistic fascism.
 */

#include "ctdlsh.h"


void mailq_show_this_queue_entry(StrBuf * MsgText)
{
	const char *Pos = NULL;
	StrBuf *Line = NewStrBuf();
	int sip = 0;

	do {
		sip = StrBufSipLine(Line, MsgText, &Pos);

		if (!strncasecmp(ChrPtr(Line), HKEY("msgid|"))) {
			printf("Message %ld:\n", atol(ChrPtr(Line) + 6));
		} else if (!strncasecmp(ChrPtr(Line), HKEY("submitted|"))) {
			time_t submitted = atol(ChrPtr(Line) + 10);
			printf("Originally submitted: %s", asctime(localtime(&submitted)));
		} else if (!strncasecmp(ChrPtr(Line), HKEY("attempted|"))) {
			time_t attempted = atol(ChrPtr(Line) + 10);
			printf("Last delivery attempt: %s", asctime(localtime(&attempted)));
		} else if (!strncasecmp(ChrPtr(Line), HKEY("bounceto|"))) {
			printf("Sender: %s\n", ChrPtr(Line) + 9);
		} else if (!strncasecmp(ChrPtr(Line), HKEY("remote|"))) {
			printf("Recipient: %s\n", ChrPtr(Line) + 7);
		}
	} while (sip);

	FreeStrBuf(&Line);
	printf("\n");
}


int cmd_mailq(int server_socket, char *cmdbuf)
{
	char buf[1024];
	long *msgs = NULL;
	int num_msgs = 0;
	int num_alloc = 0;
	int i;
	StrBuf *MsgText;

	sock_puts(server_socket, "GOTO __CitadelSMTPspoolout__");
	sock_getln(server_socket, buf, sizeof buf);
	if (buf[0] != '2') {
		printf("%s\n", &buf[4]);
		return (cmdret_error);
	}

	sock_puts(server_socket, "MSGS ALL");
	sock_getln(server_socket, buf, sizeof buf);
	if (buf[0] != '1') {
		printf("%s\n", &buf[4]);
		return (cmdret_error);
	}

	MsgText = NewStrBuf();
	while (sock_getln(server_socket, buf, sizeof buf), strcmp(buf, "000")) {

		if (num_alloc == 0) {
			num_alloc = 100;
			msgs = malloc(num_alloc * sizeof(long));
		} else if (num_msgs >= num_alloc) {
			num_alloc *= 2;
			msgs = realloc(msgs, num_alloc * sizeof(long));
		}

		msgs[num_msgs++] = atol(buf);
	}

	for (i = 0; i < num_msgs; ++i) {
		sock_printf(server_socket, "MSG2 %ld\n", msgs[i]);
		sock_getln(server_socket, buf, sizeof buf);
		if (buf[0] == '1') {
			FlushStrBuf(MsgText);
			while (sock_getln(server_socket, buf, sizeof buf), strcmp(buf, "000")) {
				StrBufAppendPrintf(MsgText, "%s\n", buf);
			}
			if (bmstrcasestr((char *) ChrPtr(MsgText), "\nContent-type: application/x-citadel-delivery-list") != NULL) {
				mailq_show_this_queue_entry(MsgText);
			}
		}
	}

	if (msgs != NULL) {
		free(msgs);
	}
	FreeStrBuf(&MsgText);
	return (cmdret_ok);
}
