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
	document.body.style.cursor = "wait";
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

			+ "<span class=\"ctdl-msg-button\">"				// Reply
			+ "<a href=\"javascript:open_reply_box('"+prefix+"',"+msgnum+",false,'"+msg.wefw+"','"+msg.msgn+"');\">"
			+ "<i class=\"fa fa-reply\"></i> " 
			+ _("Reply")
			+ "</a></span>"

			+ "<span class=\"ctdl-msg-button\">"				// ReplyQuoted
			+ "<a href=\"javascript:open_reply_box('"+prefix+"',"+msgnum+",true,'"+msg.wefw+"','"+msg.msgn+"');\">"
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
			document.body.style.cursor = "default";
		}
	}
	fetch_message();
}


// Compose a references string using existing references plus the message being replied to
function compose_references(references, msgid) {
	if (references.includes("@")) {
		refs = references + "|";
	}
	else {
		refs = "";
	}
	refs += msgid;

	console.log("initial len: " + refs.length);
	// If the resulting string is too big, we can trim it here
	while (refs.length > 900) {
		r = refs.split("|");
		r.splice(1,1);		// remove the second element so we keep the root
		refs = r.join("|");
		console.log("split len: " + refs.length);
	}


	return refs;
}


// Open a reply box directly below a specific message
function open_reply_box(prefix, msgnum, is_quoted, references, msgid) {
	target_div_name = prefix+msgnum;
	new_div_name = prefix + "reply_to_" + msgnum;
	document.getElementById(target_div_name).outerHTML += "<div id=\"" + new_div_name + "\">reply box put here</div>";

	// FIXME - we need to retain the message number being replied to

	replybox =
	  "<div class=\"ctdl-msg-wrapper ctdl-msg-reply\">"		// begin message wrapper
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

	+ "<span class=\"ctdl-msg-button\">"				// bold button
	+ "<a href=\"javascript:void(0)\" onclick=\"forum_format('bold')\">"
	+ "<i class=\"fa fa-bold fa-fw\"></i>" 
	+ "</a></span>"

	+ "<span class=\"ctdl-msg-button\">"				// italic button
	+ "<a href=\"javascript:void(0)\" onclick=\"forum_format('italic')\">" 
	+ "<i class=\"fa fa-italic fa-fw\"></i>" 
	+ "</a></span>"

	+ "<span class=\"ctdl-msg-button\">"				// list button
	+ "<a href=\"javascript:void(0)\" onclick=\"forum_format('insertunorderedlist')\">"
	+ "<i class=\"fa fa-list fa-fw\"></i>" 
	+ "</a></span>"

	+ "<span class=\"ctdl-msg-button\">"				// link button
	+ "<a href=\"javascript:void(0)\" onclick=\"forum_display_urlbox()\">"
	+ "<i class=\"fa fa-link fa-fw\"></i>" 
	+ "</a></span>"

	+ "</span>";							// end buttons on right side
	if (msg.subj) {
		replybox +=
	  	"<br><span id=\"ctdl-subject\" class=\"ctdl-msgsubject\">" + "FIXME subject" + "</span>";
	}
	else {								// hidden filed for empty subject
		replybox += "<span id=\"ctdl-subject\" style=\"display:none\"></span>";
	}
	replybox +=
	  "</div><br>"							// end header

	+ "<span id=\"ctdl-replyreferences\" style=\"display:none\">"	// hidden field for references
	+ compose_references(references,msgid) + "</span>"
									// begin body
	+ "<div class=\"ctdl-msg-body\" id=\"ctdl-editor-body\" style=\"padding:5px;\" contenteditable=\"true\">"
	+ "\n"								// empty initial content
	+ "</div>"							// end body

	+ "<div class=\"ctdl-msg-header\">"				// begin footer
	+ "<span class=\"ctdl-msg-header-info\">"			// begin footer info on left side
	+ "x"
	+ "</span>"							// end footer info on left side
	+ "<span class=\"ctdl-msg-header-buttons\">"			// begin buttons on right side

	+ "<span class=\"ctdl-msg-button\"><a href=\"javascript:forum_save_message('" +  new_div_name + "', '" + msgnum + "');\">"
	+ "<i class=\"fa fa-check\" style=\"color:green\"></i> "	// save button
	+ _("Post message")
	+ "</a></span>"

	+ "<span class=\"ctdl-msg-button\"><a href=\"javascript:forum_cancel_post('" +  new_div_name + "');\">"
	+ "<i class=\"fa fa-trash\" style=\"color:red\"></i> " 		// cancel button
	+ _("Cancel")
	+ "</a></span>"

	+ "</span>"							// end buttons on right side
	+ "</div><br>"							// end footer


	+ "</div>"							// end content
	+ "</div>"							// end wrapper

	+ "<div id=\"forum_url_entry_box\" class=\"w3-modal\">"		// begin URL entry modal
	+ "	<div class=\"w3-modal-content w3-animate-top w3-card-4\">"
	+ "		<header class=\"w3-container w3-blue\"> "
	+ "			<p><span>URL:</span></p>"
	+ "		</header>"
	+ "		<div class=\"w3-container w3-blue\">"
	+ "			<input id=\"forum_txtFormatUrl\" placeholder=\"http://\" style=\"width:100%\">"
	+ "		</div>"
	+ "		<footer class=\"w3-container w3-blue\">"
	+ "			<p><span class=\"ctdl-msg-button\"><a href=\"javascript:forum_close_urlbox(true);\">"
	+ "				<i class=\"fa fa-check\" style=\"color:green\"></i> "
	+ 				_("Save")
	+ 			"</a></span>"
	+ "			<span class=\"ctdl-msg-button\"><a href=\"javascript:forum_close_urlbox(false);\">"
	+ "				<i class=\"fa fa-trash\" style=\"color:red\"></i> "
	+ 				_("Cancel")
	+ 			"</a></span></p>"
	+ "		</footer>"
	+ "		</div>"
	+ "	</div>"
	+ "	<input id=\"forum_selection_start\" style=\"display:none\"></input>"	// hidden fields
	+ "	<input id=\"forum_selection_end\" style=\"display:none\"></input>"	// to store selection range
	+ "</div>"							// end URL entry modal
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


// Abort a message post (it simply destroys the div)
function forum_cancel_post(div_name) {
	document.getElementById(div_name).outerHTML = "";		// make it cease to exist
}


// Save the posted message to the server
function forum_save_message(div_name, reply_to_msgnum) {

	document.body.style.cursor = "wait";
	wefw = (document.getElementById("ctdl-replyreferences").innerHTML).replaceAll("|","!");	// references (if present)
	subj = document.getElementById("ctdl-subject").innerHTML;				// subject (if present)

	url = "/ctdl/r/" + escapeHTMLURI(current_room)
		+ "/dummy_name_for_new_message"
		+ "?wefw=" + wefw
		+ "&subj=" + subj
	boundary = randomString(20);
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
						alert("div_name=" + div_name + " , reply_to_msgnum = " + reply_to_msgnum + " , new_msg_num = " + new_msg_num);
					}
				}
				document.getElementById(div_name).outerHTML = "";		// close the editor
			}
			else {
				error_message = request.responseText;
				if (error_message.length == 0) {
					error_message = _("An error has occurred.");
				}
				alert(error_message);
			}
		}
	};
	request.send(body_text);
}


