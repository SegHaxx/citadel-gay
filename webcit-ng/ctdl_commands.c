/*
 * Copyright (c) 1996-2018 by the citadel.org team
 *
 * This program is open source software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include "webcit.h"


/*
 * /ctdl/c/info returns a JSON representation of the output of an INFO command.
 */
void serv_info(struct http_transaction *h, struct ctdlsession *c)
{
	char buf[1024];

	ctdl_printf(c, "INFO");
	ctdl_readline(c, buf, sizeof(buf));
	if (buf[0] != '1') {
		do_502(h);
		return;
	}

	JsonValue *j = NewJsonObject(HKEY("serv_info"));
	int i = 0;
	while (ctdl_readline(c, buf, sizeof(buf)), strcmp(buf, "000"))
		switch (i++) {
		case 0:
			JsonObjectAppend(j, NewJsonNumber(HKEY("serv_pid"), atol(buf)));
			break;
		case 1:
			JsonObjectAppend(j, NewJsonPlainString(HKEY("serv_nodename"), buf, -1));
			break;
		case 2:
			JsonObjectAppend(j, NewJsonPlainString(HKEY("serv_humannode"), buf, -1));
			break;
		case 3:
			JsonObjectAppend(j, NewJsonPlainString(HKEY("serv_fqdn"), buf, -1));
			break;
		case 4:
			JsonObjectAppend(j, NewJsonPlainString(HKEY("serv_software"), buf, -1));
			break;
		case 5:
			JsonObjectAppend(j, NewJsonNumber(HKEY("serv_rev_level"), atol(buf)));
			break;
		case 6:
			JsonObjectAppend(j, NewJsonPlainString(HKEY("serv_bbs_city"), buf, -1));
			break;
		case 7:
			JsonObjectAppend(j, NewJsonPlainString(HKEY("serv_sysadm"), buf, -1));
			break;
		case 14:
			JsonObjectAppend(j, NewJsonBool(HKEY("serv_supports_ldap"), atoi(buf)));
			break;
		case 15:
			JsonObjectAppend(j, NewJsonBool(HKEY("serv_newuser_disabled"), atoi(buf)));
			break;
		case 16:
			JsonObjectAppend(j, NewJsonPlainString(HKEY("serv_default_cal_zone"), buf, -1));
			break;
		case 20:
			JsonObjectAppend(j, NewJsonBool(HKEY("serv_supports_sieve"), atoi(buf)));
			break;
		case 21:
			JsonObjectAppend(j, NewJsonBool(HKEY("serv_fulltext_enabled"), atoi(buf)));
			break;
		case 22:
			JsonObjectAppend(j, NewJsonPlainString(HKEY("serv_svn_revision"), buf, -1));
			break;
		case 23:
			JsonObjectAppend(j, NewJsonBool(HKEY("serv_supports_openid"), atoi(buf)));
			break;
		case 24:
			JsonObjectAppend(j, NewJsonBool(HKEY("serv_supports_guest"), atoi(buf)));
			break;
		}

	StrBuf *sj = NewStrBuf();
	SerializeJson(sj, j, 1);	// '1' == free the source array

	add_response_header(h, strdup("Content-type"), strdup("application/json"));
	h->response_code = 200;
	h->response_string = strdup("OK");
	h->response_body_length = StrLength(sj);
	h->response_body = SmashStrBuf(&sj);
}


/*
 * Dispatcher for paths starting with /ctdl/c/
 */
void ctdl_c(struct http_transaction *h, struct ctdlsession *c)
{
	if (!strcasecmp(h->uri, "/ctdl/c/info")) {
		serv_info(h, c);
	} else {
		do_404(h);
	}
}
