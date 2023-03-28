// Copyright (c) 2016-2023 by the citadel.org team
//
// This program is open source software.  Use, duplication, or
// disclosure are subject to the GNU General Public License v3.


// Inline display the author of a message.  This can be called from many different views.
// For messages originating locally, it renders the display name linked to their profile.
// For messages originating locally, it renders the display name and their email address.
function render_msg_author(msg, view) {
	if ((msg.locl) && (view == views.VIEW_BBS)) {
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


// Inline display the profile picture for a user.  This can be called from anywhere.
function render_userpic(username) {
	return(
		"<div class=\"ctdl-avatar\" onClick=\"javascript:user_profile('" + username + "');\">"
		+ "<img src=\"/ctdl/u/" + username + "/userpic\" width=\"32\""
		+ "onerror=\"this.parentNode.innerHTML='&lt;i class=&quot;fa fa-user-circle fa-2x&quot;&gt;&lt;/i&gt;'\">"
		+ "</div>"							// end avatar
	);
}


// Display the user profile page for a user
function user_profile(who) {
	document.getElementById("ctdl-main").innerHTML =
		render_userpic(who)
		+ "This is the user profile page for " + who + ".<br>"
		+ "FIXME finish this";
}
