// Copyright (c) 2016-2022 by the citadel.org team
//
// This program is open source software.  Use, duplication, or
// disclosure are subject to the GNU General Public License v3.


function render_room_list() {

	document.getElementById("ctdl-newmsg-button").style.display = "none";		// There is no "enter" button on this screen
	document.getElementById("ctdl-main").innerHTML = "<div class=\"ctdl-middle\"><img src=\"/ctdl/s/images/throbber.gif\" /></div>";	// show throbber while loading
	clear_sidebar_selection();
	document.getElementById("ctdl-sidebar-button-forums").classList.add("ctdl-sidebar-button-selected");

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

				"<div class=\"ctdl-roomlist-room\" onClick=\"javascript:gotoroom('"	// container
				+ escapeJS(escapeHTML(room_list[i].name)) + "');\">"

				+ "<div><i class=\"ctdl-roomlist-roomicon "				// room icon
				+ (room_list[i].hasnewmsgs ? "w3-blue" : "w3-gray")
				+ " fas fa-comments fa-fw\"></i></div>"

				+ "<div class=\"ctdl-roomlist-roomname"					// room name
				+ (room_list[i].hasnewmsgs ? " ctdl-roomlist-roomname-hasnewmsgs" : "")
				+ "\">"
				+ escapeHTML(room_list[i].name)
				+ "</div>"

				+ "<div class=\"ctdl-roomlist-mtime\">"					// date/time of last post
				+ string_timestamp(room_list[i].mtime)
				+ "</div>"

				+ "</div>"								// end container
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

