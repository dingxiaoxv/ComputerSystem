# 专题：Unix Domain Socket —— 本机进程间通信

> 挂在第 10 章的补充专题，不是书里的正式小节。
>
> **为什么放第 10 章**：UDS 本质是 IPC 不是网络，它和本章三件事直接咬合——① socket 也是一种文件（`s` 类型）；② socket fd 上照样有 short count，所以能复用本章的 RIO 包；③ fd / 打开文件表那套抽象原样成立。它是第 11 章 `AF_INET` 网络编程的前奏：**借用同一套 socket API，但数据不走网络**。
>
> **配套代码**：`experiments/uds_server.c` + `experiments/uds_client.c`，`cd experiments && make uds` 一键自测。文档中所有数值（`sun_path=108`、`sizeof=110`、strace 片段）均在本机实测核对过。

---

## 1. 一句话：UDS 是什么

Unix Domain Socket（`AF_UNIX`，POSIX 里等价的名字是 `AF_LOCAL`）是**同一台机器内**进程间通信的通道。

它复用网络 socket 的整套 API（`socket` / `bind` / `listen` / `accept` / `connect` / `read` / `write`），但：

- 数据**不进 IP/TCP 协议栈**——内核直接在 socket 缓冲之间把字节从一个进程搬到另一个进程；
- 寻址不用 IP+端口，而用**文件系统路径**（如 `/tmp/x.sock`）。

```
        进程 A                        进程 B
      ┌────────┐                    ┌────────┐
      │ write()│                    │ read() │
      └───┬────┘                    └───▲────┘
          │                             │
          │        ┌──────────┐         │
          └───────►│  内核缓冲 │────────┘
                   │ (内存拷贝)│
                   └──────────┘
             不经过网卡、不封 IP/TCP 包
```

---

## 2. 和 TCP `127.0.0.1` 的区别（最容易混的点）

很多人第一反应是「本机通信就用 `127.0.0.1`」。能用，但 UDS 才是本机 IPC 的正解。关键差别在**数据要不要穿过网络协议栈**：

```
  TCP loopback (127.0.0.1)                 Unix Domain Socket
 ┌─────────────┐                          ┌─────────────┐
 │  进程 A     │                          │  进程 A     │
 └──────┬──────┘                          └──────┬──────┘
        │ write                                  │ write
 ┌──────▼──────┐  ← 每次收发都要            ┌──────▼──────┐
 │  socket 层  │     走这一整摞：           │  socket 层  │
 ├─────────────┤     分段、序号、           └──────┬──────┘
 │  TCP 层     │     校验和、ACK、                 │  内核直接
 ├─────────────┤     拥塞控制…              ┌──────▼──────┐  内存拷贝
 │  IP 层      │                           │  内核缓冲   │  （没有分层）
 ├─────────────┤                           └──────┬──────┘
 │ loopback 网卡│                                  │
 └──────┬──────┘                          ┌──────▼──────┐
        │                                 │  进程 B     │
 ┌──────▼──────┐                          └─────────────┘
 │  进程 B     │
 └─────────────┘
```

| 维度 | UDS (`AF_UNIX`) | TCP loopback (`127.0.0.1`) |
|------|-----------------|----------------------------|
| 寻址 | 文件系统路径 `/tmp/x.sock` | IP + 端口 |
| 数据路径 | 内核缓冲间内存拷贝 | 走 TCP/IP 协议处理（分段、序号/ACK、拥塞控制状态） |
| 性能 | 更快、更低延迟、更高吞吐 | 慢一截 |
| 权限控制 | 文件系统权限位（`chmod` sock 文件） | 靠端口/防火墙 |
| 独有能力 | **传递 fd**（`SCM_RIGHTS`）、**查对端凭证**（`SO_PEERCRED`） | 无 |

> ⚠️ 补一句严谨的：Linux 上 loopback 也经过内核 TCP/IP 协议处理，但不走真实网卡驱动/线缆，且 `lo` 上**校验和通常被跳过**（本机回环没必要算），所以上表没把「校验和」算进 loopback 的开销。UDS 的优势是**连 TCP 协议处理这一整摞都省了**，所以更快——但差距是常数级，不要神化，真正的重头戏是下面的「传 fd」和「权限管控」。

---

## 3. 地址长什么样：`struct sockaddr_un`

