// Copyright (c) 2016-2023 by the citadel.org team
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


// This function is the dispatcher that determines the correct view for a room, and calls the correct renderer.
function render_room_view() {

	// The view renderer will set its own room navigation buttons
	for (const d of ["ctdl-newmsg-button", "ctdl-ungoto-button", "ctdl-skip-button", "ctdl-goto-button", "ctdl-delete-button"]) {
		document.getElementById(d).style.display = "none";
	}

	clear_sidebar_selection();
	document.getElementById("ctdl-main").innerHTML = _("Loading messages from server, please wait");

	switch(current_view) {

		// The "forum" module displays rooms with the "VIEW_BBS" view as classic style web forums.
		case views.VIEW_BBS:
			document.getElementById("ctdl-sidebar-button-forums").classList.add("ctdl-sidebar-button-selected");
			view_render_forums();
			break;

		// The "mail" module displays rooms with the VIEW_MAILBOX view as a webmail program.
		case views.VIEW_MAILBOX:
		case views.VIEW_DRAFTS:
			document.getElementById("ctdl-sidebar-button-mail").classList.add("ctdl-sidebar-button-selected");
			display_mail_folder_list("ctdl_mail_folder_list");
			view_render_mail();
			break;

		// The "contacts" module displays rooms with the VIEW_ADDRESSBOOK view as a contacts manager.
		case views.VIEW_ADDRESSBOOK:
			document.getElementById("ctdl-main").innerHTML =
				"<div class=\"ctdl-middle\">'" + current_room + "' is an address book but there is no renderer.</div>";
			break;

		case views.VIEW_CALENDAR:
		case views.VIEW_CALBRIEF:
			document.getElementById("ctdl-main").innerHTML =
				"<div class=\"ctdl-middle\">'" + current_room + "' is a calendar but there is no renderer.</div>";
			break;

		case views.VIEW_TASKS:
			document.getElementById("ctdl-main").innerHTML =
				"<div class=\"ctdl-middle\">'" + current_room + "' is a task list but there is no renderer.</div>";
			break;

		case views.VIEW_NOTES:
			document.getElementById("ctdl-main").innerHTML =
				"<div class=\"ctdl-middle\">'" + current_room + "' is a notes list but there is no renderer.</div>";
			break;

		case views.VIEW_WIKI:
			document.getElementById("ctdl-main").innerHTML =
				"<div class=\"ctdl-middle\">'" + current_room + "' is a wiki but there is no renderer.</div>";
			break;

		case views.VIEW_JOURNAL:
			document.getElementById("ctdl-main").innerHTML =
				"<div class=\"ctdl-middle\">'" + current_room + "' is a journal but there is no renderer.</div>";
			break;

		case views.VIEW_BLOG:
			document.getElementById("ctdl-main").innerHTML =
				"<div class=\"ctdl-middle\">'" + current_room + "' is a blog but there is no renderer.</div>";
			break;

		case views.VIEW_QUEUE:
			document.getElementById("ctdl-main").innerHTML =
				"<div class=\"ctdl-middle\">We ought to be displaying the email queue here.</div>";
			break;

		default:
			document.getElementById("ctdl-main").innerHTML =
				"<div class=\"ctdl-middle\">The view for " + current_room + " is " + current_view + " but there is no renderer.</div>";
			break;
	}

}


// This gets called when the user clicks the "enter message" or "post message" or "add item" button etc.
function entmsg_dispatcher() {
	switch(current_view) {
		case views.VIEW_BBS:
			forum_entmsg();
			break;
		case views.VIEW_MAILBOX:
			mail_compose(false, "", 0, "", "", "");
			break;
		default:
			break;
	}
}


// This gets called when the user clicks the "delete" button etc.
function delete_dispatcher() {
	switch(current_view) {
		case views.VIEW_MAILBOX:
			mail_delete_selected();
			break;
		default:
			break;
	}
}


