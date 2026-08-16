#include "../../../Chapter11/socket/net.h"
#include "../../../Chapter10/rio/rio.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define MAXLINE 1024

/* 子进程退出时，内核会给父进程发送 SIGCHLD。
 * 父进程在 handler 里用 waitpid 回收，避免 child 变成 zombie。 */
static void sigchld_handler(int sig) {
  (void)sig;
  int olderrno = errno; /* handler 可能打断主流程，返回前恢复 errno。 */
  /* 一次 SIGCHLD 可能对应多个已退出子进程；WNOHANG 保证这里不阻塞。 */
  while (waitpid(-1, NULL, WNOHANG) > 0) {
    ;
  }
  errno = olderrno;
}

static void install_handlers(void) {
  struct sigaction sa;
  memset(&sa, 0, sizeof(sa));
  sa.sa_handler = sigchld_handler;
  sigemptyset(&sa.sa_mask);
  /* 让 accept 等可重启的阻塞系统调用在 handler 返回后尽量自动继续。 */
  sa.sa_flags = SA_RESTART;
  if (sigaction(SIGCHLD, &sa, NULL) < 0) {
    perror("sigaction(SIGCHLD)");
    exit(1);
  }

  struct sigaction ign;
  memset(&ign, 0, sizeof(ign));
  /* 客户端提前断开时，继续 write 可能触发 SIGPIPE；忽略它，改由 write 返回 EPIPE。 */
  ign.sa_handler = SIG_IGN;
  sigemptyset(&ign.sa_mask);
  if (sigaction(SIGPIPE, &ign, NULL) < 0) {
    perror("sigaction(SIGPIPE)");
    exit(1);
  }
}

static void echo(int connfd) {
  rio_t rio;
  char buf[MAXLINE];
  rio_initb(&rio, connfd);

  ssize_t n;
  while ((n = rio_readlineb(&rio, buf, sizeof(buf))) > 0) {
    printf("child %ld received %zd bytes\n", (long)getpid(), n);
    if (rio_writen(connfd, buf, (size_t)n) != n) {
      perror("rio_writen");
      break;
    }
  }

  if (n < 0) {
    perror("rio_readlineb");
  }
}

int main(int argc, char **argv) {
  if (argc != 2) {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }

  setvbuf(stdout, NULL, _IOLBF, 0);
  install_handlers();

  int listenfd = open_serverfd(argv[1]);
  if (listenfd < 0) {
    fprintf(stderr, "open_serverfd failed\n");
    exit(1);
  }

  while (1) {
    struct sockaddr_storage clientaddr;
    socklen_t clientlen = sizeof(clientaddr);
    int connfd = accept(listenfd, (struct sockaddr *)&clientaddr, &clientlen);
    if (connfd < 0) {
      if (errno == EINTR) {
        continue;
      }
      perror("accept");
      continue;
    }

    char host[256], serv[32];
    getnameinfo((struct sockaddr *)&clientaddr, clientlen, host, sizeof(host), serv,
                sizeof(serv), NI_NUMERICHOST | NI_NUMERICSERV);
    printf("parent %ld accepted %s:%s\n", (long)getpid(), host, serv);

    pid_t pid = fork();
    if (pid == 0) {
      close(listenfd);
      printf("child %ld serving %s:%s\n", (long)getpid(), host, serv);
      echo(connfd);
      close(connfd);
      printf("child %ld done\n", (long)getpid());
      _exit(0);
    }

    if (pid < 0) {
      perror("fork");
      close(connfd);
      continue;
    }

    close(connfd);
  }

  return 0;
}
