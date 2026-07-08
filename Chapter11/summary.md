# 第 11 章 网络编程

这一章的主线是：**把第 8-10 章学的进程、I/O、并发落到「两台机器隔着网络对话」这件事上——网络在程序员眼里就是又一种 I/O 设备，socket 就是又一种文件描述符**。往下要搞清楚三层地址（IP 地址定位主机、端口定位主机上的服务、DNS 把域名翻译成 IP），往上要搞清楚 socket 接口那套固定动作（服务端 `socket`→`bind`→`listen`→`accept`，客户端 `socket`→`connect`），以及怎么用 `getaddrinfo` 把「主机名/服务名」和「二进制地址结构」解耦，最后用一个真实的 HTTP/CGI 服务器 Tiny 把 fork、dup2、execve、RIO 全部串起来。读完要能回答：一次 `connect` 背后发生了什么、为什么服务端要 `bind`+`listen`+`accept` 三步、`getaddrinfo` 替我们挡掉了什么、一个 HTTP 事务从请求行到响应体长什么样。

> 本章代码组织：`socket/net.h` 放 `open_clientfd`/`open_serverfd` 封装（复用 §11.4 的 `getaddrinfo` 惯用法）；`experiments/` 放 `addrinfo.c`（域名→IP）、echo 客户端/服务端，以及 `http_client.cpp`（libcurl C++ HTTP 客户端）；`tiny_web/` 是完整的 Tiny Web 服务器 + CGI（`make` 后浏览器访问）。RIO 直接复用 `../Chapter10/rio`。
>
> 标注「【补充】」的小节是书本没展开、但工程里必须懂的内容（TCP/UDP、DNS 排障、backlog 取值、HTTP 版本与方法），按 Yanxu 的要求补进来。另有两份独立专题：[TCP / UDP 协议与 tcpdump 排障](tcp_udp.md)、[libcurl 在 C++ 里做 HTTP 客户端](libcurl_cpp.md)。

---

## 客户端-服务器编程模型（§11.1）

**🎯 一个事务，四步**

网络应用的核心抽象不是「网络」而是**事务（transaction）**，和数据库事务无关：

1. 客户端发**请求**，向服务器要一个服务
2. 服务器收到请求，**解释**它，操作自己的资源
3. 服务器发**响应**，等下一个请求
4. 客户端收到响应，处理它

**⚠️ 客户端和服务器是进程，不是机器**

一台机器可以同时跑很多客户端和服务器进程。「客户端-服务器」描述的是进程间的角色关系，不是硬件。数据库服务器和它的客户端完全可以在同一台主机上（就是本机 socket 通信）。

---

## 网络：一个互连的网络（§11.2）

**🎯 对主机来说，网络就是又一种 I/O 设备**

数据从网络到达时经过适配器（网卡）DMA 进内存，发送时反过来。所以在程序员这一层，收发网络数据和读写文件用的是同一组系统调用（`read`/`write`），这正是第 10 章「一切皆文件」的延续。

**🎯 分层：LAN → WAN → internet**

- 最底层是 **LAN**（局域网），最流行的是以太网（Ethernet），一段电缆上挂多台主机，用 48 位 MAC 地址寻址
- 多个不兼容的 LAN 通过**路由器**连成 **WAN**（广域网），再连成「网络的网络」——**internet**
- 让互不兼容的网络能对话的关键是**协议软件**：它做两件事，① 给数据加统一的**命名**（IP 地址）② 定义统一的**分组格式**

**🎯 封装（encapsulation）**

数据每下一层就被套一层头。主机 A 的应用数据先加 LAN1 帧头发到路由器，路由器剥掉 LAN1 帧头、读 IP 头决定转发、再套 LAN2 帧头发出去。每一层只看自己那层的头——这就是分层协议栈的本质。

---

## 全球 IP 因特网（§11.3）

**🎯 三个由 TCP/IP 协议族撑起的能力**

