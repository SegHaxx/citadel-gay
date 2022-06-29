//
// Floor functions
//
// Copyright (c) 1996-2022 by the citadel.org team
//
// This program is open source software.  Use, duplication, or
// disclosure are subject to the GNU General Public License v3.

#include "webcit.h"


// Dispatcher for "/ctdl/f" and "/ctdl/f/" for the floor list
void floor_list(struct http_transaction *h, struct ctdlsession *c) {
	char buf[1024];
	char floorname[1024];

	ctdl_printf(c, "LFLR");
	ctdl_readline(c, buf, sizeof(buf));
	if (buf[0] != '1') {
		do_502(h);
		return;
	}

	JsonValue *j = NewJsonArray(HKEY("lflr"));
	while (ctdl_readline(c, buf, sizeof(buf)), strcmp(buf, "000")) {

		// 0  |1   |2
		// num|name|refcount
		JsonValue *jr = NewJsonObject(HKEY("floor"));

		extract_token(floorname, buf, 1, '|', sizeof floorname);
		JsonObjectAppend(jr, NewJsonPlainString(HKEY("name"), floorname, -1));

		JsonObjectAppend(jr, NewJsonNumber(HKEY("num"), extract_int(buf, 0)));
		JsonObjectAppend(jr, NewJsonNumber(HKEY("refcount"), extract_int(buf, 2)));

		JsonArrayAppend(j, jr);	// add the room to the array
	}

	StrBuf *sj = NewStrBuf();
	SerializeJson(sj, j, 1);	// '1' == free the source array

	add_response_header(h, strdup("Content-type"), strdup("application/json"));
	h->response_code = 200;
	h->response_string = strdup("OK");
	h->response_body_length = StrLength(sj);
	h->response_body = SmashStrBuf(&sj);
}


// Dispatcher for paths starting with /ctdl/f/
// (This is a stub ... we will need to add more functions when we can do more than just a floor list)
void ctdl_f(struct http_transaction *h, struct ctdlsession *c) {
	floor_list(h, c);
	return;
}
