CFLAGS := -ggdb -Wno-format-truncation
LDFLAGS := 

# link
webcit: server/*.c
	gcc server/*.c $(LDFLAGS) -lcitadel -lpthread -lcrypto -lssl -lexpat -o webcit

# remove compilation products
clean:
	rm -f webcit

distclean: clean

# install to target directory
install:
	echo Not yet...
