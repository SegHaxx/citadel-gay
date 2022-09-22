// Copyright (c) 2016-2022 by the citadel.org team
//
// This program is open source software.  Use, duplication, or
// disclosure are subject to the GNU General Public License v3.



// Clear the sidebar buttons of any style indicating that one is selected
function clear_sidebar_selection() {
	var items = document.getElementById("ctdl-sidebar").getElementsByTagName("*");
	for (var i = items.length; i--;) {
		if (items[i].id.includes("ctdl-sidebar-button-")) {
			items[i].classList.remove("ctdl-sidebar-button-selected");
		}
	}
	document.getElementById("ctdl_mail_folder_list").style.display = "none";
}


// This function is the dispatcher that determines the correct view for a room,
// and calls the correct renderer.  Greater/Less than bounds are accepted.
function render_room_view(gt_msg, lt_msg) {

	document.getElementById("ctdl-newmsg-button").style.display = "none";		// the view renderer will set this
	clear_sidebar_selection();

	switch(current_view) {

		// The "forum" module displays rooms with the "VIEW_BBS" view as classic style web forums.
		case views.VIEW_BBS:
			document.getElementById("ctdl-sidebar-button-forums").classList.add("ctdl-sidebar-button-selected");
			document.getElementById("ctdl-main").innerHTML = "<div id=\"ctdl-mrp\" class=\"ctdl-forum-reading-pane\"></div>";
			forum_readmessages("ctdl-mrp", gt_msg, lt_msg);
			break;

		// The "mail" module displays rooms with the VIEW_MAILBOX view as a webmail program.
		case views.VIEW_MAILBOX:
			document.getElementById("ctdl-sidebar-button-mail").classList.add("ctdl-sidebar-button-selected");
			document.getElementById("ctdl-main").innerHTML = "";
			mail_display();
			break;

		default:
			document.getElementById("ctdl-main").innerHTML =
				"<center>The view for " + current_room + " is " + current_view + " but there is no renderer.</center>";
			break;
	}

	// Show the mail folder list only when the Mail view is active.
	if (current_view == views.VIEW_MAILBOX) {
		document.getElementById("ctdl_mail_folder_list").style.display = "block";
	}

}


// This gets called when the user clicks the "enter message" or "post message" or "add item" button etc.
function entmsg_dispatcher() {
	switch(current_view) {
		case views.VIEW_BBS:
			forum_entmsg();
			break;
		case views.VIEW_MAILBOX:
			mail_compose(false, "", "");
			break;
		default:
			break;
	}
}

