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
 *   编译：见 Makefile 的 `make uds`（抽象命名空间版 `make uds_abstract`）
 *   运行：./uds_server [socket_path]   默认 /tmp/csapp_uds.sock
 *         路径以 '@' 开头 → 抽象命名空间（如 ./uds_server @csapp_uds）
 */
#include "rio.h"
#include <ctype.h>
#include <errno.h>
#include <signal.h>
#include <stddef.h> /* offsetof */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h> /* struct sockaddr_un —— UDS 专属的地址结构 */
#include <unistd.h>

#define DEFAULT_PATH "/tmp/csapp_uds.sock"
#define BACKLOG 8

/* g_sock_path 以 '@' 开头 → 抽象命名空间（Linux 特有）：不落文件系统、无需
 * unlink、无 EADDRINUSE，但失去文件权限管控。否则是普通文件系统路径。 */
static const char *g_sock_path = DEFAULT_PATH;
static int is_abstract(void) { return g_sock_path[0] == '@'; }

/* 退出前删除 socket 文件：路径式 bind 会落一个 s 类型文件，不清理下次会
 * EADDRINUSE；抽象命名空间没有文件可删，内核在最后一个引用关闭时自动回收。 */
static void cleanup(void) {
  if (!is_abstract())
    unlink(g_sock_path);
}

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

/* 填充 UDS 地址，返回该传给 bind/connect 的 addrlen。
 *   name 以 '@' 开头 → 抽象命名空间：首字节置 '\0'（这就是"抽象"的标志），
 *     名字放其后。addrlen 必须【精确】算，绝不能用 sizeof(*addr)——否则
 *     sun_path 后面的填零字节会全被算进名字，变成一堆尾随空字节的名字。
 *   否则 → 文件系统路径：可传整个结构，内核按 sun_path 里的 '\0' 截断。 */
static socklen_t fill_uds_addr(struct sockaddr_un *addr, const char *name) {
  memset(addr, 0, sizeof(*addr));
  addr->sun_family = AF_UNIX;
  if (name[0] == '@') {
    const char *abs = name + 1; /* 去掉前导 '@' 标记，它只是本程序的约定 */
    size_t len = strlen(abs);
    if (len + 1 > sizeof(addr->sun_path)) /* 首字节 '\0' + 名字 */
      die("abstract name too long");
    addr->sun_path[0] = '\0';
    memcpy(addr->sun_path + 1, abs, len);
    return offsetof(struct sockaddr_un, sun_path) + 1 + len;
  }
  size_t len = strlen(name);
  if (len >= sizeof(addr->sun_path))
    die("socket path too long");
  memcpy(addr->sun_path, name, len);
  return sizeof(*addr);
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

  /* 2) 填地址结构。路径式或抽象式由 fill_uds_addr 按 '@' 前缀分流，
   *    返回精确的 addrlen（抽象式尤其不能用 sizeof）。 */
  struct sockaddr_un addr;
  socklen_t addrlen = fill_uds_addr(&addr, g_sock_path);

  /* 3) bind 前先清掉可能残留的旧 socket 文件（抽象式为 no-op），否则 EADDRINUSE。
   *    bind 成功返回 0、失败返回 -1。 */
  cleanup();
  if (bind(listenfd, (struct sockaddr *)&addr, addrlen) < 0)
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
