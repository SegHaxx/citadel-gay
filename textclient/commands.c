/*
 * This file contains functions which implement parts of the
 * text-mode user interface.
 *
 * Copyright (c) 1987-2018 by the citadel.org team
 *
 * This program is open source software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include "textclient.h"

char *helpnames[] = {
	"help",
	"admin",
	"floors",
	"intro",
	"mail",
	"network",
	"software"
};

char *helptexts[] = {

	"                          Citadel Help Menu\n"
	    "  \n"
	    " ?         Help. (Typing a '?' will give you a menu almost anywhere)\n"
	    " A         Abandon this room where you stopped reading, goto next room.\n"
	    " C         Chat (multiuser chat, where available)\n"
	    " D         Prints directory, if there is one in the current room.\n"
	    " E         Enter a message.\n"
	    " F         Read all messages in the room, forward.\n"
	    " G         Goto next room which has UNREAD messages.\n"
	    " H         Help. Same as '?'\n"
	    " I         Reads the Information file for this room.\n"
	    " K         List of Known rooms.\n"
	    " L         Reads the last five messages in the room.\n"
	    " N         Reads all new messages in the room.\n"
	    " O         Reads all old messages, backwards.\n"
	    " P         Page another user (send an instant message)\n"
	    " R         Reads all messages in the room, in reverse order.\n"
	    " S         Skips current room without making its messages old.\n"
	    " T         Terminate (logout)\n"
	    " U         Ungoto (returns to the last room you were in)\n"
	    " W         Displays who is currently logged in.\n"
	    " X         Toggle eXpert mode (menus and help blurbs on/off)\n"
	    " Z         Zap (forget) room. (Removes the room from your list)\n"
	    " + -       Goto next, previous room on current floor.\n"
	    " > <       Goto next, previous floor.\n"
	    " *         Enter any locally installed 'doors'.\n"
	    "   \n"
	    " In addition, there are dot commands. You hit the . (dot), then press the\n"
	    "first letter of each word of the command. As you hit the letters, the words\n"
	    "pop onto your screen. Exceptions: after you hit .Help or .Goto, the remainder\n"
	    "of the command is a help file name or room name.\n" "    \n" "      *** USE  .<H>elp ?    for additional help *** \n",

	"The following commands are available only to Admins.  A subset of these\n"
	    "commands are available to room aides when they are currently in the room\n"
	    "they are room aide for.\n"
	    "\n"
	    " <.> <A>dmin <K>ill this room   (Delete the current room)\n"
	    " <.> <A>dmin <E>dit this room   (Edit the current room's parameters)\n"
	    " <.> <A>dmin <W>ho knows room   (List users with access to this room)\n"
	    " <.> <A>dmin edit <U>ser        (Change user's access level, password, etc.)\n"
	    " <.> <A>dmin <V>alidate new users   (Process new user registrations)\n"
	    " <.> <A>dmin enter <I>nfo file      (Create/change this room's banner)\n"
	    " <.> <A>dmin <R>oom <I>nvite user   (Grant access to an invitation-only room)\n"
	    " <.> <A>dmin <R>oom <K>ick out user (Revoke access to an invitation-only room)\n"
	    " <.> <A>dmin <F>ile <D>elete        (Delete a file from the room's directory)\n"
	    " <.> <A>dmin <F>ile <S>end over net (Transmit a file to another node)\n"
	    " <.> <A>dmin <F>ile <M>ove          (Move a file to another room's directory)\n"
	    " <.> <A>dmin <M>essage edit:        (Edit system banners)\n"
	    " <.> <A>dmin <P>ost                 (Post a message on behalf of another user)\n"
	    " <.> <A>dmin <S>ystem configuration <G>eneral   (Edit global site config)\n"
	    " <.> <A>dmin <S>ystem configuration <I>nternet  (Edit Internet domains)\n"
	    " <.> <A>dmin <S>ystem configuration check <M>essage base   (Internal checks)\n"
	    " <.> <A>dmin <S>ystem configuration <N>etwork   (Netting with other Citadels)\n"
	    " <.> <A>dmin <S>ystem configuration network <F>ilter list\n"
	    " <.> <A>dmin <T>erminate server <N>ow          (Shut down Citadel server now)\n"
	    " <.> <A>dmin <T>erminate server <S>cheduled    (Shut down Citadel server later)\n"
	    " <.> <A>dmin mailing <L>ist recipients         (For mailing list rooms)\n"
	    " <.> <A>dmin mailing list <D>igest recipients  (For mailing list rooms)\n"
	    " <.> <A>dmin <N>etwork room sharing     (Replication with other Citadels)\n"
	    " \n" " In addition, the <M>ove and <D>elete commands are available at the\n" "message prompt.\n",

	" Floors\n"
	    " ------\n"
	    "   Floors in Citadel are used to group rooms into related subject areas,\n"
	    "just as rooms are used to group messages into manageable groups.\n"
	    " \n"
	    "   You, as a user, do NOT have to use floors.  If you choose not to, you suffer\n"
	    "no penalty; you will not lose access to any rooms.  You may use .EC or ;C (the\n"
	    "latter is easier to use) to decide if you want to use floors.  Feel free to\n"
	    "experiment.\n"
	    " \n"
	    "   Floor options are accessed two ways.  First, if you are in floor mode, the\n"
	    "<G>oto and <S>kip commands take you to the next room with new messages on the\n"
	    "current floor; if there are none left, then the system will automatically\n"
	    "switch floors (and let you know) and put you in the first room with new messages\n"
	    "on that level.  (Notice that your pattern of basic use of Citadel therefore\n"
	    "doesn't really change.)\n"
	    " \n"
	    "   Direct access to floor options is via the use of a ';' command.\n"
	    "The following commands are currently available (more can be\n"
	    "added if needed):\n"
	    " \n"
	    " <;C>onfigure\n"
	    " This command toggles your floor mode.\n"
	    " \n"
	    " <;G>oto FLOORNAME\n"
	    " This command causes the system to take you to the named floor.\n"
	    " \n"
	    " <;K>nown rooms on floors\n"
	    " List all rooms on all floors.  This is a very readable way to get a list of\n"
	    "all rooms on the system.\n"
	    " \n"
	    " <;S>kip FLOORNAME\n"
	    " This command causes the system to mark all rooms on the current floor as\n"
	    "Skipped and takes you to the floor that you specify.\n"
	    " \n"
	    " <;Z>Forget floor\n"
	    "   This command causes you to forget all the rooms currently on the current\n"
	    "floor.  Unfortunately, it doesn't apply to rooms that are subsequently created\n"
	    "or moved to this floor.  (Sorry.)\n"
	    " \n"
	    "   Feel free to experiment, you can't hurt yourself or the system with the\n"
	    "floor stuff unless you ZForget a floor by accident.\n",

	"                  New User's Introduction to the site\n"
	    "  \n"
	    " This is an introduction to the Citadel BBS concept.  It is intended\n"
	    "for new users so that they can more easily become acquainted to using\n"
	    "Citadel when accessing it in the form of a text-based BBS.  Of\n"
	    "course, old users might learn something new each time they read\n"
	    "through it.\n"
	    " \n"
	    " Full help for the BBS commands can be obtained by typing <.H>elp SUMMARY\n"
	    "  \n"
	    " The CITADEL BBS room concept\n"
	    " ----------------------------\n"
	    "   The term BBS stands for 'Bulletin Board System'.  The analogy is\n"
	    "appropriate: one posts messages so that others may read them.  In\n"
	    "order to organize the posts, people can post in different areas of the\n"
	    "BBS, called rooms.\n"
	    "   In order to post in a certain room, you need to be 'in' that room.\n"
	    "Your current prompt is usually the room that you are in, followed the\n"
	    "greater-than-sign, such as:\n"
	    " \n"
	    " Lobby>\n"
	    " \n"
	    " The easiest way to traverse the room structure is with the 'Goto'\n"
	    "command, on the 'G' key.  Pressing 'G' will take you to the next room\n"
	    "in the 'march list' (see below) that has new messages in it.  You can\n"
	    "read these new messages with the 'N' key.\n"
	    " Once you've 'Gotoed' every room in the system (or all of the ones\n"
	    "you choose to read) you return to the 'Lobby,' the first and last room\n"
	    "in the system.  If new messages get posted to rooms you've already\n"
	    "read during your session you will be brought BACK to those rooms so\n"
	    "you can read them.\n"
	    " \n"
	    " March List\n"
	    " ----------\n"
	    "   All the room names are stored in a march list, which is just a\n"
	    "list containing all the room names.  When you <G>oto or <S>kip a\n"
	    "room, you are placed in the next room in your march list THAT HAS NEW\n"
	    "MESSAGES.  If you have no new messages in any of the rooms on your\n"
	    "march list, you will keep going to the Lobby>.  You can choose not to\n"
	    "read certain rooms (that don't interest you) by 'Z'apping them.  When\n"
	    "you <Z>ap a room, you are merely deleting it from your march list (but\n"
	    "not from anybody else's).\n"
	    " \n"
	    "   You can use the <.G>oto (note the period before the G.  You can also use\n"
	    "<J>ump on some systems) to go to any room in the\n"
	    "system.  You don't have to type in the complete name of a room to\n"
	    "'jump' to it; you merely need to type in enough to distinguish it from\n"
	    "the other rooms.  Left-aligned matches carry a heavier weight, so if you\n"
	    "typed (for example) '.Goto TECH', you might be taken to a room called\n"
	    "'Tech Area>' even if it found a room called 'Biotech/Ethics>' first.\n"
	    " \n"
	    "  To return to a room you have previously <Z>apped, use the <.G>oto command\n"
	    "to enter it, and it will be re-inserted into your march list.  In the case\n"
	    "of returning to Zapped rooms, you must type the room name in its entirety.\n"
	    "REMEMBER, rooms with no new messages will not show on your\n"
	    "march list!  You must <.G>oto to a room with no new messages.\n"
	    "Incidentally, you cannot change the order of the rooms on your march list.\n"
	    "It's the same for everybody.\n"
	    " \n"
	    " Special rooms\n"
	    " -------------\n"
	    "   There are two special rooms on a Citadel that you should know about.\n"
	    "  \n"
	    "   The first is the Lobby>.  It's used for system announcements and other\n"
	    "such administrativia.  You cannot <Z>ap the Lobby>.  Each time you first\n"
	    "login, you will be placed in the Lobby>.\n"
	    " \n"
	    "   The second is Mail>.  In Mail>, when you post a messages, you are\n"
	    "prompted to enter the screen name of the person who you want to send the\n"
	    "message to.  Only the person who you send the message to can read the\n"
	    "message.  NO ONE else can read it, not even the admins.  Mail> is the\n"
	    "first room on the march list, and is un-<Z>appable, so you can be sure\n"
	    "that the person will get the message.\n"
	    "   \n"
	    " System admins\n"
	    " -------------\n"
	    "   These people, along with the room admins, keep the site running smoothly.\n"
	    "\n"
	    "   Among the many things that admins do are: create rooms, delete\n"
	    "rooms, set access levels, invite users, check registration, grant\n"
	    "room admin status, and countless other things.  They have access to the\n"
	    "Aide> room, a special room only for admins.\n"
	    " \n"
	    "   If you enter a mail message to 'Sysop' it will be placed in the\n"
	    "Aide> room so that the next admin online will read it and deal with it.\n"
	    "Admins cannot <Z>ap rooms.  All the rooms are always on each admin's\n"
	    "march list.  Admins can read *any* and *every* room, but they *CAN* *NOT*\n"
	    "read other users' Mail!\n"
	    "  \n"
	    " Room admins\n"
	    " -----------\n"
	    "   Room admins are granted special privileges in specific rooms.\n"
	    "They are *NOT* true system admins; their power extends only over the\n"
	    "rooms that they control, and they answer to the system admins.\n"
	    "  \n"
	    "   A room admin's job is to keep the topic of the their room on track,\n"
	    "with nudges in the right direction now and then.  A room admin can also\n"
	    "move an off topic post to another room, or delete a post, if he/she\n"
	    "feels it is necessary. \n"
	    "  \n"
	    "   Currently, very few rooms have room admins.  Most rooms do not need\n"
	    "their own specific room admin.  Being a room admin requires a certain\n"
	    "amount of trust, due to the additional privileges granted.\n"
	    "  \n"
	    " Citadel messages\n"
	    " ----------------\n"
	    "   Most of the time, the BBS code does not print a lot of messages\n"
	    "to your screen.  This is a great benefit once you become familiar\n"
	    "with the system, because you do not have endless menus and screens\n"
	    "to navigate through.  nevertheless, there are some messages which you\n"
	    "might see from time to time.\n"
	    "  \n"
	    "  'There were messages posted while you were entering.'\n"
	    "  \n"
	    "   This is also known as 'simulposting.'  When you start entering a \n"
	    "message, the system knows where you last left off.  When you save\n"
	    "your message, the system checks to see if any messages were entered\n"
	    "while you were typing.  This is so that you know whether you need\n"
	    "to go back and re-read the last few messages.  This message may appear\n"
	    "in any room.\n"
	    "   \n"
	    " '*** You have new mail'\n"
	    "  \n"
	    "   This message is essentially the same as the above message, but can\n"
	    "appear at any time.  It simply means that new mail has arrived for you while\n"
	    "you are logged in.  Simply go to the Mail> room to read it.\n"
	    "  \n"
	    " Who list\n"
	    " --------\n"
	    "   The <W>ho command shows you the names of all users who are currently\n"
	    "online.  It also shows you the name of the room they are currently in.  If\n"
	    "they are in any type of private room, however, the room name will simply\n"
	    "display as '<private room>'.  Along with this information is displayed the\n"
	    "name of the host computer the user is logged in from.\n",

	"To send mail on this system, go to the Mail> room (using the command .G Mail)\n"
	    "and press E to enter a message.  You will be prompted with:\n"
	    " \n"
	    " Enter Recipient:\n"
	    " \n"
	    "   At this point you may enter the name of another user on the system.  Private\n"
	    "mail is only readable by the sender and recipient.  There is no need to delete\n"
	    "mail after it is read; it will scroll out automatically.\n"
	    "  \n"
	    "   To send mail to another user on the Citadel network, simply type the\n"
	    "user's name, followed by @ and then the system name. For example,\n"
	    "  \n"
	    " Enter Recipient: Joe Schmoe @ citadrool\n"
	    "  \n"
	    "  If your account is enabled for Internet mail, you can also send email to\n"
	    "anyone on the Internet here.  Simply enter their address at the prompt:\n"
	    "  \n" " Enter Recipient: ajc@herring.fishnet.com\n",

	"  Welcome to the network. Messages entered in a network room will appear in\n"
	    "that room on all other systems carrying it (The name of the room, however,\n" "may be different on other systems).\n",

	"   Citadel is the premier 'online community' (i.e. Bulletin Board System)\n"
	    "software.  It runs on all POSIX-compliant systems, including Linux.  It is an\n"
	    "advanced client/server application, and is being actively maintained.\n"
	    " \n" "   For more info, visit UNCENSORED! BBS at uncensored.citadel.org\n",

	"Extended commands are available using the period ( . ) key. To use\n"
	    "a dot command, press the . key, and then enter the first letter of\n"
	    "each word in the command. The words will appear as you enter the keys.\n"
	    "You can also backspace over partially entered commands. The following\n"
	    "commands are available:\n"
	    "\n"
	    " <.> <H>elp:    Displays help files.  Type .H followed by a help file\n"
	    "                name.  You are now reading <.H>elp SUMMARY\n"
	    " \n"
	    " <.> <G>oto:    Jumps directly to the room you specify.  You can also\n"
	    "                type a partial room name, just enough to make it unique,\n"
	    "                and it'll find the room you're looking for.  As with the\n"
	    "                regular <G>oto command, messages in the current room will\n"
	    "                be marked as read.\n"
	    " \n"
	    " <.> <S>kip, goto:    This is similar to <.G>oto, except it doesn't mark\n"
	    "                      messages in the current room as read.\n"
	    " \n"
	    " <.> list <Z>apped rooms      Shows all rooms you've <Z>apped (forgotten)\n"
	    "\n"
	    "  \n"
	    " Terminate (logoff) commands:\n"
	    " \n"
	    " <.> <T>erminate and <Q>uit               Log off and disconnect.\n"
	    " <.> <T>erminate and <S>tay online        Log in as a different user.\n"
	    " \n"
	    " \n"
	    " Read commands:\n"
	    "\n"
	    " <.> <R>ead <N>ew messages                Same as <N>ew\n"
	    " <.> <R>ead <O>ld msgs reverse            Same as <O>ld\n"
	    " <.> <R>ead <L>ast five msgs              Same as <L>ast5\n"
	    " <.> read <L>ast:                         Allows you to specify how many\n"
	    "                                          messages you wish to read.\n"
	    "\n"
	    " <.> <R>ead <U>ser listing:               Lists all users on the system if\n"
	    "                                          you just hit enter, otherwise\n"
	    "                                          you can specify a partial match\n"
	    "\n"
	    " <.> <R>ead <T>extfile formatted          File 'download' commands.\n"
	    " <.> <R>ead file using <X>modem   \n"
	    " <.> <R>ead file using <Y>modem   \n"
	    " <.> <R>ead file using <Z>modem   \n"
	    " <.> <R>ead <F>ile unformatted   \n"
	    " <.> <R>ead <D>irectory   \n"
	    "\n"
	    " <.> <R>ead <I>nfo file                   Read the room info file.\n"
	    " <.> <R>ead <B>io                         Read other users' 'bio' files.\n"
	    " <.> <R>ead <C>onfiguration               Display your 'preferences'.\n"
	    " <.> <R>ead <S>ystem info                 Display system statistics.\n"
	    "\n"
	    " \n"
	    " Enter commands:\n"
	    "\n"
	    " <.> <E>nter <M>essage                    Post a message in this room.\n"
	    " <.> <E>nter message with <E>ditor        Post using a full-screen editor.\n"
	    " <.> <E>nter <A>SCII message              Post 'raw' (use this when 'pasting'\n"
	    "                                          a message from your clipboard).\n"
	    "\n"
	    " <.> <E>nter <P>assword                   Change your password.\n"
	    " <.> <E>nter <C>onfiguration              Change your 'preferences'.\n"
	    " <.> <E>nter a new <R>oom                 Create a new room.\n"
	    " <.> <E>nter re<G>istration               Register (name, address, etc.)\n"
	    " <.> <E>nter <B>io                        Enter/change your 'bio' file.\n"
	    "\n"
	    " <.> <E>nter <T>extfile                   File 'upload' commands.\n"
	    " <.> <E>nter file using <X>modem   \n"
	    " <.> <E>nter file using <Y>modem   \n"
	    " <.> <E>nter file using <Z>modem   \n"
	    "  \n"
	    "  \n"
	    "  Wholist commands:\n"
	    " \n"
	    " <.> <W>holist <L>ong             Same as <W>ho is online, but displays\n"
	    "                                  more detailed information.\n"
	    " <.> <W>holist <R>oomname         Masquerade your room name (other users\n"
	    "                                  see the name you enter rather than the\n"
	    "                                  actual name of the room you're in)\n"
	    " <.> <W>holist <H>ostname         Masquerade your host name\n"
	    " <.> <E>nter <U>sername           Masquerade your user name (Admins only)\n"
	    " <.> <W>holist <S>tealth mode     Enter/exit 'stealth mode' (when in stealth\n"
	    "                                  mode you are invisible on the wholist)\n"
	    " \n"
	    " \n"
	    " Floor commands (if using floor mode)\n"
	    " ;<C>onfigure floor mode            - turn floor mode on or off\n"
	    " ;<G>oto floor:                     - jump to a specific floor\n"
	    " ;<K>nown rooms                     - list all rooms on all floors\n"
	    " ;<S>kip to floor:                  - skip current floor, jump to another\n"
	    " ;<Z>ap floor                       - zap (forget) all rooms on this floor\n"
	    " \n"
	    " \n"
	    " Administrative commands: \n"
	    " \n"
	    " <.> <A>dmin <K>ill this room   \n"
	    " <.> <A>dmin <E>dit this room   \n"
	    " <.> <A>dmin <W>ho knows room   \n"
	    " <.> <A>dmin edit <U>ser   \n"
	    " <.> <A>dmin <V>alidate new users   \n"
	    " <.> <A>dmin enter <I>nfo file   \n"
	    " <.> <A>dmin <R>oom <I>nvite user  \n"
	    " <.> <A>dmin <R>oom <K>ick out user  \n"
	    " <.> <A>dmin <F>ile <D>elete  \n"
	    " <.> <A>dmin <F>ile <S>end over net  \n"
	    " <.> <A>dmin <F>ile <M>ove  \n"
	    " <.> <A>dmin <M>essage edit:   \n"
	    " <.> <A>dmin <P>ost   \n"
	    " <.> <A>dmin <S>ystem configuration   \n"
	    " <.> <A>dmin <T>erminate server <N>ow\n" " <.> <A>dmin <T>erminate server <S>cheduled\n"
};


struct citcmd {
	struct citcmd *next;
	int c_cmdnum;
	int c_axlevel;
	char c_keys[5][64];
};

#define IFNEXPERT if ((userflags&US_EXPERT)==0)


int rc_exp_beep;
char rc_exp_cmd[1024];
int rc_allow_attachments;
int rc_display_message_numbers;
int rc_force_mail_prompts;
int rc_remember_passwords;
int rc_ansi_color;
int rc_color_use_bg;
int rc_prompt_control = 0;
time_t rc_idle_threshold = (time_t) 900;
char rc_url_cmd[SIZ];
char rc_open_cmd[SIZ];
char rc_gotmail_cmd[SIZ];

int next_lazy_cmd = 5;

extern int screenwidth, screenheight;
extern int termn8;
extern CtdlIPC *ipc_for_signal_handlers;	/* KLUDGE cover your eyes */

