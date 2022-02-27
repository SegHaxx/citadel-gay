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
	document.getElementById("ctdl_big_modal").innerHTML = `
		<div class="w3-modal-content w3-card-4 w3-animate-zoom" style="max-width:600px">
	
			<div class="w3-center"><br>
				<span class="w3-button w3-xlarge w3-transparent w3-display-topright" title="Close Modal">×</span>
				<span class="w3-circle w3-margin-top"><i class="fa fa-user"></i></span>
			</div>

			<div class="w3-container">
				<div class="w3-section">
					<span class="w3-center w3-red">${any_message}</span>
				</div>
				<div class="w3-section">
					<label><b>${_("User name:")}</b></label>
					<input class="w3-input w3-border w3-margin-bottom" type="text" id="username" required>
					<label><b>${_("Password:")}</b></label>
					<input class="w3-input w3-border" type="password" id="password" required>
					<button class="w3-button w3-block w3-blue w3-section w3-padding" onClick="javascript:login_button()">${_("Log in")}</button>
					<input class="w3-check w3-margin-top" type="checkbox" checked="checked"> Remember me
				</div>
			</div>

			<div class="w3-container w3-border-top w3-padding-16 w3-light-grey">
				<button type="button" class="w3-button w3-red">Cancel</button>
				<span class="w3-right w3-padding w3-hide-small">Forgot <a href="#">password?</a></span>
			</div>
		</div>
	`;

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
		if (this.readyState === XMLHttpRequest.DONE) {
			login_result(JSON.parse(this.responseText));
		}
	};
	request.send(parms);
	request = null;
}


// Feed this a JSON output from login_button() or a similar function
function login_result(data) {
	if (data.result) {
		document.getElementById("ctdl_big_modal").style.display = "none";
		ctdl_startup();				// let the regular startup code take care of everything else
	}
	else {
		display_login_screen(data.message);
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
