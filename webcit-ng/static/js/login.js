// Copyright (c) 2016-2022 by the citadel.org team
//
// This program is open source software.  Use, duplication, or
// disclosure are subject to the GNU General Public License v3.


// This is where the login screen (modal) is displayed.
// It appears in the "ctdl_big_modal" div which is defined in index.html and is used for several different modals.
// If you want to change the look of the login dialog, this is where to change it.
function display_login_screen(any_message) {
	document.getElementById("ctdl_big_modal").innerHTML = `

		<div class="ctdl-modal-header">
				<span><i class="fa fa-user"></i></span>
				<span>${any_message}</span>
		</div>

		<div class="ctdl-login-screen-grid-container">
			<div class="ctdl-login-screen-grid-item"><label><b>${_("User name:")}</b></label></div>
			<div class="ctdl-login-screen-grid-item"><input type="text" id="username" required></div>
			<div class="ctdl-login-screen-grid-item"><label><b>${_("Password:")}</b></label></div>
			<div class="ctdl-login-screen-grid-item"><input type="password" id="password" required></div>
			<div class="ctdl-login-screen-grid-item"></div>
			<div class="ctdl-login-screen-grid-item">
				<input type="checkbox" checked="checked">
				Remember me
			</div>
			<div class="ctdl-login-screen-grid-item"></div>
			<div class="ctdl-login-screen-grid-item">
				<button onClick="javascript:login_button()">${_("Log in")}</button>
				<button type="button">Cancel</button>
			</div>
		</div>
	`;

	document.getElementById("ctdl_big_modal").style.display = "block";
}


// When the user elects to log out, we just call /ctdl/a/logout and let the system flush the session.
// When we go back to ctdl_startup() it will detect that we are no longer logged in, and do the right thing.
//function logout() {
//logout = async() => {
	//response = await fetch("/ctdl/a/logout");
	//ctdl_startup();					// let the regular startup code take care of everything else
//}


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