网络编程用 `sockaddr_in`（IP+端口），UDS 用 `sockaddr_un`（`<sys/un.h>`），地址就是一个路径字符串：

```c
struct sockaddr_un {
    sa_family_t sun_family;      // 恒为 AF_UNIX，占 2 字节
    char        sun_path[108];   // 文件系统路径，Linux 上只有 108 字节！
};
// 本机实测：sizeof(struct sockaddr_un)=110, sizeof(sun_path)=108, offsetof(sun_path)=2
```

```
  sun_family        sun_path[108]
 ┌──────────┬───────────────────────────────────────┐
 │ AF_UNIX  │ "/tmp/csapp_uds.sock\0"                │
 └──────────┴───────────────────────────────────────┘
   2 字节                最多 107 字符 + 结尾 '\0'
```

⚠️ `sun_path` 很短（Linux 上 108 字节），路径过长会被**静默截断**——一定要检查长度（配套 `uds_server.c` 里就有 `strlen >= sizeof(sun_path)` 的显式检查）。

**路径放哪：按场景选，别习惯性用 `/tmp`**

| 场景 | 推荐位置 | 理由 |
|------|----------|------|
| 系统级 daemon | `/run/<服务名>/x.sock` | `/run` 是 tmpfs，重启自动清空；放进带权限的子目录做访问控制 |
| 用户级服务 | `$XDG_RUNTIME_DIR/x.sock`（即 `/run/user/$UID/`） | systemd 建的 `0700` 私有目录、登出自动清理，现代正解 |
| 父子/兄弟进程 | 不用路径，`socketpair`（见 §7③） | 无命名 → 无清理、无权限问题 |
| 随手 demo | `/tmp/x.sock` | 方便，但**别用于生产** |

- **别在生产用 `/tmp`**：全局可写（sticky 位）+ 路径可预测 → 存在**符号链接/抢占攻击**（攻击者抢先在你的路径上建对象）。要放进你独占、权限收紧的目录。
- **权限即访问控制**（UDS 相对 TCP 端口的一大优势）：谁能 `connect` 取决于 ① socket 文件权限位（`chmod 600`，Linux 在 `connect` 时检查）；② 更可靠的是**容纳目录的权限**（`chmod 700 /run/myapp/`）。**优先靠目录权限**——更可移植、更难绕过。想把 IPC 限到同一用户，把 socket 放进 `0700` 私有目录即可。
- **清理**：路径式 socket 是持久文件，进程崩溃**不自动删**，下次 `bind` 同路径直接 `EADDRINUSE`——所以要 `bind` 前 `unlink`、退出时再 `unlink`（配套 `uds_server.c` 的 `atexit` + 信号处理）。嫌这套麻烦？看下面 §7① 的抽象命名空间。

---

## 4. 五步 API：和网络编程一模一样，只有「地址」不同

服务器和客户端的骨架与 `AF_INET` 完全一致，差异只有两处：地址族改成 `AF_UNIX`、地址填路径。下面是**时序图**（配套代码就是这个流程）：

```
   服务器 (uds_server)                        客户端 (uds_client)
 ────────────────────────                    ────────────────────────
   socket(AF_UNIX,SOCK_STREAM)                 socket(AF_UNIX,SOCK_STREAM)
        │                                            │
   unlink(path)   ← 清残留                           │
        │                                            │
   bind(path)     ← 落一个 s 类型文件               │
        │                                            │
   listen(backlog)                                   │
        │                                            │
   accept() ─────────阻塞等待──────────◄── connect(path)
        │                                            │
   connfd 就绪                                   fd 就绪
        │                                            │
        │◄──────── rio_writen("hello\n") ────────────┤
        │                                            │
   rio_readlineb 读到 "hello\n"                       │
   大写化                                            │
        ├──────── rio_writen("HELLO\n") ────────────►│
        │                                     rio_readlineb 读回显
        │                                     打印 "echo: HELLO"
        │                                            │
   rio_readlineb 返回 0  ◄──────── close(fd) ─────────┤ (stdin EOF)
   (对端关闭=EOF)                                     │
        │                                        进程结束
   close(connfd)
```

对应的最小代码骨架：

