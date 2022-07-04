// This module handles the view for "mailbox" rooms.
//
// Copyright (c) 2016-2022 by the citadel.org team
//
// This program is open source software.  Use, duplication, or
// disclosure are subject to the GNU General Public License v3.


// Remember the last message that was selected
var selected_message = 0;
var RefreshMailboxInterval;


// A message has been selected...
function select_message(msgnum) {
	// unhighlight any previously selected message
	try {
		document.getElementById("ctdl-msgsum-" + selected_message).classList.remove("w3-blue");
	}
	catch {
	}

	// highlight the newly selected message
	selected_message = msgnum;
	document.getElementById("ctdl-msgsum-" + selected_message).classList.add("w3-blue");
	document.getElementById("ctdl-msgsum-" + selected_message).scrollIntoView();

	// display the message
	reading_pane = document.getElementById("ctdl-reading-pane").innerHTML = "message selected " + msgnum ;
}


// render one row in the mailbox table (this could be called from one of several places)
function mail_render_row(msg) {
	row	= "<tr "
		+ "id=\"ctdl-msgsum-" + msg["msgnum"] + "\" "
		+ "onClick=\"select_message(" + msg["msgnum"] + ");\" "
		//+ "onmouseenter=\"console.log('mouse in');\" "
		//+ "onmouseleave=\"console.log('mouse out');\""
		+ ">"
		+ "<td>" + msg["subject"] + "</td>"
		+ "<td>" + msg["author"] + " &lt;" + msg["addr"] + "&gt;</td>"
		+ "<td>" + convertTimestamp(msg["time"]) + "</td>"
		+ "<td>" + msg["msgnum"] + "</td>"
		+ "</tr>";
	return(row);
}


// Set up the mailbox view
function mail_display() {
	document.getElementById("ctdl-main").innerHTML = "<div id=\"ctdl-mailbox-pane\">mailbox pane</div><div id=\"ctdl-reading-pane\">reading pane</div>";
	refresh_mail_display();
	RefreshMailboxInterval = setInterval(refresh_mail_display, 10000);
}


// Display or refresh the mailbox
function refresh_mail_display() {
	console.log("refresh_mail_display()");

	// If the "ctdl-mailbox-pane" no longer exists, the user has navigated to a different part of the site,
	// so cancel the refresh.
	try {
		document.getElementById("ctdl-mailbox-pane").innerHTML;
	}
	catch {
		console.log("ending refresh_mail_display()");
		clearInterval(RefreshMailboxInterval);
		return;
	}

	// Now go to the server.
	url = "/ctdl/r/" + escapeHTMLURI(current_room) + "/mailbox";
	fetch_mailbox = async() => {
		response = await fetch(url);
		msgs = await(response.json());
		if (response.ok) {

			box =	"<table class=\"w3-table-all w3-hoverable\" width=100%>"
				+ "<tr class=\"w3-blue\">"
				+ "<th>" + _("Subject") + "</th>"
				+ "<th>" + _("Sender") + "</th>"
				+ "<th>" + _("Date") + "</th>"
				+ "<th>#</th>"
				+ "</tr>";

			for (var i=0; i<msgs.length; ++i) {
				box += mail_render_row(msgs[i]);
			}

			box +=	"</table>";
			document.getElementById("ctdl-mailbox-pane").innerHTML = box;

			if (selected_message > 0) {			// if we had a message selected, keep it selected
				select_message(selected_message);
			}
		}
	}
	fetch_mailbox();
}
