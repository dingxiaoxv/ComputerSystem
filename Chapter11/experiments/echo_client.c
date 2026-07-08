/* 最小 echo 客户端：从 stdin 逐行读，发给 server，再收回并打印 */
#include "net.h"

#define MAXLINE 1024

int main(int argc, char **argv) {
  if (argc != 3) {
    fprintf(stderr, "usage: %s <host> <port>\n", argv[0]);
    exit(1);
  }

  int clientfd = open_clientfd(argv[1], argv[2]);
  if (clientfd < 0) {
    fprintf(stderr, "open_clientfd failed\n");
    exit(1);
  }

  char buf[MAXLINE];
  while (fgets(buf, sizeof(buf), stdin) != NULL) {
    size_t len = strlen(buf);
    if (write(clientfd, buf, len) != (ssize_t)len) {
      break;
    }
    /* 最小 demo：假设一次 read 就能收到整行回显（本地小消息成立） */
    ssize_t n = read(clientfd, buf, sizeof(buf) - 1);
    if (n <= 0) {
      break;
    }
    buf[n] = '\0';
    fputs(buf, stdout);
  }

  close(clientfd);
  return 0;
}