```c
// ── 服务器 ──
int listenfd = socket(AF_UNIX, SOCK_STREAM, 0);   // 改动 1：地址族 AF_UNIX
struct sockaddr_un addr;
memset(&addr, 0, sizeof(addr));                   // 必须清零，尾部 null 才是有效的路径终止
addr.sun_family = AF_UNIX;
strncpy(addr.sun_path, "/tmp/x.sock", sizeof(addr.sun_path) - 1);  // 改动 2：地址=路径
unlink("/tmp/x.sock");                            // bind 前清残留，否则 EADDRINUSE
bind(listenfd, (struct sockaddr *)&addr, sizeof(addr));
listen(listenfd, 8);
int connfd = accept(listenfd, NULL, NULL);        // 本机通信通常不关心对端地址 → NULL

// ── 客户端 ──
int fd = socket(AF_UNIX, SOCK_STREAM, 0);
// ...填同一个 addr...
connect(fd, (struct sockaddr *)&addr, sizeof(addr));
```

connect / accept 之后，`connfd` / `fd` 就是一个**普通的双向字节流 fd**，读写和文件无异。

---

## 5. 关键关联：socket 也是文件，所以能复用 RIO

**① `bind` 会在文件系统里落一个 `s` 类型文件**（正好补全本章「文件类型」那节列的 `socket (s)`）。本机实测：

```
$ ls -l /tmp/csapp_uds.sock
srwxr-xr-x 1 yanxu yanxu 0 ... /tmp/csapp_uds.sock
▲
└─ 首字符 s = socket 文件类型（对比 - 普通文件、d 目录、p FIFO）
```

**② `SOCK_STREAM` 是无边界字节流**，语义和 TCP 一样——一次 `read` 可能只拿到半行、也可能一次拿到好几行，即 **short count**。所以本章的 RIO 包在这里原样适用，按 `\n` 切行：

```c
rio_t rio;
rio_initb(&rio, connfd);
char line[RIO_BUFSIZE];
ssize_t n;
while ((n = rio_readlineb(&rio, line, sizeof(line))) > 0)  // RIO 兜住 short count
    rio_writen(connfd, line, n);                           // n == 0 → 对端关闭 (EOF)
```

**③ 从内核视角（对照 §10.8 的三张表）**：connect/accept 之后，两个进程各自的描述符表里有一项，分别指向内核里一对相连的 socket 端点：

```
  服务器描述符表                            客户端描述符表
 ┌──────────────┐                          ┌──────────────┐
 │ fd=4 connfd  │──┐                    ┌──│ fd=3 fd      │
 └──────────────┘  │                    │  └──────────────┘
                   ▼                    ▼
              ┌─────────┐  内核内的  ┌─────────┐
              │ socket  │◄══════════►│ socket  │
              │ 端点 A  │  双向管道  │ 端点 B  │
              └─────────┘            └─────────┘
```

---

## 6. 动手：跑通配套例子

配套的 `experiments/uds_server.c`（大写化回显）和 `uds_client.c`（stdin 逐行发、读回显）。

**一键自测**（Makefile 里 `uds` target 会：后台起 server → client 喂两行 → 比对回显 → 清理）。以下是**本机真实输出**：

```
$ cd Chapter10/experiments
$ make uds
gcc -g -O1 -Wall -I../rio uds_server.c ../rio/rio.c -o uds_server
gcc -g -O1 -Wall -I../rio uds_client.c ../rio/rio.c -o uds_client
[server] listening on /tmp/csapp_uds.sock
[server] client connected (connfd=4)
[client] echo: HELLO
[client] echo: UNIX DOMAIN SOCKET
[server] client disconnected
[demo] done. 'ls -l /tmp/csapp_uds.sock' 期间可见一个 s 类型文件
```

**手动玩两个终端**（更能体会 C/S）：

```
# 终端 1：起服务器
$ ./uds_server
[server] listening on /tmp/csapp_uds.sock

# 终端 2：连上去，随便打字，看大写回显
$ ./uds_client
hello world
[client] echo: HELLO WORLD
```

**用 strace 看它到底调了哪些系统调用**（把概念和真实进程对上号）。以下是**本机 strace 实测**（`connect` 的第三个参数 `110` 正是 `sizeof(struct sockaddr_un)`）：

