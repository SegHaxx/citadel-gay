//
// Copyright (c) 2016-2019 by the citadel.org team
//
// This program is open source software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License version 3.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.


var current_room = "_BASEROOM_";
var new_messages = 0;
var total_messages = 0;
var default_view = 0;
var current_view = 0;
var logged_in = 0;
var current_user = _("Not logged in.");
var serv_info;
var last_seen = 0;
var messages_per_page = 20;
var march_list = [] ;


// Placeholder for when we add i18n later
function _(x) {
	return x;
}


// Generate a random string of the specified length
// Useful for generating one-time-use div names
//
function randomString(length) {
	var chars = '0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghiklmnopqrstuvwxyz'.split('');
	var str = '';

	if (!length) {
		length = Math.floor(Math.random() * chars.length);
	}
	for (var i = 0; i < length; i++) {
		str += chars[Math.floor(Math.random() * chars.length)];
	}
	return str;
}


// string escape for html display
//
function escapeHTML(text) {
	'use strict';
	return text.replace(/[\"&<>]/g, function (a) {
		return {
			'"': '&quot;',
			'&': '&amp;',
			'<': '&lt;',
			'>': '&gt;'
		}[a];
	});
}


// string escape for html display
//
function escapeHTMLURI(text) {
	'use strict';
	return text.replace(/./g, function (a) {
		return '%' + a.charCodeAt(0).toString(16);
	});
}


// string escape for JavaScript string
//
function escapeJS(text) {
	'use strict';
	return text.replace(/[\"\']/g, function (a) {
		return '\\' + a ;
	});
}


// This is called at the very beginning of the main page load.
//
function ctdl_startup() {
	var request = new XMLHttpRequest();
	request.open("GET", "/ctdl/c/info", true);
	request.onreadystatechange = function() {
		if ((this.readyState === 4) && ((this.status / 100) == 2)) {
			ctdl_startup_2(JSON.parse(this.responseText));
		}
	};
	request.send();
	request = null;
}


// Continuation of ctdl_startup() after serv_info is retrieved
//
function ctdl_startup_2(data) {
	serv_info = data;

	if (data.serv_rev_level < 905) {
		alert("Citadel server is too old, some functions may not work");
	}

	update_banner();

	// for now, show a room list in the main div
	gotoroom("_BASEROOM_");
	display_room_list();
}


// Display a room list in the main div.
//
function display_room_list() {
	document.getElementById("roomlist").innerHTML = "<img src=\"/ctdl/s/throbber.gif\" />" ;		// show throbber while loading

	var request = new XMLHttpRequest();
	request.open("GET", "/ctdl/r/", true);
	request.onreadystatechange = function() {
		if ((this.readyState === 4) && ((this.status / 100) == 2)) {
			display_room_list_renderer(JSON.parse(this.responseText));
		}
	};
	request.send();
	request = null;
}


// Renderer for display_room_list()
//
function display_room_list_renderer(data) {
	data = data.sort(function(a,b) {
		if (a.floor != b.floor) {
			return(a.floor - b.floor);
		}
		if (a.rorder != b.rorder) {
			return(a.rorder - b.rorder);
		}
		return(a.name < b.name);
	});

	new_roomlist_text = "<ul>" ;

	for (var i in data) {
		if (i > 0) {
			if (data[i].floor != data[i-1].floor) {
				new_roomlist_text = new_roomlist_text + "<li class=\"divider\"></li>" ;
			}
		}
		new_roomlist_text = new_roomlist_text +
			"<li>"
			+ (data[i].hasnewmsgs ? "<b>" : "")
			+ "<a href=\"javascript:gotoroom('" + escapeJS(escapeHTML(data[i].name)) + "');\">"
			+ escapeHTML(data[i].name)
			+ (data[i].hasnewmsgs ? "</b>" : "")
			+ "</a></li>"
		;
	}
	new_roomlist_text = new_roomlist_text + "</ul>";
	document.getElementById("roomlist").innerHTML = new_roomlist_text ;
}


// Update the "banner" div with all relevant info.
//
function update_banner() {
	detect_logged_in();
	if (current_room) {
		document.getElementById("ctdl_banner_title").innerHTML = current_room;
		document.title = current_room;
	}
	else {
		document.getElementById("ctdl_banner_title").innerHTML = serv_info.serv_humannode;
	}
	document.getElementById("current_user").innerHTML = current_user ;
	if (logged_in) {
		document.getElementById("lilo").innerHTML = "<a href=\"/ctdl/a/logout\">" + _("Log off") + "</a>" ;
	}
	else {
		document.getElementById("lilo").innerHTML = "<a href=\"javascript:display_login_screen('')\">" + _("Log in") + "</a>" ;
	}
}


// goto room
//
function gotoroom(roomname) {
	var request = new XMLHttpRequest();
	request.open("GET", "/ctdl/r/" + escapeHTMLURI(roomname) + "/", true);
	request.onreadystatechange = function() {
		if ((this.readyState === 4) && ((this.status / 100) == 2)) {
			gotoroom_2(JSON.parse(this.responseText));
		}
	};
	request.send();
	request = null;
}
function gotoroom_2(data) {
	current_room = data.name;
	new_messages = data.new_messages;
	total_messages = data.total_messages;
	current_view = data.current_view;
	default_view = data.default_view;
	last_seen = data.last_seen;
	update_banner();
	render_room_view(0, 9999999999);
}


// Goto next room with unread messages
// which_oper is 0=ungoto, 1=skip, 2=goto
//
function gotonext(which_oper) {
	if (which_oper != 2) return;		// FIXME implement the other two
	if (march_list.length == 0) {
		load_new_march_list();		// we will recurse back here
	}
	else {
		next_room = march_list[0].name;
		march_list.splice(0, 1);
		console.log("going to " + next_room + " , " + march_list.length + " rooms remaining in march list");
		gotoroom(next_room);
	}
}


// Called by gotonext() when the march list is empty.
//
function load_new_march_list() {
	var request = new XMLHttpRequest();
	request.open("GET", "/ctdl/r/", true);
	request.onreadystatechange = function() {
		if ((this.readyState === 4) && ((this.status / 100) == 2)) {
			march_list = (JSON.parse(this.responseText));
			march_list = march_list.filter(function(room) {
				return room.hasnewmsgs;
			});
			march_list = march_list.sort(function(a,b) {
				if (a.floor != b.floor) {
					return(a.floor - b.floor);
				}
				if (a.rorder != b.rorder) {
					return(a.rorder - b.rorder);
				}
				return(a.name < b.name);
			});
			march_list.push({name:"_BASEROOM_",known:true,hasnewmsgs:true,floor:0});
			gotonext();
		}
	};
	request.send();
	request = null;
}
