//
// Copyright (c) 2016-2017 by the citadel.org team
//
// This program is open source software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License version 3.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.


function display_login_screen(any_message)
{
	document.getElementById("ctdl-main").innerHTML =
		"<center><br><br>Put the login screen here, dummary<br><br>" +
		any_message + "<br><br>" +
		"<table border=0><tr><td>" +
		_("User name:") + "</td><td><input type=\"text\" id=\"username\"></td></tr><tr><td>" +
		_("Password:") + "</td><td><input type=\"password\" id=\"password\"></td></tr></table><br>" +
		"<a href=\"javascript:login_button()\">" + _("Log in") + "</a></center>"
	;

	update_banner();
}


function logout()
{
	var request = new XMLHttpRequest();
	request.open("GET", "/ctdl/a/logout", true);
	request.onreadystatechange = function() {
		login_result(this.responseText);
	};
	request.send();
	request = null;
}


function login_button(username)
{
	parms = 
		document.getElementById("username").value
		+ "|"
		+ document.getElementById("password").value
		+ "|"
	;

	var request = new XMLHttpRequest();
	request.open("POST", "/ctdl/a/login", true);
	request.onreadystatechange = function() {
		login_result(this.responseText);
	};
	request.send(parms);
	request = null;
}


function login_result(data)
{
	if (data.substring(0,1) == "2") {
		logged_in = 1;
		current_user = data.substring(4).split("|")[0];
		update_banner();
		document.getElementById("ctdl-main").innerHTML = "FIXME ok we are logged in as " + current_user + " ... " ;
	}
	else {
		display_login_screen(data.substring(4));
	}
}


// Detect whether the Citadel session is logged in as a user and update our internal variables accordingly.
//
function detect_logged_in()
{
	var request = new XMLHttpRequest();
	request.open("GET", "/ctdl/a/whoami", true);
	request.onreadystatechange = function() {
		detect_logged_in_2(this.responseText);
	};
	request.send();
	request = null;
}
function detect_logged_in_2(data)
{
	if (data.length > 0) {
		logged_in = 1;
		current_user = data;
	}
	else {
		logged_in = 0;
		current_user = _("Not logged in.");
	}
}
