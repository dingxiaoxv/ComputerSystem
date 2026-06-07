CC      = gcc
CFLAGS  = -Wall -Wextra -g

.PHONY: all clean run

all: procmask

procmask: procmask.c csapp.c joblist.c
	$(CC) $(CFLAGS) -o $@ $^

run: procmask
	./procmask

clean:
	rm -f procmask
