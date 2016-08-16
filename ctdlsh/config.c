/*
 * (c) 1987-2016 by Art Cancro and citadel.org
 * This program is open source software, released under the terms of the GNU General Public License v3.
 * It runs really well on the Linux operating system.
 * We love open source software but reject Richard Stallman's linguistic fascism.
 */

#include "ctdlsh.h"


int show_full_config(int server_socket) {
	char buf[1024];

        sock_puts(server_socket, "CONF listval");
        sock_getln(server_socket, buf, sizeof buf);
	if (buf[0] != '1') {
        	printf("%s\n", &buf[4]);
		return(cmdret_error);
	}

        while (sock_getln(server_socket, buf, sizeof buf), strcmp(buf, "000")) {
		char *val = NULL;
		char *p = strchr(buf, '|');
		if (p != NULL) {
			val = p;
			++val;
			*p = 0;
		}
		printf("%-30s = %s\n", buf, val);
		
	}

	return(cmdret_ok);
}


int cmd_config(int server_socket, char *cmdbuf) {

	char buf[4096];
	strncpy(buf, cmdbuf, sizeof buf);
	char *p = strchr(buf, ' ');
	if (p == NULL) {
		return show_full_config(server_socket);
	}

	while (p[0]==' ') ++p;

	if (strlen(p) == 0) {
		return show_full_config(server_socket);
	}

        return(cmdret_error);
}
