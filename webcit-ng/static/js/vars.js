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
