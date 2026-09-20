CC = gcc
CFLAGS = -Wall -Wextra -ggdb -I.

TARGETS = server client

all: $(TARGETS)

server: examples/server.c croco.h
	$(CC) $(CFLAGS) $< -o $@

client: examples/client.c croco.h
	$(CC) $(CFLAGS) $< -o $@

clean:
	rm -f $(TARGETS) examples/server examples/client

.PHONY: all clean
