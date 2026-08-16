# §12.2 基于 I/O 多路复用的并发编程

这一节的主线是：**不再为每个客户端创建一个进程或线程，而是让一条执行流通过 I/O 多路复用同时管理多个 fd；内核负责等待“哪些 fd 已就绪”，用户态事件循环负责调用 `accept`、`read`、`write` 并维护每条连接的状态**。`select`、`poll`、`epoll` 解决的是 readiness 通知，Reactor 则把“注册 → 等待 → 分发 → 处理”组织成完整的事件驱动架构。它能用少量线程维护大量大部分时间空闲的连接，但不意味着业务自动并行：任何阻塞在 event loop 中的 handler，都会拖住同一 loop 管理的其他连接。

> 本节代码组织：`experiments/select_echo_server.c`、`poll_echo_server.c`、`epoll_echo_server.c` 是三种等待机制的最小对照实现；`epoll_et_echo_server.c` 进一步展示 nonblocking、ET、读/写/accept 到 `EAGAIN`、连接级输出缓冲和按需 `EPOLLOUT`；客户端复用 `../12.1/experiments/process_echo_client.c`。前三个 server 便于观察 API 差异，但写路径仍可能阻塞，不是生产级实现。

---

## 从每连接一个执行流到事件循环

**🎯 一条执行流可以等待很多 fd**

§12.1 的进程模型让每个子进程阻塞在一个连接上；I/O 多路复用则把所有连接放进同一个等待集合，只有 fd ready 时才处理它：

```text
进程模型：client A → child A 阻塞 read
          client B → child B 阻塞 read
          client C → child C 阻塞 read

多路复用：client A ─┐
          client B ─┼→ 一个 event loop → select/poll/epoll_wait
          client C ─┘
```

最小事件循环具有固定形状：

```c
while (1) {
    int nready = wait_for_events();

    for (int i = 0; i < nready; ++i) {
        if (is_listen_event(i)) {
            accept_new_connection();
        } else if (is_read_event(i)) {
            read_and_process_connection(i);
        } else if (is_write_event(i)) {
            flush_pending_output(i);
        } else {
            handle_error(i);
        }
    }
}
```

这里的“并发”来自多个连接的处理过程在时间上交错，不等于多个 handler 同时在多个 CPU 上执行。单线程 event loop 一次仍然只执行一个 handler。

**🎯 `listenfd` 和 `connfd` 的 ready 含义不同**

两者都是 fd，都能加入多路复用器，但处理动作不同：

| ready 对象 | 通常表示 | 用户态动作 |
|---|---|---|
| 监听 socket `listenfd` | accept queue 中有已完成握手的连接 | 调用 `accept` / `accept4`，得到新 `connfd` |
| 已连接 socket `connfd` | 有数据、EOF 或错误可处理 | 调用 `read` / `recv`，检查返回值 |

```c
if (ready_fd == listenfd) {
    int connfd = accept(listenfd, NULL, NULL);
    register_connection(connfd);
} else {
    ssize_t n = read(ready_fd, buf, sizeof(buf));
    if (n > 0) {
        process(buf, (size_t)n);
    } else if (n == 0) {
        close_connection(ready_fd);  // 对端有序关闭
    } else {
        handle_read_error(ready_fd);
    }
}
```

**⚠️ ready 不是“已经完成 I/O”**

多路复用器返回 readable，只表示下一次读操作通常可以立即取得进展；它不保证一定读到业务 payload：

```text
read(fd, buf, size) > 0   → 读到数据
read(fd, buf, size) == 0  → 对端关闭写方向，读到 EOF
read(fd, buf, size) < 0   → 错误；nonblocking fd 还要区分 EAGAIN/EWOULDBLOCK
```

因此 `select`、`poll`、`epoll` 属于 readiness 模型。它们不会替应用把 socket 数据读进用户 buffer，也不会替应用解决 TCP 消息边界、短读、短写和连接生命周期问题。

**⚠️ event loop 中不能随意使用阻塞式“读完整消息”接口**

例如 `rio_readlineb` 会一直等待换行符。客户端只发送半行后停住时，单线程 event loop 会卡在该连接上，其他已 ready 的连接也无法处理：

```text
client A 发送 "half"，没有 '\n'
    ↓
rio_readlineb(connA) 等完整一行
    ↓
event loop 无法回到 select/poll/epoll_wait
    ↓
client B 即使已 ready 也只能等待
```

本节基础 server 每次只调用一次 `read`，真实协议实现则应使用 nonblocking fd，把收到的字节追加到每连接输入缓冲区，再从缓冲区中增量拆包。

---

## select：位图式兴趣集合

**🎯 `select` 使用三组 `fd_set` 和一个扫描上界**

