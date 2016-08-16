OBJS := datetime.o export.o main.o passwd.o shutdown.o sockets.o who.o

CFLAGS := -ggdb

# link
ctdlsh: $(OBJS)
	gcc $(OBJS) -lreadline -o ctdlsh

# pull in dependency info for *existing* .o files
-include $(OBJS:.o=.d)

# compile and generate dependency info
%.o: %.c
	gcc -c $(CFLAGS) $*.c -o $*.o
	gcc -MM $(CFLAGS) $*.c > $*.d

# remove compilation products
clean:
	rm -f ctdlsh *.o *.d