1. **IP**：提供**主机到主机**的基本命名和（尽力而为、不可靠的）投递
2. **UDP**：在 IP 上极薄地包一层，把投递范围从「主机到主机」细化到「**进程到进程**」，仍不可靠
3. **TCP**：在 IP 上包一层复杂的协议，提供**进程到进程的、可靠的、全双工字节流**连接

程序员看到的因特网 = 一组主机，每台有唯一 **IP 地址**，IP 地址集合又映射到一组**域名**，不同主机上的进程之间能建**连接**。

**🎯 IP 地址就是一个 32 位无符号整数（IPv4）**

存在 `struct in_addr { uint32_t s_addr; }` 里，**按网络字节序（大端）存放**。

- 网络传输统一用大端，所以有 `htonl`/`htons`（主机→网络）和 `ntohl`/`ntohs`（网络→主机）
- 人读的是**点分十进制**（`128.2.194.242`），`inet_pton`（字符串→二进制）/`inet_ntop`（二进制→字符串）负责互转

```c
// 这就是为什么 §2.1 里就埋了 htonl 的伏笔：网络字节序在这里落地
struct in_addr addr;
inet_pton(AF_INET, "128.2.194.242", &addr);   // addr.s_addr 是大端
```

**🎯 域名与 DNS**

人记不住数字，于是有**域名**（`www.google.com`）和把域名映射到 IP 的分布式数据库 **DNS**。一个域名可以映射到多个 IP（负载均衡），多个域名也可以映射到同一个 IP。程序里几乎不直接碰 DNS，而是通过 `getaddrinfo` 间接查询（见下）。DNS 的工程细节见【补充】小节。

**🎯 因特网连接由「两个 socket 地址」唯一确定**

一条 TCP 连接被一个**四元组**唯一标识：

```
(客户端 IP : 客户端端口,  服务端 IP : 服务端端口)
```

- **端口**是 16 位整数，标识主机上的某个服务。服务器用的是**知名端口**（well-known port）：Web 80、SSH 22、邮件 25，映射关系在 `/etc/services`
- 客户端端口是内核在 `connect` 时自动分配的**临时端口**（ephemeral port）
- socket 地址存在 `struct sockaddr_in`（IPv4）里；函数签名却统一用 `struct sockaddr *`——这是 C 里没有泛型时代的多态手法，用 `struct sockaddr_storage` 当「够大且对齐正确」的通用容器

---

## socket 接口（§11.4）

**🎯 一张必须背下来的动作图**

```
客户端                         服务端
                              socket()        创建 socket 描述符
                              bind()          绑定到本机某端口
                              listen()        转成监听 socket，声明 backlog
socket()   创建 socket        accept()  ←──   阻塞等待连接（返回 connfd）
connect()  ───────发起连接──→  （accept 返回，握手完成）
  ...      ←──── rio_writen / rio_readlineb 双向字节流 ────→  ...
close()    ───────EOF────────→ read 返回 0
                              close()
```

**⚠️ `listenfd` 和 `connfd` 是两个不同的描述符**

`accept` 不是「接受到 `listenfd` 上」，而是**新建一个已连接描述符 `connfd`** 返回。`listenfd` 只用来「听」新连接，`connfd` 用来和这个客户端收发数据。一个 `listenfd` 可以派生出无数个 `connfd`——这是并发服务器的基础（每个 `connfd` 交给一个进程/线程处理）。

**🎯 `getaddrinfo`：把「主机名/服务名 → socket 地址」的脏活外包出去**

老代码用 `gethostbyname` + 手填 `sockaddr_in`，又不可重入又要手动处理 IPv4/IPv6 差异。现代写法是 `getaddrinfo`：给它主机名和服务名，它吐出一个 `struct addrinfo` **链表**，每个节点直接能喂给 `socket`/`connect`/`bind`，全程不碰具体地址结构，**IPv4/IPv6 无关**。

