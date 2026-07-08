/* 最小 echo 服务器：单连接串行处理，把收到的字节原样回写 */
#include "net.h"

#define MAXLINE 1024

int main(int argc, char **argv) {
  if (argc != 2) {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }

  int listenfd = open_serverfd(argv[1]);
  if (listenfd < 0) {
    fprintf(stderr, "open_serverfd failed\n");
    exit(1);
  }

  while (1) {
    struct sockaddr_storage clientaddr; /* 协议无关，够装 IPv4/IPv6 */
    socklen_t clientlen = sizeof(clientaddr);
    int connfd = accept(listenfd, (struct sockaddr *)&clientaddr, &clientlen);
    if (connfd < 0) {
      continue;
    }

    char host[256], serv[32];
    getnameinfo((struct sockaddr *)&clientaddr, clientlen, host, sizeof(host), serv,
                sizeof(serv), NI_NUMERICHOST | NI_NUMERICSERV);
    fprintf(stdout, "connected from %s:%s\n", host, serv);

    char buf[MAXLINE];
    ssize_t n;
    /* read 返回 0 表示对端关闭；TCP 是字节流，这里对最小 demo 直接回写整段 */
    while ((n = read(connfd, buf, sizeof(buf))) > 0) {
      write(connfd, buf, n);
      fprintf(stdout, "echoed %zd bytes\n", n);
    }
    close(connfd);
    fprintf(stdout, "connection closed\n");
  }

  return 0;
}
