#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

void unix_error(char *msg);
pid_t try_fork();
char *try_gets(char *s, int size, FILE *stream);