```c
struct addrinfo hints, *plist, *p;
memset(&hints, 0, sizeof(hints));
hints.ai_socktype = SOCK_STREAM;       // 只要 TCP 流式
hints.ai_flags    = AI_ADDRCONFIG;     // 按本机实际配置的协议族返回
getaddrinfo("www.google.com", "http", &hints, &plist);
for (p = plist; p; p = p->ai_next) {   // 逐个试，能连上就用
    fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
    if (fd < 0) continue;
    if (connect(fd, p->ai_addr, p->ai_addrlen) == 0) break;
    close(fd);
}
freeaddrinfo(plist);                    // 必须释放，否则内存泄漏
```

本仓库 `socket/net.h` 里 `open_clientfd`/`open_serverfd` 就是这个惯用法的封装。几个 flag 要点：

- `AI_PASSIVE`：给服务端用，`node` 传 `NULL` 时返回**通配地址**（`INADDR_ANY`），表示监听所有本机网卡
- `AI_NUMERICSERV`：服务名参数是端口数字（如 `"8080"`）而非 `/etc/services` 里的名字，跳过一次查表
- `AI_ADDRCONFIG`：只返回本机真正配了地址的协议族（没配 IPv6 就别返回 IPv6）

`getnameinfo` 是反向操作：socket 地址 → 主机名/服务名字符串，`NI_NUMERICHOST | NI_NUMERICSERV` 让它输出数字形式（不做反向 DNS，避免卡顿）。

**⚠️ 服务端 `bind` 前几乎总要设 `SO_REUSEADDR`**

服务器重启时，上次连接可能还在 `TIME_WAIT` 状态占着端口，`bind` 会报 `Address already in use`。设 `SO_REUSEADDR` 允许复用处于 `TIME_WAIT` 的端口，这是服务端标配（见 `net.h` 里 `open_serverfd`）。

---

## 【补充】TCP vs UDP：为什么 Web 用 TCP，DNS 却常用 UDP

书本只说「TCP 可靠、UDP 不可靠」，工程上要能说清差在哪、各自适合什么。

**🎯 逐条对比**

| 维度 | TCP | UDP |
|------|-----|-----|
| 连接 | 面向连接，先三次握手 | 无连接，发了就走 |
| 数据单位 | 字节流（无消息边界） | 数据报（保留边界，一个 `sendto` = 一个包） |
| 可靠性 | 确认 + 超时重传，保证到达 | 不保证到达，丢了不管 |
| 顺序 | 保证按序交付 | 不保证顺序 |
| 流量控制 | 有（滑动窗口） | 无 |
| 拥塞控制 | 有（慢启动、拥塞避免） | 无（想发多快发多快） |
| 头部开销 | 20 字节起 | 8 字节 |
| 多播/广播 | 不支持 | 支持 |
| 典型场景 | HTTP、SSH、文件传输、数据库 | DNS 查询、DHCP、视频/语音、游戏、QUIC |

**🔧 怎么选**

- 要**可靠、有序、面向连接**（网页、登录、转账、下载）→ TCP，别自己在 UDP 上重造可靠性
- 要**低延迟、能容忍丢包、或需要多播**（实时音视频、在线游戏、遥测）→ UDP，丢一帧比卡一秒强
- **DNS 查询**默认走 UDP：请求/响应都小、一来一回、丢了重发即可，省掉 TCP 握手的一个 RTT；但响应过大（超 512 字节，如 DNSSEC、区域传输 AXFR）会回退到 TCP
- **HTTP/3 的反直觉选择**：它跑在 UDP 上，但可靠性/有序性由用户态的 QUIC 协议自己实现——为的是绕开 TCP 内核实现的队头阻塞和慢握手（见 HTTP 版本小节）

**⚠️ 字节流没有消息边界**

TCP 是字节流，`write` 100 字节对端可能分 3 次 `read` 收到，也可能和下一条消息粘在一起（**粘包**）。所以应用层必须自己定边界：要么定长、要么带长度前缀、要么用分隔符（HTTP 用 `\r\n\r\n` 分隔头部）。这正是第 10 章 RIO 的 `rio_readlineb`（按行读）存在的理由。本章 echo 的最小 demo 里注释也点了这个坑：只在「本地小消息一次收完」的假设下成立。

---

