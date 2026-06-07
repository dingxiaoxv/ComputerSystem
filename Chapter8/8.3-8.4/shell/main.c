#include "csapp.h"
#include <sys/wait.h>

extern char **environ;

#define MAXARGS 128
#define MAXLINE 8192

int parseline(char *buf, char **argv) {
  char *delim;
  int argc;
  int bg;

  /* replace trailing '\n' with space */
  buf[strlen(buf) - 1] = ' ';

  /* ignore leading spaces */
  while (*buf && (*buf == ' ')) {
    buf++;
  }

  argc = 0;
  while ((delim = strchr(buf, ' '))) {
    argv[argc++] = buf;
    *delim = '\0';
    buf = delim + 1;
    while (*buf && (*buf == ' ')) {
      buf++;
    }
  }
  argv[argc] = NULL;

  /* blank line */
  if (argc == 0)
    return 1; // blank line

  /* run in background */
  if ((bg = (*argv[argc - 1] == '&')) != 0) {
    argv[--argc] = NULL;
  }

  return bg;
}

/*
 * 内置命令 vs 外部程序的判据：命令是否需要修改 shell 自身进程的状态。
 *   - 需要改 shell 自己 → 必须内置（fork 出去改的是子进程，对父 shell 无效）
 *   - 不需要           → 交给 fork + execve 当外部程序执行
 *
 * 本例程只实现了 quit。真实 shell（及 CSAPP tsh lab）还应内置：
 *   quit / exit      —— 终止 shell 自身（fork 出去 exit 的是子进程，父 shell 不退）
 *   cd <dir>         —— chdir 改 shell 自己的工作目录（系统中无 /bin/cd 程序）
 *   export VAR=val   —— 修改 shell 自己的环境变量
 *   jobs             —— 打印 shell 内存里维护的作业表（子进程看不到这张表）
 *   fg %n / bg %n    —— 把作业切到前台/后台，操作作业表 + 发信号
 *   kill %n          —— 给指定作业发信号
 * 这些都因为要读写"只存在于 shell 进程内"的状态，所以无法 fork 出去执行。
 */
int builtin_command(char **argv) {
  if (!strcmp(argv[0], "quit")) /* quit：直接终止 shell 自身 */
    exit(0);
  if (!strcmp(argv[0], "&")) /* 忽略被误当成命令名的孤立 & */
    return 1;
  return 0; /* 非内置命令，返回 0 交给 eval 去 fork+execve */
}

void eval(char *cmdline) {
  char *argv[MAXARGS];
  char buf[MAXLINE];
  int bg = 0;
  pid_t pid;

  strcpy(buf, cmdline);
  bg = parseline(buf, argv);

  if (argv[0] == NULL) {
    return; // ignore empty line
  }

  if (builtin_command(argv))
    return;

  /* 子进程：执行用户命令 */
  if ((pid = try_fork()) == 0) {
    if (execve(argv[0], argv, environ) < 0) {
      printf("%s: command not found.\n", argv[0]);
      exit(0);
    }
  }

  /* 父进程：前台作业等待回收，后台作业打印 pid 后直接返回 */
  if (!bg) {
    int status;
    if (waitpid(pid, &status, 0) < 0) {
      unix_error("waitfg: waitpid error");
    }
  } else {
    printf("%d %s", pid, cmdline);
  }
}

int main() {
  char cmdline[MAXLINE];

  while (1) {
    printf("> ");
    try_gets(cmdline, MAXLINE, stdin);
    if (feof(stdin)) exit(0);

    eval(cmdline);
  }
}