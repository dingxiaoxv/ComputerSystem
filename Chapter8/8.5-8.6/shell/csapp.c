#include "csapp.h"

void unix_error(char *msg) {
  fprintf(stderr, "%s: %s\n", msg, strerror(errno));
  exit(1);
}

char *try_gets(char *s, int size, FILE *stream) {
  char *rptr;
  /* fgets 返回 NULL 有两种情况：读到 EOF（正常）或出错。
     只有 ferror 为真才是真正的 I/O 错误，需要报错退出。 */
  if ((rptr = fgets(s, size, stream)) == NULL && ferror(stream)) {
    unix_error("fgets error");
  }
  return rptr;
}

/* ---------- 异步信号安全 I/O（基于 K&R itoa，全程只用 write 系统调用） ---------- */

/* sio_strlen - 自己算长度，不依赖可能不可重入的库函数 */
static size_t sio_strlen(char s[]) {
  size_t i = 0;
  while (s[i] != '\0') {
    ++i;
  }
  return i;
}

/* sio_reverse - 原地反转字符串 */
static void sio_reverse(char s[], size_t len) {
  for (size_t i = 0, j = len - 1; i < j; i++, j--) {
    char c = s[i];
    s[i] = s[j];
    s[j] = c;
  }
}

/* sio_ltoa - 把 long 转成十进制字符串 */
static void sio_ltoa(long v, char s[]) {
  int neg = v < 0;
  size_t i = 0;
  if (neg) {
    v = -v;
  }
  do {
    s[i++] = (char)('0' + v % 10);
  } while ((v /= 10) > 0);
  if (neg) {
    s[i++] = '-';
  }
  s[i] = '\0';
  sio_reverse(s, i);
}

/* sio_puts - 异步信号安全地输出字符串 */
ssize_t sio_puts(char s[]) {
  return write(STDOUT_FILENO, s, sio_strlen(s));
}

/* sio_putl - 异步信号安全地输出一个 long */
ssize_t sio_putl(long v) {
  char s[128];
  sio_ltoa(v, s);
  return sio_puts(s);
}

/* sio_error - 异步信号安全地输出错误信息并退出（_exit 不刷缓冲、不跑 atexit） */
void sio_error(char s[]) {
  sio_puts(s);
  _exit(1);
}

/* ---------- 进程控制包装函数 ---------- */

pid_t try_fork(void) {
  pid_t pid;
  if ((pid = fork()) < 0) {
    unix_error("fork error");
  }
  return pid;
}

void try_execve(const char *filename, char *const argv[], char *const envp[]) {
  if (execve(filename, argv, envp) < 0) {
    unix_error("execve error");
  }
}

/* ---------- 信号包装函数 ---------- */

/* try_signal - 用 sigaction 安装处理程序，带 SA_RESTART 语义（自动重启被中断的系统调用） */
handler_t *try_signal(int signum, handler_t *handler) {
  struct sigaction action, old_action;

  action.sa_handler = handler;
  sigemptyset(&action.sa_mask); /* 处理本信号时阻塞同类信号 */
  action.sa_flags = SA_RESTART; /* 尽可能重启被中断的系统调用 */

  if (sigaction(signum, &action, &old_action) < 0) {
    unix_error("signal error");
  }
  return old_action.sa_handler;
}

void try_sigprocmask(int how, const sigset_t *set, sigset_t *oldset) {
  if (sigprocmask(how, set, oldset) < 0) {
    unix_error("sigprocmask error");
  }
}

void try_sigemptyset(sigset_t *set) {
  if (sigemptyset(set) < 0) {
    unix_error("sigemptyset error");
  }
}

void try_sigfillset(sigset_t *set) {
  if (sigfillset(set) < 0) {
    unix_error("sigfillset error");
  }
}

void try_sigaddset(sigset_t *set, int signum) {
  if (sigaddset(set, signum) < 0) {
    unix_error("sigaddset error");
  }
}
