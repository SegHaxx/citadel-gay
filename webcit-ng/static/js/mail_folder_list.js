// Display the mail folder list
//
// Copyright (c) 2016-2022 by the citadel.org team
//
// This program is open source software.  Use, duplication, or
// disclosure are subject to the GNU General Public License v3.


// Display the mail folder list in the specified div
function display_mail_folder_list(target_div) {

	display_mail_folder_list_async = async(target_div) => {
		let rendered_list = "hi from display_mail_folder_list_async()";

		// load the room list from the Citadel Server
		response = await fetch("/ctdl/r/", { method: "GET" } );
		if (response.ok) {
			roomlist = await response.json();
			rendered_list = render_mail_folder_list(roomlist);
		}
		else {
			rendered_list = "&#10060; " + response.status;
		}

		document.getElementById(target_div).innerHTML = rendered_list;
	}

	document.getElementById(target_div).innerHTML = "<img src=\"/ctdl/s/images/throbber.gif\" />";	// show throbber
	document.getElementById(target_div).style.display = "block";
	display_mail_folder_list_async(target_div);
}


// Given a JSON object containing the output of the `/ctdl/r` API call, return a rendered mail folder list.
function render_mail_folder_list(roomlist_json) {
	let rendered_list = "";

	for (let i=0; i<roomlist_json.length; ++i) {
		rendered_list += roomlist_json[i].name + "<br>";
	}

	return rendered_list;
}