```c
#include <sys/select.h>

int select(int nfds,
           fd_set *readfds,
           fd_set *writefds,
           fd_set *exceptfds,
           struct timeval *timeout);
```

常用宏和参数：

| 接口 | 含义 |
|---|---|
| `FD_ZERO(&set)` | 清空集合 |
| `FD_SET(fd, &set)` | 加入 fd |
| `FD_CLR(fd, &set)` | 删除 fd |
| `FD_ISSET(fd, &set)` | 检查 fd 是否在集合中 |
| `nfds` | 要检查的 fd 编号上界，必须是 `maxfd + 1` |
| `timeout == NULL` | 无限等待，直到事件或信号到来 |

`experiments/select_echo_server.c` 维护长期集合 `all_reads`，每轮复制出临时结果集合：

```c
fd_set all_reads;
fd_set ready_reads;
FD_ZERO(&all_reads);
FD_SET(listenfd, &all_reads);
int maxfd = listenfd;

while (1) {
    ready_reads = all_reads;
    int nready = select(maxfd + 1, &ready_reads, NULL, NULL, NULL);

    if (FD_ISSET(listenfd, &ready_reads)) {
        int connfd = accept(listenfd, NULL, NULL);
        FD_SET(connfd, &all_reads);
        if (connfd > maxfd) {
            maxfd = connfd;
        }
    }
}
```

**⚠️ `select` 会原地改写 `fd_set`**

调用前的 `readfds` 表示“想监听哪些 fd”，返回后的同一对象只保留“本轮哪些 fd ready”。因此不能把唯一的长期集合直接传进去：

```c
ready_reads = all_reads;  // 每轮必须重新复制
select(maxfd + 1, &ready_reads, NULL, NULL, NULL);
```

客户端关闭后也必须同时完成三个动作：

```c
close(fd);
FD_CLR(fd, &all_reads);
if (fd == maxfd) {
    recompute_maxfd(&maxfd, &all_reads);
}
```

否则会继续监测无效 fd；若该 fd 数字随后被复用，还可能误操作另一条新连接。

**⚠️ `nfds` 不是连接数**

假设只监测 fd 3 和 fd 100，共两个 fd：

```text
错误：select(2, ...)       只检查 fd 0..1
正确：select(101, ...)     检查 fd 0..100
```

`select` 在内核和用户态都要围绕 `0..maxfd` 扫描；fd 编号很稀疏时会做很多无效工作。

**⚠️ `FD_SETSIZE` 是经典硬限制**

glibc 中 `fd_set` 通常只能表示 fd 0–1023。`FD_SET(1500, &set)` 不会优雅返回错误，而可能越界写内存。本仓库示例在加入 fd 前显式拒绝：

```c
if (connfd >= FD_SETSIZE) {
    fprintf(stderr, "reject fd %d: exceeds FD_SETSIZE %d\n",
            connfd, FD_SETSIZE);
    close(connfd);
}
```

这也是大量连接服务不选择 `select` 的重要原因。

---

## poll：数组式兴趣集合

**🎯 `poll` 改善接口和 fd 表达能力，但没有消除全量扫描**

```c
#include <poll.h>

struct pollfd {
    int fd;
    short events;   // 用户输入：关心什么
    short revents;  // 内核输出：发生了什么
};

int poll(struct pollfd *fds, nfds_t nfds, int timeout);
```

与 `select` 相比，`poll` 不需要 `maxfd + 1`，也没有固定大小位图；`nfds` 是 `pollfd[]` 中有效项的数量：

```c
struct pollfd fds[MAX_CLIENTS + 1];
nfds_t nfds = 1;

fds[0].fd = listenfd;
fds[0].events = POLLIN;
fds[0].revents = 0;

int nready = poll(fds, nfds, -1);
```

常见事件：

| 事件 | 含义 | 处理重点 |
|---|---|---|
| `POLLIN` | 可读；监听 socket 上表示可 `accept` | 对连接仍需区分数据、EOF、错误 |
| `POLLOUT` | 可写 | 仅在确有待发送数据时监听 |
| `POLLERR` | fd 发生错误 | 查询错误或关闭连接 |
| `POLLHUP` | 对端挂起/关闭 | 可能仍应先读取剩余数据 |
| `POLLNVAL` | fd 无效 | 从数组删除或把槽位设为 `-1` |

**⚠️ `poll` 返回的是 ready 项数量，不是数组下标**

```c
int nready = poll(fds, nfds, -1);
for (nfds_t i = 0; i < nfds && nready > 0; ++i) {
    if (fds[i].revents == 0) {
        continue;
    }
    --nready;
    handle_poll_event(&fds[i]);
}
```

即使返回 `3`，也不能推断 `fds[3]` ready；必须扫描数组并检查每一项的 `revents`。

