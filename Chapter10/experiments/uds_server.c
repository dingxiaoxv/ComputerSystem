/*
 * uds_server.c —— Unix Domain Socket 回显服务器（SOCK_STREAM）
 *
 * 主线：socket 也是 fd，SOCK_STREAM 语义和 TCP 一样（字节流、有 short count），
 *       所以直接复用本章的 RIO 包按行读，把读到的行大写化后回显。
 *
 * 与 AF_INET 网络编程（第 11 章）的唯一区别，全部集中在「地址」上：
 *   - 地址族用 AF_UNIX 而不是 AF_INET
 *   - 地址是文件系统路径（struct sockaddr_un.sun_path），不是 IP + 端口
 * 其余 socket/bind/listen/accept 流程与网络编程完全一致。
 *
 *   编译：见 Makefile 的 `make uds`
 *   运行：./uds_server [socket_path]   默认 /tmp/csapp_uds.sock
 */
#include "rio.h"
#include <ctype.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h> /* struct sockaddr_un —— UDS 专属的地址结构 */
#include <unistd.h>

#define DEFAULT_PATH "/tmp/csapp_uds.sock"
#define BACKLOG 8

static const char *g_sock_path = DEFAULT_PATH;

/* 退出前删除 socket 文件：bind 会在文件系统里落一个 s 类型的文件，
 * 不清理，下次 bind 同一路径会得到 EADDRINUSE。 */
static void cleanup(void) { unlink(g_sock_path); }

/* 信号处理器：Ctrl-C / kill 时也要删掉 socket 文件后再退出。
 * 只能调异步信号安全函数（unlink/_exit 安全，printf/exit 不安全）；
 * _exit 不会触发 atexit，所以这里必须显式再 cleanup 一次。 */
static void on_signal(int sig) {
  (void)sig;
  cleanup();
  _exit(0);
}

static void die(const char *msg) {
  perror(msg);
  cleanup();
  exit(1);
}

static void buf_toupper(char *buf, size_t len) {
  for (size_t i = 0; i < len; i++)
    buf[i] = (char)toupper((unsigned char)buf[i]);
}

int main(int argc, char *argv[]) {
  if (argc > 1)
    g_sock_path = argv[1];

  /* stdout 一旦被重定向/管道就默认全缓冲，而本进程走信号 _exit 退出不刷缓冲，
   * 日志会全部丢失。设为无缓冲，让每次 printf 立即落地，不依赖退出路径。 */
  setvbuf(stdout, NULL, _IONBF, 0);

  /* 1) 创建 socket：AF_UNIX + SOCK_STREAM = 本机字节流 */
  int listenfd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (listenfd < 0)
    die("socket");

  /* 2) 填地址结构。UDS 的地址就是一个文件系统路径。
   *    memset 清零很关键：bind 靠 sun_path 里的 '\0' 定位路径结尾，
   *    不清零会读到栈上垃圾。sun_path 通常仅 108 字节，超长会被静默截断，故先查长度。 */
  struct sockaddr_un addr;
  memset(&addr, 0, sizeof(addr));
  addr.sun_family = AF_UNIX;
  if (strlen(g_sock_path) >= sizeof(addr.sun_path))
    die("socket path too long");
  strncpy(addr.sun_path, g_sock_path, sizeof(addr.sun_path) - 1);

  /* 3) bind 前先清掉可能残留的旧 socket 文件，否则 EADDRINUSE。
   *    bind 成功返回 0、失败返回 -1。 */
  cleanup();
  if (bind(listenfd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    die("bind");

  /* 进程被 Ctrl-C / kill 时也要删掉 socket 文件 */
  atexit(cleanup);
  signal(SIGINT, on_signal);
  signal(SIGTERM, on_signal);
  /* 客户端可能在服务器回写前就 close：往断开的连接 write 会触发 SIGPIPE，
   * 默认动作直接杀死进程。忽略它，让 write 改以返回 -1 + errno=EPIPE 报错，
   * 交给下面 rio_writen 的返回值检查去处理，只丢这个连接、不拖垮整个服务器。 */
  signal(SIGPIPE, SIG_IGN);

  /* 4) listen：把主动 socket 变成监听 socket */
  if (listen(listenfd, BACKLOG) < 0)
    die("listen");
  printf("[server] listening on %s\n", g_sock_path);

  /* 5) accept 循环：单连接串行处理（教学够用；并发见实验题） */
  for (;;) {
    /* UDS 不关心对端地址，传 NULL。accept 被信号打断（EINTR）就重试，
     * 否则才当真出错——不检查会在返回 -1 时空转成忙等。 */
    int connfd = accept(listenfd, NULL, NULL);
    if (connfd < 0) {
      if (errno == EINTR)
        continue;
      die("accept");
    }
    printf("[server] client connected (connfd=%d)\n", connfd);

    /* 用 RIO 按行读——socket 上一样有 short count，RIO 帮我们兜住；
     * n == 0 表示对端关闭连接（EOF），循环自然结束。 */
    rio_t rio;
    rio_initb(&rio, connfd);
    char line[RIO_BUFSIZE];
    ssize_t n;
    while ((n = rio_readlineb(&rio, line, sizeof(line))) > 0) {
      printf("[server] recv: %s", line);
      buf_toupper(line, n);
      /* 回写失败（对端已关闭 → EPIPE，或其它错误）：rio_writen 返回 -1，
       * 别 die，只记一笔并跳出，服务这一连接结束、回去 accept 下一个。 */
      if (rio_writen(connfd, line, n) != n) {
        perror("[server] rio_writen");
        break;
      }
    }

    printf("[server] client disconnected\n");
    close(connfd);
  }

  return 0;
}