struct citcmd *cmdlist = NULL;


/* these variables are local to this module */
char keepalives_enabled = KA_YES;	/* send NOOPs to server when idle */
int ok_to_interrupt = 0;	/* print instant msgs asynchronously */
time_t AnsiDetect;		/* when did we send the detect code? */
int enable_color = 0;		/* nonzero for ANSI color */




/*
 * If an interesting key has been pressed, return its value, otherwise 0
 */
char was_a_key_pressed(void)
{
	fd_set rfds;
	struct timeval tv;
	int the_character;
	int retval;

	FD_ZERO(&rfds);
	FD_SET(0, &rfds);
	tv.tv_sec = 0;
	tv.tv_usec = 0;
	retval = select(1, &rfds, NULL, NULL, &tv);

	/* Careful!  Disable keepalives during keyboard polling; we're probably
	 * in the middle of a data transfer from the server, in which case
	 * sending a NOOP would throw the client protocol out of sync.
	 */
	if ((retval > 0) && FD_ISSET(0, &rfds)) {
		set_keepalives(KA_NO);
		the_character = inkey();
		set_keepalives(KA_YES);
	} else {
		the_character = 0;
	}
	return (the_character);
}





/*
 * print_instant()  -  print instant messages if there are any
 */
void print_instant(void)
{
	char buf[1024];
	FILE *outpipe;
	time_t timestamp;
	struct tm stamp;
	int flags = 0;
	char sender[64];
	char node[64];
	char *listing = NULL;
	int r;			/* IPC result code */

	if (instant_msgs == 0)
		return;

	if (rc_exp_beep) {
		ctdl_beep();
	}
	if (IsEmptyStr(rc_exp_cmd)) {
		color(BRIGHT_RED);
		scr_printf("\r---");
	}

	while (instant_msgs != 0) {
		r = CtdlIPCGetInstantMessage(ipc_for_signal_handlers, &listing, buf);
		if (r / 100 != 1)
			return;

		instant_msgs = extract_int(buf, 0);
		timestamp = extract_long(buf, 1);
		flags = extract_int(buf, 2);
		extract_token(sender, buf, 3, '|', sizeof sender);
		extract_token(node, buf, 4, '|', sizeof node);
		strcpy(last_paged, sender);

		localtime_r(&timestamp, &stamp);

		/* If the page is a Logoff Request, honor it. */
		if (flags & 2) {
			termn8 = 1;
			return;
		}

		if (!IsEmptyStr(rc_exp_cmd)) {
			outpipe = popen(rc_exp_cmd, "w");
			if (outpipe != NULL) {
				/* Header derived from flags */
				if (flags & 2)
					fprintf(outpipe, "Please log off now, as requested ");
				else if (flags & 1)
					fprintf(outpipe, "Broadcast message ");
				else if (flags & 4)
					fprintf(outpipe, "Chat request ");
				else
					fprintf(outpipe, "Message ");
				/* Timestamp.  Can this be improved? */
				if (stamp.tm_hour == 0 || stamp.tm_hour == 12)
					fprintf(outpipe, "at 12:%02d%cm", stamp.tm_min, stamp.tm_hour ? 'p' : 'a');
				else if (stamp.tm_hour > 12)	/* pm */
					fprintf(outpipe, "at %d:%02dpm", stamp.tm_hour - 12, stamp.tm_min);
				else	/* am */
					fprintf(outpipe, "at %d:%02dam", stamp.tm_hour, stamp.tm_min);
				fprintf(outpipe, " from %s", sender);
				if (strncmp(ipc_for_signal_handlers->ServInfo.nodename, node, 32))
					fprintf(outpipe, " @%s", node);
				fprintf(outpipe, ":\n%s\n", listing);
				pclose(outpipe);
				if (instant_msgs == 0)
					return;
				continue;
			}
		}
		/* fall back to built-in instant message display */
		scr_printf("\n");

		/* Header derived from flags */
		if (flags & 2)
			scr_printf("Please log off now, as requested ");
		else if (flags & 1)
			scr_printf("Broadcast message ");
		else if (flags & 4)
			scr_printf("Chat request ");
		else
			scr_printf("Message ");

		/* Timestamp.  Can this be improved? */
		if (stamp.tm_hour == 0 || stamp.tm_hour == 12)	/* 12am/12pm */
			scr_printf("at 12:%02d%cm", stamp.tm_min, stamp.tm_hour ? 'p' : 'a');
		else if (stamp.tm_hour > 12)	/* pm */
			scr_printf("at %d:%02dpm", stamp.tm_hour - 12, stamp.tm_min);
		else		/* am */
			scr_printf("at %d:%02dam", stamp.tm_hour, stamp.tm_min);

		/* Sender */
		scr_printf(" from %s", sender);

		/* Remote node, if any */
		if (strncmp(ipc_for_signal_handlers->ServInfo.nodename, node, 32))
			scr_printf(" @%s", node);

		scr_printf(":\n");
		fmout(screenwidth, NULL, listing, NULL, 0);
		free(listing);

	}
	scr_printf("\n---\n");
	color(BRIGHT_WHITE);


}


