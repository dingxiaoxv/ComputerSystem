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