/* §8.5 优化版简易 shell —— 在 §8.4 版本基础上解决"后台作业变僵尸"的问题
 *
 * §8.4 原版的毛病：前台作业用 waitpid 回收了，但后台作业（bg）只 printf
 * 一个 pid 就返回，从此再没人 wait 它 —— 后台子进程一结束就变僵尸，
 * 一直占着进程表项，直到 shell 退出才被 init 收走。
 *
 * §8.5 的修复（综合用上本章三节技术）：
 *   · §8.5.5  安装 SIGCHLD handler，用 waitpid(-1,&st,WNOHANG) 循环回收
 *             所有已结束的子进程（前台 + 后台），从根上消灭僵尸；
 *   · §8.5.6  fork 之前先阻塞 SIGCHLD，addjob 之后再解除 —— 保证 addjob
 *             一定先于 handler 里的 deletejob，避免竞态删到不存在的 job；
 *   · §8.5.7  前台作业不能再裸 waitpid（会被 handler 抢着回收导致 ECHILD），
 *             改用 sigsuspend 显式等待 handler 把它回收。
 */

#include "csapp.h"

extern char **environ;

#define MAXARGS 128
#define MAXLINE 8192
#define MAXJOBS 16

/* ---------------- 作业表（只存在于 shell 进程内的状态） ---------------- */

struct job_t {
  pid_t pid;              /* 0 表示空槽 */
  int bg;                 /* 1=后台作业，0=前台作业 */
  char cmdline[MAXLINE];  /* 原始命令行，供 jobs 命令展示 */
};

static struct job_t jobs[MAXJOBS];

/* 前台作业是否已结束：由 handler 置 1，被 eval 的等待循环轮询 */
static volatile sig_atomic_t fg_done;

static void init_jobs(void) {
  for (int i = 0; i < MAXJOBS; i++) {
    jobs[i].pid = 0;
  }
}

/* 加入作业。调用方必须已阻塞 SIGCHLD（与 handler 的 delete_job 互斥） */
static void add_job(pid_t pid, int bg, const char *cmdline) {
  for (int i = 0; i < MAXJOBS; i++) {
    if (jobs[i].pid == 0) {
      jobs[i].pid = pid;
      jobs[i].bg = bg;
      strcpy(jobs[i].cmdline, cmdline);
      return;
    }
  }
  fprintf(stderr, "作业表已满\n");
}

/* 删除作业，返回它的 bg 标志（0/1）；没找到返回 -1。
   在 handler 里调用，调用方已阻塞所有信号。 */
static int delete_job(pid_t pid) {
  for (int i = 0; i < MAXJOBS; i++) {
    if (jobs[i].pid == pid) {
      int bg = jobs[i].bg;
      jobs[i].pid = 0;
      return bg;
    }
  }
  return -1;
}

/* 打印作业表。调用方必须已阻塞 SIGCHLD。 */
static void list_jobs(void) {
  for (int i = 0; i < MAXJOBS; i++) {
    if (jobs[i].pid != 0) {
      printf("(%d) %s %s", (int)jobs[i].pid,
             jobs[i].bg ? "后台" : "前台", jobs[i].cmdline);
    }
  }
}

/* ---------------- SIGCHLD 处理程序：统一回收所有子进程 ---------------- */

void sigchld_handler(int sig) {
  int olderrno = errno;
  sigset_t mask_all, prev;
  pid_t pid;
  int status;
  (void)sig;

  try_sigfillset(&mask_all);
  /* WNOHANG：只回收"已经结束"的，不阻塞等待还在运行的子进程。
     循环是因为一次 SIGCHLD 可能对应多个同时结束的子进程（信号不排队）。 */
  while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
    try_sigprocmask(SIG_BLOCK, &mask_all, &prev); /* 访问 jobs 前阻塞所有信号 */
    int bg = delete_job(pid);
    try_sigprocmask(SIG_SETMASK, &prev, NULL);

    if (bg == 0) {
      fg_done = 1; /* 前台作业结束，唤醒 eval 里的 sigsuspend 等待 */
    } else if (bg == 1) {
      /* 后台作业结束：只能用异步信号安全的 sio_* 通知 */
      sio_puts("[bg] (");
      sio_putl(pid);
      sio_puts(") 已结束\n");
    }
  }
  if (pid < 0 && errno != ECHILD) {
    sio_error("waitpid error");
  }
  errno = olderrno;
}

