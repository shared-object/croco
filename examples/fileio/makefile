CC     = gcc
CFLAGS = -Wall -Wextra -ggdb -I../..

all: fileio

fileio: fileio.c ../../croco.h
	$(CC) $(CFLAGS) fileio.c -o fileio

clean:
	rm -f fileio

.PHONY: all clean
