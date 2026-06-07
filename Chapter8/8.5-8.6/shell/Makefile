CC      = gcc
CFLAGS  = -Wall -Wextra -g

.PHONY: all clean

all: tsh

tsh: main.c csapp.c
	$(CC) $(CFLAGS) -o $@ $^

clean:
	rm -f tsh
