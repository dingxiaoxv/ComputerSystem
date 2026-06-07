#include "csapp.h"
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

void handler(int sig) {
  int olderrno = errno;
  while (waitpid(-1, NULL, 0) > 0) {
    sio_puts("Handler reaped child\n");
  }

  if (errno != ECHILD) {
    sio_error("waitpid error");
  }

  sleep(1);
  errno = olderrno;
}

int main() {
  int i, n;
  char buf[MAXBUF];

  if (signal(SIGCHLD, handler) == SIG_ERR) {
    unix_error("signal error");
  }

  for (i = 0; i < 3; i++) {
    if (try_fork() == 0) {
      printf("Hello from child %d\n", (int)getpid());
      exit(0);
    }
  }

  while ((n = read(STDIN_FILENO, buf, sizeof(buf))) < 0) {
    if (errno != EINTR) /* 真出错才退出 */
      unix_error("read error");
    /* errno == EINTR：被信号打断，重试 */
  }

  printf("Parent prosessing input\n");
  while (1) {
  }

  exit(0);
}