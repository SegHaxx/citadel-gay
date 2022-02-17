//
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


// This function is the dispatcher that determines the correct view for a room,
// and calls the correct renderer.  Greater/Less than bounds are accepted.
function render_room_view(gt_msg, lt_msg) {

	document.getElementById("ctdl-newmsg-button").style.display = "none";		// the view renderer will set this

	// Clear the highlighting out of all the sidebar buttons
	var items = document.getElementById("ctdl-sidebar").getElementsByTagName("*");
	for (var i = items.length; i--;) {
		if (items[i].id.includes("ctdl-sidebar-button-")) {
			items[i].classList.remove("w3-blue");
		}
	}

	switch(current_view) {
		case views.VIEW_BBS:
			document.getElementById("ctdl-sidebar-button-forums").classList.add("w3-blue");
			document.getElementById("ctdl-main").innerHTML = "<div id=\"ctdl-mrp\" class=\"ctdl-msg-reading-pane\"></div>";
			forum_readmessages("ctdl-mrp", gt_msg, lt_msg);
			break;
		default:
			document.getElementById("ctdl-main").innerHTML =
				"<center>The view for " + current_room + " is " + current_view + " but there is no renderer.</center>";
			break;
	}

}


// This gets called when the user clicks the "enter message" or "post message" or "add item" button etc.
function entmsg_dispatcher() {
	switch(current_view) {
		case views.VIEW_BBS:
			forum_entmsg();
			break;
		default:
			alert("no handler");
			break;
	}
}