/* ---------------- 命令行解析与求值 ---------------- */

int parseline(char *buf, char **argv) {
  char *delim;
  int argc;
  int bg;

  buf[strlen(buf) - 1] = ' '; /* 把结尾的 '\n' 换成空格 */
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

  if (argc == 0) {
    return 1; /* 空行 */
  }

  if ((bg = (*argv[argc - 1] == '&')) != 0) { /* 结尾 & 表示后台运行 */
    argv[--argc] = NULL;
  }
  return bg;
}

/*
 * 内置命令：需要读写"只存在于 shell 进程内"的状态，无法 fork 出去执行。
 * 本例实现 quit 和 jobs，真实 shell 还有 cd / fg / bg / kill 等。
 */
int builtin_command(char **argv) {
  if (!strcmp(argv[0], "quit")) { /* 终止 shell 自身 */
    exit(0);
  }
  if (!strcmp(argv[0], "jobs")) { /* 打印作业表，访问前阻塞 SIGCHLD */
    sigset_t mask_all, prev;
    try_sigfillset(&mask_all);
    try_sigprocmask(SIG_BLOCK, &mask_all, &prev);
    list_jobs();
    try_sigprocmask(SIG_SETMASK, &prev, NULL);
    return 1;
  }
  if (!strcmp(argv[0], "&")) { /* 忽略孤立的 & */
    return 1;
  }
  return 0; /* 非内置命令，交给 eval 去 fork + execve */
}

void eval(char *cmdline) {
  char *argv[MAXARGS];
  char buf[MAXLINE];
  int bg;
  pid_t pid;
  sigset_t mask_chld, mask_all, prev;

  strcpy(buf, cmdline);
  bg = parseline(buf, argv);
  if (argv[0] == NULL) {
    return; /* 空行 */
  }
  if (builtin_command(argv)) {
    return;
  }

  try_sigemptyset(&mask_chld);
  try_sigaddset(&mask_chld, SIGCHLD);
  try_sigfillset(&mask_all);

  /* fork 前阻塞 SIGCHLD，避免子进程瞬间结束时 handler 抢在 add_job 之前运行 */
  try_sigprocmask(SIG_BLOCK, &mask_chld, &prev);

  if ((pid = try_fork()) == 0) {                 /* 子进程 */
    try_sigprocmask(SIG_SETMASK, &prev, NULL);   /* 恢复掩码，别让 SIGCHLD 一直被阻塞 */
    if (execve(argv[0], argv, environ) < 0) {
      fprintf(stderr, "%s: 命令未找到\n", argv[0]);
      _exit(1);                                  /* _exit：不刷新继承自父进程的缓冲区 */
    }
  }

  /* 父进程：访问共享 jobs 前升级到阻塞所有信号 */
  try_sigprocmask(SIG_BLOCK, &mask_all, NULL);
  add_job(pid, bg, cmdline);
  try_sigprocmask(SIG_SETMASK, &mask_chld, NULL); /* 降回只阻塞 SIGCHLD */

  if (!bg) {
    /* 前台作业：用 sigsuspend 显式等待 handler 把它回收。
       此刻 SIGCHLD 仍被阻塞，先把标志清零再进等待，杜绝竞态。 */
    fg_done = 0;
    while (!fg_done) {
      sigsuspend(&prev); /* 原子地"解除 SIGCHLD 阻塞 + 挂起" */
    }
    try_sigprocmask(SIG_SETMASK, &prev, NULL); /* 完全解除阻塞 */
  } else {
    printf("[bg] (%d) %s", (int)pid, cmdline);
    try_sigprocmask(SIG_SETMASK, &prev, NULL); /* 解除阻塞 */
  }
}

int main(void) {
  char cmdline[MAXLINE];

  init_jobs();
  try_signal(SIGCHLD, sigchld_handler); /* 装好回收僵尸的处理程序 */

  while (1) {
    printf("> ");
    fflush(stdout);
    try_gets(cmdline, MAXLINE, stdin);
    if (feof(stdin)) {
      exit(0);
    }
    eval(cmdline);
  }
}
