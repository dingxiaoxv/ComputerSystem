/*
 * passfd_send.c —— 【独立进程】版传 fd：发送端
 *
 * 和 uds_passfd.c（socketpair+fork 父子版）对照：这里用【命名 UDS】建立连接，
 * 所以通信双方是两个各自 ./ 启动、毫无亲缘关系的进程。传 fd 的机制完全一样，
 * send_fd() 和父子版里的一模一样——传 fd 只依赖一条已连通的 AF_UNIX 连接，
 * 跟连接怎么建立、两端有无亲缘关系都无关。
 *
 * 流程：bind 一个 UDS 路径 -> listen -> accept 等接收端连上来 ->
 *       open 一个文件 -> 把这个文件的 fd 用 SCM_RIGHTS 发过去。
 *
 *   编译：见 Makefile 的 `make passfd2`
 *   运行：./passfd_send [sock_path] [file_to_send]
 *         默认 /tmp/csapp_passfd.sock  /etc/hostname
 *
 *   两终端演示：
 *     终端1: ./passfd_send
 *     终端2: ./passfd_recv
 */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#define DEFAULT_SOCK "/tmp/csapp_passfd.sock"
#define DEFAULT_FILE "/etc/hostname"

static void die(const char *msg) {
  perror(msg);
  exit(1);
}

/* 把 fd_to_send 通过 sock 发出去。fd 不放普通数据缓冲，而是塞进 cmsg（SCM_RIGHTS）。 */
static void send_fd(int sock, int fd_to_send) {
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
    die("sendmsg");
}

int main(int argc, char *argv[]) {
  const char *sock_path = (argc > 1) ? argv[1] : DEFAULT_SOCK;
  const char *file_path = (argc > 2) ? argv[2] : DEFAULT_FILE;

  setvbuf(stdout, NULL, _IONBF, 0);

  int listenfd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (listenfd < 0)
    die("socket");

  struct sockaddr_un addr;
  memset(&addr, 0, sizeof(addr));
  addr.sun_family = AF_UNIX;
  if (strlen(sock_path) >= sizeof(addr.sun_path))
    die("socket path too long");
  strncpy(addr.sun_path, sock_path, sizeof(addr.sun_path) - 1);

  unlink(sock_path); /* 清残留，否则 EADDRINUSE */
  if (bind(listenfd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    die("bind");
  if (listen(listenfd, 8) < 0)
    die("listen");
  printf("[send] listening on %s, 等接收端连接...\n", sock_path);

  int connfd = accept(listenfd, NULL, NULL);
  if (connfd < 0)
    die("accept");
  printf("[send] 接收端已连接 (connfd=%d)\n", connfd);

  /* 打开要传递的文件，把它的 fd 发过去 */
  int filefd = open(file_path, O_RDONLY);
  if (filefd < 0)
    die("open");
  printf("[send] opened %s as fd=%d, 发送该 fd...\n", file_path, filefd);

  send_fd(connfd, filefd);

  close(filefd); /* 本端立刻关掉，接收端照样能读——传的是内核对象 */
  close(connfd);
  close(listenfd);
  unlink(sock_path);
  printf("[send] done.\n");
  return 0;
}
