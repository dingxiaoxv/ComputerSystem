/*
 * uds_client.c —— Unix Domain Socket 回显客户端（SOCK_STREAM）
 *
 * 从标准输入逐行读，发给服务器，再用 RIO 读回显打印。
 * connect 之后，socket 就是一个普通的双向字节流 fd，读写和文件无异。
 *
 * 与网络客户端（第 11 章）的唯一区别同样只在地址：AF_UNIX + 路径。
 *
 *   运行：./uds_client [socket_path]   默认 /tmp/csapp_uds.sock
 *   例：  printf 'hello\nworld\n' | ./uds_client
 */
#include "rio.h"
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

int main(int argc, char *argv[]) {
  const char *sock_path = (argc > 1) ? argv[1] : DEFAULT_PATH;

  /* 1) 创建 socket：地址族/类型必须和服务器一致 */
  int fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (fd < 0)
    die("socket");

  /* 2) 填服务器地址（同一个路径） */
  struct sockaddr_un addr;
  memset(&addr, 0, sizeof(addr));
  addr.sun_family = AF_UNIX;
  strncpy(addr.sun_path, sock_path, sizeof(addr.sun_path) - 1);

  /* 3) connect：向监听 socket 发起连接 */
  if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
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