## 【补充】DNS 深入：多个公共 DNS、怎么运行、怎么排障

**🎯 DNS 是一棵分布式的树**

没有哪台机器存着全世界的域名。DNS 是层级委派的分布式数据库：

```
根 (.)  →  顶级域 TLD (.com / .cn / .org)  →  权威服务器 (google.com 自己的)
```

真正替你跑腿的是**递归解析器（recursive resolver）**：你问它 `www.google.com`，它从根开始一级级迭代查询（根告诉它去问 `.com`，`.com` 告诉它去问 `google.com` 的权威服务器），拿到结果后**按 TTL 缓存**再返回给你。

**🎯 8.8.8.8 / 114.114.114.114 是什么**

它们都是**公共递归解析器**，谁都能用：

- `8.8.8.8` / `8.8.4.4`：Google Public DNS
- `1.1.1.1`：Cloudflare（主打隐私）
- `114.114.114.114`：国内老牌公共 DNS
- `223.5.5.5`：阿里公共 DNS

**为什么要有多个 / 配多个：**

- **冗余**：一个挂了自动用备用（`/etc/resolv.conf` 里可列多行 `nameserver`，按顺序尝试）
- **就近降延迟**：选地理/网络上更近的解析器，查询更快
- **解析质量/策略差异**：不同解析器缓存新鲜度、对某些域名的解析结果、隐私政策、是否有广告过滤都不同
- **不是负载均衡**：客户端通常只在前一个超时后才试下一个，配多个是「主备」而非「分摊」

**🔧 DNS 是怎么运行的（一次查询的路径）**

1. 程序调 `getaddrinfo` → glibc 的 **stub resolver**
2. stub resolver 按 `/etc/nsswitch.conf` 的 `hosts:` 行决定顺序，通常先查 `/etc/hosts`，再查 DNS
3. 查 DNS 时读 `/etc/resolv.conf` 拿 `nameserver` 地址，发 **UDP/53** 查询给递归解析器（现代发行版常是本机的 `systemd-resolved`，监听 `127.0.0.53`，它再转发出去）
4. 递归解析器命中缓存就直接返回，否则从根迭代查询，得到结果按 TTL 缓存

**🔧 域名解析出问题时怎么排障（从内到外）**

```bash
# 1. 先分清是「DNS 解析」问题还是「网络连通」问题
getent hosts example.com     # 走系统完整解析链（nsswitch + hosts + DNS），最贴近程序真实行为
ping example.com             # 能解析到 IP 但 ping 不通 → 是网络/防火墙，不是 DNS

# 2. 直接问 DNS，绕开 /etc/hosts 和缓存
dig example.com              # 看 ANSWER 段有没有 A 记录、status 是不是 NOERROR
dig +short example.com       # 只要结果
nslookup example.com         # 老工具，效果类似

# 3. 对比不同解析器：怀疑本地解析器有问题就指定公共 DNS
dig @8.8.8.8 example.com     # 如果 @8.8.8.8 能解析、默认的不行 → 本地 resolver/resolv.conf 问题

# 4. 逐级追踪，定位是哪一层坏了（根/TLD/权威）
dig +trace example.com

# 5. 检查配置文件
cat /etc/resolv.conf         # nameserver 对不对、是不是被覆盖成 127.0.0.53
cat /etc/nsswitch.conf       # hosts: 行顺序
cat /etc/hosts               # 有没有被手工/恶意写死一条错误映射

# 6. systemd-resolved 系统
resolvectl status            # 看每个网卡实际用的 DNS 服务器
resolvectl query example.com
resolvectl flush-caches      # 清缓存（解析结果过期/被污染时）

# 7. 抓包看查询到底发出去没、回了什么
sudo tcpdump -n -i any port 53
```

**⚠️ 常见坑与判断**

