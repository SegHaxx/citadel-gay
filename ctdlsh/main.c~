/*
 * (c) 2009-2020 by Art Cancro and citadel.org
 * This program is open source.  It runs great on the Linux operating system.
 * It's released under the General Public License (GPL) version 3.
 */

#include "ctdlsh.h"


/*
 * Commands understood by ctdlsh
 */
typedef struct {
	char *name;
	ctdlsh_cmdfunc_t *func;
	char *doc;
} COMMAND;

COMMAND commands[] = {
	{"?", cmd_help, "Display this message"},
	{"help", cmd_help, "Display this message"},
	{"date", cmd_datetime, "Print the server's date and time"},
	{"config", cmd_config, "Configure the Citadel server"},
	{"export", cmd_export, "Export all Citadel databases"},
	{"shutdown", cmd_shutdown, "Shut down the Citadel server"},
	{"time", cmd_datetime, "Print the server's date and time"},
	{"passwd", cmd_passwd, "Set or change an account password"},
	{"who", cmd_who, "Display a list of online users"},
	{"mailq", cmd_mailq, "Show the outbound email queue"},
	{NULL, NULL, NULL}
};



int cmd_help(int sock, char *cmdbuf)
{
	int i;

	for (i = 0; commands[i].func != NULL; ++i) {
		printf("%10s %s\n", commands[i].name, commands[i].doc);
	}
}


int do_one_command(int server_socket, char *cmd)
{
	int i;
	int ret;
	for (i = 0; commands[i].func != NULL; ++i) {
		if (!strncasecmp(cmd, commands[i].name, strlen(commands[i].name))) {
			ret = (*commands[i].func) (server_socket, cmd);
		}
	}
	return ret;
}

char *command_name_generator(const char *text, int state)
{
	static int list_index, len;
	char *name;

	if (!state) {
		list_index = 0;
		len = strlen(text);
	}

	while (name = commands[list_index++].name) {
		if (strncmp(name, text, len) == 0) {
			return strdup(name);
		}
	}

	return NULL;
}


char **command_name_completion(const char *text, int start, int end)
{
	rl_attempted_completion_over = 1;
	return rl_completion_matches(text, command_name_generator);
}


void do_main_loop(int server_socket)
{
	char *cmd = NULL;
	char prompt[1024];
	char buf[1024];
	char server_reply[1024];
	int i;
	int ret = (-1);

	strcpy(prompt, "> ");

	/* Do an INFO command and learn the hostname for the prompt */
	sock_puts(server_socket, "INFO");
	sock_getln(server_socket, buf, sizeof buf);
	if (buf[0] == '1') {
		i = 0;
		while (sock_getln(server_socket, buf, sizeof buf), strcmp(buf, "000")) {
			if (i == 1) {
				sprintf(prompt, "\n%s> ", buf);
			}
			++i;
		}
	}

	/* Here we go ... main command loop */
	rl_attempted_completion_function = command_name_completion;
	while ( (cmd = readline(prompt)) , cmd ) {
		if (*cmd) {
			add_history(cmd);
			ret = do_one_command(server_socket, cmd);
		}
		free(cmd);
	}
}


/*
 * If you don't know what main() does by now you probably shouldn't be reading this code.
 */
int main(int argc, char **argv)
{
	int server_socket = 0;
	char buf[1024];
	int i;
	char *ctdldir = CTDLDIR;
	char cmd[1024] = { 0 };
	int exitcode = 0;

	for (i = 1; i < argc; ++i) {
		if (!strcmp(argv[i], "-h")) {
			ctdldir = argv[++i];
		} else {
			if (strlen(cmd) > 0) {
				strcat(cmd, " ");
			}
			strcat(cmd, argv[i]);
		}
	}

	int is_interactive = ((strlen(cmd) == 0) ? 1 : 0);

	if (is_interactive) {
		printf("\nCitadel administration shell (c) 2009-2020 by citadel.org\n"
		       "This is open source software made available to you under the terms\n"
		       "of the GNU General Public License v3.  All other rights reserved.\n");
		printf("Connecting to Citadel server in %s...\n", ctdldir);
	}

	sprintf(buf, "%s/citadel-admin.socket", ctdldir);
	server_socket = uds_connectsock(buf);
	if (server_socket < 0) {
		exit(1);
	}

	sock_getln(server_socket, buf, sizeof buf);
	if (buf[0] == '2') {
		if (is_interactive) {
			printf("Connected: %s\n", buf);
			do_main_loop(server_socket);
		} else {
			exitcode = do_one_command(server_socket, cmd);
		}
	}

	sock_puts(server_socket, "QUIT");
	sock_getln(server_socket, buf, sizeof buf);
	if (is_interactive) {
		printf("%s\n", buf);
	}
	close(server_socket);
	exit(exitcode);
}