```
$ strace -e trace=socket,connect,write,read ./uds_client
socket(AF_UNIX, SOCK_STREAM, 0)         = 3
connect(3, {sa_family=AF_UNIX, sun_path="/tmp/csapp_uds.sock"}, 110) = 0
write(3, "hello\n", 6)                  = 6
read(3, "HELLO\n", 8192)                = 6
```

一眼就能看到：地址族是 `AF_UNIX`、`sun_path` 是那个路径、收发就是普通 `read`/`write`。

---

## 7. 三个变体（了解即可）

**① 抽象命名空间（abstract namespace，Linux 特有）**：名字不落文件系统，最后一个引用关闭时内核自动回收——**无需 `unlink`、永不 `EADDRINUSE`**。工具里显示成 `@myservice`（那个 `@` 就代表前导空字节）。代价：不落盘也就**失去了文件/目录权限管控**，访问控制只能靠 `SO_PEERCRED` 查对端凭证或 network namespace 隔离；且是 Linux 特有，不可移植。

怎么填地址，三个要点（第 3 点最容易错）：

```c
#include <stddef.h>   // offsetof
struct sockaddr_un addr;
memset(&addr, 0, sizeof(addr));
addr.sun_family = AF_UNIX;

const char *name = "myservice";                 // 逻辑名
addr.sun_path[0] = '\0';                        // ① 首字节 '\0' —— 这就是"抽象"的标志
memcpy(addr.sun_path + 1, name, strlen(name));  // ② 名字放在首个 '\0' 之后

// ③ addrlen 必须【精确】算，不能传 sizeof(addr)！
socklen_t len = offsetof(struct sockaddr_un, sun_path) + 1 + strlen(name);
bind(fd, (struct sockaddr *)&addr, len);        // connect 端用【完全相同】的填法和 len
```

- **① 首字节 `\0` 是开关**：内核见 `sun_path[0]=='\0'` 就走抽象命名空间，否则当路径。
- **② 名字边界由 `addrlen` 定，不由 `\0` 截断**：抽象名字甚至可含嵌入的 `\0`，语义和路径式「靠 `\0` 找结尾」完全不同。
- **③ `addrlen` 必须精确——头号坑**：路径式可偷懒传 `sizeof(struct sockaddr_un)`（内核按 `\0` 截断），但抽象式若传 `sizeof(addr)`，`sun_path` 后面的填零字节会**全被算进名字**，服务名变成 `"myservice\0\0\0…"`。这时能自己连自己（两端都错得一样），但换个正确填 `addrlen` 的客户端就连不上，极其隐蔽。务必用 `offsetof(...) + 1 + strlen(name)`。

**配套实测**：`uds_server.c` / `uds_client.c` 已支持 `@` 前缀切换（`make uds_abstract`），内部用一个 `fill_uds_addr()` 按 `@` 分流路径式/抽象式并返回精确 `addrlen`。本机验证：`@csapp_uds` 能正常回显、`/tmp` 下无任何 socket 文件、`ss -xl` 与 `/proc/net/unix` 里显示 `@csapp_uds`、且 server 杀掉后可**立即重启同名**（无 `EADDRINUSE`，对比路径式的残留文件）。

**② `SOCK_DGRAM`**：本机版「UDP」——保留消息边界（一次 `recv` 对应一次 `send`）、本机传输可靠不丢，适合定长消息。

**③ `socketpair(AF_UNIX, SOCK_STREAM, 0, fds)`**：一步造出一对**已连通**的 fd，专给 `fork` 后的父子进程用，省掉 bind/listen/connect：

```c
int fds[2];
socketpair(AF_UNIX, SOCK_STREAM, 0, fds);
if (fork() == 0) {
    close(fds[0]);  write(fds[1], ...);   // 子进程用 fds[1]
} else {
    close(fds[1]);  read(fds[0], ...);    // 父进程用 fds[0]
}
```

这是「父子进程双向通信」最省事的做法（比 `pipe` 强在**双向**）。

---

## 8. UDS 独有的杀手锏：传递文件描述符（`SCM_RIGHTS`）

TCP 做不到、UDS 能做的事：把一个**打开的 fd** 通过 `sendmsg` 的辅助数据（`cmsg`，类型 `SCM_RIGHTS`）发给另一个进程。

