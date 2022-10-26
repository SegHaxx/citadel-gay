// This module handles the view for "mailbox" rooms.
//
// Copyright (c) 2016-2022 by the citadel.org team
//
// This program is open source software.  Use, duplication, or
// disclosure are subject to the GNU General Public License v3.


var selected_message = 0;							// Remember the last message that was selected
var RefreshMailboxInterval;							// We store our refresh timer here


// Render a message into the mailbox view
// (We want the message number and the message itself because we need to keep the msgnum for reply purposes)
function mail_render_one(msgnum, msg, target_div, include_controls) {
	let div = "FIXME";
	try {
		outmsg =
	  	  "<div class=\"ctdl-mmsg-wrapper\">"				// begin message wrapper
		;

		if (include_controls) {						// omit controls if this is a pull quote
			outmsg +=
			  render_userpic(msg.from)				// user avatar
			+ "<div class=\"ctdl-mmsg-content\">"			// begin content
			+ "<div class=\"ctdl-msg-header\">"			// begin header
			+ "<span class=\"ctdl-msg-header-info\">"		// begin header info on left side
			+ render_msg_author(msg)
			+ "<span class=\"ctdl-msgdate\">"
			+ string_timestamp(msg.time,0)
			+ "</span>"						// end msgdate
			+ "</span>"						// end header info on left side
			+ "<span class=\"ctdl-msg-header-buttons\">"		// begin buttons on right side
		
			+ "<span class=\"ctdl-msg-button\">"			// Reply (mail is always Quoted)
			+ "<a href=\"javascript:mail_compose(true,'"+msg.wefw+"','"+msgnum+"');\">"
			+ "<i class=\"fa fa-reply\"></i> " 
			+ _("Reply")
			+ "</a></span>"
		
			+ "<span class=\"ctdl-msg-button\">"			// Reply-All (mail is always Quoted)
			+ "<a href=\"javascript:mail_compose(true,'"+msg.wefw+"','"+msgnum+"');\">"
			+ "<i class=\"fa fa-reply-all\"></i> " 
			+ _("ReplyAll")
			+ "</a></span>";
		
			if (can_delete_messages) {
				outmsg +=
			  	"<span class=\"ctdl-msg-button\">"
				+ "<a href=\"javascript:forum_delete_message('"+div+"','"+msg.msgnum+"');\">"
				+ "<i class=\"fa fa-trash\"></i> " 
				+ _("Delete")
				+ "</a></span>";
			}
		
			outmsg +=
			  "</span>";						// end buttons on right side
			if (msg.subj) {
				outmsg +=
		  		"<br><span class=\"ctdl-msgsubject\">" + msg.subj + "</span>";
			}
			outmsg +=
		  	  "</div>";						// end header
		}

		outmsg +=
		  "<div class=\"ctdl-msg-body\" id=\"" + div + "_body\">"	// begin body
		+ msg.text
		+ "</div>"							// end body
		+ "</div>"							// end content
		+ "</div>"							// end wrapper
		;
	}
	catch(err) {
		outmsg = "<div class=\"ctdl-mmsg-wrapper\">" + err.message + "</div>";
	}

	target_div.innerHTML = outmsg;
}


// display an individual message (note: this wants an actual div object, not a string containing the name of a div)
function mail_display_message(msgnum, target_div, include_controls) {
	url = "/ctdl/r/" + escapeHTMLURI(current_room) + "/" + msgnum + "/json";
	mail_fetch_msg = async() => {
		response = await fetch(url);
		msg = await(response.json());
		if (response.ok) {
			mail_render_one(msgnum, msg, target_div, include_controls);
		}
	}
	mail_fetch_msg();
}


// A message has been selected...
function select_message(msgnum) {
	// unhighlight any previously selected message
	try {
		document.getElementById("ctdl-msgsum-" + selected_message).classList.remove("ctdl-mail-selected");
	}
	catch {
	}

	// highlight the newly selected message
	document.getElementById("ctdl-msgsum-" + msgnum).classList.add("ctdl-mail-selected");
	//document.getElementById("ctdl-msgsum-" + msgnum).scrollIntoView();

	// display the message if it isn't already displayed
	if (selected_message != msgnum) {
		selected_message = msgnum;
		mail_display_message(msgnum, document.getElementById("ctdl-mailbox-reading-pane"), 1);
	}
}