**🔧 紧凑数组删除：尾元素覆盖当前位置**

本仓库示例避免删除后整体搬移数组：

```c
static void remove_pollfd(struct pollfd fds[], nfds_t *nfds, nfds_t i) {
    close(fds[i].fd);
    fds[i] = fds[*nfds - 1];
    --(*nfds);
}
```

遍历中删除后要重新检查被换入当前位置的元素：

```c
remove_pollfd(fds, &nfds, i);
--i;
```

这里的 `MAX_CLIENTS == 1024` 只是实验程序静态数组的人为上限，不是 `poll` 接口固有的 1024 限制。

**⚠️ `poll` 的主要成本仍是 `O(nfds)`**

每轮都要把 `pollfd[]` 交给内核，内核逐项检查；返回后用户态还要逐项扫描 `revents`：

```text
总连接 n = 10000，本轮活跃 k = 3
poll：仍扫描 10000 项，再找出 3 项 ready
```

它适合 fd 数量不大、可移植性和接口清晰度更重要的程序；大量低活跃长连接则更适合 `epoll`。

---

## epoll：内核长期保存兴趣集合

**🎯 `epoll` 把“注册兴趣”和“取得 ready 事件”拆成三个接口**

```c
#include <sys/epoll.h>

int epoll_create1(int flags);
int epoll_ctl(int epfd, int op, int fd, struct epoll_event *event);
int epoll_wait(int epfd, struct epoll_event *events,
               int maxevents, int timeout);
```

典型控制流：

```c
int epfd = epoll_create1(EPOLL_CLOEXEC);

struct epoll_event ev = {0};
ev.events = EPOLLIN;
ev.data.fd = listenfd;
epoll_ctl(epfd, EPOLL_CTL_ADD, listenfd, &ev);

struct epoll_event events[1024];
while (1) {
    int nready = epoll_wait(epfd, events, 1024, -1);
    for (int i = 0; i < nready; ++i) {
        handle_event(events[i]);
    }
}
```

- `EPOLL_CTL_ADD`：加入 fd 及其关注事件。
- `EPOLL_CTL_MOD`：修改事件掩码或用户数据。
- `EPOLL_CTL_DEL`：删除 fd。
- `epoll_wait`：直接返回本轮 ready 的 `epoll_event[]`。

`EPOLL_CLOEXEC` 让 `epfd` 在 `execve` 时自动关闭，避免描述符意外泄漏到新程序。

**🎯 关键结构是 interest set 与 ready list**

```text
epoll instance
├── interest set：长期记录 fd、事件掩码和 data
├── ready list：记录当前已 ready 的事件
└── wait queue：没有 ready 事件时，让 epoll_wait 调用线程睡眠
```

fd 状态变化时，相关等待队列回调把事件放进 ready list，并唤醒 `epoll_wait`。因此应用不必在每轮等待时重新传入所有 fd，也不必在返回后扫描全部连接。

`struct epoll_event.data` 会在事件返回时原样带回。教学程序可存 fd，真实服务更常存连接对象：

```c
// 教学代码
 ev.data.fd = connfd;

// 连接状态较复杂时
 ev.data.ptr = conn;
```

`experiments/epoll_et_echo_server.c` 使用 `data.ptr`，连接对象中保存 fd、输出缓冲区、发送偏移和半关闭状态。

**🎯 三种接口的性能差异看 `n` 与 `k`**

- `n`：管理的总 fd 数。
- `k`：本轮 ready 的 fd 数。

| 维度 | `select` | `poll` | `epoll` |
|---|---|---|---|
| 兴趣集合 | 用户态 `fd_set` | 用户态 `pollfd[]` | 内核长期保存 |
| 每轮传全部 fd | 是 | 是 | 否 |
| 返回形式 | 被改写的 `fd_set` | 每项 `revents` | ready event 数组 |
| 用户态查找 ready | 扫描 `0..maxfd` | 扫描 `nfds` 项 | 遍历返回的 `k` 项 |
| 固定接口上限 | 常见 `FD_SETSIZE=1024` | 无固定 bitset 上限 | 无固定 bitset 上限 |
| 适合场景 | 教学、少量 fd | 中小规模、可移植 | Linux 大量低活跃长连接 |

`epoll_wait` 的工作量可近似理解为与返回事件数 `k` 相关，但不能简单宣称 epoll 永远 `O(1)` 或永远更快：`epoll_ctl` 有维护成本；如果 fd 很少或几乎全部同时活跃，业务读写和事件遍历仍然不可省略。

**⚠️ 本仓库 LT 基础版与 ET 完整版定位不同**

`epoll_echo_server.c` 使用默认 LT、阻塞 `accept`、单次 `read` 和 `rio_writen`，目的是与 `select`/`poll` 对照等待机制；`epoll_et_echo_server.c` 才展示生产网络路径中的核心机制：

