// This module handles the view for "forum" (message board) rooms.
//
// Copyright (c) 2016-2022 by the citadel.org team
//
// This program is open source software.  Use, duplication, or
// disclosure are subject to the GNU General Public License v3.


// Forum view (flat)
function forum_readmessages(target_div_name, gt_msg, lt_msg) {
	target_div = document.getElementById(target_div_name);
	original_text = target_div.innerHTML;					// in case we need to replace it after an error
	target_div.innerHTML = ""

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
			target_div.innerHTML = "" ;

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
				new_old_div_name = randomString();
				if (msgs.length < 1) {
					newlt = lt_msg;
				}
				else {
					newlt = msgs[0];
				}
				target_div.innerHTML +=
					"<div id=\"" + new_old_div_name + "\">" +
					"<div class=\"ctdl-forum-nav\">" +
					"<a href=\"javascript:forum_readmessages('" + new_old_div_name + "', 0, " + newlt + ");\">" +
					"<i class=\"fa fa-arrow-circle-up\"></i>&nbsp;&nbsp;" +
					_("Older posts") + "&nbsp;&nbsp;<i class=\"fa fa-arrow-circle-up\"></a></div></div></a></div></div>" ;
			}

			// The messages will go here.
			let msgs_div_name = randomString();
			target_div.innerHTML += "<div id=\"" + msgs_div_name + "\"> </div>" ;

			if (lt_msg == 9999999999) {
				new_new_div_name = randomString();
				if (msgs.length <= 0) {
					newgt = gt_msg;
				}
				else {
					newgt = msgs[msgs.length-1];
				}
				target_div.innerHTML +=
					"<div id=\"" + new_new_div_name + "\">" +
					"<div id=\"ctdl-newmsg-here\"></div>" +
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
			forum_render_messages(msgs, msgs_div_name, scroll_to)
		}
		else {
			// if xhr fails, this will make the link reappear so the user can try again
			target_div.innerHTML = original_text;
		}
	}
	fetch_msg_list();

	// make the nav buttons appear (post a new message, skip this room, goto next room)

	document.getElementById("ctdl-newmsg-button").innerHTML = "<i class=\"fa fa-edit\"></i>" + _("Post message");
	document.getElementById("ctdl-newmsg-button").style.display = "block";

	document.getElementById("ctdl-skip-button").innerHTML = "<i class=\"fa fa-arrow-alt-circle-right\"></i>" + _("Skip this room");
	document.getElementById("ctdl-skip-button").style.display = "block";

	document.getElementById("ctdl-goto-button").innerHTML = "<i class=\"fa fa-arrow-circle-right\"></i>" + _("Goto next room");
	document.getElementById("ctdl-goto-button").style.display = "block";
}


// Render a range of messages into the specified target div
function forum_render_messages(message_numbers, msgs_div_name, scroll_to) {

	// Build an array of Promises and then wait for them all to resolve.
	let num_msgs = message_numbers.length;
	let msg_promises = Array.apply(null, Array(num_msgs));
	for (i=0; i<num_msgs; ++i) {
		msg_promises[i] = fetch("/ctdl/r/" + escapeHTMLURI(current_room) + "/" + message_numbers[i] + "/json")
			.then(response => response.json())
			.catch((error) => {
				response => null;
				console.error('Error: ', error);
			})
		;
	}

	// Here is the async function that waits for all the messages to be loaded, and then renders them.
	fetch_msg_list = async() => {
		document.body.style.cursor = "wait";
		activate_loading_modal();
		await Promise.all(msg_promises);
		deactivate_loading_modal();
		document.body.style.cursor = "default";
		
		// At this point all of the Promises are resolved and we can render.
		// Note: "let" keeps "i" in scope even through the .then scope
		let scroll_to_div = null;
		for (let i=0; i<num_msgs; ++i) {
			msg_promises[i].then((one_message) => {
				let new_msg_div = forum_render_one(one_message, null);
				document.getElementById(msgs_div_name).append(new_msg_div);
				if (message_numbers[i] == scroll_to) {
					scroll_to_div = new_msg_div;
				}
				if (i == num_msgs - 1) {
					scroll_to_div.scrollIntoView({behavior: "smooth", block: "start", inline: "nearest"});
				}
			});
		}
	}

	fetch_msg_list();

	// Make a note of the highest message number we saw, so we can mark it when we "Goto next room"
	// (Compared to the text client, this is actually more like <A>bandon than <G>oto)
	if ((num_msgs > 0) && (message_numbers[num_msgs-1] > last_seen)) {
		last_seen = message_numbers[num_msgs-1];
	}
}


