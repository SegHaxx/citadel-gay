/*
 * (c) 2009-2019 by Art Cancro and citadel.org
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



struct wow {
	char *cmd[5];			// increase this if we need to have larger commands
	char *description;
};

struct wow wows[] = {
	{{ "help" }					, "list available commands"				},
	{{ "date" }					, "print the server's date and time"			},
	{{ "show", "eggs" }				, "show how many eggs are available for serving"	},
	{{ "kill", "mark", "zuckerberg" }		, "die motherfucker die"				},
	{{ "show", "undead", "zombies", "real" }	, "show how many zombies are actually undead"		},
	{{ "show", "undead", "zombies", "hollywood" }	, "show how many zombies are hollywood communists"	},
	{{ NULL }					, "NULL"						}
};


int num_wows = sizeof(wows) / sizeof(struct wow);





int cmd_help(int sock, char *cmdbuf)
{
	int i;

	for (i = 0; wows[i].cmd[0]; ++i) {
		printf("%-10s %s\n", wows[i].cmd[0], wows[i].description);
	}
}


/* Auto-completer function */
char *command_generator(const char *text, int state)
{
	static int list_index;
	static int len;
	char *name;

	if (!state) {
		list_index = 0;
		len = strlen(text);
	}

	while (name = commands[list_index].name) {
		++list_index;

		if (!strncmp(name, text, len)) {
			return (strdup(name));
		}
	}

	return (NULL);
}


/* Auto-completer function */
char **ctdlsh_completion(const char *text, int start, int end)
{
	char **matches = (char **) NULL;

	rl_completer_word_break_characters = " ";
	if (start == 0) {
		matches = rl_completion_matches(text, command_generator);
	} else {
		rl_bind_key('\t', rl_abort);
	}

	return (matches);
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

	/* Tell libreadline how we will help with auto-completion of commands */
	rl_attempted_completion_function = ctdlsh_completion;

	/* Here we go ... main command loop */
	while ( (cmd = readline(prompt)) , ((cmd) && (*cmd)) ) {
		add_history(cmd);
		ret = do_one_command(server_socket, cmd);
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
		printf("\nCitadel administration shell (c) 2009-2019 by citadel.org\n"
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
