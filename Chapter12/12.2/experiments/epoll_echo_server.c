#define _GNU_SOURCE

#include "../../../Chapter11/socket/net.h"
#include "../../../Chapter10/rio/rio.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <unistd.h>

#define MAX_EVENTS 1024
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
    printf("epoll server received %zd bytes from fd %d\n", n, fd);
    if (rio_writen(fd, buf, (size_t)n) != n) {
      perror("rio_writen");
      return -1;
    }
    return 0;
  }

  if (n == 0) {
    printf("epoll server: fd %d closed by peer\n", fd);
    return -1;
  }

  if (errno == EINTR) {
    return 0;
  }

  perror("read");
  return -1;
}

static void add_epoll_fd(int epfd, int fd, uint32_t events) {
  struct epoll_event ev;
  memset(&ev, 0, sizeof(ev));
  ev.events = events;
  ev.data.fd = fd;

  if (epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev) < 0) {
    perror("epoll_ctl ADD");
    close(fd);
  }
}

static void close_epoll_fd(int epfd, int fd) {
  if (epoll_ctl(epfd, EPOLL_CTL_DEL, fd, NULL) < 0 && errno != EBADF &&
      errno != ENOENT) {
    perror("epoll_ctl DEL");
  }
  close(fd);
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

  int epfd = epoll_create1(EPOLL_CLOEXEC);
  if (epfd < 0) {
    perror("epoll_create1");
    close(listenfd);
    exit(1);
  }

  /* 本例使用 epoll 默认 LT 模式，便于和 select/poll 的 ready 语义对比。 */
  add_epoll_fd(epfd, listenfd, EPOLLIN);

  struct epoll_event events[MAX_EVENTS];
  while (1) {
    int nready = epoll_wait(epfd, events, MAX_EVENTS, -1);
    if (nready < 0) {
      if (errno == EINTR) {
        continue;
      }
      perror("epoll_wait");
      break;
    }

    for (int i = 0; i < nready; ++i) {
      int fd = events[i].data.fd;
      uint32_t ev = events[i].events;

      if (fd == listenfd) {
        struct sockaddr_storage clientaddr;
        socklen_t clientlen = sizeof(clientaddr);
        int connfd = accept(listenfd, (struct sockaddr *)&clientaddr, &clientlen);
        if (connfd < 0) {
          if (errno != EINTR) {
            perror("accept");
          }
          continue;
        }

        char host[256], serv[32];
        getnameinfo((struct sockaddr *)&clientaddr, clientlen, host, sizeof(host),
                    serv, sizeof(serv), NI_NUMERICHOST | NI_NUMERICSERV);
        printf("epoll server accepted fd %d from %s:%s\n", connfd, host, serv);

        add_epoll_fd(epfd, connfd, EPOLLIN | EPOLLRDHUP);
        continue;
      }

      if (ev & EPOLLERR) {
        fprintf(stderr, "epoll server: fd %d error event\n", fd);
        close_epoll_fd(epfd, fd);
        continue;
      }

      int close_it = 0;
      if (ev & EPOLLIN) {
        if (echo_once(fd) < 0) {
          close_it = 1;
        } else {
          /* HUP/RDHUP 可能和 EPOLLIN 同时出现；读到数据后先保留 fd，下一轮再读 EOF。 */
          continue;
        }
      }

      if (!close_it && (ev & (EPOLLHUP | EPOLLRDHUP))) {
        printf("epoll server: fd %d hangup event\n", fd);
        close_it = 1;
      }

      if (close_it) {
        close_epoll_fd(epfd, fd);
      }
    }
  }

  close(epfd);
  close(listenfd);
  return 0;
}
