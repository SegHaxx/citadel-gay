CFLAGS := -Wformat-truncation=0 -ggdb -DHAVE_OPENSSL
LDFLAGS :=  -lssl -lcrypto -lz
PREFIX := /usr/local
BINDIR := /usr/local/bin
CTDLDIR := /usr/local/citadel