```text
nonblocking listenfd/connfd
+ EPOLLET
+ accept4 到 EAGAIN
+ read 到 EAGAIN
+ 输出缓冲区
+ 按需 EPOLLOUT
```

不能因为基础 LT demo 使用了 `epoll`，就把它当成不会被慢客户端阻塞的生产级服务器。

---

## Nonblocking、LT 与 ET

**🎯 `O_NONBLOCK` 改变 I/O 行为，LT/ET 改变通知方式**

这些标志不能混为一谈：

| 标志 | 设置位置 | 作用 |
|---|---|---|
| `O_NONBLOCK` | `fcntl(F_SETFL)` 或 `SOCK_NONBLOCK` | 暂时不能完成时返回 `EAGAIN`，不阻塞线程 |
| `FD_CLOEXEC` | `fcntl(F_SETFD)` 或 `SOCK_CLOEXEC` | `execve` 时自动关闭 fd |
| `EPOLLET` | `epoll_event.events` | 使用边缘触发通知 |

正确设置 nonblocking 时，要保留原有 file status flags：

```c
static int set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        return -1;
    }
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}
```

Linux 上 `accept` 返回的连接 socket 不自动继承监听 socket 的 `O_NONBLOCK`，可直接使用：

```c
int connfd = accept4(listenfd, NULL, NULL,
                     SOCK_NONBLOCK | SOCK_CLOEXEC);
```

**🎯 LT：状态仍 ready，就继续通知**

默认 epoll 模式是 Level Triggered：

```c
ev.events = EPOLLIN;  // 默认 LT
```

假设 socket 中有 1000 字节，本轮只读 100 字节，接收缓冲区还剩 900 字节，下一轮 `epoll_wait` 仍会报告 `EPOLLIN`。`select` 和 `poll` 也具有类似的水平触发语义。

LT 编程更稳健，不容易因一次没处理干净而永久遗漏数据，因此应优先用 LT 写通正确的连接状态机。

**🎯 ET：状态发生边缘变化时通知，必须 drain 到 `EAGAIN`**

```c
ev.events = EPOLLIN | EPOLLET;
```

ET 下不能只读一次；应使用 nonblocking fd 循环读取：

```c
while (1) {
    ssize_t n = read(fd, buf, sizeof(buf));
    if (n > 0) {
        consume(buf, (size_t)n);
    } else if (n == 0) {
        peer_eof = true;
        break;
    } else if (errno == EINTR) {
        continue;
    } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
        break;  // 本轮已经读空
    } else {
        close_connection(fd);
        break;
    }
}
```

监听 socket 的 ET 路径也必须循环：

```c
while (1) {
    int connfd = accept4(listenfd, NULL, NULL,
                         SOCK_NONBLOCK | SOCK_CLOEXEC);
    if (connfd >= 0) {
        add_connection(connfd);
        continue;
    }
    if (errno == EINTR) {
        continue;
    }
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
        break;  // accept queue 已取空
    }
    handle_accept_error();
    break;
}
```

如果 ET fd 是阻塞模式，“读到没有数据为止”的最后一次 `read` 会阻塞整个 event loop；如果 ET 只处理一次，缓冲区残留数据又可能不再产生新的边缘通知。

**⚠️ EOF、hangup 和错误要与可读数据一起处理**

`EPOLLRDHUP` 表示对端关闭了写方向，但接收缓冲区中可能仍有数据，返回事件可能是：

```c
EPOLLIN | EPOLLRDHUP
```

稳健处理顺序是先 drain 可读数据，再记录 EOF/半关闭状态；输出缓冲区仍有响应时，可以发完后再关闭。看到 `EPOLLRDHUP` 就立即丢弃连接，可能丢掉已经到达的数据。

`EPOLLONESHOT` 是另一种独立机制：事件交付一次后自动禁用 fd，多线程 Reactor 可用它避免多个线程同时处理同一连接；处理完必须通过 `EPOLL_CTL_MOD` 重新 arm。单线程 echo demo 通常不需要它。

---

## 写路径、输出缓冲与背压

**🎯 writable 是常态，不能永久监听 `POLLOUT` / `EPOLLOUT`**

TCP 发送缓冲区大多数时候都有空间。如果应用没有待发数据却一直监听 `EPOLLOUT`，事件循环可能持续立即返回，形成 busy loop。

正确状态机是：

```text
outbuf 为空：只监听 EPOLLIN
    ↓ 产生响应，write 短写或 EAGAIN
outbuf 非空：监听 EPOLLIN | EPOLLOUT
    ↓ EPOLLOUT 到来，继续 flush
outbuf 清空：取消 EPOLLOUT，恢复只监听 EPOLLIN
```

