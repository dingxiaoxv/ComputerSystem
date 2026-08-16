#define _GNU_SOURCE

#include "../../../Chapter11/socket/net.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <unistd.h>

#define MAX_EVENTS 1024
#define READ_BUFFER_SIZE 8192
#define INITIAL_OUTPUT_CAPACITY 4096U
#define MAX_OUTPUT_CAPACITY (1024U * 1024U)

struct connection {
  int fd;
  char *output;
  size_t output_capacity;
  size_t output_begin;
  size_t output_end;
  bool peer_eof;
};

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

static int set_nonblocking(int fd) {
  int flags = fcntl(fd, F_GETFL, 0);
  if (flags < 0) {
    return -1;
  }
  if ((flags & O_NONBLOCK) != 0) {
    return 0;
  }
  return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static size_t pending_output(const struct connection *conn) {
  return conn->output_end - conn->output_begin;
}

static uint32_t connection_events(const struct connection *conn) {
  uint32_t events = EPOLLET;
  if (!conn->peer_eof) {
    events |= EPOLLIN | EPOLLRDHUP;
  }
  if (pending_output(conn) > 0) {
    events |= EPOLLOUT;
  }
  return events;
}

static int add_listenfd(int epfd, int listenfd) {
  struct epoll_event ev;
  memset(&ev, 0, sizeof(ev));
  ev.events = EPOLLIN | EPOLLET;
  ev.data.ptr = NULL; /* NULL 专门表示 listenfd 事件。 */
  return epoll_ctl(epfd, EPOLL_CTL_ADD, listenfd, &ev);
}

static int add_connection(int epfd, struct connection *conn) {
  struct epoll_event ev;
  memset(&ev, 0, sizeof(ev));
  ev.events = connection_events(conn);
  ev.data.ptr = conn;
  return epoll_ctl(epfd, EPOLL_CTL_ADD, conn->fd, &ev);
}

static int modify_connection(int epfd, struct connection *conn) {
  struct epoll_event ev;
  memset(&ev, 0, sizeof(ev));
  ev.events = connection_events(conn);
  ev.data.ptr = conn;
  return epoll_ctl(epfd, EPOLL_CTL_MOD, conn->fd, &ev);
}

static void close_connection(int epfd, struct connection *conn) {
  if (epoll_ctl(epfd, EPOLL_CTL_DEL, conn->fd, NULL) < 0 && errno != EBADF &&
      errno != ENOENT) {
    perror("epoll_ctl DEL");
  }
  close(conn->fd);
  free(conn->output);
  free(conn);
}

static int append_output(struct connection *conn, const char *data, size_t length) {
  size_t pending = pending_output(conn);
  if (length > MAX_OUTPUT_CAPACITY - pending) {
    errno = ENOBUFS;
    return -1;
  }

  if (conn->output_begin > 0 && conn->output_end + length > conn->output_capacity) {
    memmove(conn->output, conn->output + conn->output_begin, pending);
    conn->output_begin = 0;
    conn->output_end = pending;
  }

  size_t required = conn->output_end + length;
  if (required > conn->output_capacity) {
    size_t capacity = conn->output_capacity;
    if (capacity == 0) {
      capacity = INITIAL_OUTPUT_CAPACITY;
    }
    while (capacity < required) {
      if (capacity > MAX_OUTPUT_CAPACITY / 2) {
        capacity = MAX_OUTPUT_CAPACITY;
      } else {
        capacity *= 2;
      }
    }

    char *new_output = realloc(conn->output, capacity);
    if (new_output == NULL) {
      return -1;
    }
    conn->output = new_output;
    conn->output_capacity = capacity;
  }

  memcpy(conn->output + conn->output_end, data, length);
  conn->output_end += length;
  return 0;
}

/* 把输出缓冲区写到全部完成或 write 返回 EAGAIN。 */
static int flush_output(struct connection *conn) {
  while (pending_output(conn) > 0) {
    ssize_t n = write(conn->fd, conn->output + conn->output_begin,
                      pending_output(conn));
    if (n > 0) {
      conn->output_begin += (size_t)n;
      continue;
    }
    if (n < 0 && errno == EINTR) {
      continue;
    }
    if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
      return 0;
    }
    return -1;
  }

  conn->output_begin = 0;
  conn->output_end = 0;
  return 0;
}

