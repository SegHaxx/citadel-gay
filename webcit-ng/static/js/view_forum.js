//
// Copyright (c) 2016-2021 by the citadel.org team
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


// Forum view (flat)
//
function forum_readmessages(target_div, gt_msg, lt_msg) {
	original_text = document.getElementById(target_div).innerHTML;		// in case we need to replace it after an error
	document.getElementById(target_div).innerHTML = 
		"<div class=\"ctdl-forum-nav\"><i class=\"fas fa-spinner fa-spin\"></i>&nbsp;&nbsp;"
		+ _("Loading messages from server, please wait") + "</div>";

	if (lt_msg < 9999999999) {
		url = "/ctdl/r/" + escapeHTMLURI(current_room) + "/msgs.lt|" + lt_msg;
	}
	else {
		url = "/ctdl/r/" + escapeHTMLURI(current_room) + "/msgs.gt|" + gt_msg;
	}

	fetch_msg_list = async() => {
		response = await fetch(url);
		msgs = await(response.json());
		if (response.ok) {
			document.getElementById(target_div).innerHTML = "" ;

			// If we were given an explicit starting point, by all means start there.
			// Note that we don't have to remove them from the array because we did a 'msgs gt|xxx' command to Citadel.
			if (gt_msg > 0) {
				msgs = msgs.slice(0, messages_per_page);
			}

			// Otherwise, show us the last 20 messages
			else {
				if (msgs.length > messages_per_page) {
					msgs = msgs.slice(msgs.length - messages_per_page);
				}
				new_old_div_name = randomString(5);
				if (msgs.length < 1) {
					newlt = lt_msg;
				}
				else {
					newlt = msgs[0];
				}
				document.getElementById(target_div).innerHTML +=
					"<div id=\"" + new_old_div_name + "\">" +
					"<div class=\"ctdl-forum-nav\">" +
					"<a href=\"javascript:forum_readmessages('" + new_old_div_name + "', 0, " + newlt + ");\">" +
					"<i class=\"fa fa-arrow-circle-up\"></i>&nbsp;&nbsp;" +
					_("Older posts") + "&nbsp;&nbsp;<i class=\"fa fa-arrow-circle-up\"></a></div></div></a></div></div>" ;
			}

			// Render an empty div for each message.  We will fill them in later.
			for (var i in msgs) {
				document.getElementById(target_div).innerHTML += "<div id=\"ctdl_msg_" + msgs[i] + "\"> </div>" ;
				document.getElementById("ctdl_msg_"+msgs[i]).style.display = "none";
			}
			if (lt_msg == 9999999999) {
				new_new_div_name = randomString(5);
				if (msgs.length <= 0) {
					newgt = gt_msg;
				}
				else {
					newgt = msgs[msgs.length-1];
				}
				document.getElementById(target_div).innerHTML +=
					"<div id=\"" + new_new_div_name + "\">" +
					"<div class=\"ctdl-forum-nav\">" +
					"<a href=\"javascript:forum_readmessages('" + new_new_div_name + "', " + newgt + ", 9999999999);\">" +
					"<i class=\"fa fa-arrow-circle-down\"></i>&nbsp;&nbsp;" +
					_("Newer posts") + "&nbsp;&nbsp;<i class=\"fa fa-arrow-circle-down\"></a></div></div>" ;
			}

			// Now figure out where to scroll to after rendering.
			if (gt_msg > 0) {
				scroll_to = msgs[0];
			}
			else if (lt_msg < 9999999999) {
				scroll_to = msgs[msgs.length-1];
			}
			else if ( (logged_in) && (gt_msg == 0) && (lt_msg == 9999999999) ) {
				scroll_to = msgs[msgs.length-1];
			}
			else {
				scroll_to = msgs[0];		// FIXME this is too naive
			}

			// Render the individual messages in the divs
			forum_render_messages(msgs, "ctdl_msg_", scroll_to)
		}
		else {
			// if xhr fails, this will make the link reappear so the user can try again
			document.getElementById(target_div).innerHTML = original_text;
		}
	}
	fetch_msg_list();
}


// Render a range of messages, with the div prefix specified
//
function forum_render_messages(msgs, prefix, scroll_to) {
	for (i=0; i<msgs.length; ++i) {
		forum_render_one(prefix, msgs[i], scroll_to);
	}
}


