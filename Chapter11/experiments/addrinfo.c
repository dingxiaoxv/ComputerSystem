#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

#define MAXLINE 1024

int main(int argc, char **argv) {
  if (argc != 2) {
    fprintf(stderr, "usage: %s <domain name>\n", argv[0]);
    exit(0);
  }

  struct addrinfo *p, *plist, hints;
  int rc;
  memset(&hints, 0, sizeof(struct addrinfo));
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  if ((rc = getaddrinfo(argv[1], NULL, &hints, &plist)) != 0) {
    fprintf(stderr, "getaddrinfo error: %s <domain name>\n", gai_strerror(rc));
    exit(1);
  }

  char buf[MAXLINE];
  for (p = plist; p; p = p->ai_next) {
    getnameinfo(p->ai_addr, p->ai_addrlen, buf, MAXLINE, NULL, 0, NI_NUMERICHOST);
    fprintf(stdout, "%s\n", buf);
  }

  freeaddrinfo(plist);
  exit(0);
}