// render one row in the mailbox table (this could be called from one of several places)
function mail_render_row(msg) {
	row	= "<tr "
		+ "id=\"ctdl-msgsum-" + msg["msgnum"] + "\" "
		+ "onClick=\"select_message(" + msg["msgnum"] + ");\" "
		//+ "onmouseenter=\"console.log('mouse in');\" "
		//+ "onmouseleave=\"console.log('mouse out');\""
		+ ">"
		+ "<td class=\"ctdl-mail-subject\">" + msg["subject"] + "</td>"
		+ "<td class=\"ctdl-mail-sender\">" + msg["author"] + "</td>"
		+ "<td class=\"ctdl-mail-date\">" + string_timestamp(msg["time"],1) + "</td>"
		+ "<td class=\"ctdl-mail-msgnum\">" + msg["msgnum"] + "</td>"
		+ "</tr>";
	return(row);
}


// Set up the mailbox view
function mail_display() {

	// Put the "enter new message" button into the sidebar
	document.getElementById("ctdl-newmsg-button").innerHTML = "<i class=\"fa fa-edit\"></i>" + _("Write mail");
	document.getElementById("ctdl-newmsg-button").style.display = "block";

	document.getElementById("ctdl-main").innerHTML
		= "<div id=\"ctdl-mailbox-grid-container\" class=\"ctdl-mailbox-grid-container\">"
		+ "<div id=\"ctdl-mailbox-pane\" class=\"ctdl-mailbox-pane\"></div>"
		+ "<div id=\"ctdl-mailbox-reading-pane\" class=\"ctdl-mailbox-reading-pane\"></div>"
		+ "</div>"
	;

	render_mailbox_display();
	try {							// if this was already set up, clear it so there aren't multiple
		clearInterval(RefreshMailboxInterval);
	}
	catch {
	}
	RefreshMailboxInterval = setInterval(refresh_mail_display, 10000);
}


// Refresh the mailbox, either for the first time or whenever needed
function refresh_mail_display() {

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

	// Ask the server if the room has been written to since our last look at it.
	url = "/ctdl/r/" + escapeHTMLURI(current_room) + "/stat";
	fetch_stat = async() => {
		response = await fetch(url);
		stat = await(response.json());
		if (stat.room_mtime > room_mtime) {
			room_mtime = stat.room_mtime;
			render_mailbox_display();
		}
	}
	fetch_stat();
}


