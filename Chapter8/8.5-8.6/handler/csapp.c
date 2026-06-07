#include "csapp.h"

void unix_error(char *msg) {
  fprintf(stderr, "%s: %s\n", msg, strerror(errno));
  exit(0);
}

pid_t try_fork() {
  pid_t pid;
  if ((pid = fork()) < 0) {
    unix_error("fork error");
  }

  return pid;
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

/* sio_strlen - 自己算长度，不依赖可能不可重入的库函数 */
static size_t sio_strlen(char s[]) {
  size_t i = 0;
  while (s[i] != '\0') {
    ++i;
  }
  return i;
}

/* sio_puts - 异步信号安全地输出字符串（直接用 write 系统调用） */
ssize_t sio_puts(char s[]) {
  return write(STDOUT_FILENO, s, sio_strlen(s));
}

/* sio_error - 异步信号安全地输出错误信息并退出（_exit 不刷缓冲、不跑 atexit） */
void sio_error(char s[]) {
  sio_puts(s);
  _exit(1);
}