- **能解析但连不上** → 不是 DNS 问题，是路由/防火墙/服务没起；别在 DNS 上耗时间
- **`ping` 用了 IP 能通、用域名不通** → DNS 问题坐实
- **`dig` 能解析、程序不能** → 大概率 `/etc/hosts` 或 `nsswitch.conf` 或本地缓存作祟（`dig` 只查 DNS，不走 hosts）
- **时快时慢** → 可能多个 `nameserver` 里第一个在超时，或 TTL 过期后回源慢
- **解析到错误 IP** → 缓存污染（`resolvectl flush-caches`）或 `/etc/hosts` 被写了脏记录

---

## 【补充】listen 的 backlog 到底填多少

`listen(listenfd, backlog)` 里的 `backlog` 是工程面试常问、又常被误解的参数。本仓库 `net.h` 里用的是 `LISTENQ = 1024`。

**🎯 backlog 限的是「已完成握手、等 accept」的队列**

内核对每个监听 socket 维护**两个队列**：

- **半连接队列（SYN queue）**：收到客户端 SYN、回了 SYN-ACK、还没收到最后 ACK 的连接（`SYN_RCVD`）。长度由 `net.ipv4.tcp_max_syn_backlog` 控制，开了 syncookies 后可抗 SYN flood
- **全连接队列（accept queue）**：三次握手已完成、`ESTABLISHED`、正排队等你 `accept()` 取走的连接。**`backlog` 限的就是这个队列**

**⚠️ 你填的 backlog 会被内核悄悄截断**

实际生效值 = `min(backlog, net.core.somaxconn)`。所以：

- 老内核 `somaxconn` 默认 **128**，你填 1024 也只生效 128
- Linux 5.4+ 默认提到 **4096**
- 想让大 backlog 真正生效，**必须同时调大 `somaxconn`**（`sysctl -w net.core.somaxconn=1024`），否则白填

**🔧 取值经验**

- backlog **只吸收「瞬时突发」**：它是 accept 慢于建连时的缓冲垫。稳态吞吐取决于你 `accept()` + 处理连接的速度，不是队列长度
- 队列**满了会发生什么**：默认（`tcp_abort_on_overflow=0`）内核**丢弃那个 ACK/静默忽略**，客户端以为丢包会重传，表现为连接建立变慢；设成 1 则直接回 RST 让客户端立刻失败
- **参考值**：Nginx 默认 `511`，Redis 默认 `511`，很多服务用 `1024`；高并发短连接（大量瞬时新连接）适当调大 backlog + `somaxconn`。低并发服务 128 足够
- **别指望靠调大 backlog 扛住过载**：队列长只是把「拒绝」推迟成「排队+延迟」。根治要靠更快的 accept、多进程/多线程/多路复用处理连接
- 监控：`ss -lnt` 的 `Recv-Q`（当前 accept 队列积压）/`Send-Q`（backlog 上限）；`netstat -s | grep -i listen` 看有没有「listen queue overflow」计数在涨

---

## Web 服务器与 HTTP（§11.5）

**🎯 HTTP 事务：纯文本、行式、`\r\n` 结尾**

HTTP 建在 TCP 之上。一次事务就是客户端发一段文本请求、服务器回一段文本响应，行与行之间用 `\r\n`，头部用一个空行 `\r\n` 结束。

请求：
```
GET /home.html HTTP/1.1\r\n      ← 请求行：方法 URI 版本
Host: localhost:8080\r\n         ← 请求头（若干行）
\r\n                             ← 空行，头部结束
```

响应：
```
HTTP/1.0 200 OK\r\n              ← 响应行：版本 状态码 原因短语
Content-type: text/html\r\n      ← 响应头
Content-length: 120\r\n
\r\n                             ← 空行
<html>...</html>                 ← 响应体
```

`Content-length` 告诉客户端体有多长（字节流没边界，靠它切），`Content-type` 是 MIME 类型（`text/html`、`image/png`…）。

**🎯 静态内容 vs 动态内容**