void set_keepalives(int s)
{
	keepalives_enabled = (char) s;
}

/* 
 * This loop handles the "keepalive" messages sent to the server when idling.
 */

static time_t idlet = 0;
static void really_do_keepalive(void)
{

	time(&idlet);

	/* This may sometimes get called before we are actually connected
	 * to the server.  Don't do anything if we aren't connected. -IO
	 */
	if (!ipc_for_signal_handlers)
		return;

	/* If full keepalives are enabled, send a NOOP to the server and
	 * wait for a response.
	 */
	if (keepalives_enabled == KA_YES) {
		CtdlIPCNoop(ipc_for_signal_handlers);
		if (instant_msgs > 0) {
			if (ok_to_interrupt == 1) {
				scr_printf("\r%64s\r", "");
				print_instant();
				scr_printf("%s%c ", room_name, room_prompt(room_flags));
				scr_flush();
			}
		}
	}

	/* If half keepalives are enabled, send a QNOP to the server (if the
	 * server supports it) and then do nothing.
	 */
	if ((keepalives_enabled == KA_HALF)
	    && (ipc_for_signal_handlers->ServInfo.supports_qnop > 0)) {
		CtdlIPC_chat_send(ipc_for_signal_handlers, "QNOP");
	}
}

/* I changed this from static to not because I need to call it from
 * screen.c, either that or make something in screen.c not static.
 * Fix it how you like. Why all the staticness? stu
 */
