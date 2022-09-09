// Copyright (c) 2016-2022 by the citadel.org team
//
// This program is open source software.  Use, duplication, or
// disclosure are subject to the GNU General Public License v3.


// Display the author of a message.  This can be called from many different views.
// For messages originating locally, it renders the display name linked to their profile.
// For messages originating locally, it renders the display name and their email address.
function render_msg_author(msg) {
	if (msg.locl) {
		return(
			"<span class=\"ctdl-username\" onClick=\"javascript:user_profile('" + msg.from + "');\">"
			+ msg.from
			+ "</span>"
		);
	}
	else {
		return("<span class=\"ctdl-username\">" + msg.from + " &lt;" + msg.rfca + "&gt;</span>");
	}
}


// Display the user profile for a user
function user_profile(who) {
	document.getElementById("ctdl-main").innerHTML = `user_profile(${who})`;
}
