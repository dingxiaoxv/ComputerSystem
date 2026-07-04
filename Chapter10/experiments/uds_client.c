/*
 * uds_client.c —— Unix Domain Socket 回显客户端（SOCK_STREAM）
 *
 * 从标准输入逐行读，发给服务器，再用 RIO 读回显打印。
 * connect 之后，socket 就是一个普通的双向字节流 fd，读写和文件无异。
 *
 * 与网络客户端（第 11 章）的唯一区别同样只在地址：AF_UNIX + 路径。
 *
 *   运行：./uds_client [socket_path]   默认 /tmp/csapp_uds.sock
 *         路径以 '@' 开头 → 抽象命名空间（须与服务器一致）
 *   例：  printf 'hello\nworld\n' | ./uds_client
 */
#include "rio.h"
#include <stddef.h> /* offsetof */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#define DEFAULT_PATH "/tmp/csapp_uds.sock"

static void die(const char *msg) {
  perror(msg);
  exit(1);
}

/* 与 uds_server.c 的 fill_uds_addr 保持一致：'@' 前缀走抽象命名空间。
 * 客户端与服务器必须用【完全相同】的填法和 addrlen 才能连上。 */
static socklen_t fill_uds_addr(struct sockaddr_un *addr, const char *name) {
  memset(addr, 0, sizeof(*addr));
  addr->sun_family = AF_UNIX;
  if (name[0] == '@') {
    const char *abs = name + 1;
    size_t len = strlen(abs);
    if (len + 1 > sizeof(addr->sun_path))
      die("abstract name too long");
    addr->sun_path[0] = '\0';
    memcpy(addr->sun_path + 1, abs, len);
    return offsetof(struct sockaddr_un, sun_path) + 1 + len;
  }
  size_t len = strlen(name);
  if (len >= sizeof(addr->sun_path))
    die("socket path too long");
  memcpy(addr->sun_path, name, len);
  return sizeof(*addr);
}

int main(int argc, char *argv[]) {
  const char *sock_path = (argc > 1) ? argv[1] : DEFAULT_PATH;

  /* 1) 创建 socket：地址族/类型必须和服务器一致 */
  int fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (fd < 0)
    die("socket");

  /* 2) 填服务器地址（路径式或 '@' 抽象式，必须和服务器一致） */
  struct sockaddr_un addr;
  socklen_t addrlen = fill_uds_addr(&addr, sock_path);

  /* 3) connect：向监听 socket 发起连接 */
  if (connect(fd, (struct sockaddr *)&addr, addrlen) < 0)
    die("connect");

  /* 4) 收发循环：stdin 一行 -> 发送 -> 读回显 -> 打印 */
  rio_t rio;
  rio_initb(&rio, fd);
  char line[RIO_BUFSIZE];
  while (fgets(line, sizeof(line), stdin)) {
    ssize_t len = strlen(line);
    if (rio_writen(fd, line, len) != len)
      die("rio_writen");

    ssize_t n = rio_readlineb(&rio, line, sizeof(line));
    if (n <= 0) {
      fprintf(stderr, "[client] server closed\n");
      break;
    }
    line[n] = '\0';
    printf("[client] echo: %s", line); /* 回显里已带 \n */
  }

  close(fd); /* 关闭后服务器那边 rio_readlineb 返回 0（EOF） */
  return 0;
}