内核会在接收进程的描述符表里**新建一项，指向同一个打开文件表项**——等于**跨进程做了一次 `dup`**（对照 §10.8：fd 号变了，但打开文件表项和 v-node 是同一个，偏移量共享）：

```
  发送进程                                    接收进程
 ┌────────────┐                             ┌────────────┐
 │ fd=5 ──────┼──┐   sendmsg(SCM_RIGHTS) ┌──┼─► fd=7     │
 └────────────┘  │   ────────────────►   │  └────────────┘
                 ▼                        ▼
              ┌──────────────────────────────┐
              │  同一个「打开文件表项」        │  ← 内核新建 fd=7 指向它
              │  （共享偏移量 k、访问模式）    │
              └──────────────┬───────────────┘
                             ▼
                        ┌─────────┐
                        │ v-node  │
                        └─────────┘
```

**真实用途**：主进程 `accept` 到一个连接，把 `connfd` 发给某个 worker 进程去处理。传 fd 传的是「打开文件表项」这一层，不是数字本身，所以两边 fd 号不同但指向同一个内核对象。

> ⚠️ **它不要求两端有亲缘关系**——这是最容易被 `socketpair + fork` 的演示带偏的地方。传 fd 的机制（`sendmsg`/`SCM_RIGHTS`）只要求两端有一条**已连通的 AF_UNIX 连接**；这条连接是 `socketpair`（父子/共同祖先）还是**命名 UDS**（`bind`/`accept` + `connect`，两个各自启动、毫无亲缘的进程）造的，都无所谓。真正"零亲缘"的教科书例子是 **Wayland**：合成器先起，GUI 客户端后起、`connect` 上来，把 dmabuf/共享内存的 fd 传过去共享画面缓冲；**D-Bus** 的 `UNIX_FD` 类型同理。（nginx master/worker 其实是 fork 出来的，属"有亲缘"，不是这条的例子。）另一个强约束：fd **不能跨机器**，因为传的是本机内核里那个打开文件表项的引用——这也正是 UDS 相对 TCP loopback 的独有能力。

**配套可运行代码**：`experiments/uds_passfd.c`（`cd experiments && make passfd`），用 `socketpair + fork` 省掉 bind/connect，聚焦「传 fd」本身。核心是 `sendmsg` 的**辅助数据**——fd 不放普通数据缓冲，而是塞进 `msg_control` 的 cmsg，类型标 `SCM_RIGHTS`：

```c
// 发送端：把一个 fd 塞进 cmsg 发出去（关键片段）
char dummy = '*';                                   // ← 必须捎 ≥1 字节正常数据，
struct iovec iov = { &dummy, 1 };                   //   否则多数内核不携带 SCM_RIGHTS
union { char buf[CMSG_SPACE(sizeof(int))];          // ← cmsg 有对齐要求，缓冲大小
        struct cmsghdr align; } u;                  //   必须用 CMSG_SPACE 算，别手写
struct msghdr msg = {0};
msg.msg_iov = &iov;  msg.msg_iovlen = 1;
msg.msg_control = u.buf;  msg.msg_controllen = sizeof(u.buf);

struct cmsghdr *c = CMSG_FIRSTHDR(&msg);
c->cmsg_level = SOL_SOCKET;                          // socket 层
c->cmsg_type  = SCM_RIGHTS;                          // 「传的是 fd」
c->cmsg_len   = CMSG_LEN(sizeof(int));
memcpy(CMSG_DATA(c), &fd_to_send, sizeof(int));      // fd 写进 cmsg 数据区
sendmsg(sock, &msg, 0);
// 接收端对称：recvmsg 后用 CMSG_FIRSTHDR 取出，校验 level/type/len，再 memcpy 出 fd
```

**本机实测输出**（父进程 fd 号和子进程收到的 fd 号不同，但读出同一个文件——证明传的是内核对象而非数字）：

```
$ make passfd
[parent] opened /etc/hostname as fd = 4, sending...
[child]  received fd = 3 (自己进程里的新号)
[child]  read from that fd: HP-Pro-Tower
```

⚠️ 三个必踩的坑，例子里都处理了：① **必带 ≥1 字节正常数据**（`iov` 里那个占位字节）；② **缓冲区用 `CMSG_SPACE`、长度字段用 `CMSG_LEN`**，cmsg 有对齐要求，手算 `sizeof(int)` 会错；③ **接收端务必校验 `cmsg_level/type/len`** 再取 fd，不能盲 `memcpy`。

