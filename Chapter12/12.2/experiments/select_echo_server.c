#include "../../../Chapter11/socket/net.h"
#include "../../../Chapter10/rio/rio.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <unistd.h>

#define MAXLINE 1024

static void install_handlers(void) {
  struct sigaction ign;
  memset(&ign, 0, sizeof(ign));
  ign.sa_handler = SIG_IGN;
  sigemptyset(&ign.sa_mask);
  if (sigaction(SIGPIPE, &ign, NULL) < 0) {
    perror("sigaction(SIGPIPE)");
    exit(1);
  }
}

static int echo_once(int fd) {
  char buf[MAXLINE];

  /* fd ready 只表示 read 不会长期阻塞，不表示一定有业务数据。 */
  ssize_t n = read(fd, buf, sizeof(buf));
  if (n > 0) {
    printf("select server received %zd bytes from fd %d\n", n, fd);
    if (rio_writen(fd, buf, (size_t)n) != n) {
      perror("rio_writen");
      return -1;
    }
    return 0;
  }

  if (n == 0) {
    printf("select server: fd %d closed by peer\n", fd);
    return -1;
  }

  if (errno == EINTR) {
    return 0;
  }

  perror("read");
  return -1;
}

static void recompute_maxfd(int *maxfd, const fd_set *all_reads) {
  while (*maxfd >= 0 && !FD_ISSET(*maxfd, all_reads)) {
    --(*maxfd);
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

  if (listenfd >= FD_SETSIZE) {
    fprintf(stderr, "listenfd %d >= FD_SETSIZE %d\n", listenfd, FD_SETSIZE);
    close(listenfd);
    exit(1);
  }

  fd_set all_reads;
  fd_set ready_reads;
  FD_ZERO(&all_reads);
  FD_SET(listenfd, &all_reads);
  int maxfd = listenfd;

  while (1) {
    /* select 会原地修改 fd_set，所以每轮必须从长期集合复制。 */
    ready_reads = all_reads;
    int nready = select(maxfd + 1, &ready_reads, NULL, NULL, NULL);
    if (nready < 0) {
      if (errno == EINTR) {
        continue;
      }
      perror("select");
      break;
    }

    if (FD_ISSET(listenfd, &ready_reads)) {
      struct sockaddr_storage clientaddr;
      socklen_t clientlen = sizeof(clientaddr);
      int connfd = accept(listenfd, (struct sockaddr *)&clientaddr, &clientlen);
      if (connfd < 0) {
        if (errno != EINTR) {
          perror("accept");
        }
      } else if (connfd >= FD_SETSIZE) {
        fprintf(stderr, "reject fd %d: exceeds FD_SETSIZE %d\n", connfd,
                FD_SETSIZE);
        close(connfd);
      } else {
        char host[256], serv[32];
        getnameinfo((struct sockaddr *)&clientaddr, clientlen, host, sizeof(host),
                    serv, sizeof(serv), NI_NUMERICHOST | NI_NUMERICSERV);
        printf("select server accepted fd %d from %s:%s\n", connfd, host, serv);

        FD_SET(connfd, &all_reads);
        if (connfd > maxfd) {
          maxfd = connfd;
        }
      }

      if (--nready == 0) {
        continue;
      }
    }

    for (int fd = 0; fd <= maxfd && nready > 0; ++fd) {
      if (fd == listenfd || !FD_ISSET(fd, &ready_reads)) {
        continue;
      }

      --nready;
      if (echo_once(fd) < 0) {
        close(fd);
        FD_CLR(fd, &all_reads);
        if (fd == maxfd) {
          recompute_maxfd(&maxfd, &all_reads);
        }
      }
    }
  }

  close(listenfd);
  return 0;
}