// We have to put each XHR for forum_render_messages() into its own stack frame, otherwise it jumbles them together.  I don't know why.
function forum_render_one(prefix, msgnum, scroll_to) {
	fetch_message = async() => {
		response = await fetch("/ctdl/r/" + escapeHTMLURI(current_room) + "/" + msgs[i] + "/json");
		msg = await response.json();
		if (response.ok) {
			outmsg =
			  "<div class=\"ctdl-msg-wrapper\">"				// begin message wrapper
			+ "<div class=\"ctdl-avatar\">"					// begin avatar
			+ "<img src=\"/ctdl/u/" + msg.from + "/userpic\" width=\"32\" "
			+ "onerror=\"this.parentNode.innerHTML='&lt;i class=&quot;fa fa-user-circle fa-2x&quot;&gt;&lt;/i&gt; '\">"
			+ "</div>"							// end avatar
			+ "<div class=\"ctdl-msg-content\">"				// begin content
			+ "<div class=\"ctdl-msg-header\">"				// begin header
			+ "<span class=\"ctdl-msg-header-info\">"			// begin header info on left side
			+ "<span class=\"ctdl-username\"><a href=\"#\">"		// FIXME link to user profile
			+ msg.from
			+ "</a></span>"							// end username
			+ "<span class=\"ctdl-msgdate\">"
			+ msg.time
			+ "</span>"							// end msgdate
			+ "</span>"							// end header info on left side
			+ "<span class=\"ctdl-msg-header-buttons\">"			// begin buttons on right side

			+ "<span class=\"ctdl-msg-button\">"				// Reply button FIXME make this work
			+ "<a href=\"javascript:open_reply_box('"+prefix+"',"+msgnum+",false);\">"
			+ "<i class=\"fa fa-reply\"></i> " 
			+ _("Reply")
			+ "</a></span>"

			+ "<span class=\"ctdl-msg-button\">"				// ReplyQuoted , only show in forums FIXME
			+ "<a href=\"javascript:open_reply_box('"+prefix+"',"+msgnum+",true);\">"
			+ "<i class=\"fa fa-comment\"></i> " 
			+ _("ReplyQuoted")
			+ "</a></span>"

			+ "<span class=\"ctdl-msg-button\"><a href=\"#\">"		// Delete , show only with permission FIXME
			+ "<i class=\"fa fa-trash\"></i> " 
			+ _("Delete")
			+ "</a></span>"

			+ "</span>";							// end buttons on right side
			if (msg.subj) {
				outmsg +=
			  	"<br><span class=\"ctdl-msgsubject\">" + msg.subj + "</span>";
			}
			outmsg +=
			  "</div><br>"							// end header
			+ "<div class=\"ctdl-msg-body\">"				// begin body
			+ msg.text
			+ "</div>"							// end body
			+ "</div>"							// end content
			+ "</div>"							// end wrapper
			;
			document.getElementById(prefix+msgnum).innerHTML = outmsg;
		}
		else {
			document.getElementById(prefix+msgnum).innerHTML = "ERROR";
		}
		document.getElementById(prefix+msgnum).style.display  = "inline";
		if (msgnum == scroll_to) {
			document.getElementById(prefix+msgnum).scrollIntoView({behavior: "smooth", block: "start", inline: "nearest"});
		}
	}
	fetch_message();
}


// Open a reply box directly below a specific message
function open_reply_box(prefix, msgnum, is_quoted) {
	target_div_name = prefix+msgnum;
	new_div_name = prefix + "_reply_to_" + msgnum;
	document.getElementById(target_div_name).outerHTML += "<div id=\"" + new_div_name + "\">reply box put here</div>";

	replybox =
	  "<div class=\"ctdl-msg-wrapper\">"				// begin message wrapper
	+ "<div class=\"ctdl-avatar\">"					// begin avatar
	+ "<img src=\"/ctdl/u/" + "FIXME my name" + "/userpic\" width=\"32\" "
	+ "onerror=\"this.parentNode.innerHTML='&lt;i class=&quot;fa fa-user-circle fa-2x&quot;&gt;&lt;/i&gt; '\">"
	+ "</div>"							// end avatar
	+ "<div class=\"ctdl-msg-content\">"				// begin content
	+ "<div class=\"ctdl-msg-header\">"				// begin header
	+ "<span class=\"ctdl-msg-header-info\">"			// begin header info on left side
	+ "<span class=\"ctdl-username\"><a href=\"#\">"		// FIXME link to user profile
	+ "FIXME my name"
	+ "</a></span>"							// end username
	+ "<span class=\"ctdl-msgdate\">"
	+ "FIXME now time"
	+ "</span>"							// end msgdate
	+ "</span>"							// end header info on left side
	+ "<span class=\"ctdl-msg-header-buttons\">"			// begin buttons on right side
	+ "x"
	+ "</span>";							// end buttons on right side
	if (msg.subj) {
		replybox +=
	  	"<br><span class=\"ctdl-msgsubject\">" + "FIXME subject" + "</span>";
	}
	replybox +=
	  "</div><br>"							// end header


	+ "<div class=\"ctdl-msg-body\" id=\"ctdl-editor-body\" style=\"height:30vh; padding:5px;\" contenteditable=\"true\">"	// begin body
	+ "This is where the reply text will go."
	+ "</div>"							// end body


	+ "<div class=\"ctdl-msg-header\">"				// begin footer
	+ "<span class=\"ctdl-msg-header-info\">"			// begin footer info on left side
	+ "x"
	+ "</span>"							// end footer info on left side
	+ "<span class=\"ctdl-msg-header-buttons\">"			// begin buttons on right side

	+ "<span class=\"ctdl-msg-button\"><a href=\"#\">"		// FIXME save and cancel buttons
	+ "<i class=\"fa fa-trash\"></i> " 
	+ _("Post message")
	+ "</a></span>"

	+ "<span class=\"ctdl-msg-button\"><a href=\"#\">"		// FIXME save and cancel buttons
	+ "<i class=\"fa fa-trash\"></i> " 
	+ _("Cancel")
	+ "</a></span>"

	+ "</span>";							// end buttons on right side
	if (msg.subj) {
		replybox +=
	  	"<br><span class=\"ctdl-msgsubject\">" + "FIXME subject" + "</span>";
	}
	replybox +=
	  "</div><br>"							// end footer


	+ "</div>"							// end content
	+ "</div>"							// end wrapper
	;

	document.getElementById(new_div_name).innerHTML = replybox;
	document.getElementById(new_div_name).scrollIntoView({behavior: "smooth", block: "end", inline: "nearest"});

	// These actions must happen *after* the initial render loop completes.
	setTimeout(function() {
            	var tag = document.getElementById("ctdl-editor-body");
            	tag.focus();						// sets the focus
		window.getSelection().collapse(tag.firstChild, 0);	// positions the cursor
	}, 0);
}
