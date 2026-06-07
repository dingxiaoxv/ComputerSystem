#ifndef CSAPP_H
#define CSAPP_H

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define MAXBUF 8192

/* 信号处理程序的类型：接收信号编号，无返回值 */
typedef void handler_t(int);

/* 出错报告：打印 msg 和 errno 对应的描述后退出 */
void unix_error(char *msg);

/* 带错误检查的 fgets：读到 EOF 返回 NULL（正常），仅 I/O 出错才退出 */
char *try_gets(char *s, int size, FILE *stream);

/* 异步信号安全 I/O：信号处理程序里只能用这类函数，不能用 printf */
ssize_t sio_puts(char s[]);
ssize_t sio_putl(long v);
void sio_error(char s[]);

/* 进程控制包装函数（try_ 前缀：内部检查返回值，出错即调用 unix_error 退出） */
pid_t try_fork(void);
void try_execve(const char *filename, char *const argv[], char *const envp[]);

/* 信号包装函数 */
handler_t *try_signal(int signum, handler_t *handler);
void try_sigprocmask(int how, const sigset_t *set, sigset_t *oldset);
void try_sigemptyset(sigset_t *set);
void try_sigfillset(sigset_t *set);
void try_sigaddset(sigset_t *set, int signum);

#endif /* CSAPP_H */
