/*
 * passfd.h —— 通过 UDS 传递文件描述符的可复用工具（SCM_RIGHTS）
 *
 * send_fd / recv_fd 只依赖一条【已连通的 AF_UNIX 连接】，对两端进程有无
 * 亲缘关系【零要求】——这条连接是 socketpair 造的（父子），还是 bind/listen
 * /accept + connect 造的（两个独立进程），这两个函数都原样适用。
 *
 * cmsg 那段有对齐约束、最易写错，所以抽到这里只写一遍，两个程序共用。
 */
#ifndef PASSFD_H
#define PASSFD_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

static inline void pf_die(const char *msg) {
  perror(msg);
  exit(1);
}

/* 把 fd_to_send 通过 sock 发出去。fd 不放普通数据缓冲，而是塞进 cmsg（SCM_RIGHTS）。 */
static inline void send_fd(int sock, int fd_to_send) {
  /* 必须捎带 >=1 字节正常数据：多数内核实现下，没有正常数据的 sendmsg
   * 无法携带 SCM_RIGHTS。这里发一个占位字节。 */
  char dummy = '*';
  struct iovec iov = {.iov_base = &dummy, .iov_len = 1};

  /* cmsg 有对齐要求，缓冲区大小必须用 CMSG_SPACE 算；union 保证按 cmsghdr 对齐。 */
  union {
    char buf[CMSG_SPACE(sizeof(int))];
    struct cmsghdr align;
  } u;
  memset(&u, 0, sizeof(u));

  struct msghdr msg;
  memset(&msg, 0, sizeof(msg));
  msg.msg_iov = &iov;
  msg.msg_iovlen = 1;
  msg.msg_control = u.buf;
  msg.msg_controllen = sizeof(u.buf);

  struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
  cmsg->cmsg_level = SOL_SOCKET;
  cmsg->cmsg_type = SCM_RIGHTS;
  cmsg->cmsg_len = CMSG_LEN(sizeof(int));
  memcpy(CMSG_DATA(cmsg), &fd_to_send, sizeof(int));

  if (sendmsg(sock, &msg, 0) < 0)
    pf_die("sendmsg");
}

/* 从 sock 收一个 fd，返回【本进程里新的 fd 号】。 */
static inline int recv_fd(int sock) {
  char dummy;
  struct iovec iov = {.iov_base = &dummy, .iov_len = 1};

  union {
    char buf[CMSG_SPACE(sizeof(int))];
    struct cmsghdr align;
  } u;
  memset(&u, 0, sizeof(u));

  struct msghdr msg;
  memset(&msg, 0, sizeof(msg));
  msg.msg_iov = &iov;
  msg.msg_iovlen = 1;
  msg.msg_control = u.buf;
  msg.msg_controllen = sizeof(u.buf);

  ssize_t n = recvmsg(sock, &msg, 0);
  if (n < 0)
    pf_die("recvmsg");

  /* 取出 fd 前务必校验 level/type/len，别盲取 */
  struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
  if (cmsg == NULL || cmsg->cmsg_level != SOL_SOCKET ||
      cmsg->cmsg_type != SCM_RIGHTS || cmsg->cmsg_len != CMSG_LEN(sizeof(int))) {
    fprintf(stderr, "recv_fd: no valid SCM_RIGHTS cmsg\n");
    exit(1);
  }
  int fd;
  memcpy(&fd, CMSG_DATA(cmsg), sizeof(int));
  return fd;
}

#endif /* PASSFD_H */