/* ET 读路径必须一直读到 EAGAIN；短读不能表示 socket 已经读空。 */
static int drain_input(struct connection *conn) {
  char buf[READ_BUFFER_SIZE];
  size_t total = 0;

  while (1) {
    ssize_t n = read(conn->fd, buf, sizeof(buf));
    if (n > 0) {
      total += (size_t)n;
      if (append_output(conn, buf, (size_t)n) < 0) {
        if (errno == ENOBUFS) {
          fprintf(stderr, "epoll ET server: fd %d output buffer exceeds %u bytes\n",
                  conn->fd, MAX_OUTPUT_CAPACITY);
        } else {
          perror("append_output");
        }
        return -1;
      }

      /* 先尝试立即回显；写不完的数据留在 output 中等待 EPOLLOUT。 */
      if (flush_output(conn) < 0) {
        perror("write");
        return -1;
      }
      continue;
    }

    if (n == 0) {
      conn->peer_eof = true;
      printf("epoll ET server: fd %d read %zu bytes, peer closed write side\n",
             conn->fd, total);
      return 0;
    }

    if (errno == EINTR) {
      continue;
    }
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      if (total > 0) {
        printf("epoll ET server: fd %d drained %zu bytes to EAGAIN\n", conn->fd,
               total);
      }
      return 0;
    }

    perror("read");
    return -1;
  }
}

/* ET 监听路径同样要循环 accept4，直到 accept queue 返回 EAGAIN。 */
static int drain_accept_queue(int epfd, int listenfd) {
  while (1) {
    struct sockaddr_storage clientaddr;
    socklen_t clientlen = sizeof(clientaddr);
    int connfd = accept4(listenfd, (struct sockaddr *)&clientaddr, &clientlen,
                         SOCK_NONBLOCK | SOCK_CLOEXEC);
    if (connfd >= 0) {
      struct connection *conn = calloc(1, sizeof(*conn));
      if (conn == NULL) {
        perror("calloc connection");
        close(connfd);
        continue;
      }
      conn->fd = connfd;

      if (add_connection(epfd, conn) < 0) {
        perror("epoll_ctl ADD connfd");
        close_connection(epfd, conn);
        continue;
      }

      char host[256], serv[32];
      int rc = getnameinfo((struct sockaddr *)&clientaddr, clientlen, host,
                           sizeof(host), serv, sizeof(serv),
                           NI_NUMERICHOST | NI_NUMERICSERV);
      if (rc == 0) {
        printf("epoll ET server accepted fd %d from %s:%s\n", connfd, host, serv);
      } else {
        printf("epoll ET server accepted fd %d\n", connfd);
      }
      continue;
    }

    if (errno == EINTR) {
      continue;
    }
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      return 0;
    }

    perror("accept4");
    return -1;
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
  if (set_nonblocking(listenfd) < 0) {
    perror("set_nonblocking listenfd");
    close(listenfd);
    exit(1);
  }

  int epfd = epoll_create1(EPOLL_CLOEXEC);
  if (epfd < 0) {
    perror("epoll_create1");
    close(listenfd);
    exit(1);
  }
  if (add_listenfd(epfd, listenfd) < 0) {
    perror("epoll_ctl ADD listenfd");
    close(epfd);
    close(listenfd);
    exit(1);
  }

  struct epoll_event events[MAX_EVENTS];
  bool running = true;
  while (running) {
    int nready = epoll_wait(epfd, events, MAX_EVENTS, -1);
    if (nready < 0) {
      if (errno == EINTR) {
        continue;
      }
      perror("epoll_wait");
      break;
    }

    for (int i = 0; i < nready; ++i) {
      uint32_t ready = events[i].events;
      struct connection *conn = events[i].data.ptr;

      if (conn == NULL) {
        if (ready & (EPOLLERR | EPOLLHUP)) {
          fprintf(stderr, "epoll ET server: listenfd error/hangup\n");
          running = false;
          break;
        }
        if ((ready & EPOLLIN) && drain_accept_queue(epfd, listenfd) < 0) {
          running = false;
          break;
        }
        continue;
      }

      if (ready & EPOLLERR) {
        fprintf(stderr, "epoll ET server: fd %d error event\n", conn->fd);
        close_connection(epfd, conn);
        continue;
      }

      bool failed = false;
      if (ready & (EPOLLIN | EPOLLRDHUP | EPOLLHUP)) {
        failed = drain_input(conn) < 0;
      }
      if (!failed && (ready & EPOLLOUT)) {
        if (flush_output(conn) < 0) {
          perror("write");
          failed = true;
        }
      }

      if (failed || (conn->peer_eof && pending_output(conn) == 0)) {
        close_connection(epfd, conn);
        continue;
      }

      /* 仅在确有待发送数据时监听 EPOLLOUT，避免 socket 常态可写导致空转。 */
      if (modify_connection(epfd, conn) < 0) {
        perror("epoll_ctl MOD connfd");
        close_connection(epfd, conn);
      }
    }
  }

  close(epfd);
  close(listenfd);
  return 0;
}
