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
		response = await fetch("/ctdl/r/");
		room_list = await(response.json());
		if (response.ok) {
			display_room_list_renderer(room_list);
		}
		else {
			document.getElementById("ctdl-main").innerHTML = "<i>error</i>";
		}
	}
	fetch_room_list();
}


// Renderer for display_room_list()
function display_room_list_renderer(data) {
	data = data.sort(function(a,b) {
		if (a.floor != b.floor) {
			return(a.floor - b.floor);
		}
		if (a.rorder != b.rorder) {
			return(a.rorder - b.rorder);
		}
		return(a.name < b.name);
	});

	new_roomlist_text = "<ul>" ;

	for (var i in data) {
		if (i > 0) {
			if (data[i].floor != data[i-1].floor) {
				new_roomlist_text = new_roomlist_text + "<li class=\"divider\"></li>" ;
			}
		}
		new_roomlist_text = new_roomlist_text +
			  "<li>"
			+ (data[i].hasnewmsgs ? "<b>" : "")
			+ "<a href=\"javascript:gotoroom('" + escapeJS(escapeHTML(data[i].name)) + "');\">"
			+ escapeHTML(data[i].name)
			+ (data[i].hasnewmsgs ? "</b>" : "")
			+ "</a>"
		if (data[i].current_view == views.VIEW_BBS) {
			new_roomlist_text = new_roomlist_text + "(FORUM)";
		}
		new_roomlist_text = new_roomlist_text +
			  "</li>"
		;
	}
	new_roomlist_text = new_roomlist_text + "</ul>";
	document.getElementById("ctdl-main").innerHTML = new_roomlist_text ;
}

