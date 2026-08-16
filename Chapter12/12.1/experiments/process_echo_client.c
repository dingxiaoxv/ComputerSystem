/* 配套客户端：从 stdin 逐行发送给 server，再按行读回显 */
#include "../../../Chapter11/socket/net.h"
#include "../../../Chapter10/rio/rio.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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

  rio_t rio;
  rio_initb(&rio, clientfd);

  char buf[MAXLINE];
  while (fgets(buf, sizeof(buf), stdin) != NULL) {
    size_t len = strlen(buf);
    if (rio_writen(clientfd, buf, len) != (ssize_t)len) {
      perror("rio_writen");
      break;
    }

    ssize_t n = rio_readlineb(&rio, buf, sizeof(buf));
    if (n <= 0) {
      break;
    }
    fputs(buf, stdout);
  }

  close(clientfd);
  return 0;
}
