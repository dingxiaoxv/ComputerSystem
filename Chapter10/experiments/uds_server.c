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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>       /* struct sockaddr_un —— UDS 专属的地址结构 */
#include "rio.h"

#define DEFAULT_PATH "/tmp/csapp_uds.sock"
#define BACKLOG      8

static const char *g_sock_path = DEFAULT_PATH;

/* 退出前删除 socket 文件：bind 会在文件系统里落一个 s 类型的文件，
 * 不清理，下次 bind 同一路径会得到 EADDRINUSE。 */
static void cleanup(void) { unlink(g_sock_path); }

static void on_signal(int sig) {
    (void)sig;
    cleanup();
    _exit(0);       /* 信号处理里只能调异步信号安全函数，unlink 是安全的 */
}

static void die(const char *msg) {
    perror(msg);
    cleanup();
    exit(1);
}

int main(int argc, char *argv[]) {
    if (argc > 1) g_sock_path = argv[1];

    /* 1) 创建 socket：AF_UNIX + SOCK_STREAM = 本机字节流 */
    int listenfd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (listenfd < 0) die("socket");

    /* 2) 填地址结构。UDS 的地址就是一个文件系统路径 */
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    if (strlen(g_sock_path) >= sizeof(addr.sun_path))
        die("socket path too long");                 /* sun_path 通常仅 108 字节 */
    strncpy(addr.sun_path, g_sock_path, sizeof(addr.sun_path) - 1);

    /* 3) bind 前先清掉可能残留的旧 socket 文件，否则 EADDRINUSE */
    unlink(g_sock_path);
    if (bind(listenfd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
        die("bind");

    /* 进程被 Ctrl-C / kill 时也要删掉 socket 文件 */
    atexit(cleanup);
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    /* 4) listen：把主动 socket 变成监听 socket */
    if (listen(listenfd, BACKLOG) < 0) die("listen");
    printf("[server] listening on %s\n", g_sock_path);
    fflush(stdout);

    /* 5) accept 循环：单连接串行处理（教学够用；并发见实验题） */
    for (;;) {
        int connfd = accept(listenfd, NULL, NULL);   /* UDS 不关心对端地址，传 NULL */
        if (connfd < 0) {
            if (errno == EINTR) continue;
            die("accept");
        }
        printf("[server] client connected (connfd=%d)\n", connfd);
        fflush(stdout);

        /* 用 RIO 按行读——socket 上一样有 short count，RIO 帮我们兜住 */
        rio_t rio;
        rio_initb(&rio, connfd);
        char line[RIO_BUFSIZE];
        ssize_t n;
        while ((n = rio_readlineb(&rio, line, sizeof(line))) > 0) {
            /* 大写化后原样回显，让客户端能验证「确实回来了」 */
            for (ssize_t i = 0; i < n; i++) line[i] = toupper((unsigned char)line[i]);
            if (rio_writen(connfd, line, n) != n) { perror("rio_writen"); break; }
        }
        /* n == 0 表示对端关闭了连接（EOF） */
        printf("[server] client disconnected\n");
        fflush(stdout);
        close(connfd);
    }
    /* 不会到这里；正常退出路径靠信号处理 */
    return 0;
}
