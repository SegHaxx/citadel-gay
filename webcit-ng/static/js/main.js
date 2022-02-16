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


// This is called at the very beginning of the main page load.
ctdl_startup = async() => {
	response = await fetch("/ctdl/c/info");

	if (response.ok) {
		serv_info = await(response.json());
		if (serv_info.serv_rev_level < 905) {
			alert(_("Citadel server is too old, some functions may not work"));
		}

		update_banner();

		// What do we do upon landing?

		if ( (serv_info.serv_supports_guest) || (logged_in) ) {			// If the Lobby is visible,
			gotonext(1);							// go there.
		}
		else {									// Otherwise,
			display_login_screen("");					// display the login modal.
		}
	}
	else {
		document.getElementById("ctdl-main").innerHTML =
			"<div class=\"w3-panel w3-red\"><p>"
			+ _("This program was unable to connect or stay connected to the Citadel server.  Please report this problem to your system administrator.")
			+ "</div>";
	}
}


// Display a room list in the main div.

// Update the "banner" div with all relevant info.
function update_banner() {
	detect_logged_in();
	if (current_room) {
		document.getElementById("ctdl_banner_title").innerHTML = current_room;
		document.title = current_room;



	}
	else {
		document.getElementById("ctdl_banner_title").innerHTML = serv_info.serv_humannode;
	}
	document.getElementById("current_user").innerHTML = current_user ;
	if (logged_in) {
		document.getElementById("lilo").innerHTML = "<a href=\"/ctdl/a/logout\"><i class=\"fa-regular fa-right-from-bracket\"></i>" + _("Log off") + "</a>" ;
	}
	else {
		document.getElementById("lilo").innerHTML = "<a href=\"javascript:display_login_screen('')\">" + _("Log in") + "</a>" ;
	}
}


// goto room
function gotoroom(roomname) {

	fetch_room = async() => {
		response = await fetch("/ctdl/r/" + escapeHTMLURI(roomname) + "/");
		if (response.ok) {
			data = await(response.json());
			current_room = data.name;
			new_messages = data.new_messages;
			total_messages = data.total_messages;
			current_view = data.current_view;
			default_view = data.default_view;
			last_seen = data.last_seen;
			update_banner();
			render_room_view(0, 9999999999);
		}
	}
	fetch_room();
}


// Goto next room with unread messages
// which_oper is 0=ungoto, 1=skip, 2=goto
function gotonext(which_oper) {
	if (which_oper == 2) {					// Goto needs to mark messages as seen

		set_last_read_pointer = async() => {
			response = await fetch("/ctdl/r/" + escapeHTMLURI(current_room) + "/slrp?last=" + last_seen);
		}
		set_last_read_pointer();
	}

	if ((which_oper == 1) || (which_oper == 2)) {		// Skip or Goto both take us to the "next" room
		if (march_list.length == 0) {
			load_new_march_list(which_oper);	// we will recurse back here
		}
		else {
			next_room = march_list[0].name;
			march_list.splice(0, 1);
			gotoroom(next_room);
		}
	}

	// FIXME implement mode 0 (ungoto)

}


// Called by gotonext() when the march list is empty.
function load_new_march_list(which_oper) {
	fetchm = async() => {
		response = await fetch("/ctdl/r/");
		march_list = await(response.json());
		if (response.ok) {
			march_list = march_list.filter(function(room) {
				return room.hasnewmsgs;
			});
			march_list = march_list.sort(function(a,b) {
				if (a.floor != b.floor) {
					return(a.floor - b.floor);
				}
				if (a.rorder != b.rorder) {
					return(a.rorder - b.rorder);
				}
				return(a.name < b.name);
			});

			// If there are still no rooms with new messages, go to the Lobby.
			if (march_list.length == 0) {
				march_list.push({name:"_BASEROOM_",known:true,hasnewmsgs:true,floor:0});
			}

			gotonext(which_oper);
		}
	}
	fetchm();
}


// Activate the "Loading..." modal
function activate_loading_modal() {
	document.getElementById("ctdl_big_modal").innerHTML =
		  "<div class=\"w3-modal-content\">"
		+ "  <div class=\"w3-container\">"

		+ "<i class=\"fas fa-spinner fa-spin\"></i>&nbsp;&nbsp;"
		+ _("Loading messages from server, please wait")

		+ "  </div>"
		+ "</div>";
	document.getElementById("ctdl_big_modal").style.display = "block";
}


// Disappear the "Loading..." modal
function deactivate_loading_modal() {
	document.getElementById("ctdl_big_modal").style.display = "none";
}
