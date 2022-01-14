// Copyright (c) 2016-2022 by the citadel.org team
//
// This program is open source software.  It runs great on the
// Linux operating system (and probably elsewhere).  You can use,
// copy, and run it under the terms of the GNU General Public
// License version 3.  Richard Stallman is an asshole communist.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.


function display_login_screen(any_message) {

	document.getElementById("ctdl_big_modal").innerHTML =
		  "<div class=\"w3-modal-content\">"
		+ "  <div class=\"w3-container\">"

		+ "Put the login screen here, dummy<br><br>"
		+ any_message + "<br><br>"
		+ "<table border=0><tr><td>"
		+ _("User name:") + "</td><td><input type=\"text\" id=\"username\"></td></tr><tr><td>"
		+ _("Password:") + "</td><td><input type=\"password\" id=\"password\"></td></tr></table><br>"
		+ "<a href=\"javascript:login_button()\">" + _("Log in") + "</a>"

		+ "  </div>"
		+ "</div>";
	document.getElementById("ctdl_big_modal").style.display = "block";
}


function logout() {
	var request = new XMLHttpRequest();
	request.open("GET", "/ctdl/a/logout", true);
	request.onreadystatechange = function() {
		login_result(this.responseText);
	};
	request.send();
	request = null;
}


function login_button(username) {
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


function login_result(data) {
	if (data.substring(0,1) == "2") {
		logged_in = 1;
		current_user = data.substring(4).split("|")[0];
		update_banner();
		document.getElementById("ctdl-main").innerHTML = "FIXME ok we are logged in as " + current_user + " ... " ;
		document.getElementById("ctdl_big_modal").style.display = "none";
	}
	else {
		display_login_screen(data.substring(4));
	}
}


// Detect whether the Citadel session is logged in as a user and update our internal variables accordingly.
function detect_logged_in() {
	try {
		wcauth_decoded = atob(getCookieValue("wcauth"));
		wcauth_user = wcauth_decoded.split(":")[0];
	}
	catch(err) {
		wcauth_user = "";
	}
	if (wcauth_user.length > 0) {
		logged_in = 1;
		current_user = wcauth_user;
	}
	else {
		logged_in = 0;
		current_user = _("Not logged in.");
	}
}