- **静态**：内容就是磁盘上的文件，服务器原样发回。Tiny 用 `mmap` 把文件映射进内存再一次性 `write`，省掉「读进用户缓冲区」这一步拷贝
- **动态**：内容由服务器现场运行一个程序生成。经典机制是 **CGI**：服务器 `fork` 子进程，用 `execve` 运行 CGI 程序，把 URI 里 `?` 后的参数通过**环境变量 `QUERY_STRING`** 传进去，再用 `dup2(connfd, STDOUT_FILENO)` 把 CGI 的标准输出**重定向到客户端 socket**——于是 CGI 里一句 `printf` 就直接发到了浏览器

**🔧 CGI 把前三章全串起来了**

`serve_dynamic` 这十几行代码同时用到：`fork`（§8 进程创建）、`setenv`（§8 进程环境）、`dup2`（§10 重定向）、`execve`（§8 加载运行）、`wait`（§8 回收僵尸）。这是本章最好的综合复习点——一个 Web 请求触发一次进程创建 + I/O 重定向 + 程序加载。

---

## Tiny：把整章串起来（§11.6）

**🎯 一个 250 行的完整 Web 服务器**

Tiny 是**迭代式（串行）** HTTP/1.0 服务器，主循环就是第 10、11 章拼起来的：

```c
listenfd = open_serverfd(port);           // §11.4 socket/bind/listen
while (1) {
    connfd = accept(listenfd, ...);       // §11.4 取一个连接
    doit(connfd);                          // 处理完整一条事务
    close(connfd);
}
```

`doit` 的流程：`rio_readlineb` 读请求行 → `sscanf` 拆出方法/URI/版本 → 只认 `GET`（否则回 501）→ `read_requesthdrs` 读完并丢弃其余头 → `parse_uri` 判断静态/动态（URI 含 `cgi-bin` 即动态）→ `stat` 查文件在不在（不在回 404）、权限够不够（不够回 403）→ 分派给 `serve_static` 或 `serve_dynamic`。

**⚠️ Tiny 是教学玩具，不是生产服务器**

它的短板恰好是后续章节的动机：

- **串行**：一次只能处理一个客户端，慢请求会阻塞所有人 → 第 12 章并发（多进程/多线程/I/O 多路复用）
- **HTTP/1.0 + `Connection: close`**：每个事务一条 TCP 连接，握手开销大 → 现代用持久连接（见下）
- **不校验路径**：`parse_uri` 直接 `strcat` 拼 URI，`../../etc/passwd` 能穿越目录 → 真实服务器必须做路径规范化
- **`sprintf` 拼头部无边界检查**：超长请求会缓冲区溢出 → 第 3 章栈溢出的现实回响

---

## 【补充】HTTP 版本演进与请求方法怎么选

**🎯 版本演进：每一代都在解决上一代的瓶颈**

| 版本 | 传输层 | 关键改进 | 解决的问题 |
|------|--------|----------|-----------|
| HTTP/1.0 | TCP | 每请求一条连接 | —（Tiny 用的就是它） |
| HTTP/1.1 | TCP | 持久连接（默认 keep-alive）、管线化、`Host` 头、分块传输 `chunked`、缓存控制 | 复用连接省握手；`Host` 头让一个 IP 托管多站点（虚拟主机） |
| HTTP/2 | TCP | 二进制分帧、**多路复用**（一条 TCP 连接并发多请求）、头部压缩 HPACK、服务器推送 | 消除 HTTP 层队头阻塞、头部冗余 |
| HTTP/3 | **QUIC/UDP** | 在 UDP 上自建可靠传输、0-RTT/1-RTT 快握手、连接迁移 | 消除 **TCP 层**队头阻塞（丢一个包不再卡住所有流）、换网不断连 |

**⚠️ HTTP/2 没根治队头阻塞**

HTTP/2 在应用层多路复用，但底下还是单条 TCP。TCP 保证按序交付，某个包丢了，后面所有流的数据都得等它重传——**TCP 层队头阻塞**。HTTP/3 把可靠性下沉到 UDP 之上的 QUIC，每条流独立，才真正解决。这就是「HTTP/3 反而用不可靠的 UDP」的深层原因。

**🎯 常用方法及语义**

