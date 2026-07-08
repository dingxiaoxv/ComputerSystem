/*
 * uds.h —— Chapter10 UDS 实验的公共代码
 *
 * 把 client / server / passfd 各 .c 里反复出现、逐字相同的几段收进来：
 *   - uds_die          出错打印 errno + 退出
 *   - fill_uds_addr    填 sockaddr_un（含 '@' 抽象命名空间分流），client/server 共用
 *   - send_fd/recv_fd  用 SCM_RIGHTS 传/收一个打开的 fd，passfd 三个版本共用
 *
 * 全部 static inline —— 允许被多个 .c 各自 #include、各留一份副本，不必单独编译成
 * .o 再链接，纯头文件即可复用（教学项目够用）；且未被某个 .c 用到的函数不会触发
 * -Wunused-function（inline 抑制了该告警）。
 *
 * 现代 C++ 想要的 RAII/异常版见 unix_socket.hpp，那是另一套世界，不走本头文件。
 */
#ifndef UDS_H
#define UDS_H

#include <stddef.h> /* offsetof */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

/* 出错即打印 + 退出。原先各文件里的 die() 逐字相同；uds_server.c 那版另做了
 * cleanup()，现已交由 atexit(cleanup) + 信号处理器兜底，故也统一到这里。 */
static inline void uds_die(const char *msg) {
  perror(msg);
  exit(1);
}

/* 填充 UDS 地址，返回该传给 bind/connect 的 addrlen。
 *   name 以 '@' 开头 → 抽象命名空间：首字节置 '\0'（这就是“抽象”的标志），
 *     名字放其后。addrlen 必须【精确】算，绝不能用 sizeof(*addr)——否则
 *     sun_path 后面的填零字节会全被算进名字，变成一堆尾随空字节的名字。
 *   否则 → 文件系统路径：可传整个结构，内核按 sun_path 里的 '\0' 截断。
 * 客户端与服务器必须用【完全相同】的填法和 addrlen 才能连上。 */
static inline socklen_t fill_uds_addr(struct sockaddr_un *addr, const char *name) {
  memset(addr, 0, sizeof(*addr));
  addr->sun_family = AF_UNIX;
  if (name[0] == '@') {
    const char *abs = name + 1; /* 去掉前导 '@' 标记，它只是本程序的约定 */
    size_t len = strlen(abs);
    if (len + 1 > sizeof(addr->sun_path)) /* 首字节 '\0' + 名字 */
      uds_die("abstract name too long");
    addr->sun_path[0] = '\0';
    memcpy(addr->sun_path + 1, abs, len);
    return offsetof(struct sockaddr_un, sun_path) + 1 + len;
  }
  size_t len = strlen(name);
  if (len >= sizeof(addr->sun_path))
    uds_die("socket path too long");
  memcpy(addr->sun_path, name, len);
  return sizeof(*addr);
}

/* 通过 UDS 把 fd_to_send 发到 sock 上。
 * 关键：fd 不放在普通数据缓冲里，而是放进 msg_control 的 cmsg（SCM_RIGHTS）。 */
static inline void send_fd(int sock, int fd_to_send) {
  /* 必须捎带至少 1 字节普通数据：不少内核实现下，没有正常数据的
   * sendmsg 无法携带 SCM_RIGHTS。这里发一个占位字节。 */
  char dummy = '*';
  struct iovec iov = {.iov_base = &dummy, .iov_len = 1};

  /* cmsg 有对齐要求，缓冲区大小必须用 CMSG_SPACE 算，不能手写 sizeof(int)。
   * union 是为了保证按 struct cmsghdr 对齐。 */
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

  /* 填第一个（也是唯一一个）控制消息头 */
  struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
  cmsg->cmsg_level = SOL_SOCKET; /* socket 层 */
  cmsg->cmsg_type = SCM_RIGHTS;  /* 「传的是 fd（权限）」 */
  cmsg->cmsg_len = CMSG_LEN(sizeof(int));
  memcpy(CMSG_DATA(cmsg), &fd_to_send, sizeof(int)); /* 把 fd 写进 cmsg 数据区 */

  if (sendmsg(sock, &msg, 0) < 0)
    uds_die("sendmsg");
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
    uds_die("recvmsg");

  /* 从辅助数据里把 fd 取出来，务必校验类型，别盲取 */
  struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
  if (cmsg == NULL || cmsg->cmsg_level != SOL_SOCKET || cmsg->cmsg_type != SCM_RIGHTS ||
      cmsg->cmsg_len != CMSG_LEN(sizeof(int))) {
    fprintf(stderr, "recv_fd: no valid SCM_RIGHTS cmsg\n");
    exit(1);
  }
  int fd;
  memcpy(&fd, CMSG_DATA(cmsg), sizeof(int));
  return fd;
}

#endif /* UDS_H */