// This is where the rendering of the message list in the mailbox view is performed.
function render_mailbox_display() {

	url = "/ctdl/r/" + escapeHTMLURI(current_room) + "/mailbox";
	fetch_mailbox = async() => {
		response = await fetch(url);
		msgs = await(response.json());
		if (response.ok) {

			box =	"<table class=\"ctdl-mailbox-table\" width=100%><tr>"
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


// Compose a new mail message (called by the Reply button here, or by the dispatcher in views.js)
function mail_compose(is_quoted, references, msgnum) {
	quoted_div_name = randomString();

	// Make the "Write mail" button disappear.  We're already there!
	document.getElementById("ctdl-newmsg-button").style.display = "none";

	// is_quoted	true or false depending on whether the user selected "reply quoted" (is this appropriate for mail?)
	// references	list of references, be sure to use this in a reply
	// msgid	if a reply, the msgid of the most recent message in the chain, the one to which we are replying

	// Now display the screen.
	compose_screen =
		// Hidden values that we are storing right here in the document tree for later
		  "<input id=\"ctdl_mc_is_quoted\" style=\"display:none\" value=\"" + is_quoted + "\"></input>"
		+ "<input id=\"ctdl_mc_references\" style=\"display:none\" value=\"" + references + "\"></input>"

		// Header fields, the composition window, and the button bar are arranged using a Grid layout.
		+ "<div id=\"ctdl-compose-mail\" class=\"ctdl-compose-mail\">"

		// Visible To: field, plus a box to make the CC/BCC lines appear
		+ "<div class=\"ctdl-compose-to-label\">" + _("To:") + "</div>"
		+ "<div class=\"ctdl-compose-to-line\">"
		+ "<div class=\"ctdl-compose-to-field\" id=\"ctdl-compose-to-field\" contenteditable=\"true\"></div>"
		+ "<div class=\"ctdl-cc-bcc-buttons ctdl-msg-button\" id=\"ctdl-cc-bcc-buttons\" "
		+ "onClick=\"make_cc_bcc_visible()\">"
		+ _("CC:") + "/" + _("BCC:") + "</div>"
		+ "</div>"

		// CC/BCC
		+ "<div class=\"ctdl-compose-cc-label\" id=\"ctdl-compose-cc-label\">" + _("CC:") + "</div>"
		+ "<div class=\"ctdl-compose-cc-field\" id=\"ctdl-compose-cc-field\" contenteditable=\"true\"></div>"
		+ "<div class=\"ctdl-compose-bcc-label\" id=\"ctdl-compose-bcc-label\">" + _("BCC:") + "</div>"
		+ "<div class=\"ctdl-compose-bcc-field\" id=\"ctdl-compose-bcc-field\" contenteditable=\"true\"></div>"

		// Visible subject field
		+ "<div class=\"ctdl-compose-subject-label\">" + _("Subject:") + "</div>"
		+ "<div class=\"ctdl-compose-subject-field\" id=\"ctdl-compose-subject-field\" contenteditable=\"true\"></div>"

		// Message composition box
		+ "<div class=\"ctdl-compose-message-box\" id=\"ctdl-editor-body\" contenteditable=\"true\">"
	;

	if (is_quoted) {
		compose_screen +=
			  "<br><br><blockquote><div id=\"" + quoted_div_name + "\">"
			+ "FIXME get the quoted message into here"
			+ "</div></blockquote>";
		;
	}

	compose_screen +=
		  "</div>"

		// The button bar is a Grid element, and is also a Flexbox container.
		+ "<div class=\"ctdl-compose-toolbar\">"
		+ "<span class=\"ctdl-msg-button\" onclick=\"mail_save_message()\"><i class=\"fa fa-paper-plane\" style=\"color:green\"></i> " + _("Send message") + "</span>"
		+ "<span class=\"ctdl-msg-button\">" + _("Save to Drafts") + "</span>"
		+ "<span class=\"ctdl-msg-button\">" + _("Attachments:") + " 0" + "</span>"
		+ "<span class=\"ctdl-msg-button\">" + _("Contacts") + "</span>"
		+ "<span class=\"ctdl-msg-button\" onClick=\"gotoroom(current_room)\"><i class=\"fa fa-trash\" style=\"color:red\"></i> " + _("Cancel") + "</span>"
		+ "</div>"
	;

	document.getElementById("ctdl-main").innerHTML = compose_screen;
	mail_display_message(msgnum, document.getElementById(quoted_div_name), 0);

}

function make_cc_bcc_visible() {
	document.getElementById("ctdl-cc-bcc-buttons").style.display = "none";
	document.getElementById("ctdl-compose-cc-label").style.display = "block";
	document.getElementById("ctdl-compose-cc-field").style.display = "block";
	document.getElementById("ctdl-compose-bcc-label").style.display = "block";
	document.getElementById("ctdl-compose-bcc-field").style.display = "block";
}


// Helper function for mail_save_messages() to extract form values.
// (We have to replace "|" with "!" because "|" is a field separator in the Citadel protocol)
function msm_field(element_name, separator) {
	return (document.getElementById(element_name).innerHTML).replaceAll("|",separator);
}


// Save the posted message to the server
function mail_save_message() {

	document.body.style.cursor = "wait";
	url = "/ctdl/r/" + escapeHTMLURI(current_room)
		+ "/dummy_name_for_new_mail"
		+ "?wefw="	+ msm_field("ctdl_mc_references", "!")				// references (if present)
		+ "&subj="	+ msm_field("ctdl-compose-subject-field", " ")			// subject (if present)
		+ "&mailto="	+ msm_field("ctdl-compose-to-field", ",")			// To: (required)
		+ "&mailcc="	+ msm_field("ctdl-compose-cc-field", ",")			// Cc: (if present)
		+ "&mailbcc="	+ msm_field("ctdl-compose-bcc-field", ",")			// Bcc: (if present)
	;
	boundary = randomString();
	body_text =
		"--" + boundary + "\r\n"
		+ "Content-type: text/html\r\n"
		+ "Content-transfer-encoding: quoted-printable\r\n"
		+ "\r\n"
		+ quoted_printable_encode(
			"<html><body>" + document.getElementById("ctdl-editor-body").innerHTML + "</body></html>"
		) + "\r\n"
		+ "--" + boundary + "--\r\n"
	;

	var request = new XMLHttpRequest();
	request.open("PUT", url, true);
	request.setRequestHeader("Content-type", "multipart/mixed; boundary=\"" + boundary + "\"");
	request.onreadystatechange = function() {
		if (request.readyState == 4) {
			document.body.style.cursor = "default";
			if (Math.trunc(request.status / 100) == 2) {
				headers = request.getAllResponseHeaders().split("\n");
				for (var i in headers) {
					if (headers[i].startsWith("etag: ")) {
						new_msg_num = headers[i].split(" ")[1];
					}
				}

				// After saving the message, go back to the mailbox view.
				gotoroom(current_room);

			}
			else {
				error_message = request.responseText;
				if (error_message.length == 0) {
					error_message = _("An error has occurred.");
				}
				alert(error_message);						// editor remains open
			}
		}
	};
	request.send(body_text);
}