void do_keepalive(void)
{
	time_t now;

	time(&now);
	if ((now - idlet) < ((long) S_KEEPALIVE)) {
		return;
	}

	/* Do a space-backspace to keep terminal sessions from idling out */
	scr_printf(" %c", 8);
	scr_flush();

	really_do_keepalive();
}



int inkey(void)
{				/* get a character from the keyboard, with   */
	int a;			/* the watchdog timer in effect if necessary */
	fd_set rfds;
	struct timeval tv;
	time_t start_time;

	scr_flush();
	time(&start_time);

	do {
		/* This loop waits for keyboard input.  If the keepalive
		 * timer expires, it sends a keepalive to the server if
		 * necessary and then waits again.
		 */
		do {
			do_keepalive();

			FD_ZERO(&rfds);
			FD_SET(0, &rfds);
			tv.tv_sec = S_KEEPALIVE;
			tv.tv_usec = 0;

			select(1, &rfds, NULL, NULL, &tv);
		} while (!FD_ISSET(0, &rfds));

		/* At this point, there's input, so fetch it.
		 * (There's a hole in the bucket...)
		 */
		a = scr_getc(SCR_BLOCK);
		if (a == 127) {
			a = 8;
		}
		if (a == 13) {
			a = 10;
		}
	} while (a == 0);
	return (a);
}


int yesno(void)
{				/* Returns 1 for yes, 0 for no */
	int a;
	while (1) {
		a = inkey();
		a = tolower(a);
		if (a == 'y') {
			scr_printf("Yes\n");
			return (1);
		}
		if (a == 'n') {
			scr_printf("No\n");
			return (0);
		}
	}
}

/* Returns 1 for yes, 0 for no, arg is default value */
int yesno_d(int d)
{
	int a;
	while (1) {
		a = inkey();
		a = tolower(a);
		if (a == 10)
			a = (d ? 'y' : 'n');
		if (a == 'y') {
			scr_printf("Yes\n");
			return (1);
		}
		if (a == 'n') {
			scr_printf("No\n");
			return (0);
		}
	}
}




/*
 * Function to read a line of text from the terminal.
 *
 * string		Pointer to string buffer
 * lim			Maximum length
 * noshow		Echo asterisks instead of keystrokes?
 * bs			Allow backspacing out of the prompt? (returns -1 if this happens)
 *
 * returns: string length
 */
