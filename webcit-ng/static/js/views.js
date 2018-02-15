//
// Copyright (c) 2016-2018 by the citadel.org team
//
// This program is open source software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License version 3.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.


// List of defined views shamelessly swiped from libcitadel headers
//
var views = {
	VIEW_BBS		: 0,	/* Bulletin board view */
	VIEW_MAILBOX		: 1,	/* Mailbox summary */
	VIEW_ADDRESSBOOK	: 2,	/* Address book view */
	VIEW_CALENDAR		: 3,	/* Calendar view */
	VIEW_TASKS		: 4,	/* Tasks view */
	VIEW_NOTES		: 5,	/* Notes view */
	VIEW_WIKI		: 6,	/* Wiki view */
	VIEW_CALBRIEF		: 7,	/* Brief Calendar view */
	VIEW_JOURNAL		: 8,	/* Journal view */
	VIEW_DRAFTS		: 9,	/* Drafts view */
	VIEW_BLOG		: 10,	/* Blog view */
	VIEW_QUEUE		: 11,   /* SMTP QUEUE rooms */
	VIEW_WIKIMD		: 12,	/* Markdown Wiki view */
};


// This function is the dispatcher that determines the correct view for a room,
// and calls the correct renderer.  Greater/Less than bounds are accepted.
//
function render_room_view(gt_msg, lt_msg)
{
	switch(current_view)
	{
		case views.VIEW_MAILBOX:						// FIXME view mail rooms as forums for now
		case views.VIEW_BBS:
			forum_readmessages("ctdl-main", gt_msg, lt_msg);
			break;
		default:
			document.getElementById("ctdl-main").innerHTML = "The view for " + current_room + " is " + current_view + " but there is no renderer." ;
			break;
	}

}


// Forum view (flat) -- let's have another go at this with the rendering done client-side
//
function forum_readmessages(target_div, gt_msg, lt_msg)
{
	original_text = document.getElementById(target_div).innerHTML;		// in case we need to replace it after an error
	document.getElementById(target_div).innerHTML = 
		"<i class=\"fas fa-spinner fa-spin\"></i>&nbsp;&nbsp;"
		+ _("Loading messages from server, please wait") ;

	var request = new XMLHttpRequest();
	if (lt_msg < 9999999999)
	{
		request.open("GET", "/ctdl/r/" + escapeHTMLURI(current_room) + "/msgs.lt|" + lt_msg, true);
	}
	else
	{
		request.open("GET", "/ctdl/r/" + escapeHTMLURI(current_room) + "/msgs.gt|" + gt_msg, true);
	}
	request.onreadystatechange = function()
	{
		if (this.readyState === 4)
		{
			if ((this.status / 100) == 2)
			{
				msgs = JSON.parse(this.responseText);
				document.getElementById(target_div).innerHTML = "" ;

				// If we were given an explicit starting point, by all means start there.
				// Note that we don't have to remove them from the array because we did a 'msgs gt|xxx' command to Citadel.
				if (gt_msg > 0)
				{
					msgs = msgs.slice(0, messages_per_page);
				}

				// Otherwise, show us the last 20 messages
				else
				{
					if (msgs.length > messages_per_page)
					{
						msgs = msgs.slice(msgs.length - messages_per_page);
					}
					new_old_div_name = randomString(5);
					if (msgs.length < 1)
					{
						newlt = lt_msg;
					}
					else
					{
						newlt = msgs[0];
					}
					document.getElementById(target_div).innerHTML +=
						"<div id=\"" + new_old_div_name + "\">" +
						"<a href=\"javascript:forum_readmessages('" + new_old_div_name + "', 0, " + newlt + ");\">" +
						"link to msgs less than " + newlt + "</a></div>" ;
				}

				// Render the divs (we will fill them in later)
				for (var i in msgs)
				{
					document.getElementById(target_div).innerHTML += "<div id=\"ctdl_msg_" + msgs[i] + "\">#" + msgs[i] + "</div>" ;
				}
				if (lt_msg == 9999999999)
				{
					new_new_div_name = randomString(5);
					if (msgs.length <= 0)
					{
						newgt = gt_msg;
					}
					else
					{
						newgt = msgs[msgs.length-1];
					}
					document.getElementById(target_div).innerHTML +=
						"<div id=\"" + new_new_div_name + "\">" +
						"<a href=\"javascript:forum_readmessages('" + new_new_div_name + "', " + newgt + ", 9999999999);\">" +
						"link to msgs greater than " + newgt + "</a></div>" ;
				}

				// Render the individual messages in the divs
				render_messages(msgs, "ctdl_msg_", views.VIEW_BBS);
			}
			else
			{
				document.getElementById(target_div).innerHTML = original_text;		// this will make the link reappear so the user can try again
			}
		}
	};
	request.send();
	request = null;
}


// Render a range of messages, in the view specified, with the div prefix specified
//
function render_messages(msgs, prefix, view)
{
	for (i=0; i<msgs.length; ++i)
	{
		render_one(prefix+msgs[i], msgs[i], view);
	}
}


// We have to put each XHR for render_messages() into its own stack frame, otherwise it jumbles them together.  I don't know why.
function render_one(div, msgnum, view)
{
	var request = new XMLHttpRequest();
	request.open("GET", "/ctdl/r/" + escapeHTMLURI(current_room) + "/" + msgs[i] + "/html", true);
	request.onreadystatechange = function()
	{
		if (this.readyState === 4)
		{
			if ((this.status / 100) == 2)
			{
				document.getElementById(div).innerHTML = this.responseText;	// FIXME don't let the C server render it.  do JSON now.
			}
			else
			{
				document.getElementById(div).innerHTML = "ERROR";
			}
		}
	};
	request.send();
	request = null;
}

