/*
 * uds_passfd.c —— 用 SCM_RIGHTS 在两个进程间传递一个打开的 fd
 *
 * 演示 UDS 独有、TCP 做不到的杀手锏：把一个「打开的 fd」通过 sendmsg 的
 * 辅助数据（ancillary data，cmsg，类型 SCM_RIGHTS）发给另一个进程。
 * 内核会在接收进程的描述符表里新建一项，指向【同一个打开文件表项】——
 * 等于跨进程做了一次 dup（对照 §10.8：fd 号不同，但打开文件表项、v-node
 * 相同，偏移量共享）。
 *
 * 本例用 socketpair + fork 把 C/S 那套 bind/listen/connect 全省掉，
 * 聚焦「传 fd」本身：
 *   - 父进程：open("/etc/hostname") 得到 fd，把它 send 给子进程；
 *   - 子进程：recv 到一个【新的 fd 号】，用它 read 出文件内容并打印。
 * 若「传的是数字」，子进程那个 fd 号在自己进程里根本无效；能读出内容，
 * 恰恰证明传过去的是内核对象。
 *
 *   编译：见 Makefile 的 `make passfd`
 *   运行：./uds_passfd            （无参数，自己 fork）
 */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

static void die(const char *msg) {
  perror(msg);
  exit(1);
}

/* 通过 UDS 把 fd_to_send 发到 sock 上。
 * 关键：fd 不放在普通数据缓冲里，而是放进 msg_control 的 cmsg（SCM_RIGHTS）。 */
static void send_fd(int sock, int fd_to_send) {
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
    die("sendmsg");
}

/* 从 sock 收一个 fd，返回【本进程里新的 fd 号】。 */
static int recv_fd(int sock) {
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
    die("recvmsg");

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

int main(void) {
  /* socketpair 一步造出一对已连通的 UDS，专给 fork 后的父子用 */
  int sv[2];
  if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) < 0)
    die("socketpair");

  pid_t pid = fork();
  if (pid < 0)
    die("fork");

  if (pid == 0) {
    /* ── 子进程：收 fd 并用它读文件 ── */
    close(sv[0]);
    int fd = recv_fd(sv[1]);
    printf("[child]  received fd = %d (自己进程里的新号)\n", fd);

    char content[256];
    ssize_t n = read(fd, content, sizeof(content) - 1);
    if (n < 0)
      die("read");
    content[n] = '\0';
    printf("[child]  read from that fd: %s", content);

    close(fd);
    close(sv[1]);
    exit(0); /* 用 exit 而非 _exit：要刷新 stdio 缓冲，否则管道下 printf 丢失 */
  } else {
    /* ── 父进程：打开文件，把 fd 发给子进程 ── */
    close(sv[1]);
    int fd = open("/etc/hostname", O_RDONLY);
    if (fd < 0)
      die("open");
    printf("[parent] opened /etc/hostname as fd = %d, sending...\n", fd);

    send_fd(sv[0], fd);
    close(fd); /* 父进程这就关掉，子进程照样能读——因为传的是内核对象 */
    close(sv[0]);
    wait(NULL);
  }
  return 0;
}
