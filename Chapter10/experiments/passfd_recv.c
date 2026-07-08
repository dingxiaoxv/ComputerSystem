/*
 * passfd_recv.c —— 【独立进程】版传 fd：接收端
 *
 * connect 到发送端的命名 UDS，收下一个 fd，用它读出文件内容。
 * 收到的 fd 号几乎必然和发送端不同，但读出的是同一个文件——证明传的是
 * 内核对象（打开文件表项），不是数字本身。
 *
 * recv_fd() 和父子版 uds_passfd.c 里的一模一样：传 fd 只依赖一条已连通的
 * AF_UNIX 连接，跟连接怎么建立、两端有无亲缘关系都无关。
 *
 *   运行：./passfd_recv [sock_path]   默认 /tmp/csapp_passfd.sock
 */
#include "uds.h" /* uds_die / recv_fd —— 与 uds_passfd.c / passfd_send.c 共用 */

#define DEFAULT_SOCK "/tmp/csapp_passfd.sock"

int main(int argc, char *argv[]) {
  const char *sock_path = (argc > 1) ? argv[1] : DEFAULT_SOCK;

  setvbuf(stdout, NULL, _IONBF, 0);

  int fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (fd < 0)
    uds_die("socket");

  struct sockaddr_un addr;
  memset(&addr, 0, sizeof(addr));
  addr.sun_family = AF_UNIX;
  strncpy(addr.sun_path, sock_path, sizeof(addr.sun_path) - 1);

  if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    uds_die("connect");
  printf("[recv] connected to %s\n", sock_path);

  int passed = recv_fd(fd);
  printf("[recv] 收到 fd=%d (本进程里的新号)\n", passed);

  char content[256];
  ssize_t n = read(passed, content, sizeof(content) - 1);
  if (n < 0)
    uds_die("read");
  content[n] = '\0';
  printf("[recv] 用该 fd 读到内容: %s", content);

  close(passed);
  close(fd);
  return 0;
}