连接不能只保存一个 fd，还要保存写状态：

```c
struct connection {
    int fd;
    char *output;
    size_t output_capacity;
    size_t output_begin;
    size_t output_end;
    bool peer_eof;
};
```

`experiments/epoll_et_echo_server.c` 根据是否有待发送字节动态计算事件掩码：

```c
static uint32_t connection_events(const struct connection *conn) {
    uint32_t events = EPOLLET;
    if (!conn->peer_eof) {
        events |= EPOLLIN | EPOLLRDHUP;
    }
    if (pending_output(conn) > 0) {
        events |= EPOLLOUT;
    }
    return events;
}
```

**⚠️ nonblocking `write` 成功也可能只写一部分**

```c
while (pending_output(conn) > 0) {
    ssize_t n = write(conn->fd,
                      conn->output + conn->output_begin,
                      pending_output(conn));
    if (n > 0) {
        conn->output_begin += (size_t)n;
    } else if (n < 0 && errno == EINTR) {
        continue;
    } else if (n < 0 &&
               (errno == EAGAIN || errno == EWOULDBLOCK)) {
        break;  // 保留剩余内容，等待 EPOLLOUT
    } else {
        close_connection(conn);
        break;
    }
}
```

短写不是连接失败；未发完的数据必须保留到后续事件。相反，阻塞式 `rio_writen` 虽然会尽量写满，却可能因为一个慢客户端长时间等待，拖住整个 event loop。

**⚠️ 输出缓冲区必须有上限**

如果客户端持续产生请求但几乎不读取响应，`outbuf` 会增长。真实服务器应设置高水位并选择暂停该连接的读事件、限流、拒绝新请求或关闭连接，而不是无限分配内存。本仓库 ET server 为每条连接设置 1 MiB 输出上限：

```c
#define MAX_OUTPUT_CAPACITY (1024U * 1024U)

if (length > MAX_OUTPUT_CAPACITY - pending) {
    errno = ENOBUFS;
    return -1;
}
```

这就是背压：下游消费速度跟不上时，系统必须反向限制上游生产速度。

---

## Reactor：把多路复用组织成事件驱动架构

**🎯 Reactor 不是 `epoll` 的别名**

`select`、`poll`、`epoll` 是 demultiplexer；Reactor 是围绕它们构建的控制流：

| Reactor 角色 | 本节对应物 |
|---|---|
| Handle | `listenfd`、`connfd`、`eventfd` |
| Event | readable、writable、error、hangup |
| Demultiplexer | `select`、`poll`、`epoll_wait` |
| Handler | accept/read/write/error handler |
| Dispatcher / Reactor | `while` 事件循环与事件分发代码 |

```text
注册 fd 和关注事件
    ↓
等待 fd ready
    ↓
分发给对应 handler
    ↓
handler 调 accept/read/write
    ↓
必要时修改兴趣集合
    ↓
回到事件循环
```

本仓库三个基础 echo server 都是最小 Reactor，只是 demultiplexer 和兴趣集合的保存位置不同。

**⚠️ handler 必须短小、非阻塞**

```c
void on_readable(struct connection *conn) {
    read_available_bytes(conn);
    sleep(5);  // 错误示例：同一 loop 的所有连接都被延迟
}
```

慢 CPU 计算、阻塞数据库查询、磁盘 I/O、第三方 RPC 和长时间等锁都不应直接留在 I/O loop。网络库使用 epoll 并不能消除业务阻塞；如果 Redis 执行一条耗时命令或 Node.js callback 长时间占用 event loop，其他连接的延迟仍会一起上升。

**🎯 重业务可交给 worker，但连接状态仍由 event loop 统一拥有**

常见单 Reactor + worker pool 流程：

```text
I/O loop：nonblocking read + 拆出完整请求
    ↓
有界 task queue
    ↓
worker：计算或执行允许阻塞的业务
    ↓
completion queue：保存结果
    ↓
worker write(eventfd)：唤醒 I/O loop
    ↓
I/O loop：校验连接 → 追加 outbuf → nonblocking write
```

`eventfd` 只传“有新结果”的计数，真正响应内容放在用户态 completion queue。发布顺序必须是先入队，再通知：

```c
completion_queue_push(result);

uint64_t one = 1;
write(notifyfd, &one, sizeof(one));
```

worker 通常不直接修改 `connfd`、`outbuf` 或调用 `epoll_ctl`，目的是维持单一所有权，避免并发写、短写协调、响应乱序、关闭竞态和 fd 复用问题。

**⚠️ 跨线程任务必须带稳定连接身份**

