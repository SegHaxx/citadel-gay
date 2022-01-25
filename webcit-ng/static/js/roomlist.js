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
	room_list = room_list.sort(function(a,b) {
		if (a.floor != b.floor) {
			return(a.floor - b.floor);
		}
		if (a.rorder != b.rorder) {
			return(a.rorder - b.rorder);
		}
		return(a.name < b.name);
	});

	output = [];

	for (var f in floor_list) {
		output[floor_list[f].num] = "<div><b>Room list for " + floor_list[f].name + "</b><br>";
	}

	for (var i in room_list) {
		output[room_list[i].floor] += (room_list[i].hasnewmsgs ? "<b>" : "");
		output[room_list[i].floor] += "<a href=\"javascript:gotoroom('" + escapeJS(escapeHTML(room_list[i].name)) + "');\">";
		output[room_list[i].floor] += escapeHTML(room_list[i].name);
		output[room_list[i].floor] += (room_list[i].hasnewmsgs ? "</b>" : "");
		output[room_list[i].floor] += "</a>";
		if (room_list[i].current_view == views.VIEW_BBS) {
			output[room_list[i].floor] += "(FORUM)";
		}
		output[room_list[i].floor] += "<br>";
	}

	final_output = "";
	for (var f in floor_list) {
		final_output += output[floor_list[f].num];
		final_output += "<br>";
	}
	document.getElementById("ctdl-main").innerHTML = final_output ;
}