int ctdl_getline(char *string, int lim, int noshow, int bs)
{
	int pos = strlen(string);
	int ch;

	if (noshow && !IsEmptyStr(string)) {
		int num_stars = strlen(string);
		while (num_stars--) {
			scr_putc('*');
		}
	} else {
		scr_printf("%s", string);
	}

	while (1) {
		ch = inkey();

		if ((ch == 8) && (pos > 0)) {	/* backspace */
			--pos;
			scr_putc(8);
			scr_putc(32);
			scr_putc(8);
		}

		else if ((ch == 8) && (pos == 0) && (bs)) {	/* backspace out of the prompt */
			return (-1);
		}

		else if ((ch == 23) && (pos > 0)) {	/* Ctrl-W deletes a word */
			while ((pos > 0) && !isspace(string[pos])) {
				--pos;
				scr_putc(8);
				scr_putc(32);
				scr_putc(8);
			}
			while ((pos > 0) && !isspace(string[pos - 1])) {
				--pos;
				scr_putc(8);
				scr_putc(32);
				scr_putc(8);
			}
		}

		else if (ch == 10) {	/* return */
			string[pos] = 0;
			scr_printf("\n");
			return (pos);
		}

		else if (isprint(ch)) {	/* payload characters */
			scr_putc((noshow ? '*' : ch));
			string[pos] = ch;
			++pos;
		}
	}
}


/* 
 * newprompt()		prompt for a string, print the existing value, and
 *			allow the user to press return to keep it...
 *			If len is negative, pass the "noshow" flag to ctdl_getline()
 */
void strprompt(char *prompt, char *str, int len)
{
	print_instant();
	color(DIM_WHITE);
	scr_printf("%s", prompt);
	color(DIM_WHITE);
	scr_printf(": ");
	color(BRIGHT_CYAN);
	ctdl_getline(str, abs(len), (len < 0), 0);
	color(DIM_WHITE);
}

/*
 * boolprompt()  -  prompt for a yes/no, print the existing value and
 *                  allow the user to press return to keep it...
 */
int boolprompt(char *prompt, int prev_val)
{
	int r;

	color(DIM_WHITE);
	scr_printf("%s ", prompt);
	color(DIM_MAGENTA);
	scr_printf("[");
	color(BRIGHT_MAGENTA);
	scr_printf("%s", (prev_val ? "Yes" : "No"));
	color(DIM_MAGENTA);
	scr_printf("]: ");
	color(BRIGHT_CYAN);
	r = (yesno_d(prev_val));
	color(DIM_WHITE);
	return r;
}

/* 
 * intprompt()  -  like strprompt(), except for an integer
 *                 (note that it RETURNS the new value!)
 */
int intprompt(char *prompt, int ival, int imin, int imax)
{
	char buf[16];
	int i;
	int p;

	do {
		i = ival;
		snprintf(buf, sizeof buf, "%d", i);
		strprompt(prompt, buf, 15);
		i = atoi(buf);
		for (p = 0; !IsEmptyStr(&buf[p]); ++p) {
			if ((!isdigit(buf[p]))
			    && ((buf[p] != '-') || (p != 0)))
				i = imin - 1;
		}
		if (i < imin)
			scr_printf("*** Must be no less than %d.\n", imin);
		if (i > imax)
			scr_printf("*** Must be no more than %d.\n", imax);
	} while ((i < imin) || (i > imax));
	return (i);
}

/* 
 * newprompt()		prompt for a string with no existing value
 *			(clears out string buffer first)
 *			If len is negative, pass the "noshow" flag to ctdl_getline()
 */
void newprompt(char *prompt, char *str, int len)
{
	str[0] = 0;
	color(BRIGHT_MAGENTA);
	scr_printf("%s", prompt);
	color(DIM_MAGENTA);
	ctdl_getline(str, abs(len), (len < 0), 0);
	color(DIM_WHITE);
}


int lkey(void)
{				/* returns a lower case value */
	int a;
	a = inkey();
	if (isupper(a))
		a = tolower(a);
	return (a);
}

/*
 * parse the citadel.rc file
 */
void load_command_set(void)
{
	FILE *ccfile;
	char buf[1024];
	struct citcmd *cptr;
	struct citcmd *lastcmd = NULL;
	int a, d;
	int b = 0;

	/* first, set up some defaults for non-required variables */

	strcpy(editor_path, "");
	strcpy(printcmd, "");
	strcpy(imagecmd, "");
	strcpy(rc_username, "");
	strcpy(rc_password, "");
	rc_floor_mode = 0;
	rc_exp_beep = 1;
	rc_allow_attachments = 0;
	rc_remember_passwords = 0;
	strcpy(rc_exp_cmd, "");
	rc_display_message_numbers = 0;
	rc_force_mail_prompts = 0;
	rc_ansi_color = 0;
	rc_color_use_bg = 0;
	strcpy(rc_url_cmd, "");
	strcpy(rc_open_cmd, "");
	strcpy(rc_gotmail_cmd, "");
#ifdef HAVE_OPENSSL
	rc_encrypt = RC_DEFAULT;
#endif

	/* now try to open the citadel.rc file */

	ccfile = NULL;
	if (getenv("HOME") != NULL) {
		snprintf(buf, sizeof buf, "%s/.citadelrc", getenv("HOME"));
		ccfile = fopen(buf, "r");
	}
	if (ccfile == NULL) {
		ccfile = fopen(file_citadel_rc, "r");
	}
	if (ccfile == NULL) {
		ccfile = fopen("/etc/citadel.rc", "r");
	}
	if (ccfile == NULL) {
		ccfile = fopen("./citadel.rc", "r");
	}
	if (ccfile == NULL) {
		perror("commands: cannot open citadel.rc");
		logoff(NULL, 3);
	}
	while (fgets(buf, sizeof buf, ccfile) != NULL) {
		while ((!IsEmptyStr(buf)) ? (isspace(buf[strlen(buf) - 1])) : 0)
			buf[strlen(buf) - 1] = 0;

		if (!strncasecmp(buf, "encrypt=", 8)) {
			if (!strcasecmp(&buf[8], "yes")) {
#ifdef HAVE_OPENSSL
				rc_encrypt = RC_YES;
#else
				fprintf(stderr, "citadel.rc requires encryption support but citadel is not compiled with OpenSSL");
				logoff(NULL, 3);
#endif
			}
#ifdef HAVE_OPENSSL
			else if (!strcasecmp(&buf[8], "no")) {
				rc_encrypt = RC_NO;
			} else if (!strcasecmp(&buf[8], "default")) {
				rc_encrypt = RC_DEFAULT;
			}
#endif
		}

		if (!strncasecmp(buf, "editor=", 7)) {
			strcpy(editor_path, &buf[7]);
		}

		if (!strncasecmp(buf, "printcmd=", 9))
			strcpy(printcmd, &buf[9]);

		if (!strncasecmp(buf, "imagecmd=", 9))
			strcpy(imagecmd, &buf[9]);

		if (!strncasecmp(buf, "expcmd=", 7))
			strcpy(rc_exp_cmd, &buf[7]);

		if (!strncasecmp(buf, "use_floors=", 11)) {
			if (!strcasecmp(&buf[11], "yes"))
				rc_floor_mode = RC_YES;
			if (!strcasecmp(&buf[11], "no"))
				rc_floor_mode = RC_NO;
			if (!strcasecmp(&buf[11], "default"))
				rc_floor_mode = RC_DEFAULT;
		}
		if (!strncasecmp(buf, "beep=", 5)) {
			rc_exp_beep = atoi(&buf[5]);
		}
		if (!strncasecmp(buf, "allow_attachments=", 18)) {
			rc_allow_attachments = atoi(&buf[18]);
		}
		if (!strncasecmp(buf, "idle_threshold=", 15)) {
			rc_idle_threshold = atol(&buf[15]);
		}
		if (!strncasecmp(buf, "remember_passwords=", 19)) {
			rc_remember_passwords = atoi(&buf[19]);
		}
		if (!strncasecmp(buf, "display_message_numbers=", 24)) {
			rc_display_message_numbers = atoi(&buf[24]);
		}
		if (!strncasecmp(buf, "force_mail_prompts=", 19)) {
			rc_force_mail_prompts = atoi(&buf[19]);
		}
		if (!strncasecmp(buf, "ansi_color=", 11)) {
			if (!strncasecmp(&buf[11], "on", 2))
				rc_ansi_color = 1;
			if (!strncasecmp(&buf[11], "auto", 4))
				rc_ansi_color = 2;	/* autodetect */
			if (!strncasecmp(&buf[11], "user", 4))
				rc_ansi_color = 3;	/* user config */
		}
		if (!strncasecmp(buf, "status_line=", 12)) {
			if (!strncasecmp(&buf[12], "on", 2))
				enable_status_line = 1;
		}
		if (!strncasecmp(buf, "use_background=", 15)) {
			if (!strncasecmp(&buf[15], "on", 2))
				rc_color_use_bg = 9;
		}
		if (!strncasecmp(buf, "prompt_control=", 15)) {
			if (!strncasecmp(&buf[15], "on", 2))
				rc_prompt_control = 1;
			if (!strncasecmp(&buf[15], "user", 4))
				rc_prompt_control = 3;	/* user config */
		}
		if (!strncasecmp(buf, "username=", 9))
			strcpy(rc_username, &buf[9]);

		if (!strncasecmp(buf, "password=", 9))
			strcpy(rc_password, &buf[9]);

		if (!strncasecmp(buf, "urlcmd=", 7))
			strcpy(rc_url_cmd, &buf[7]);

		if (!strncasecmp(buf, "opencmd=", 7))
			strcpy(rc_open_cmd, &buf[8]);

		if (!strncasecmp(buf, "gotmailcmd=", 11))
			strcpy(rc_gotmail_cmd, &buf[11]);

		if (!strncasecmp(buf, "cmd=", 4)) {
			strcpy(buf, &buf[4]);

			cptr = (struct citcmd *) malloc(sizeof(struct citcmd));

			cptr->c_cmdnum = atoi(buf);
			for (d = strlen(buf); d >= 0; --d)
				if (buf[d] == ',')
					b = d;
			strcpy(buf, &buf[b + 1]);

			cptr->c_axlevel = atoi(buf);
			for (d = strlen(buf); d >= 0; --d)
				if (buf[d] == ',')
					b = d;
			strcpy(buf, &buf[b + 1]);

			for (a = 0; a < 5; ++a)
				cptr->c_keys[a][0] = 0;

			a = 0;
			b = 0;
			buf[strlen(buf) + 1] = 0;
			while (!IsEmptyStr(buf)) {
				b = strlen(buf);
				for (d = strlen(buf); d >= 0; --d)
					if (buf[d] == ',')
						b = d;
				strncpy(cptr->c_keys[a], buf, b);
				cptr->c_keys[a][b] = 0;
				if (buf[b] == ',')
					strcpy(buf, &buf[b + 1]);
				else
					strcpy(buf, "");
				++a;
			}

			cptr->next = NULL;
			if (cmdlist == NULL)
				cmdlist = cptr;
			else
				lastcmd->next = cptr;
			lastcmd = cptr;
		}
	}
	fclose(ccfile);
}



