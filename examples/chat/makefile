CC     = gcc
CFLAGS = -Wall -Wextra -ggdb -I../..

all: server client

server: server.c ../../croco.h
	$(CC) $(CFLAGS) server.c -o server

client: client.c ../../croco.h
	$(CC) $(CFLAGS) client.c -o client

clean:
	rm -f server client

.PHONY: all clean