| 方法 | 作用 | 幂等 | 安全（只读） | 参数位置 |
|------|------|------|------|----------|
| `GET` | 获取资源 | ✅ | ✅ | URL 查询串 |
| `HEAD` | 只要响应头、不要体（探测存在/大小/类型） | ✅ | ✅ | URL |
| `POST` | 提交数据、创建资源、触发操作 | ❌ | ❌ | 请求体 |
| `PUT` | 用请求体**整体替换/创建**指定资源 | ✅ | ❌ | 请求体 |
| `PATCH` | **局部更新**资源 | ❌ | ❌ | 请求体 |
| `DELETE` | 删除资源 | ✅ | ❌ | URL |
| `OPTIONS` | 查询服务器支持哪些方法 / CORS 预检 | ✅ | ✅ | — |

- **幂等**：同一请求发一次和发多次，服务器最终状态一样（`PUT x=5` 发几次都是 5；`POST` 加一条记录，发几次多几条）
- **安全**：只读、不改服务器状态（`GET`/`HEAD`/`OPTIONS`）

**🔧 怎么选（RESTful 惯例）**

- 读数据、可缓存、参数不敏感 → `GET`
- 只探测资源是否存在 / 拿大小和类型、不下载体 → `HEAD`
- 提交表单、新建资源、参数敏感或体积大（不该进 URL/日志）→ `POST`
- 用完整新版本覆盖已知资源 → `PUT`
- 只改资源的某几个字段 → `PATCH`
- 删除 → `DELETE`
- 跨域请求前浏览器自动发的预检 → `OPTIONS`（你一般不手写）

**⚠️ 别把写操作放进 `GET`**：`GET /delete?id=5` 会被浏览器预取、被缓存、被爬虫触发，造成「没点却执行了」的事故。改状态的操作必须用 `POST`/`PUT`/`PATCH`/`DELETE`。

---

## 易错点

- **`accept` 返回的不是 `listenfd`**：它新建一个 `connfd`，`listenfd` 继续听、`connfd` 收发数据；混用会导致「只能服务一个客户端」
- **忘了网络字节序**：IP/端口在 socket 结构里是大端，直接当本机整数打印会得到错乱的值，必须 `ntohs`/`ntohl` 或让 `getnameinfo` 代劳
- **把 TCP 当消息协议**：字节流没有边界，一次 `read` 收到的可能是半条、也可能是一条半（粘包）；必须应用层自己切分（定长/长度前缀/分隔符）
- **`getaddrinfo` 返回的链表不释放**：必须 `freeaddrinfo`，否则每次查询泄漏
- **服务端不设 `SO_REUSEADDR`**：重启时 `TIME_WAIT` 占端口导致 `bind` 失败
- **以为 backlog 就是最大并发连接数**：它只是「等 accept 的队列长度」，且被 `somaxconn` 截断；真正的并发能力取决于你多快 accept + 处理
- **`dig` 能解析就断定 DNS 没问题**：`dig` 只查 DNS，程序还要过 `/etc/hosts` 和 `nsswitch.conf`；排障要用 `getent hosts` 才贴近程序真实行为
- **把改状态的操作放进 `GET`**：会被缓存/预取/爬虫误触发

---

## 工程关联

- **`ss -lnt` / `netstat -lntp`**：看监听 socket 的 backlog 上限（`Send-Q`）和当前积压（`Recv-Q`），判断 accept 是否跟不上建连
- **`/etc/services`**：知名端口到服务名的映射，`getaddrinfo` 传 `"http"` 就是查这里得到 80
- **`/etc/resolv.conf` + `/etc/nsswitch.conf` + `/etc/hosts`**：解析链的三个配置点，DNS 排障的第一现场；现代发行版还多一层 `systemd-resolved`（`127.0.0.53`，用 `resolvectl` 管）
- **`strace -e trace=network ./tiny 8080`**：观察 `socket`/`bind`/`listen`/`accept`/`read`/`write` 系统调用序列，把 §11.4 的动作图落到真实调用上
- **`tcpdump -n port 8080` / Wireshark**：抓 TCP 三次握手和 HTTP 明文请求响应，眼见为实
- **`curl -v` / `curl -I`**：`-v` 打印完整请求响应头（对照 §11.5 的报文格式），`-I` 发 `HEAD` 只看头
- **`sysctl net.core.somaxconn` / `net.ipv4.tcp_max_syn_backlog`**：全连接/半连接队列上限，压测高并发服务前必调
- **CGI 与 `QUERY_STRING`/`dup2`**：Web 动态内容是 fork+exec+重定向的真实应用，把 §8、§10 的知识点串成一条链