/*
 * return the key associated with a command
 */
char keycmd(char *cmdstr)
{
	int a;

	for (a = 0; !IsEmptyStr(&cmdstr[a]); ++a)
		if (cmdstr[a] == '&')
			return (tolower(cmdstr[a + 1]));
	return (0);
}


/*
 * Output the string from a key command without the ampersand
 * "mode" should be set to 0 for normal or 1 for <C>ommand key highlighting
 */
char *cmd_expand(char *strbuf, int mode)
{
	int a;
	static char exp[64];
	char buf[1024];

	strcpy(exp, strbuf);

	for (a = 0; exp[a]; ++a) {
		if (strbuf[a] == '&') {

			/* dont echo these non mnemonic command keys */
			int noecho = strbuf[a + 1] == '<' || strbuf[a + 1] == '>' || strbuf[a + 1] == '+' || strbuf[a + 1] == '-';

			if (mode == 0) {
				strcpy(&exp[a], &exp[a + 1 + noecho]);
			}
			if (mode == 1) {
				exp[a] = '<';
				strcpy(buf, &exp[a + 2]);
				exp[a + 2] = '>';
				exp[a + 3] = 0;
				strcat(exp, buf);
			}
		}
		if (!strncmp(&exp[a], "^r", 2)) {
			strcpy(buf, exp);
			strcpy(&exp[a], room_name);
			strcat(exp, &buf[a + 2]);
		}
		if (!strncmp(&exp[a], "^c", 2)) {
			exp[a] = ',';
			strcpy(&exp[a + 1], &exp[a + 2]);
		}
	}

	return (exp);
}



/*
 * Comparison function to determine if entered commands match a
 * command loaded from the config file.
 */
int cmdmatch(char *cmdbuf, struct citcmd *cptr, int ncomp)
{
	int a;
	int cmdax;

	cmdax = 0;
	if (is_room_aide)
		cmdax = 1;
	if (axlevel >= 6)
		cmdax = 2;

	for (a = 0; a < ncomp; ++a) {
		if ((tolower(cmdbuf[a]) != keycmd(cptr->c_keys[a]))
		    || (cptr->c_axlevel > cmdax))
			return (0);
	}
	return (1);
}


/*
 * This function returns 1 if a given command requires a string input
 */
int requires_string(struct citcmd *cptr, int ncomp)
{
	int a;
	char buf[64];

	strcpy(buf, cptr->c_keys[ncomp - 1]);
	for (a = 0; !IsEmptyStr(&buf[a]); ++a) {
		if (buf[a] == ':')
			return (1);
	}
	return (0);
}


/*
 * Input a command at the main prompt.
 * This function returns an integer command number.  If the command prompts
 * for a string then it is placed in the supplied buffer.
 */
