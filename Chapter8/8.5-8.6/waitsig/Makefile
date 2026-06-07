CC      = gcc
CFLAGS  = -Wall -Wextra -g

.PHONY: all clean run

all: waitforsignal

waitforsignal: waitforsignal.c csapp.c
	$(CC) $(CFLAGS) -o $@ $^

run: waitforsignal
	./waitforsignal

clean:
	rm -f waitforsignal