// Render a message.  Returns a div object.
function forum_render_one(msg, existing_div) {
	let div = null;
	if (existing_div != null) {						// If an existing div was supplied, render into it
		div = existing_div;
	}
	else {									// Otherwise, create a new one
		div = document.createElement("div");
	}

	mdiv = randomString();							// Give the div a new name
	div.id = mdiv;

	try {
		outmsg =
	  	  "<div class=\"ctdl-fmsg-wrapper\">"				// begin message wrapper
		+ "<div class=\"ctdl-avatar\" onClick=\"javascript:user_profile('" + msg.from + "');\">"
		+ "<img src=\"/ctdl/u/" + msg.from + "/userpic\" width=\"32\" "
		+ "onerror=\"this.parentNode.innerHTML='&lt;i class=&quot;fa fa-user-circle fa-2x&quot;&gt;&lt;/i&gt; '\">"
		+ "</div>"							// end avatar
		+ "<div class=\"ctdl-msg-content\">"				// begin content
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
		+ "<a href=\"javascript:open_reply_box('"+mdiv+"',false,'"+msg.wefw+"','"+msg.msgn+"');\">"
		+ "<i class=\"fa fa-reply\"></i> " 
		+ _("Reply")
		+ "</a></span>"
	
		+ "<span class=\"ctdl-msg-button\">"				// ReplyQuoted
		+ "<a href=\"javascript:open_reply_box('"+mdiv+"',true,'"+msg.wefw+"','"+msg.msgn+"');\">"
		+ "<i class=\"fa fa-comment\"></i> " 
		+ _("ReplyQuoted")
		+ "</a></span>";
	
		if (can_delete_messages) {
			outmsg +=
		  	"<span class=\"ctdl-msg-button\">"
			+ "<a href=\"javascript:forum_delete_message('"+mdiv+"','"+msg.msgnum+"');\">"
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
	  	  "</div><br>"							// end header
		+ "<div class=\"ctdl-msg-body\" id=\"" + mdiv + "_body\">"	// begin body
		+ msg.text
		+ "</div>"							// end body
		+ "</div>"							// end content
		+ "</div>"							// end wrapper
		;
	}
	catch(err) {
		outmsg = "<div class=\"ctdl-fmsg-wrapper\">" + err.message + "</div>";
	}

	div.innerHTML = outmsg;
	return(div);
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

	// If the resulting string is too big, we can trim it here
	while (refs.length > 900) {
		r = refs.split("|");
		r.splice(1,1);		// remove the second element so we keep the root
		refs = r.join("|");
	}
	return refs;
}

// Delete a message.
// We don't bother checking for permission because the button only appears if we have permission,
// and even if someone hacks the client, the server will deny any unauthorized deletes.
function forum_delete_message(message_div, message_number) {
	if (confirm(_("Delete this message?")) == true) {
		async_forum_delete_message = async() => {
			response = await fetch(
				"/ctdl/r/" + escapeHTMLURI(current_room) + "/" + message_number,
				{ method: "DELETE" }
			);
			if (response.ok) {		// If the server accepted the delete, blank out the message div.
				document.getElementById(message_div).outerHTML = "";
			}
		}
		async_forum_delete_message();
	}
}


// Open a reply box directly below a specific message
function open_reply_box(parent_div, is_quoted, references, msgid) {
	let new_div = document.createElement("div");
	let new_div_name = randomString();
	new_div.id = new_div_name;

	document.getElementById(parent_div).append(new_div);

	replybox =
	  "<div class=\"ctdl-fmsg-wrapper ctdl-msg-reply\">"		// begin message wrapper
	+ "<div class=\"ctdl-avatar\">"					// begin avatar
	+ "<img src=\"/ctdl/u/" + current_user + "/userpic\" width=\"32\" "
	+ "onerror=\"this.parentNode.innerHTML='&lt;i class=&quot;fa fa-user-circle fa-2x&quot;&gt;&lt;/i&gt; '\">"
	+ "</div>"							// end avatar
	+ "<div class=\"ctdl-msg-content\">"				// begin content
	+ "<div class=\"ctdl-msg-header\">"				// begin header
	+ "<span class=\"ctdl-msg-header-info\">"			// begin header info on left side
	+ "<span class=\"ctdl-username\">"
	+ current_user							// user = me !
	+ "</span>"
	+ "<span class=\"ctdl-msgdate\">"
	+ convertTimestamp(Date.now() / 1000)				// the current date/time (temporary for display)
	+ "</span>"
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
	//if (msg.subj) {
		//replybox +=
	  	//"<br><span id=\"ctdl-subject\" class=\"ctdl-msgsubject\">" + "FIXME subject" + "</span>";
	//}
	//else {								// hidden filed for empty subject
		replybox += "<span id=\"ctdl-subject\" style=\"display:none\"></span>";
	//}
	replybox +=
	  "</div><br>"							// end header

	+ "<span id=\"ctdl-replyreferences\" style=\"display:none\">"	// hidden field for references
	+ compose_references(references,msgid) + "</span>"
									// begin body
	+ "<div class=\"ctdl-msg-body\" id=\"ctdl-editor-body\" style=\"padding:5px;\" contenteditable=\"true\">"
	+ "\n";								// empty initial content

	if (is_quoted) {
		replybox += "<br><blockquote>"
			+ document.getElementById(parent_div+"_body").innerHTML
			+ "</blockquote>";
	}

	replybox +=
	  "</div>"							// end body

	+ "<div class=\"ctdl-msg-header\">"				// begin footer
	+ "<span class=\"ctdl-msg-header-info\">"			// begin footer info on left side
	+ "&nbsp;"							// (nothing here for now)
	+ "</span>"							// end footer info on left side
	+ "<span class=\"ctdl-msg-header-buttons\">"			// begin buttons on right side

	+ "<span class=\"ctdl-msg-button\"><a href=\"javascript:forum_save_message('" + new_div_name + "');\">"
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
function forum_save_message(editor_div_name) {

	document.body.style.cursor = "wait";
	wefw = (document.getElementById("ctdl-replyreferences").innerHTML).replaceAll("|","!");	// references (if present)
	subj = document.getElementById("ctdl-subject").innerHTML;				// subject (if present)

	url = "/ctdl/r/" + escapeHTMLURI(current_room)
		+ "/dummy_name_for_new_message"
		+ "?wefw=" + wefw
		+ "&subj=" + subj
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

				// After saving the message, load it back from the server and replace the editor div with it.
				replace_editor_with_final_message = async() => {
					response = await fetch("/ctdl/r/" + escapeHTMLURI(current_room) + "/" + new_msg_num + "/json");
					if (response.ok) {
						newly_posted_message = await(response.json());
						forum_render_one(newly_posted_message, document.getElementById(editor_div_name));
					}
				}
				replace_editor_with_final_message();

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


// User has clicked the "Post message" button.  This is roughly the same as "reply" except there is no parent message.
function forum_entmsg() {
	open_reply_box("ctdl-newmsg-here", false, "", "");
}
