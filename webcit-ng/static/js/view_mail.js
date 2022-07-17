// This module handles the view for "mailbox" rooms.
//
// Copyright (c) 2016-2022 by the citadel.org team
//
// This program is open source software.  Use, duplication, or
// disclosure are subject to the GNU General Public License v3.


var selected_message = 0;							// Remember the last message that was selected
var RefreshMailboxInterval;							// We store our refresh timer here


// Render a message into the mailbox view
function mail_render_one(msg, target_div) {
	let div = "FIXME";
	try {
		outmsg =
	  	  "<div class=\"ctdl-mmsg-wrapper\">"				// begin message wrapper
		+ "<div class=\"ctdl-avatar\" onClick=\"javascript:user_profile('" + msg.from + "');\">"
		+ "<img src=\"/ctdl/u/" + msg.from + "/userpic\" width=\"32\" "
		+ "onerror=\"this.parentNode.innerHTML='&lt;i class=&quot;fa fa-user-circle fa-2x&quot;&gt;&lt;/i&gt; '\">"
		+ "</div>"							// end avatar
		+ "<div class=\"ctdl-mmsg-content\">"				// begin content
		+ "<div class=\"ctdl-msg-header\">"				// begin header
		+ "<span class=\"ctdl-msg-header-info\">"			// begin header info on left side
		+ "<span class=\"ctdl-username\" onClick=\"javascript:user_profile('" + msg.from + "');\">"
		+ msg.from
		+ "</a></span>"							// end username
		+ "<span class=\"ctdl-msgdate\">"
		+ convertTimestamp(msg.time)
		+ "</span>"							// end msgdate
		+ "</span>"							// end header info on left side
		+ "<span class=\"ctdl-msg-header-buttons\">"			// begin buttons on right side
	
		+ "<span class=\"ctdl-msg-button\">"				// Reply
		+ "<a href=\"javascript:open_reply_box('"+div+"',false,'"+msg.wefw+"','"+msg.msgn+"');\">"
		+ "<i class=\"fa fa-reply\"></i> " 
		+ _("Reply")
		+ "</a></span>"
	
		+ "<span class=\"ctdl-msg-button\">"				// ReplyQuoted
		+ "<a href=\"javascript:open_reply_box('"+div+"',true,'"+msg.wefw+"','"+msg.msgn+"');\">"
		+ "<i class=\"fa fa-comment\"></i> " 
		+ _("ReplyQuoted")
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
		  "</span>";							// end buttons on right side
		if (msg.subj) {
			outmsg +=
	  		"<br><span class=\"ctdl-msgsubject\">" + msg.subj + "</span>";
		}
		outmsg +=
	  	  "</div>"							// end header
		+ "<div class=\"ctdl-msg-body\" id=\"" + div + "_body\">"	// begin body
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


// display an individual message
function mail_display_message(msgnum, target_div) {
	url = "/ctdl/r/" + escapeHTMLURI(current_room) + "/" + msgnum + "/json";
	mail_fetch_msg = async() => {
		response = await fetch(url);
		msg = await(response.json());
		if (response.ok) {
			mail_render_one(msg, target_div);
		}
	}
	mail_fetch_msg();
}


// A message has been selected...
function select_message(msgnum) {
	// unhighlight any previously selected message
	try {
		document.getElementById("ctdl-msgsum-" + selected_message).classList.remove("w3-blue");
	}
	catch {
	}

	// highlight the newly selected message
	document.getElementById("ctdl-msgsum-" + msgnum).classList.add("w3-blue");
	//document.getElementById("ctdl-msgsum-" + msgnum).scrollIntoView();

	// display the message if it isn't already displayed
	if (selected_message != msgnum) {
		selected_message = msgnum;
		mail_display_message(msgnum, document.getElementById("ctdl-reading-pane"));
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
		+ "<td>" + msg["subject"] + "</td>"
		+ "<td>" + msg["author"] + " &lt;" + msg["addr"] + "&gt;</td>"
		+ "<td>" + convertTimestamp(msg["time"]) + "</td>"
		+ "<td class=\"w3-right-align\">" + msg["msgnum"] + "</td>"
		+ "</tr>";
	return(row);
}


// Set up the mailbox view
function mail_display() {
	document.getElementById("ctdl-stuffbar").style.display = "block";

	document.getElementById("ctdl-stuffbar").innerHTML
		= "<div id=\"ctdl-mailbox-pane\" class=\"ctdl-mailbox-pane\"></div>"

	document.getElementById("ctdl-main").innerHTML
		= "<div id=\"ctdl-reading-pane\" class=\"ctdl-reading-pane\"></div>"
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
		document.getElementById("ctdl-stuffbar").style.display = "none";
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

			box =	"<table class=\"w3-table-all w3-hoverable\" width=100%>"
				+ "<tr class=\"ctdl-mailbox-heading w3-blue\">"
				+ "<th>" + _("Subject") + "</th>"
				+ "<th>" + _("Sender") + "</th>"
				+ "<th>" + _("Date") + "</th>"
				+ "<th class=\"w3-right-align\">#</th>"
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
