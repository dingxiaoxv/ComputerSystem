CC = gcc
CFLAGS = -Wall -Wextra -I../socket -I../../Chapter10/rio

RIO_DIR = ../../Chapter10/rio
NET_H = ../socket/net.h

all: tiny cgi-bin/adder

tiny: tiny.c $(RIO_DIR)/rio.c $(RIO_DIR)/rio.h $(NET_H)
	$(CC) $(CFLAGS) -o $@ tiny.c $(RIO_DIR)/rio.c

cgi-bin/adder: cgi-bin/adder.c
	$(CC) $(CFLAGS) -o $@ cgi-bin/adder.c

clean:
	rm -f tiny cgi-bin/adder

.PHONY: all clean
