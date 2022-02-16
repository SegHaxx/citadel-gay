//
// Copyright (c) 2016-2022 by the citadel.org team
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


// Placeholder for when we add i18n later
function _(x) {
	return x;
}


var current_room = "_BASEROOM_";
var new_messages = 0;
var total_messages = 0;
var default_view = 0;
var current_view = 0;
var logged_in = 0;
var current_user = _("Not logged in.");
var serv_info;
var last_seen = 0;
var messages_per_page = 20;
var march_list = [] ;


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
