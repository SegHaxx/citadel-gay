/*
 * (c) 1987-2016 by Art Cancro and citadel.org
 * This program is open source software, released under the terms of the GNU General Public License v3.
 * It runs really well on the Linux operating system.
 * We love open source software but reject Richard Stallman's linguistic fascism.
 */

#include "ctdlsh.h"


int show_full_config(int server_socket)
{
	char buf[1024];

	sock_puts(server_socket, "CONF listval");
	sock_getln(server_socket, buf, sizeof buf);
	if (buf[0] != '1') {
		printf("%s\n", &buf[4]);
		return (cmdret_error);
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

	return (cmdret_ok);
}


int show_single_config(int server_socket, char *keyname)
{
	char buf[1024];

	sock_printf(server_socket, "CONF getval|%s\n", keyname);
	sock_getln(server_socket, buf, sizeof buf);
	if (buf[0] == '2') {
		char *v = &buf[4];
		char *t = NULL;
		while (t = strrchr(v, '|'), t != NULL)
			t[0] = 0;
		printf("%-30s = %s\n", keyname, v);
		return (cmdret_ok);
	} else {
		printf("\n");
		return (cmdret_error);
	}
}


int set_single_config(int server_socket, char *keyname, char *val)
{
	char buf[1024];

	sock_printf(server_socket, "CONF putval|%s|%s\n", keyname, val);
	sock_getln(server_socket, buf, sizeof buf);
	if (buf[0] != '2') {
		printf("%s\n", buf);
		return (cmdret_error);
	}
	return (show_single_config(server_socket, keyname));
}


int cmd_config(int server_socket, char *cmdbuf)
{

	char buf[4096];
	strncpy(buf, cmdbuf, sizeof buf);
	char *k = strchr(buf, ' ');
	if (k == NULL) {
		return show_full_config(server_socket);
	}

	while (k[0] == ' ')
		++k;

	if (strlen(k) == 0) {
		return show_full_config(server_socket);
	}

	if (k[0] == '?') {
		printf("config                 Print values of all configuration keys\n");
		printf("config ?               Display this message\n");
		printf("config [key]           Print value of configuration key 'key'\n");
		printf("config [key] [value]   Set configuration key 'key' to 'value'\n");
		return (cmdret_ok);
	}

	char *v = strchr(k, ' ');
	if (v == NULL) {
		return show_single_config(server_socket, k);
	}

	v[0] = 0;
	++v;
	while (v[0] == ' ')
		++v;

	if (strlen(v) == 0) {
		return show_single_config(server_socket, k);
	}

	return set_single_config(server_socket, k, v);
}
