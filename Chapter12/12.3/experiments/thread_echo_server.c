#include "../../../Chapter11/socket/net.h"
#include "../../../Chapter10/rio/rio.h"

#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MAXLINE 1024

static void ignore_sigpipe(void) {
  struct sigaction action;
  memset(&action, 0, sizeof(action));
  action.sa_handler = SIG_IGN;
  sigemptyset(&action.sa_mask);

  if (sigaction(SIGPIPE, &action, NULL) < 0) {
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
    printf("thread %lu received %zd bytes\n", (unsigned long)pthread_self(), n);
    if (rio_writen(connfd, buf, (size_t)n) != n) {
      perror("rio_writen");
      break;
    }
  }

  if (n < 0) {
    perror("rio_readlineb");
  }
}

static void *serve_client(void *arg) {
  int connfd = *(int *)arg;
  free(arg);

  int rc = pthread_detach(pthread_self());
  if (rc != 0) {
    fprintf(stderr, "pthread_detach: %s\n", strerror(rc));
  }

  printf("thread %lu serving fd %d\n", (unsigned long)pthread_self(), connfd);
  echo(connfd);

  if (close(connfd) < 0) {
    perror("close(connfd)");
  }
  printf("thread %lu done\n", (unsigned long)pthread_self());
  return NULL;
}

int main(int argc, char **argv) {
  if (argc != 2) {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }

  setvbuf(stdout, NULL, _IOLBF, 0);
  ignore_sigpipe();

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

    char host[256] = "?";
    char serv[32] = "?";
    int rc = getnameinfo((struct sockaddr *)&clientaddr, clientlen, host, sizeof(host), serv,
                         sizeof(serv), NI_NUMERICHOST | NI_NUMERICSERV);
    if (rc != 0) {
      fprintf(stderr, "getnameinfo: %s\n", gai_strerror(rc));
    }
    printf("main thread accepted %s:%s on fd %d\n", host, serv, connfd);

    int *connfdp = malloc(sizeof(*connfdp));
    if (connfdp == NULL) {
      perror("malloc");
      close(connfd);
      continue;
    }
    *connfdp = connfd;

    pthread_t tid;
    rc = pthread_create(&tid, NULL, serve_client, connfdp);
    if (rc != 0) {
      fprintf(stderr, "pthread_create: %s\n", strerror(rc));
      free(connfdp);
      close(connfd);
      continue;
    }

    /* 创建成功后，参数和 connfd 都归 worker；线程共享 fd table，main 不能关闭它。 */
  }

  return 0;
}
