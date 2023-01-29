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

	// Sort first by floor then by room order
	roomlist_json.sort(function(a, b) {
		if (a.floor > b.floor) return 1;
		if (a.floor < b.floor) return -1;
		if (a.name == "Mail") return -1;
		if (b.name == "Mail") return 1;
		if (a.rorder > b.rorder) return 1;
		if (a.rorder < b.rorder) return -1;
		return 0;
	});

	// Turn it into displayable markup
	let rendered_list = "";
	rendered_list += "<ul class=\"ctdl_mail_folders\">\n";
	for (let i=0; i<roomlist_json.length; ++i) {
		if (roomlist_json[i].current_view == views.VIEW_MAILBOX) {
			rendered_list += "<li onClick=\"gotoroom('" + roomlist_json[i].name + "');\">";
			rendered_list += ((roomlist_json[i].name == "Mail") ? _("INBOX") : escapeHTML(roomlist_json[i].name));
			rendered_list += "</li>\n";
		}
	}
	rendered_list += "</ul>";
	console.log(rendered_list);

	return rendered_list;
}
