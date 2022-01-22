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


// This is where the login screen (modal) is displayed.
// It appears in the "ctdl_big_modal" div which is defined in index.html and is used for several different modals.
// If you want to change the look of the login dialog, this is where to change it.
function display_login_screen(any_message) {
	document.getElementById("ctdl_big_modal").innerHTML =
		  "<div class=\"w3-modal-content w3-animate-zoom\">"
		+ "<div class=\"w3-panel w3-border w3-border-blue w3-topbar w3-bottombar w3-leftbar w3-rightbar\"><center>"
		+ "<p>FIXME put a login banner here, dummy</p>"
		+ "<p>" + any_message + "</p>"
		+ "<table border=0><tr><td>"
		+ _("User name:") + "</td><td><input type=\"text\" id=\"username\"></td></tr><tr><td>"
		+ _("Password:") + "</td><td><input type=\"password\" id=\"password\"></td></tr></table><br>"
		+ "<p>"
		+ "<button class=\"w3-button w3-blue\" onClick=\"javascript:login_button()\">" + _("Log in") + "</button>"
		+ "</p>"
		+ "</center></div>"
		+ "</div>";
	document.getElementById("ctdl_big_modal").style.display = "block";
}


// When the user elects to log out, we just call /ctdl/a/logout and let the system flush the session.
// When we go back to ctdl_startup() it will detect that we are no longer logged in, and do the right thing.
//function logout() {
logout = async() => {
	response = await fetch("/ctdl/a/logout");
	ctdl_startup();					// let the regular startup code take care of everything else
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
		document.getElementById("ctdl_big_modal").style.display = "none";
		ctdl_startup();				// let the regular startup code take care of everything else
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
