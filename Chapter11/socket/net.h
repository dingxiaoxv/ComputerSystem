#ifndef NET_H
#define NET_H

#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define LISTENQ 1024

static inline int open_clientfd(char *hostname, char *port) {
  struct addrinfo hints, *plist, *p;
  memset(&hints, 0, sizeof(struct addrinfo));
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_NUMERICSERV | AI_ADDRCONFIG;

  int rc;
  if ((rc = getaddrinfo(hostname, port, &hints, &plist)) != 0) {
    fprintf(stderr, "open_clientfd: getaddrinfo(%s:%s) error: %s\n", hostname, port,
            gai_strerror(rc));
    return -1;
  }

  int clientfd;
  for (p = plist; p; p = p->ai_next) {
    if ((clientfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) < 0) {
      continue;
    }

    if (connect(clientfd, p->ai_addr, p->ai_addrlen) != -1) {
      char host[256], serv[32];
      getnameinfo(p->ai_addr, p->ai_addrlen, host, sizeof(host), serv, sizeof(serv),
                  NI_NUMERICHOST | NI_NUMERICSERV);
      fprintf(stdout, "connected to %s:%s\n", host, serv);
      break;
    }
    close(clientfd);
  }

  freeaddrinfo(plist);
  if (!p) {
    return -1;
  } else {
    return clientfd;
  }
}

static inline int open_serverfd(char *port) {
  struct addrinfo hints, *plist, *p;
  memset(&hints, 0, sizeof(struct addrinfo));
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_NUMERICSERV | AI_PASSIVE | AI_ADDRCONFIG;

  int rc;
  if ((rc = getaddrinfo(NULL, port, &hints, &plist)) != 0) {
    fprintf(stderr, "open_serverfd: getaddrinfo(*:%s) error: %s\n", port, gai_strerror(rc));
    return -1;
  }

  int listenfd;
  for (p = plist; p; p = p->ai_next) {
    if ((listenfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) < 0) {
      continue;
    }

    int optval = 1;
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, (const void *)&optval, sizeof(int));

    if (bind(listenfd, p->ai_addr, p->ai_addrlen) == 0) {
      break;
    }

    close(listenfd);
  }

  freeaddrinfo(plist);
  if (!p) {
    return -1;
  }

  if (listen(listenfd, LISTENQ) < 0) {
    close(listenfd);
    return -1;
  }

  fprintf(stdout, "listening on port %s\n", port);
  return listenfd;
}

#endif