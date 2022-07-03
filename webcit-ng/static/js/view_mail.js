// Copyright (c) 2016-2022 by the citadel.org team
//
// This program is open source software.  Use, duplication, or
// disclosure are subject to the GNU General Public License v3.


// A message has been selected...
function select_message(msgnum) {
	reading_pane = document.getElementById("ctdl-reading-pane").innerHTML = "message selected " + msgnum ;
}


// Set up the mailbox view
function mail_display() {
	target_div = document.getElementById("ctdl-main");
	target_div.innerHTML = "<div id=\"ctdl-mailbox-pane\">mailbox pane</div><div id=\"ctdl-reading-pane\">reading pane</div>";
	mailbox_pane = document.getElementById("ctdl-mailbox-pane");
	reading_pane = document.getElementById("ctdl-reading-pane");

	activate_loading_modal();
	url = "/ctdl/r/" + escapeHTMLURI(current_room) + "/mailbox";
	fetch_mailbox = async() => {
		response = await fetch(url);
		msgs = await(response.json());
		if (response.ok) {

			box =	"<table class=\"w3-table-all\" width=100%>"
				+ "<tr class=\"w3-blue\">"
				+ "<th>" + _("Subject") + "</th>"
				+ "<th>" + _("Sender") + "</th>"
				+ "<th>" + _("Date") + "</th>"
				+ "<th>#</th>"
				+ "</tr>";

			for (var i=0; i<msgs.length; ++i) {
				box +=	"<tr "
					+ "id=\"ctdl-msgsum-" + msgs[i]["msgnum"] + "\""
					+ "onClick=\"select_message(" + msgs[i]["msgnum"] + ")\""
					+ ">"
					+ "<td>" + msgs[i]["subject"] + "</td>"
					+ "<td>" + msgs[i]["author"] + " &lt;" + msgs[i]["addr"] + "&gt;</td>"
					+ "<td>" + convertTimestamp(msgs[i]["time"]) + "</td>"
					+ "<td>" + msgs[i]["msgnum"] + "</td>"
					+ "</tr>";
			}

			box +=	"</table>";
			mailbox_pane.innerHTML = box;
		}
	}
	fetch_mailbox();
	deactivate_loading_modal();
}
