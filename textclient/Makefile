# MAKEFILE FOR CITADEL TEXT CLIENT
# This file is part of "conf-IG-ure"
# Copyright (C) 2016-2022 by Art Cancro
#
# This program is open source software.  Use, duplication, and/or
# disclosure are subject to the GNU General Purpose License version 3.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.

# config.mk is generated by ./configure
include config.mk

OBJS := citadel.o citadel_ipc.o client_chat.o client_passwords.o commands.o ipc_c_tcp.o messages.o rooms.o routines2.o routines.o screen.o tuiconfig.o


# link
citadel: $(OBJS) config.mk
	gcc $(OBJS) $(LDFLAGS) -lcitadel -o citadel

# pull in dependency info for *existing* .o files
-include $(OBJS:.o=.d)

# compile and generate dependency info
%.o: %.c
	gcc -c $(CFLAGS) -DCTDLDIR=\"$(CTDLDIR)\" $*.c -o $*.o
	gcc -MM $(CFLAGS) $*.c > $*.d

config.mk:
	./configure

clean:
	rm -f citadel *.o *.d

distclean: clean
	rm -f config.mk

install: citadel citadel.rc
	install citadel $(BINDIR)/citadel
	install citadel.rc $(ETCDIR)/citadel.rc

uninstall:
	rm -vf $(BINDIR)/citadel
