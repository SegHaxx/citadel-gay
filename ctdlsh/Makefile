include config.mk

OBJS := datetime.o export.o main.o passwd.o shutdown.o sockets.o who.o config.o

# link
ctdlsh: $(OBJS) config.mk
	gcc $(OBJS) $(LDFLAGS) -lreadline -o ctdlsh

# pull in dependency info for *existing* .o files
-include $(OBJS:.o=.d)

# compile and generate dependency info
%.o: %.c
	gcc -c $(CFLAGS) $*.c -o $*.o
	gcc -MM $(CFLAGS) $*.c > $*.d

config.mk:
	./configure

clean:
	rm -f ctdlsh *.o *.d

distclean: clean
	rm -f config.mk

install: ctdlsh
	echo Not yet
