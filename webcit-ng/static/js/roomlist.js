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


function render_room_list() {

	document.getElementById("ctdl-newmsg-button").style.display = "none";		// There is no "enter" button on this screen
	document.getElementById("ctdl-main").innerHTML = "<img src=\"/ctdl/s/images/throbber.gif\" />";	// show throbber while loading

	fetch_room_list = async() => {
		floor_response = await fetch("/ctdl/f/");
		floor_list = await(floor_response.json());
		room_response = await fetch("/ctdl/r/");
		room_list = await(room_response.json());
		if (response.ok) {
			display_room_list_renderer(floor_list, room_list);
		}
		else {
			document.getElementById("ctdl-main").innerHTML = "<i>error</i>";
		}
	}
	fetch_room_list();
}


// Renderer for display_room_list()
function display_room_list_renderer(floor_list, room_list) {

	// First sort by the room order indicated by the server
	room_list = room_list.sort(function(a,b) {
		if (a.floor != b.floor) {
			return(a.floor - b.floor);
		}
		if (a.rorder != b.rorder) {
			return(a.rorder - b.rorder);
		}
		return(a.name < b.name);
	});

	// Then split the sorted list out into floors
	output = [];
	for (var f in floor_list) {
		output[floor_list[f].num] = "";
	}

	for (var i in room_list) {
		if (room_list[i].current_view == views.VIEW_BBS) {
			output[room_list[i].floor] +=
				"<div class=\"w3-button ctdl-roomlist-room\" onClick=\"javascript:gotoroom('"
				+ escapeJS(escapeHTML(room_list[i].name)) + "');\">"
				+ "<i class=\"ctdl-roomlist-roomicon w3-left "
				+ (room_list[i].hasnewmsgs ? " ctdl-roomlist-roomicon-hasnewmsgs" : "")
				+ " fas fa-comments fa-fw\"></i><span class=\"ctdl-roomlist-roomname"
				+ (room_list[i].hasnewmsgs ? " ctdl-roomlist-roomname-hasnewmsgs" : "")
				+ " w3-left\">"
				+ escapeHTML(room_list[i].name)
				+ "</span><span class=\"ctdl-roomlist-mtime w3-right\">"
				+ convertTimestamp(room_list[i].mtime)
				+ "</span></div>";
		}
	}

	final_output = "<div class=\"ctdl-roomlist-top\">";
	for (var f in floor_list) {
		final_output +=
			"<div class=\"ctdl-roomlist-floor\">"
			+ "<div class=\"ctdl-roomlist-floor-label\">" + floor_list[f].name + "</div>"
			+ "<div class=\"ctdl-roomlist-floor-rooms\">"
			+ output[floor_list[f].num]
			+ "</div></div>";
	}
	final_output += "</div>";
	document.getElementById("ctdl-main").innerHTML = final_output;
}

