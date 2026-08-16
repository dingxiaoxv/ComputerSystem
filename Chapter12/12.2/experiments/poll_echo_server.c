/* poll 版单进程并发 echo 服务器：pollfd 数组保存 fd 和关注事件 */
#include "../../../Chapter11/socket/net.h"
#include "../../../Chapter10/rio/rio.h"

#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MAX_CLIENTS 1024
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

  ssize_t n = read(fd, buf, sizeof(buf));
  if (n > 0) {
    printf("poll server received %zd bytes from fd %d\n", n, fd);
    if (rio_writen(fd, buf, (size_t)n) != n) {
      perror("rio_writen");
      return -1;
    }
    return 0;
  }

  if (n == 0) {
    printf("poll server: fd %d closed by peer\n", fd);
    return -1;
  }

  if (errno == EINTR) {
    return 0;
  }

  perror("read");
  return -1;
}

static void remove_pollfd(struct pollfd fds[], nfds_t *nfds, nfds_t i) {
  close(fds[i].fd);
  fds[i] = fds[*nfds - 1];
  --(*nfds);
}

static void add_client(struct pollfd fds[], nfds_t *nfds, int connfd) {
  if (*nfds >= MAX_CLIENTS + 1) {
    fprintf(stderr, "too many clients, reject fd %d\n", connfd);
    close(connfd);
    return;
  }

  fds[*nfds].fd = connfd;
  fds[*nfds].events = POLLIN;
  fds[*nfds].revents = 0;
  ++(*nfds);
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

  struct pollfd fds[MAX_CLIENTS + 1];
  nfds_t nfds = 1;
  fds[0].fd = listenfd;
  fds[0].events = POLLIN;
  fds[0].revents = 0;

  while (1) {
    int nready = poll(fds, nfds, -1);
    if (nready < 0) {
      if (errno == EINTR) {
        continue;
      }
      perror("poll");
      break;
    }

    /* poll 返回 ready 数量，不是 ready 下标；结果写在每项 revents 里。 */
    if (fds[0].revents != 0) {
      if (fds[0].revents & POLLIN) {
        struct sockaddr_storage clientaddr;
        socklen_t clientlen = sizeof(clientaddr);
        int connfd = accept(listenfd, (struct sockaddr *)&clientaddr, &clientlen);
        if (connfd < 0) {
          if (errno != EINTR) {
            perror("accept");
          }
        } else {
          char host[256], serv[32];
          getnameinfo((struct sockaddr *)&clientaddr, clientlen, host,
                      sizeof(host), serv, sizeof(serv),
                      NI_NUMERICHOST | NI_NUMERICSERV);
          printf("poll server accepted fd %d from %s:%s\n", connfd, host, serv);
          add_client(fds, &nfds, connfd);
        }
      }

      if (--nready == 0) {
        continue;
      }
    }

    for (nfds_t i = 1; i < nfds && nready > 0; ++i) {
      short revents = fds[i].revents;
      if (revents == 0) {
        continue;
      }

      --nready;
      int close_it = 0;

      if (revents & (POLLIN | POLLHUP)) {
        if (echo_once(fds[i].fd) < 0) {
          close_it = 1;
        }
      }

      if (!close_it && (revents & (POLLERR | POLLNVAL))) {
        fprintf(stderr, "poll server: fd %d revents=0x%x\n", fds[i].fd,
                revents);
        close_it = 1;
      }

      if (close_it) {
        remove_pollfd(fds, &nfds, i);
        --i; /* 尾元素换到当前位置，本轮不能跳过它。 */
      }
    }
  }

  for (nfds_t i = 0; i < nfds; ++i) {
    close(fds[i].fd);
  }
  return 0;
}