---

## 实验题

**🧪 题 1：域名解析全流程对照**

用本仓库 `experiments/addrinfo.c`（`getaddrinfo` + `getnameinfo` 把域名转成 IP 列表）：

```c
// 已实现：./addrinfo www.google.com  会逐行打印所有 A 记录 IP
```

要求：
1. `make` 后运行 `./addrinfo www.baidu.com`，看它返回几个 IP（体会「一个域名多个 IP」的负载均衡）
2. 用 `dig www.baidu.com` 对照 `addrinfo` 的输出，确认二者一致
3. 用 `dig @8.8.8.8 www.baidu.com` 和 `dig @114.114.114.114 www.baidu.com` 对比，观察不同递归解析器返回的 IP 是否不同（CDN 就近调度）
4. `strace -e trace=network,openat ./addrinfo www.baidu.com`，找出它读了 `/etc/resolv.conf`、发了 UDP/53 查询的证据

**🧪 题 2：echo 客户端/服务端 + 抓包看握手**

用 `experiments/echo_server.c` / `echo_client.c`：

要求：
1. 一个终端跑 `./echo_server 8080`，另一个跑 `./echo_client localhost 8080`，输入几行验证回显
2. 开 `sudo tcpdump -n -i lo port 8080`，重新连一次，在抓包里找出**三次握手**（SYN / SYN-ACK / ACK）和**四次挥手**（FIN…）
3. 客户端连着时，在第三个终端 `ss -tnp | grep 8080`，观察连接的四元组和状态 `ESTABLISHED`
4. 把 `echo_client` 一次 `write` 改成发一大段（如 8KB），在 server 端打印每次 `read` 返回的字节数，**验证字节流被拆成多次 read**（粘包/拆包现象）

**🧪 题 3：Tiny Web 服务器 + 观察 CGI 的进程创建**

用 `tiny_web/`：

要求：
1. `make` 后 `./tiny 8080`，浏览器访问 `http://localhost:8080/home.html`（静态）和 `http://localhost:8080/cgi-bin/adder?15000&213`（动态），确认求和结果正确
2. `curl -v http://localhost:8080/home.html`，逐行对照 §11.5 的请求行/请求头/空行/响应体结构
3. `curl -I http://localhost:8080/home.html`（`HEAD` 方法）—— 观察 Tiny 只实现了 `GET`，会返回什么（提示：Tiny 对非 GET 回 501）
4. 访问动态 URL 时 `strace -f -e trace=clone,execve,dup2 ./tiny 8080`，抓到 `serve_dynamic` 里 **fork（clone）→ dup2（重定向 stdout 到 socket）→ execve（运行 adder）** 三连击
5. 尝试 `curl 'http://localhost:8080/../../../../etc/passwd'`，验证 Tiny 的目录穿越漏洞（`parse_uri` 不做路径规范化），思考真实服务器该怎么防

**🧪 题 4：backlog 与队列溢出（进阶）**

要求：
1. 把 `net.h` 里 `LISTENQ` 临时改成 `1`，重编 `echo_server`，用脚本快速发起 10 个并发连接但**故意不让 server 及时 accept**（在 accept 前 `sleep`），观察多余连接的表现
2. `ss -lnt | grep 8080` 看 `Recv-Q`（积压）逼近 `Send-Q`（上限）
3. `netstat -s | grep -i "listen"` 看有没有 overflow 计数增长
4. 对照 `sysctl net.core.somaxconn`，验证「填 1024 但 somaxconn=128 时实际只生效 128」的截断行为