int getcmd(CtdlIPC * ipc, char *argbuf)
{
	char cmdbuf[5];
	int cmdspaces[5];
	int cmdpos;
	int ch;
	int a;
	int got;
	int this_lazy_cmd;
	struct citcmd *cptr;

	/*
	 * Starting a new command now, so set sigcaught to 0.  This variable
	 * is set to nonzero (usually NEXT_KEY or STOP_KEY) if a command has
	 * been interrupted by a keypress.
	 */
	sigcaught = 0;

	/* Switch color support on or off if we're in user mode */
	if (rc_ansi_color == 3) {
		if (userflags & US_COLOR)
			enable_color = 1;
		else
			enable_color = 0;
	}
	/* if we're running in idiot mode, display a cute little menu */

	IFNEXPERT {
		scr_printf("-----------------------------------------------------------------------\n");
		scr_printf("Room cmds:    <K>nown rooms, <G>oto next room, <.G>oto a specific room,\n");
		scr_printf("              <S>kip this room, <A>bandon this room, <Z>ap this room,\n");
		scr_printf("              <U>ngoto (move back)\n");
		scr_printf("Message cmds: <N>ew msgs, <F>orward read, <R>everse read, <O>ld msgs,\n");
		scr_printf("              <L>ast five msgs, <E>nter a message\n");
		scr_printf("General cmds: <?> help, <T>erminate, <C>hat, <W>ho is online\n");
		scr_printf("Misc:         <X> toggle eXpert mode, <D>irectory\n");
		scr_printf("\n");
		scr_printf(" (Type .Help SUMMARY for extended commands, <X> to hide this menu)\n");
		scr_printf("-----------------------------------------------------------------------\n");
	}

	print_instant();
	strcpy(argbuf, "");
	cmdpos = 0;
	for (a = 0; a < 5; ++a)
		cmdbuf[a] = 0;
	/* now the room prompt... */
	ok_to_interrupt = 1;
	color(BRIGHT_WHITE);
	scr_printf("\n%s", room_name);
	color(DIM_WHITE);
	scr_printf("%c ", room_prompt(room_flags));

	while (1) {
		ch = inkey();
		ok_to_interrupt = 0;

		/* Handle the backspace key, but only if there's something
		 * to backspace over...
		 */
		if ((ch == 8) && (cmdpos > 0)) {
			back(cmdspaces[cmdpos - 1] + 1);
			cmdbuf[cmdpos] = 0;
			--cmdpos;
		}
		/* Spacebar invokes "lazy traversal" commands */
		if ((ch == 32) && (cmdpos == 0)) {
			this_lazy_cmd = next_lazy_cmd;
			if (this_lazy_cmd == 13)
				next_lazy_cmd = 5;
			if (this_lazy_cmd == 5)
				next_lazy_cmd = 13;
			for (cptr = cmdlist; cptr != NULL; cptr = cptr->next) {
				if (cptr->c_cmdnum == this_lazy_cmd) {
					for (a = 0; a < 5; ++a)
						if (cptr->c_keys[a][0] != 0)
							scr_printf("%s ", cmd_expand(cptr->c_keys[a], 0));
					scr_printf("\n");
					return (this_lazy_cmd);
				}
			}
			scr_printf("\n");
			return (this_lazy_cmd);
		}
		/* Otherwise, process the command */
		cmdbuf[cmdpos] = tolower(ch);

		for (cptr = cmdlist; cptr != NULL; cptr = cptr->next) {
			if (cmdmatch(cmdbuf, cptr, cmdpos + 1)) {

				scr_printf("%s", cmd_expand(cptr->c_keys[cmdpos], 0));
				cmdspaces[cmdpos] = strlen(cmd_expand(cptr->c_keys[cmdpos], 0));
				if (cmdpos < 4)
					if ((cptr->c_keys[cmdpos + 1]) != 0)
						scr_putc(' ');
				++cmdpos;
			}
		}

		for (cptr = cmdlist; cptr != NULL; cptr = cptr->next) {
			if (cmdmatch(cmdbuf, cptr, 5)) {
				/* We've found our command. */
				if (requires_string(cptr, cmdpos)) {
					argbuf[0] = 0;
					ctdl_getline(argbuf, 64, 0, 0);
				} else {
					scr_printf("\n");
				}

				/* If this command is one that changes rooms,
				 * then the next lazy-command (space bar)
				 * should be "read new" instead of "goto"
				 */
				if ((cptr->c_cmdnum == 5)
				    || (cptr->c_cmdnum == 6)
				    || (cptr->c_cmdnum == 47)
				    || (cptr->c_cmdnum == 52)
				    || (cptr->c_cmdnum == 16)
				    || (cptr->c_cmdnum == 20))
					next_lazy_cmd = 13;

				/* If this command is "read new"
				 * then the next lazy-command (space bar)
				 * should be "goto"
				 */
				if (cptr->c_cmdnum == 13)
					next_lazy_cmd = 5;

				return (cptr->c_cmdnum);

			}
		}

		if (ch == '?') {
			scr_printf("\rOne of ...                         \n");
			for (cptr = cmdlist; cptr != NULL; cptr = cptr->next) {
				if (cmdmatch(cmdbuf, cptr, cmdpos)) {
					for (a = 0; a < 5; ++a) {
						keyopt(cmd_expand(cptr->c_keys[a], 1));
						scr_printf(" ");
					}
					scr_printf("\n");
				}
			}
			sigcaught = 0;

			scr_printf("\n%s%c ", room_name, room_prompt(room_flags));
			got = 0;
			for (cptr = cmdlist; cptr != NULL; cptr = cptr->next) {
				if ((got == 0) && (cmdmatch(cmdbuf, cptr, cmdpos))) {
					for (a = 0; a < cmdpos; ++a) {
						scr_printf("%s ", cmd_expand(cptr->c_keys[a], 0));
					}
					got = 1;
				}
			}
		}
	}

}





/*
 * set tty modes.  commands are:
 * 
 * 01- set to Citadel mode
 * 2 - save current settings for later restoral
 * 3 - restore saved settings
 */
void stty_ctdl(int cmd)
{				/* SysV version of stty_ctdl() */
	struct termios live;
	static struct termios saved_settings;
	static int last_cmd = 0;

	if (cmd == SB_LAST)
		cmd = last_cmd;
	else
		last_cmd = cmd;

	if ((cmd == 0) || (cmd == 1)) {
		tcgetattr(0, &live);
		live.c_iflag = ISTRIP | IXON | IXANY;
		live.c_oflag = OPOST | ONLCR;
		live.c_lflag = ISIG | NOFLSH;

		live.c_cc[VINTR] = 0;
		live.c_cc[VQUIT] = 0;

		/* do we even need this stuff anymore? */
		/* live.c_line=0; */
		live.c_cc[VERASE] = 8;
		live.c_cc[VKILL] = 24;
		live.c_cc[VEOF] = 1;
		live.c_cc[VEOL] = 255;
		live.c_cc[VEOL2] = 0;
		live.c_cc[VSTART] = 0;
		tcsetattr(0, TCSADRAIN, &live);
	}
	if (cmd == 2) {
		tcgetattr(0, &saved_settings);
	}
	if (cmd == 3) {
		tcsetattr(0, TCSADRAIN, &saved_settings);
	}

}


// this is the old version which uses sgtty.h instead of termios.h
#if 0
void stty_ctdl(int cmd)
{				/* BSD version of stty_ctdl() */
	struct sgttyb live;
	static struct sgttyb saved_settings;
	static int last_cmd = 0;

	if (cmd == SB_LAST)
		cmd = last_cmd;
	else
		last_cmd = cmd;

	if ((cmd == 0) || (cmd == 1)) {
		gtty(0, &live);
		live.sg_flags |= CBREAK;
		live.sg_flags |= CRMOD;
		live.sg_flags |= NL1;
		live.sg_flags &= ~ECHO;
		if (cmd == 1)
			live.sg_flags |= NOFLSH;
		stty(0, &live);
	}
	if (cmd == 2) {
		gtty(0, &saved_settings);
	}
	if (cmd == 3) {
		stty(0, &saved_settings);
	}
}
#endif


/*
 * display_help()  -  help text viewer
 */
void display_help(CtdlIPC * ipc, char *name)
{
	int i;
	int num_helps = sizeof(helpnames) / sizeof(char *);

	for (i = 0; i < num_helps; ++i) {
		if (!strcasecmp(name, helpnames[i])) {
			fmout(screenwidth, NULL, helptexts[i], NULL, 0);
			return;
		}
	}

	scr_printf("'%s' not found.  Enter one of:\n", name);
	for (i = 0; i < num_helps; ++i) {
		scr_printf("  %s\n", helpnames[i]);
	}
}


/*
 * fmout() - Citadel text formatter and paginator
 */
