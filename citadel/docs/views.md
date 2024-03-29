How "views" work in Citadel
---------------------------
 
There's no need to be rigid and stupid about how different rooms are presented
in a Citadel client.  And we don't enforce things either.  But there's a need
to make things look the way the user wants to see them.  For example, we might
always choose to see a room full of private mail as a summary (one line per
message) rather than always dumping out the entire contents like we do on a
typical message room.  An address book room might look better as a tabbed view
or something, rather than as a bunch of messages with vCards attached to them.

This is why we define "views" for a room.  It gives the client software a
hint as to how to display the contents of a room.  This is kept on a per-user
basis by storing it in the `visit` record for a particular room/user
combination.  It is `visit.v_view` and is an integer.  Naturally, there also
needs to be a default, for users who have never visited the room before.  This
is in the room record as `room.QRdefaultview` (and is also an integer).
 
In recent versions of Citadel, the view for a room also defines when and how
it is indexed.  For example, mailboxes and bulletin boards don't need to have
an euid index, but address books and calendars do.
 
The values currently defined are:

    #define VIEW_BBS         0   /* Bulletin board view */
    #define VIEW_MAILBOX     1   /* Mailbox summary */
    #define VIEW_ADDRESSBOOK 2   /* Address book view */
    #define VIEW_CALENDAR    3   /* Calendar view */
    #define VIEW_TASKS       4   /* Tasks view */
    #define VIEW_NOTES       5   /* Notes view */
    #define VIEW_WIKI        6   /* Wiki view */