**独立进程版**（更贴近真实场景）：`experiments/passfd_send.c` + `passfd_recv.c`（`make passfd2`），把连接手段从 `socketpair` 换成**命名 UDS**（`send` 端 bind/listen、`recv` 端 connect），于是通信双方是两个各自 `./` 启动、毫无亲缘的进程；而传 fd 的 `send_fd`/`recv_fd`（那套 `sendmsg` + cmsg 代码）与父子版 `uds_passfd.c` 里的**一字不差**——正说明传 fd 只依赖一条已连通的连接，跟连接怎么建、进程有无亲缘无关。本机实测：`send` 端 `fd=5`、`recv` 端收到 `fd=4`，号不同却读到同一个文件，证明跨独立进程传的仍是内核对象。可两个终端各跑一个亲手体会。

**现代 C++17 版**：`experiments/unix_socket.hpp`（RAII 薄封装）+ `passfd_cpp.cpp`（`make passfd_cpp`）。cmsg 内核机制与 C 版**一字不差**，但用 `uds::Fd`（只移动的 RAII fd）+ `uds::UnixSocket`（工厂式 `listen`/`connect`/`accept` + `send_fd`/`recv_fd`）后，**全程无手动 `close`/`memset`/裸 errno 判断**：fd 出作用域自动关，出错抛 `std::system_error`，`sockaddr_un` 用值初始化 `{}` 清零。这正是"贴着 syscall 又要现代 C++"时该有的样子——**传 fd 的本质省不掉，能省的是资源管理与错误处理的样板**。

---

## 9. 工程实践：UDS 是本机 C/S 通信的事实标准

本机 C/S 几乎全用 UDS，图的就是**快 + 文件权限管控 + 能传 fd**：

- **Docker**：`/var/run/docker.sock`——`docker` CLI 就是往这个 UDS 发 HTTP 请求
- **数据库**：PostgreSQL（`/var/run/postgresql/.s.PGSQL.5432`）、MySQL（`/var/run/mysqld/mysqld.sock`）的本地连接默认走 UDS，比 TCP loopback 快
- **systemd**：socket activation；`nginx` → `php-fpm`；X11、`ssh-agent`、`gpg-agent`
- **权限即安全边界**：`chmod 600 x.sock` 就能把 IPC 限制到同一用户——这是 UDS 相对 TCP 端口的一大优势

---

## 10. 坑与局限（收尾清单）

**配套例子已处理的坑**：

- **bind 前必须 `unlink`**：socket 文件是持久的，进程崩溃不会自动删，下次 bind 同路径直接 `EADDRINUSE`。退出时也要清理（`uds_server.c` 用 `atexit` + `SIGINT`/`SIGTERM` 处理器 `unlink`；注意信号处理器走 `_exit`，不会触发 `atexit`，所以里面显式又调了一次 `cleanup`）。
- **`sun_path` 只有 108 字节**：路径过长被静默截断，务必检查 `strlen(path) < sizeof(sun_path)`。
- **地址结构要 `memset` 清零**：`bind` 靠 `sun_path` 里的 `\0` 定位路径结尾，不清零会读到栈上垃圾。

**配套例子为教学而故意简化、生产环境要补的**：

- **未忽略 `SIGPIPE`**：服务器是「先 read 再 write」，正常流程下客户端关闭时 `readlineb` 先返回 0 而不会写。但若客户端**发完数据后、服务器回显前**就 `close`，`rio_writen` 会遇 `EPIPE` → 默认 `SIGPIPE` 直接**杀死服务器进程**。健壮服务器应 `signal(SIGPIPE, SIG_IGN)`（或用 `MSG_NOSIGNAL`），改为处理 `write` 的 `EPIPE` 返回。
- **单连接串行**：`accept` 循环一次只服务一个客户端。真实服务器要 `fork`/线程/`epoll` 做并发。
- **`signal()` 而非 `sigaction()`**：`signal()` 的语义跨平台不确定，CSAPP 推荐用 `sigaction` 封装的 `Signal`。
- **别习惯性用 `127.0.0.1` 做本机 IPC**：多穿一整摞协议处理、还得管端口冲突和防火墙；本机通信优先 UDS。
