#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MAXBUF 8192

void unix_error(char *msg);
pid_t try_fork();
char *try_gets(char *s, int size, FILE *stream);

/* 异步信号安全的 I/O：信号处理程序里只能用这类函数，不能用 printf */
ssize_t sio_puts(char s[]);
void sio_error(char s[]);