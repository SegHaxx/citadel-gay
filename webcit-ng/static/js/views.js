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


// List of defined views shamelessly swiped from libcitadel headers
//
var views = {
	VIEW_BBS		: 0,	// Bulletin board view
	VIEW_MAILBOX		: 1,	// Mailbox summary
	VIEW_ADDRESSBOOK	: 2,	// Address book view
	VIEW_CALENDAR		: 3,	// Calendar view
	VIEW_TASKS		: 4,	// Tasks view
	VIEW_NOTES		: 5,	// Notes view
	VIEW_WIKI		: 6,	// Wiki view
	VIEW_CALBRIEF		: 7,	// Brief Calendar view
	VIEW_JOURNAL		: 8,	// Journal view
	VIEW_DRAFTS		: 9,	// Drafts view
	VIEW_BLOG		: 10,	// Blog view
	VIEW_QUEUE		: 11,   // SMTP queue rooms
	VIEW_WIKIMD		: 12,	// markdown wiki (no longer implemented)
};


// This function is the dispatcher that determines the correct view for a room,
// and calls the correct renderer.  Greater/Less than bounds are accepted.
//
function render_room_view(gt_msg, lt_msg) {
	switch(current_view) {
		case views.VIEW_MAILBOX:						// FIXME view mail rooms as forums for now
		case views.VIEW_BBS:
			document.getElementById("ctdl-main").innerHTML = "<div id=\"ctdl-mrp\" class=\"ctdl-msg-reading-pane\"></div>";
			forum_readmessages("ctdl-mrp", gt_msg, lt_msg);
			break;
		default:
			document.getElementById("ctdl-main").innerHTML =
				"The view for " + current_room + " is " + current_view + " but there is no renderer." ;
			break;
	}

}
