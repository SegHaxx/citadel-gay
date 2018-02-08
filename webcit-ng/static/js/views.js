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
// and calls the correct renderer.
//
function render_room_view()
{
	switch(current_view)
	{
		case views.VIEW_MAILBOX:						// FIXME view mail rooms as forums for now
		case views.VIEW_BBS:
			forum_readmessages();
			break;
		default:
			document.getElementById("main").innerHTML = "The view for " + current_room + " is " + current_view + " but there is no renderer." ;
			break;
	}

}


// Forum view -- flat or threaded
// The inner div exists so that if the user clicks away early, the main div doesn't get clobbered when the load completes.
// The parameter can be set to "flat" or "threads" which is passed directly to the API
//
function XX_forum_readmessages(flat_or_threads)
{
	var innerdivname = randomString(5);
	document.getElementById("main").innerHTML = "<div id=\"" + innerdivname +
		"\"><br><br><br><center><h5><i class=\"fas fa-spinner fa-spin\"></i>&nbsp;&nbsp;"
		+ _("Loading messages from server, please wait") + "</h5></center></div>" ;

	var request = new XMLHttpRequest();
	request.open("GET", "/ctdl/r/" + escapeHTMLURI(current_room) + "/" + flat_or_threads, true);
	request.onreadystatechange = function()
	{
		if (this.readyState === 4)
		{
			if ((this.status / 100) == 2)
			{
				document.getElementById(innerdivname).outerHTML = this.responseText;
			}
			else
			{
				document.getElementById(innerdivname).outerHTML = "ERROR " + this.status ;
			}
		}
	};
	request.send();
	request = null;
}


// Forum view (flat) -- let's have another go at this with the rendering done client-side
//
function forum_readmessages()
{
	var innerdivname = randomString(5);
	document.getElementById("main").innerHTML = "<div id=\"" + innerdivname +
		"\"><br><br><br><center><h5><i class=\"fas fa-spinner fa-spin\"></i>&nbsp;&nbsp;"
		+ _("Loading messages from server, please wait") + "</h5></center></div>" ;

	var request = new XMLHttpRequest();
	request.open("GET", "/ctdl/r/" + escapeHTMLURI(current_room) + "/msgs.all", true);
	request.onreadystatechange = function()
	{
		if (this.readyState === 4)
		{
			if ((this.status / 100) == 2)
			{
				msgs = JSON.parse(this.responseText);
				document.getElementById(innerdivname).innerHTML =
					"Are we logged in? " + logged_in + "<br>"
					+ "Last seen: " + last_seen + "<br>"
					+ "Number of messages: " + msgs.length + "<br>" ;

				if (msgs.length == 0)
				{
						document.getElementById(innerdivname).innerHTML += "FIXME no msgs" ;
				}

				// show us the last 20 messages and scroll to the bottom (this will become the not-logged-in behavior)
				else if ((logged_in) | (!logged_in))
				{
					if (msgs.length > messages_per_page)
					{
						msgs = msgs.slice(msgs.length - messages_per_page);
						document.getElementById(innerdivname).innerHTML += "link to msgs less than " + msgs[0] + "<br>" ;
					}
				}

				for (var i in msgs)
				{
					document.getElementById(innerdivname).innerHTML += "message # " + msgs[i] + "<br>" ;
				}
			}
			else
			{
				document.getElementById(innerdivname).innerHTML = this.status ;		// error message
			}
		}
	};
	request.send();
	request = null;
}