只保存 fd 不够：旧连接关闭后，fd 数字可能很快被新连接复用。worker 的迟到结果应通过 `conn_id + generation` 或受控连接对象在所属 loop 中重新校验；如果同一连接允许多个请求并行，还必须处理响应顺序和最大 in-flight 数量。

**🎯 Reactor 与 Proactor 的区别是 ready 与 done**

| 模型 | 通知语义 | 谁执行实际 I/O | 典型机制 |
|---|---|---|---|
| Reactor | fd 已 ready | 用户 handler 调 `read`/`write` | `select`/`poll`/`epoll` |
| Proactor | 异步 I/O 已完成 | 内核或异步 I/O 子系统 | Windows IOCP、`io_uring` completion 思路 |

例如 `epoll_wait` 返回 `EPOLLIN` 时，数据尚未自动进入应用 buffer；completion-based read 通知到来时，提交的读取操作已经产生结果。

---

## Muduo 的 one loop per thread

**🎯 一个线程一个 EventLoop，不是一个连接一个线程**

Muduo 的典型结构是：

```text
main thread
└── baseLoop：监听 listenfd，负责 accept

I/O thread 0
└── EventLoop 0 + epoll：管理很多连接

I/O thread 1
└── EventLoop 1 + epoll：管理很多连接
```

如果调用：

```cpp
TcpServer server(&loop, listenAddr, "EchoServer");
server.setThreadNum(3);
server.start();
loop.loop();
```

通常表示一个主线程中的 `baseLoop`，外加三个 I/O `EventLoop` 线程，而不是每个客户端创建三个线程。

**🎯 新连接固定归属于一个 I/O loop**

主线程 `accept` 得到 `connfd` 后，从 `EventLoopThreadPool` 选择一个 sub-loop，再把连接建立操作投递到目标线程：

```cpp
EventLoop *ioLoop = threadPool_->getNextLoop();

auto conn = std::make_shared<TcpConnection>(
    ioLoop, connName, sockfd, localAddr, peerAddr);

ioLoop->runInLoop([conn] {
    conn->connectEstablished();
});
```

此后该连接的 `read`、`write`、输入/输出缓冲、事件掩码、关闭流程和生命周期状态原则上都由所属 loop 串行管理。这让不同连接能在不同 CPU 上并行，同时让单条连接仍可按单线程状态机推理。

**🎯 跨线程调用通过任务队列和 wakeup fd 回到所属 loop**

```cpp
void EventLoop::runInLoop(Functor cb) {
    if (isInLoopThread()) {
        cb();
    } else {
        queueInLoop(std::move(cb));
    }
}
```

`queueInLoop` 先把 callback 放进线程安全队列，再写 `eventfd` 唤醒阻塞在 `epoll_wait` 的目标 loop。公共的 `TcpConnection::send` 可以跨线程调用，但真正更新输出缓冲和 socket 的操作仍回到连接所属 loop。

**⚠️ I/O 线程池不是业务线程池**

`EventLoopThreadPool` 中每个线程都运行事件循环，负责网络 I/O；CPU 密集计算、慢数据库、磁盘和阻塞 RPC 应放入另外的 business worker pool。`messageCallback` 默认仍运行在连接所属 I/O 线程中，配置多个 I/O loop 并不会让阻塞 callback 自动变安全。

Round-robin 分配只能大致平衡连接数，不能保证流量或 CPU 开销均衡；I/O 线程也不是越多越好，过多线程会增加调度、cache 失效和跨线程通信成本，应以压测数据决定。

---

## 易错点

