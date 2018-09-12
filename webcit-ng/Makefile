OBJS := http.o main.o request.o ssl.o static.o tcp_sockets.o webserver.o ctdlclient.o admin_functions.o room_functions.o util.o caldav_reports.o messages.o ctdlfunctions.o ctdl_commands.o forum_view.o html2html.o text2html.o user_functions.o
CFLAGS := -ggdb
LDFLAGS := 

# link
webcit: $(OBJS)
	gcc $(OBJS) $(LDFLAGS) -lcitadel -lpthread -lcrypto -lssl -lexpat -o webcit

# pull in dependency info for *existing* .o files
-include $(OBJS:.o=.d)

# compile and generate dependency info
%.o: %.c
	gcc -c $(CFLAGS) $*.c -o $*.o
	gcc -MM $(CFLAGS) $*.c > $*.d

# remove compilation products
clean:
	rm -f webcit *.o *.d

distclean: clean

# install to target directory
install:
	echo Not yet...