int fmout(int width,		/* screen width to use */
	  FILE * fpin,		/* file to read from, or NULL to format given text */
	  char *text,		/* text to be formatted (when fpin is NULL */
	  FILE * fpout,		/* file to write to, or NULL to write to screen */
	  int subst) {		/* nonzero if we should use hypertext mode */
	char *buffer = NULL;	/* The current message */
	char *word = NULL;	/* What we are about to actually print */
	char *e;		/* Pointer to position in text */
	char old = 0;		/* The previous character */
	int column = 0;		/* Current column */
	size_t i;		/* Generic counter */

	/* Space for a single word, which can be at most screenwidth */
	word = (char *) calloc(1, width);
	if (!word) {
		scr_printf("Can't alloc memory to print message: %s!\n", strerror(errno));
		logoff(NULL, 3);
	}

	/* Read the entire message body into memory */
	if (fpin) {
		buffer = load_message_from_file(fpin);
		if (!buffer) {
			scr_printf("Can't print message: %s!\n", strerror(errno));
			logoff(NULL, 3);
		}
	} else {
		buffer = text;
	}
	e = buffer;

	/* Run the message body */
	while (*e) {
		/* Catch characters that shouldn't be there at all */
		if (*e == '\r') {
			e++;
			continue;
		}
		/* First, are we looking at a newline? */
		if (*e == '\n') {
			e++;
			if (*e == ' ') {	/* Paragraph */
				if (fpout) {
					fprintf(fpout, "\n");
				} else {
					scr_printf("\n");
				}
				column = 0;
			} else if (old != ' ') {	/* Don't print two spaces */
				if (fpout) {
					fprintf(fpout, " ");
				} else {
					scr_printf(" ");
				}
				column++;
			}
			old = '\n';
			continue;
		}

		/* Are we looking at a nonprintable?
		 * (This section is now commented out because we could be displaying
		 * a character set like UTF-8 or ISO-8859-1.)
		 if ( (*e < 32) || (*e > 126) ) {
		 e++;
		 continue;
		 } */

		/* Or are we looking at a space? */
		if (*e == ' ') {
			e++;
			if (column >= width - 1) {
				/* Are we in the rightmost column? */
				if (fpout) {
					fprintf(fpout, "\n");
				} else {
					scr_printf("\n");
				}
				column = 0;
			} else if (!(column == 0 && old == ' ')) {
				/* Eat only the first space on a line */
				if (fpout) {
					fprintf(fpout, " ");
				} else {
					scr_printf(" ");
				}
				column++;
			}
			/* ONLY eat the FIRST space on a line */
			old = ' ';
			continue;
		}
		old = *e;

		/* Read a word, slightly messy */
		i = 0;
		while (e[i]) {
			if (!isprint(e[i]) && !isspace(e[i]))
				e[i] = ' ';
			if (isspace(e[i]))
				break;
			i++;
		}

		/* We should never see these, but... slightly messy */
		if (e[i] == '\t' || e[i] == '\f' || e[i] == '\v')
			e[i] = ' ';

		/* Break up really long words */
		/* TODO: auto-hyphenation someday? */
		if (i >= width)
			i = width - 1;
		strncpy(word, e, i);
		word[i] = 0;

		/* Decide where to print the word */
		if (column + i >= width) {
			/* Wrap to the next line */
			if (fpout) {
				fprintf(fpout, "\n");
			} else {
				scr_printf("\n");
			}
			column = 0;
		}

		/* Print the word */
		if (fpout) {
			fprintf(fpout, "%s", word);
		} else {
			scr_printf("%s", word);
		}
		column += i;
		e += i;		/* Start over with the whitepsace! */
	}

	free(word);
	if (fpin)		/* We allocated this, remember? */
		free(buffer);

	/* Is this necessary?  It makes the output kind of spacey. */
	if (fpout) {
		fprintf(fpout, "\n");
	} else {
		scr_printf("\n");
	}

	return sigcaught;
}


/*
 * support ANSI color if defined
 */
void color(int colornum)
{
	static int hold_color;
	static int current_color;

	if (colornum == COLOR_PUSH) {
		hold_color = current_color;
		return;
	}

	if (colornum == COLOR_POP) {
		color(hold_color);
		return;
	}

	current_color = colornum;
	if (enable_color) {
		/* When switching to dim white, actually output an 'original
		 * pair' sequence -- this looks better on black-on-white
		 * terminals. - Changed to ORIGINAL_PAIR as this actually
		 * wound up looking horrible on black-on-white terminals, not
		 * to mention transparent terminals.
		 */
		if (colornum == ORIGINAL_PAIR)
			printf("\033[0;39;49m");
		else
			printf("\033[%d;3%d;4%dm", (colornum & 8) ? 1 : 0, (colornum & 7), rc_color_use_bg);

	}
}

void cls(int colornum)
{
	if (enable_color) {
		printf("\033[4%dm\033[2J\033[H\033[0m", colornum ? colornum : rc_color_use_bg);
	}
}


/*
 * Detect whether ANSI color is available (answerback)
 */
void send_ansi_detect(void)
{
	if (rc_ansi_color == 2) {
		printf("\033[c");
		scr_flush();
		time(&AnsiDetect);
	}
}

void look_for_ansi(void)
{
	fd_set rfds;
	struct timeval tv;
	char abuf[512];
	time_t now;
	int a, rv;

	if (rc_ansi_color == 0) {
		enable_color = 0;
	} else if (rc_ansi_color == 1) {
		enable_color = 1;
	} else if (rc_ansi_color == 2) {

		/* otherwise, do the auto-detect */

		strcpy(abuf, "");

		time(&now);
		if ((now - AnsiDetect) < 2)
			sleep(1);

		do {
			FD_ZERO(&rfds);
			FD_SET(0, &rfds);
			tv.tv_sec = 0;
			tv.tv_usec = 1;

			select(1, &rfds, NULL, NULL, &tv);
			if (FD_ISSET(0, &rfds)) {
				abuf[strlen(abuf) + 1] = 0;
				rv = read(0, &abuf[strlen(abuf)], 1);
				if (rv < 0) {
					scr_printf("failed to read after select: %s", strerror(errno));
					break;
				}
			}
		} while (FD_ISSET(0, &rfds));

		for (a = 0; !IsEmptyStr(&abuf[a]); ++a) {
			if ((abuf[a] == 27) && (abuf[a + 1] == '[')
			    && (abuf[a + 2] == '?')) {
				enable_color = 1;
			}
		}
	}
}


/*
 * Display key options (highlight hotkeys inside angle brackets)
 */
void keyopt(char *buf)
{
	int i;

	color(DIM_WHITE);
	for (i = 0; !IsEmptyStr(&buf[i]); ++i) {
		if (buf[i] == '<') {
			scr_printf("%c", buf[i]);
			color(BRIGHT_MAGENTA);
		} else {
			if (buf[i] == '>' && buf[i + 1] != '>') {
				color(DIM_WHITE);
			}
			scr_printf("%c", buf[i]);
		}
	}
	color(DIM_WHITE);
}



/*
 * Present a key-menu line choice type of thing
 */
char keymenu(char *menuprompt, char *menustring)
{
	int i, c, a;
	int choices;
	int do_prompt = 0;
	char buf[1024];
	int ch;
	int display_prompt = 1;

	choices = num_tokens(menustring, '|');

	if (menuprompt != NULL)
		do_prompt = 1;
	if ((menuprompt != NULL) && (IsEmptyStr(menuprompt)))
		do_prompt = 0;

	while (1) {
		if (display_prompt) {
			if (do_prompt) {
				scr_printf("%s ", menuprompt);
			} else {
				for (i = 0; i < choices; ++i) {
					extract_token(buf, menustring, i, '|', sizeof buf);
					keyopt(buf);
					scr_printf(" ");
				}
			}
			scr_printf("-> ");
			display_prompt = 0;
		}
		ch = lkey();

		if ((do_prompt) && (ch == '?')) {
			scr_printf("\rOne of...                               ");
			scr_printf("                                      \n");
			for (i = 0; i < choices; ++i) {
				extract_token(buf, menustring, i, '|', sizeof buf);
				scr_printf("   ");
				keyopt(buf);
				scr_printf("\n");
			}
			scr_printf("\n");
			display_prompt = 1;
		}

		for (i = 0; i < choices; ++i) {
			extract_token(buf, menustring, i, '|', sizeof buf);
			for (c = 1; !IsEmptyStr(&buf[c]); ++c) {
				if ((ch == tolower(buf[c]))
				    && (buf[c - 1] == '<')
				    && (buf[c + 1] == '>')) {
					for (a = 0; !IsEmptyStr(&buf[a]); ++a) {
						if ((a != (c - 1)) && (a != (c + 1))) {
							scr_putc(buf[a]);
						}
					}
					scr_printf("\n");
					return ch;
				}
			}
		}
	}
}