- **把 I/O 多路复用理解成内核替应用完成读写 → 它只通知 readiness，数据仍要由用户态 `accept/read/write` 处理。**
- **把单线程事件循环理解成业务并行 → 多连接处理只是交错执行，一个 handler 阻塞仍会拖住同一 loop 的所有连接。**
- **看到 readable 就认定有业务数据 → EOF、错误和监听 socket 的 pending connection 也会表现为 readable。**
- **在 event loop 中用阻塞式 `rio_readlineb` 等完整一行 → 客户端只发半行就可能卡住整个事件循环。**
- **`select` 每轮复用同一个 `fd_set` → `select` 会原地改写集合，必须保留长期集合并每轮复制。**
- **把 `select` 的 `nfds` 传成连接数量 → 正确值是长期集合中最大 fd 加一。**
- **忽略 `FD_SETSIZE` → `FD_SET` 处理过大 fd 可能越界写内存，不会可靠地返回错误。**
- **把 `poll` 返回值当成 ready 下标 → 它是 `revents != 0` 的项数，仍需扫描数组。**
- **把实验中的 `MAX_CLIENTS=1024` 当成 `poll` 固有限制 → 这是示例静态数组容量，不是 `poll` API 的 bitset 上限。**
- **删除 `pollfd` 时用尾元素覆盖却不重新检查当前位置 → 换入的 ready fd 会被本轮循环跳过。**
- **认为 epoll 让单次 socket I/O 更快 → 它主要避免对大量空闲 fd 做每轮全量扫描。**
- **认为 epoll 永远比 poll 快 → fd 很少或几乎全部活跃时，epoll 优势可能很小。**
- **把 `O_NONBLOCK` 写进 `epoll_event.events` → 它是 fd 的 file status flag，应通过 `fcntl` 或 `SOCK_NONBLOCK` 设置。**
- **ET 模式只 `read` 或 `accept` 一次 → nonblocking ET 必须循环到 `EAGAIN`，否则可能遗留数据或连接且不再收到通知。**
- **看到 `EPOLLRDHUP` 就立即关闭 → 对端关闭写方向时缓冲区仍可能有数据，应先 drain 再决定关闭。**
- **永久监听 `POLLOUT` / `EPOLLOUT` → socket 常态可写，会造成 event loop 高频空转。**
- **把短写当成失败 → nonblocking 写路径必须保留剩余数据，并按需等待下一次可写事件。**
- **让输出缓冲区和任务队列无限增长 → 慢客户端或过载会耗尽内存，必须设置高水位和背压策略。**
- **把 Reactor 等同于 epoll → Reactor 是事件驱动架构，`select`、`poll`、`epoll` 都能作为 demultiplexer。**
- **跨线程任务只保存 fd → fd 会复用，迟到结果必须用稳定连接身份和生命周期校验。**
- **把 Muduo one loop per thread 理解成 one connection per thread → 一个 EventLoop 管理很多连接，一条连接固定归属一个 loop。**
- **把 `EventLoopThreadPool` 当成业务线程池 → 它负责网络 I/O，阻塞业务仍应交给独立 worker pool。**

---

## 工程关联

- **Nginx**：worker 进程内部用事件循环维护大量连接，epoll 的价值是让空闲 keep-alive 连接几乎不产生扫描成本。
- **Redis**：网络事件与命令执行共享主事件循环时，慢命令会抬高其他客户端延迟，直接体现“handler 不能阻塞”。
- **Node.js / libuv**：JavaScript callback 背后是跨平台事件循环；Linux 后端可使用 epoll，文件和部分阻塞工作则交给线程池。
- **Muduo / Netty**：通过多个 EventLoop 分片连接，每条连接固定归属一个 loop，以单一所有权换取更少的锁和更清晰的状态机。
- **WebSocket、IM、网关和代理**：连接数很大但瞬时活跃比例低，是 epoll 最典型的收益场景。
- **`eventfd`、`timerfd`、`signalfd`**：Linux 把跨线程通知、定时器和信号转换成 fd 后，可以与 socket 一起纳入同一 epoll loop。
- **背压与慢客户端**：高性能网络服务必须观测每连接 outbuf、task queue 和 in-flight 请求数；超过高水位时暂停读、限流或断开连接。
- **线上排障**：`strace -p` 可看 event loop 阻塞在哪个系统调用，`perf top` 可看 CPU 是否忙于 handler，`ss -tnp` 看连接状态，`/proc/<pid>/fd` 看 fd 数量。
- **容量规划**：高并发连接还受 `ulimit -n`、内核 socket 内存、listen backlog、每连接状态大小和应用缓冲区上限约束，换成 epoll 并不会自动解决这些资源限制。
- **性能判断**：`poll` 在 fd 较少时更简单且足够；只有“大量 fd、大量空闲、少量 ready”的负载形态，才最能体现 epoll 的 ready-list 优势。

---

## 实验题

**🧪 题 1：对照运行 select、poll、epoll 三种最小 Reactor**

源码片段分别见 `experiments/select_echo_server.c`、`poll_echo_server.c`、`epoll_echo_server.c`：

```c
select(maxfd + 1, &ready_reads, NULL, NULL, NULL);
poll(fds, nfds, -1);
epoll_wait(epfd, events, MAX_EVENTS, -1);
```

要求：

1. 编译并运行自动回显验证：

   ```bash
   cd Chapter12/12.2/experiments
   make clean all
   make demo
   ```

2. 确认 `make demo` 分别在 18080、18081、18082 端口验证两条顺序回显；它不包含 ET server。
3. 对照源码指出每个实现的长期兴趣集合、ready 结果、event loop、AcceptHandler 和 ReadHandler。
4. 解释为什么 `select` 要维护 `maxfd`，`poll` 要维护 `nfds`，而 `epoll_wait` 只遍历返回的 ready event。
5. 说明三个基础 server 为什么仍可能被阻塞写路径拖住，不能直接视为生产实现。

**🧪 题 2：用 strace 观察三种等待原语**

源码片段：