// Bold, italics, etc.
function forum_format(command, value) {
	document.execCommand(command, false, value);
}


// Make the URL entry box appear.
// When the user clicks into the URL box it will make the previous focus disappear, so we have to save it.
function forum_display_urlbox() {
	document.getElementById("forum_selection_start").value = window.getSelection().anchorOffset;
	document.getElementById("forum_selection_end").value = window.getSelection().focusOffset;
	document.getElementById("forum_url_entry_box").style.display = "block";
}


// When the URL box is closed, this gets called.  do_save is true for Save, false for Cancel.
function forum_close_urlbox(do_save) {
	if (do_save) {
            	var tag = document.getElementById("ctdl-editor-body");
		var start_replace = document.getElementById("forum_selection_start").value;	// use saved selection range
		var end_replace = document.getElementById("forum_selection_end").value;
		new_text = tag.innerHTML.substring(0, start_replace)
			+ "<a href=\"" + document.getElementById("forum_txtFormatUrl").value + "\">"
			+ tag.innerHTML.substring(start_replace, end_replace)
			+ "</a>"
			+ tag.innerHTML.substring(end_replace);
		tag.innerHTML = new_text;
	}
	document.getElementById("forum_txtFormatUrl").value = "";				// clear url box for next time
	document.getElementById("forum_url_entry_box").style.display = "none";
}
