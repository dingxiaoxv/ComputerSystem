
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "csapp.h"

#define N 10

void no_order_wait() {
  int status, i;
  pid_t pid;

  for (i = 0; i < N; i++) {
    if ((pid = try_fork()) == 0) {
      exit(100 + i);
    }
  }

  /* parent reaps children in no particular order */
  while ((pid = waitpid(-1, &status, 0)) > 0) {
    if (WIFEXITED(status)) {
      printf("child %d terminated normally with exit status=%d\n", pid, WEXITSTATUS(status));
    } else {
      printf("child %d terminated abnormally\n", pid);
    }
  }
}

void order_wait() {
  int status, i;
  pid_t pid[N], retpid;

  for (i =0; i < N; i++) {
    if ((pid[i] = try_fork()) == 0) {
      exit(100 + i);
    }
  }

  i = 0;
  /* parent reaps children in order */
  i = 0;
  while ((retpid = waitpid(pid[i++], &status, 0)) > 0) {
    if (WIFEXITED(status)) {
      printf("child %d terminated normally with exit status=%d\n", pid, WEXITSTATUS(status));
    } else {
      printf("child %d terminated abnormally\n", pid);
    }
  }
}

int main() {
  order_wait();  

  if (errno != ECHILD) {
    unix_error("wait pid error");
  }

  exit(0);
}