```c
int nready = select(maxfd + 1, &ready_reads, NULL, NULL, NULL);
int nready = poll(fds, nfds, -1);
int nready = epoll_wait(epfd, events, MAX_EVENTS, -1);
```

要求：

1. 分别启动并跟踪 server：

   ```bash
   strace -tt -e trace=pselect6,select,accept,read,write \
     ./select_echo_server 18080

   strace -tt -e trace=poll,ppoll,accept,read,write \
     ./poll_echo_server 18081

   strace -tt -e trace=epoll_create1,epoll_ctl,epoll_wait,accept,read,write \
     ./epoll_echo_server 18082
   ```

2. 另一个终端用 `./echo_client 127.0.0.1 <port>` 连接并发送一行。
3. 观察空闲时 server 阻塞在哪个等待调用，新连接到来后为何先处理 `listenfd`，数据到来后为何再处理 `connfd`。
4. 在 epoll 版中确认启动时出现 `epoll_create1` / `EPOLL_CTL_ADD`，而每轮 `epoll_wait` 不再提交全部 fd。
5. 客户端退出后观察 `read == 0` 或 hangup 处理，并确认连接 fd 被关闭。

**🧪 题 3：验证 LT 与 ET 的不同处理规则**

源码见 `experiments/epoll_echo_server.c` 和 `experiments/epoll_et_echo_server.c`，ET 关键片段：

```c
while (1) {
    ssize_t n = read(conn->fd, buf, sizeof(buf));
    if (n > 0) {
        append_output(conn, buf, (size_t)n);
    } else if (n < 0 &&
               (errno == EAGAIN || errno == EWOULDBLOCK)) {
        break;
    }
}
```

要求：

1. 启动 ET server：

   ```bash
   ./epoll_et_echo_server 18083
   ```

2. 另一个终端验证基本回显：

   ```bash
   printf 'hello ET\n' | ./echo_client 127.0.0.1 18083 | grep -x 'hello ET'
   ```

   `echo_client` 还会输出一行连接提示；`grep -x` 只匹配完整的回显行，并以退出状态验证是否收到 `hello ET`。

3. 用 `strace -tt -e trace=epoll_ctl,epoll_wait,accept4,read,write` 跟踪 ET server，确认使用 `accept4(..., SOCK_NONBLOCK | SOCK_CLOEXEC)`。
4. 一次快速建立多个连接，观察监听路径循环 `accept4`，直到返回 `EAGAIN`。
5. 一次发送大于 `READ_BUFFER_SIZE` 的数据，观察服务端可能执行多次 `read`，最终打印 drained to `EAGAIN`。
6. 临时把 `drain_input` 改成只读一次，对比残余数据可能不再触发新边缘；实验后恢复源码。

**🧪 题 4：观察按需 EPOLLOUT 与输出缓冲区**

源码见 `experiments/epoll_et_echo_server.c`：

```c
if (pending_output(conn) > 0) {
    events |= EPOLLOUT;
}

if (ready & EPOLLOUT) {
    flush_output(conn);
}
```

要求：

1. 保持 ET server 的 socket 为 nonblocking，并确认新连接没有待发数据时不监听 `EPOLLOUT`。
2. 使用一个连接持续发送数据但故意缓慢读取响应，让 server 的 `write` 发生短写或 `EAGAIN`。
3. 观察 `epoll_ctl(EPOLL_CTL_MOD)` 打开 `EPOLLOUT`，输出缓冲清空后再取消它。
4. 继续制造积压；只有服务端 `write` 已短写或返回 `EAGAIN`、用户态 `outbuf` 实际持续累积时，待发送数据超过 1 MiB 才会走 `ENOBUFS` 路径。若本机环境无法稳定复现，可临时把 `MAX_OUTPUT_CAPACITY` 改小（如 32 KiB）后再验证，实验结束后恢复源码。
5. 解释如果始终注册 `EPOLLOUT`，为什么 socket 常态可写会让 `epoll_wait` 高频返回。

**🧪 题 5：验证阻塞 handler 会拖住整个 loop**

在任一基础 server 的 `echo_once` 中临时加入：

```c
if (n > 0) {
    sleep(5);  // 仅用于实验
    rio_writen(fd, buf, (size_t)n);
}
```

要求：

1. 以 `-g -O1 -Wall` 重新编译 server。
2. 同时连接 client A 和 client B，让 A 先发送数据，B 紧接着发送。
3. 观察 B 的响应也被 A 的五秒 handler 延迟阻塞。
4. 用 `strace -tt` 或时间戳日志确认 event loop 在 `sleep` 期间没有回到多路复用等待调用。
5. 删除 `sleep` 并重新验证；说明 production Reactor 为什么只在 I/O loop 做快速 read/parse/dispatch，把重业务交给有界 worker pool。
