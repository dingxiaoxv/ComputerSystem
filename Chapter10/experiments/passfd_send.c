/*
 * passfd_send.c —— 【独立进程】版传 fd：发送端
 *
 * 和 uds_passfd.c（socketpair+fork 父子版）对照：这里用【命名 UDS】建立连接，
 * 所以通信双方是两个各自 ./ 启动、毫无亲缘关系的进程。传 fd 的机制完全一样，
 * send_fd() 和父子版里的一模一样——传 fd 只依赖一条已连通的 AF_UNIX 连接，
 * 跟连接怎么建立、两端有无亲缘关系都无关。
 *
 * 流程：bind -> listen -> accept -> open 一个文件 -> 【先自己读掉前一段，把 offset
 *       往后推】-> 把这个 fd 用 SCM_RIGHTS 发过去。接收端不 open、不 lseek，直接 read
 *       就从 offset 处【接着】读到后半段——证明传过去的是同一个内核打开文件表项
 *       (struct file)，offset 是它的属性、随 fd 而行，而不是重新按路径 open 一份。
 *       （若只是想让对端读某文件，发个路径名让它自己 open 即可，根本不用传 fd；
 *        fd 传递的价值恰恰在这种“共享打开状态”做不到用路径名替代的地方。）
 *
 *   编译：见 Makefile 的 `make passfd2`
 *   运行：./passfd_send [sock_path] [file_to_send]
 *         默认 /tmp/csapp_passfd.sock  /etc/os-release
 *
 *   两终端演示：
 *     终端1: ./passfd_send
 *     终端2: ./passfd_recv
 */
#include "uds.h" /* uds_die / send_fd —— 与 uds_passfd.c / passfd_recv.c 共用 */
#include <fcntl.h>

#define DEFAULT_SOCK "/tmp/csapp_passfd.sock"
#define DEFAULT_FILE "/etc/hostname"

int main(int argc, char *argv[]) {
  const char *sock_path = (argc > 1) ? argv[1] : DEFAULT_SOCK;
  const char *file_path = (argc > 2) ? argv[2] : DEFAULT_FILE;

  setvbuf(stdout, NULL, _IONBF, 0);

  int listenfd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (listenfd < 0)
    uds_die("socket");

  struct sockaddr_un addr;
  memset(&addr, 0, sizeof(addr));
  addr.sun_family = AF_UNIX;
  if (strlen(sock_path) >= sizeof(addr.sun_path))
    uds_die("socket path too long");
  strncpy(addr.sun_path, sock_path, sizeof(addr.sun_path) - 1);

  unlink(sock_path); /* 清残留，否则 EADDRINUSE */
  if (bind(listenfd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    uds_die("bind");
  if (listen(listenfd, 8) < 0)
    uds_die("listen");
  printf("[send] listening on %s, 等接收端连接...\n", sock_path);

  int connfd = accept(listenfd, NULL, NULL);
  if (connfd < 0)
    uds_die("accept");
  printf("[send] 接收端已连接 (connfd=%d)\n", connfd);

  /* 打开要传递的文件，把它的 fd 发过去 */
  int filefd = open(file_path, O_RDONLY);
  if (filefd < 0)
    uds_die("open");
  printf("[send] opened %s as fd=%d, 发送该 fd...\n", file_path, filefd);

  send_fd(connfd, filefd);

  close(filefd); /* 本端立刻关掉，接收端照样能读——传的是内核对象 */
  close(connfd);
  close(listenfd);
  unlink(sock_path);
  printf("[send] done.\n");
  return 0;
}
