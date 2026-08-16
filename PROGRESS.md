# CSAPP 学习进度跟踪

> 只记录学习进度和过程。每节的概念总结、拓展知识、实验记录见对应章节目录下的 `summary.md`。

---

## 当前状态

| 项目 | 内容 |
|------|------|
| 当前章节 | **全书读完** ✅，总结收口于 [BOOK_SUMMARY.md](BOOK_SUMMARY.md) |
| 已完成天数 | 第 1 章、第 2 章 §2.1-2.4、第 3 章 §3.1-3.11、第 5 章 §5.1-5.14、第 6 章 §6.1-6.6、第 7 章全章、第 8 章 §8.1-8.7、第 9 章 §9.1-9.11 + §9.13、第 10 章 §10.1-10.12、第 11 章 §11.1-11.6、第 12 章 §12.1-12.7 |
| 上次学习 | 2026-08-16 |
| 下一步 | 正文无缺口。各章小结（§2.5/§3.12/§5.15/§6.7/§8.8/§9.12/§12.8）由 [BOOK_SUMMARY.md](BOOK_SUMMARY.md) 统一收口；第 4 章按计划跳过不记笔记。后续可选：零页别名的微架构定量归因（🟡 见遗留问题）、按验收七问做一轮自测 |

> 2026-06-27：提前开第 6 章 §6.1 存储技术（Day 34），完成 [Chapter6/6.1/summary.md](Chapter6/6.1/summary.md)。
> 2026-07-01：第 10 章整章总结收口于单文件 [Chapter10/summary.md](Chapter10/summary.md)（§10.1-10.12），含 RIO 实现、`shared.c`（fork 共享偏移量）、TSan 竞态实验；解答「二进制文件怎么读」疑问。
> 2026-07-04：补第 10 章 UDS 专题 [Chapter10/uds_ipc.md](Chapter10/uds_ipc.md)——本机 IPC、SCM_RIGHTS 传 fd（父子/独立进程/C++17 RAII 三版本）、抽象命名空间、路径选择与权限管控；配套代码全部本机实测。
> 2026-07-05：第 11 章网络编程整章总结收口于单文件 [Chapter11/summary.md](Chapter11/summary.md)（§11.1-11.6）——socket 接口动作图、`getaddrinfo` 惯用法、HTTP 事务与 Tiny/CGI（fork+dup2+execve 串起 §8/§10）；额外补 4 个书本外专题：TCP/UDP 区别、DNS 多解析器与解析排障、listen backlog 取值、HTTP 版本演进与方法选择。代码在 `Chapter11/experiments/`（`addrinfo`、echo C/S）与 `Chapter11/tiny_web/`。
> 2026-07-12：第 12 章 §12.1 基于进程的并发编程完成于 [Chapter12/12.1/summary.md](Chapter12/12.1/summary.md)——fork-per-connection 并发服务器、SIGCHLD 回收僵尸、父子 fd 关闭规则；IPC 补充聚焦 UDS、`mmap`、`eventfd`、gRPC 四类，代码在 `Chapter12/12.1/experiments/`。
> 2026-07-12：为 §12.2 I/O 多路复用预习新增 3 个独立文档：[select](Chapter12/12.2/select.md)、[poll](Chapter12/12.2/poll.md)、[epoll](Chapter12/12.2/epoll.md)，先建立接口语义、内核等待队列/就绪队列、复杂度和常见坑的直觉。
> 2026-07-19：第 12 章 §12.2 完成于 [Chapter12/12.2/summary.md](Chapter12/12.2/summary.md)——贯通 select/poll/epoll、readiness 与 Reactor，重点补 nonblocking ET、输出缓冲/按需 `EPOLLOUT`、背压，以及 Muduo one loop per thread 的连接单一所有权。
> 2026-07-19：第 12 章 §12.3 完成于 [Chapter12/12.3/summary.md](Chapter12/12.3/summary.md)，代码在 [Chapter12/12.3/experiments](Chapter12/12.3/experiments)——梳理线程共享/私有资源、pthread 创建/终止/join/detach/once，并用教材 thread-per-connection echo server 串起参数所有权、共享 fd 关闭语义和线程资源上限。
> 2026-07-26：第 12 章 §12.4-§12.5 完成于 [§12.4](Chapter12/12.4/summary.md) 与 [§12.5](Chapter12/12.5/summary.md)——从变量实例、所有权、生命周期、不变量与 happens-before 统一分析同步错误根因；重点解释 one loop per thread 如何用连接单线程所有权减少锁，并扩展 bounded blocking MPMC、SPSC/per-slot-sequence MPMC ring，以及读者优先、写者优先和 FIFO 分阶段无饥饿读写锁。
> 2026-07-26：第 12 章 §12.6-§12.7 完成于 [§12.6](Chapter12/12.6/summary.md) 与 [§12.7](Chapter12/12.7/summary.md)——§12.6 用并行归约串起任务划分、speedup/efficiency、强弱扩展、Amdahl 上限和 Linux `perf stat` 测量；§12.7 从 API 契约梳理四类线程不安全函数、可重入性、遗留库封装、参数竞态与全局锁顺序。
> 2026-08-16：补完最后两处正文缺口，**全书读完**。§3.11 完成于 [Chapter3/3.11/summary.md](Chapter3/3.11/summary.md)——浮点寄存器与 ss/sd/ps/pd 后缀、SSE2 与 AVX(VEX) 编码对比、浮点参数独立计数与「没有 callee-saved 的 %xmm」、转换指令的截断语义、符号操作的位掩码实现与 -0.0 陷阱、comisd/ucomisd 的 PF 无序标志、常数只能从 .rodata 加载；代码在 `Chapter3/3.11/experiments/`。§9.11 完成于 [Chapter9/9.11/summary.md](Chapter9/9.11/summary.md)——十类内存错误逐个触发并用 ASan/Valgrind/编译器警告三方交叉验证，产出工具能力边界对照表；代码在 `Chapter9/9.11/experiments/`。
> 2026-08-16：产出全书总结 [BOOK_SUMMARY.md](BOOK_SUMMARY.md)，一次性收口书中全部小结小节（§2.5/§3.12/§5.15/§6.7/§8.8/§9.12/§12.8）——含「一个程序的一生」主线图、逐章骨干与必记易错点、四条跨章纵向主线（性能定位顺序 / 一个地址的完整旅程 / 控制流突变四层次 / 边界与所有权）、一张延迟速查表（含本机实测 L1≈4·L2≈14·L3≈40·DRAM≈300+ cycle）以及验收标准七问的标准答案。同步修正 PROGRESS 中第 2、3、5、6、8、9、12 章过时的状态标记。

---

## 章节完成记录

> **全书总结**：[BOOK_SUMMARY.md](BOOK_SUMMARY.md) ✅（2026-08-16）——取代书中所有小结小节，逐章骨干 + 必记易错点 + 跨章串讲 + 延迟速查表 + 验收七问答案。

### 第 1 章：计算机系统漫游 ✅

| Day | 小节 | 状态 | 总结 |
|-----|------|------|------|
| Day 1 | 1.1-1.3 | ✅ | — |
| Day 2 | 1.4-1.4.2 | ✅ | — |
| Day 3 | 1.5-1.7 | ✅ | — |
| Day 4 | 1.7.1-1.7.4 | ✅ | — |
| Day 5 | 1.8-1.10 | ✅ | — |

### 第 2 章：信息的表示和处理 ✅

| Day | 小节 | 状态 | 总结 |
|-----|------|------|------|
| Day 6 | 2.1-2.1.3 | ✅ | [Chapter2/2.1/summary.md](Chapter2/2.1/summary.md) |
| Day 7 | 2.1.4-2.1.9 | ✅ | [Chapter2/2.1/summary.md](Chapter2/2.1/summary.md) |
| Day 8 | 2.2-2.2.3 | ✅ | [Chapter2/2.2/summary.md](Chapter2/2.2/summary.md) |
| Day 9 | 2.2.4-2.2.5 | ✅ | [Chapter2/2.2/summary.md](Chapter2/2.2/summary.md) |
| Day 10 | 2.2.6-2.2.8 | ✅ | [Chapter2/2.2/summary.md](Chapter2/2.2/summary.md) |
| Day 11 | 2.3-2.3.3 | ✅ | [Chapter2/2.3/summary.md](Chapter2/2.3/summary.md) |
| Day 12 | 2.3.4-2.3.5 乘以常数 | ✅ | [Chapter2/2.3/summary.md](Chapter2/2.3/summary.md) |
| Day 13 | 2.3.6-2.3.8 除以 2 的幂 | ✅ | [Chapter2/2.3/summary.md](Chapter2/2.3/summary.md) |
| Day 14 | 2.4-2.4.3 | ✅ | [Chapter2/2.4/summary.md](Chapter2/2.4/summary.md) |
| Day 15 | 2.4.4-2.4.5 | ✅ | [Chapter2/2.4/summary.md](Chapter2/2.4/summary.md) |
| — | 2.5 小结 | ✅ | [BOOK_SUMMARY.md](BOOK_SUMMARY.md) 收口 |

### 第 3 章：程序的机器级表示 🔄

| Day | 小节 | 状态 | 总结 |
|-----|------|------|------|
| Day 16 | 3.1-3.2.1 | ✅ | [Chapter3/3.2/summary.md](Chapter3/3.2/summary.md) |
| Day 17 | 3.2.2-3.3 | ✅ | [Chapter3/3.2/summary.md](Chapter3/3.2/summary.md) |
| Day 18 | 3.4-3.4.2 | ✅ | [Chapter3/3.4/summary.md](Chapter3/3.4/summary.md) |
| Day 19 | 3.4.3-3.5 | ✅ | [Chapter3/3.5/summary.md](Chapter3/3.5/summary.md) |
| Day 20 | 3.5.1-3.5.5 | ✅ | [Chapter3/3.5/summary.md](Chapter3/3.5/summary.md) |
| Day 21 | 3.6-3.6.4 | ✅ | [Chapter3/3.6/summary.md](Chapter3/3.6/summary.md) |
| Day 22 | 3.6.5-3.6.6 | ✅ | [Chapter3/3.6/summary.md](Chapter3/3.6/summary.md) |
| Day 23 | 3.6.7-3.6.8 | ✅ | [Chapter3/3.6/summary.md](Chapter3/3.6/summary.md) |
| Day 24 | 3.7-3.7.3 | ✅ | [Chapter3/3.7/summary.md](Chapter3/3.7/summary.md) |
| Day 25 | 3.7.4-3.7.6 | ✅ | [Chapter3/3.7/summary.md](Chapter3/3.7/summary.md) |
| Day 26 | 3.8-3.8.5 | ✅ | [Chapter3/3.8/summary.md](Chapter3/3.8/summary.md) |
| Day 27 | 3.9-3.9.3 | ✅ | [Chapter3/3.9/summary.md](Chapter3/3.9/summary.md) |
| Day 28 | 3.10-3.10.3 | ✅ | [Chapter3/3.10/summary.md](Chapter3/3.10/summary.md) |
| Day 29 | 3.10.4-3.10.5 栈保护、变长栈帧 | ✅ | [Chapter3/3.10/summary.md](Chapter3/3.10/summary.md) |
| Day 30 | 3.11 浮点代码 | ✅ | [Chapter3/3.11/summary.md](Chapter3/3.11/summary.md)；[experiments](Chapter3/3.11/experiments) |
| — | 3.12 小结 | ✅ | [BOOK_SUMMARY.md](BOOK_SUMMARY.md) 收口 |

### 第 4 章：处理器体系结构 ⏭️

> 按学习计划**跳过，不记录笔记**。需要的执行模型（超标量、乱序、流水线、投机执行）已由 [§5.7](Chapter5/5.7/summary.md) 从性能视角补齐。

### 第 7 章：链接 ✅

| Day | 小节 | 状态 | 总结 |
|-----|------|------|------|
| Day 31 | 7.1-7.4 | ✅ | [Chapter7/summary.md](Chapter7/summary.md) |
| Day 32 | 7.5-7.6.3 | ✅ | [Chapter7/summary.md](Chapter7/summary.md) |
| Day 33 | 7.7-7.9 | ✅ | [Chapter7/summary.md](Chapter7/summary.md) |
| Day 34 | 7.10-7.15 | ✅ | [Chapter7/summary.md](Chapter7/summary.md) |

> 第 7 章内容较短，整章合并为单个 `Chapter7/summary.md`。

### 第 8 章：异常控制流 ✅

| Day | 小节 | 状态 | 总结 |
|-----|------|------|------|
| Day 35 | 8.1-8.1.3 | ✅ | [Chapter8/8.1/summary.md](Chapter8/8.1/summary.md) |
| Day 36 | 8.2-8.2.5 | ✅ | [Chapter8/8.2/summary.md](Chapter8/8.2/summary.md) |
| Day 37 | 8.3-8.4.3 | ✅ | [Chapter8/8.3-8.4/summary.md](Chapter8/8.3-8.4/summary.md) |
| Day 38 | 8.4.4-8.4.6 | ✅ | [Chapter8/8.3-8.4/summary.md](Chapter8/8.3-8.4/summary.md) |
| Day 39 | 8.5-8.5.3 | ✅ | [Chapter8/8.5-8.6/summary.md](Chapter8/8.5-8.6/summary.md) |
| Day 40 | 8.5.4-8.5.7 | ✅ | [Chapter8/8.5-8.6/summary.md](Chapter8/8.5-8.6/summary.md) |
| Day 41 | 8.6 | ✅ | [Chapter8/8.5-8.6/summary.md](Chapter8/8.5-8.6/summary.md) |
| Day 42 | 8.7 | ✅ | [Chapter8/8.7/summary.md](Chapter8/8.7/summary.md) |
| — | 8.8 小结 | ✅ | [BOOK_SUMMARY.md](BOOK_SUMMARY.md) 收口 |

### 第 9 章：虚拟内存 🔄

| Day | 小节 | 状态 | 总结 |
|-----|------|------|------|
| Day 42 | 9.1-9.3（含 9.3.3-9.3.6） | ✅ | [Chapter9/9.1-9.5/summary.md](Chapter9/9.1-9.5/summary.md) |
| Day 43 | 9.4-9.5 | ✅ | [Chapter9/9.1-9.5/summary.md](Chapter9/9.1-9.5/summary.md) |
| Day 44 | 9.6-9.6.4 | ✅ | [Chapter9/9.6/summary.md](Chapter9/9.6/summary.md) |
| Day 45 | 9.7 | ✅ | [Chapter9/9.7-9.8/summary.md](Chapter9/9.7-9.8/summary.md) |
| Day 46 | 9.8-9.8.4 | ✅ | [Chapter9/9.7-9.8/summary.md](Chapter9/9.7-9.8/summary.md) |
| Day 47 | 9.9-9.10 | ✅ | [Chapter9/9.9-9.10/summary.md](Chapter9/9.9-9.10/summary.md) |
| Day 47 | 9.11 常见的与内存有关的错误 | ✅ | [Chapter9/9.11/summary.md](Chapter9/9.11/summary.md)；[experiments](Chapter9/9.11/experiments) |
| — | 9.12 小结 | ✅ | [BOOK_SUMMARY.md](BOOK_SUMMARY.md) 收口 |
| 补充 | 9.13 内核虚拟内存说明书（ARM64/6.x，多文件） | ✅ | [9.13-kernel-vm-source](Chapter9/kernel-vm-source/00-index.md) |
| 补充 | zswap：512MB 小内存机器的 swap 参数与优化 | ✅ | [Chapter9/zswap.md](Chapter9/zswap.md) |

### 第 5 章：优化程序性能 ✅

| Day | 小节 | 状态 | 总结 |
|-----|------|------|------|
| Day 48 | 5.1-5.3 | ✅ | [5.1-5.3](Chapter5/5.1-5.3/summary.md) |
| Day 49 | 5.4-5.6 | ✅ | [5.4-5.6](Chapter5/5.4-5.6/summary.md) |
| Day 50 | 5.7 | ✅ | [5.7](Chapter5/5.7/summary.md) |
| Day 51 | 5.8-5.10 循环展开、多累加器、重结合 | ✅ | [5.8-5.9](Chapter5/5.8-5.9/summary.md)（§5.10 结果总结并入） |
| Day 52 | 5.11-5.14 限制因素、内存性能、profiling | ✅ | [5.11](Chapter5/5.11/summary.md) · [5.12-5.14](Chapter5/5.12-5.14/summary.md) |
| — | 5.15 小结 | ✅ | [BOOK_SUMMARY.md](BOOK_SUMMARY.md) 收口 |

### 第 6 章：存储器层次结构 ✅

| Day | 小节 | 状态 | 总结 |
|-----|------|------|------|
| Day 52 | 6.1-6.3.2 | ✅ | [6.1](Chapter6/6.1/summary.md) · [6.2-6.3](Chapter6/6.2-6.3/summary.md) |
| Day 53 | 6.4-6.6 | ✅ | [6.4+6.5](Chapter6/6.4-6.5/summary.md) · [6.6](Chapter6/6.6/summary.md)（6.5 已并入 6.4） |
| — | 6.7 小结 | ✅ | [BOOK_SUMMARY.md](BOOK_SUMMARY.md) 收口 |

### 第 10 章：系统级 I/O ✅

> 全章总结统一在单文件 [Chapter10/summary.md](Chapter10/summary.md)（§10.1-10.12）；代码在 `Chapter10/rio/`（RIO 实现）与 `Chapter10/experiments/`（`shared.c`、`tsan_race.c`、UDS 专题一系列）。
> 补充专题 [Chapter10/uds_ipc.md](Chapter10/uds_ipc.md)：Unix Domain Socket 本机 IPC（挂第 10 章，非书中正式小节）——socket 也是文件/复用 RIO、SCM_RIGHTS 传 fd、抽象命名空间、路径与权限管控。

| Day | 小节 | 状态 | 总结 |
|-----|------|------|------|
| Day 54 | 10.1-10.5.2 Unix I/O、RIO | ✅ | [Chapter10/summary.md](Chapter10/summary.md) |
| Day 55 | 10.6-10.12 元数据/目录/共享文件/重定向/标准 I/O/选择准则 | ✅ | [Chapter10/summary.md](Chapter10/summary.md) |
| 专题 | UDS 本机 IPC + 传 fd + 抽象命名空间 | ✅ | [Chapter10/uds_ipc.md](Chapter10/uds_ipc.md) |

### 第 11 章：网络编程 ✅

> 全章总结统一在单文件 [Chapter11/summary.md](Chapter11/summary.md)（§11.1-11.6）；代码在 `Chapter11/experiments/`（`addrinfo.c` 域名→IP、echo 客户端/服务端、`http_client.cpp` libcurl C++ 客户端）与 `Chapter11/tiny_web/`（Tiny Web 服务器 + CGI）；socket 封装 `Chapter11/socket/net.h`，RIO 复用 `Chapter10/rio`。
> 含 4 个书本外补充专题：TCP vs UDP、DNS 多公共解析器与域名解析排障、listen backlog 取值、HTTP 版本演进与请求方法选择；另有独立专题 [TCP / UDP 协议与 tcpdump 排障](Chapter11/tcp_udp.md)、[libcurl C++ HTTP 客户端](Chapter11/libcurl_cpp.md)。

| Day | 小节 | 状态 | 总结 |
|-----|------|------|------|
| Day 56 | 11.1-11.4 网络/IP 地址/DNS/socket 接口/getaddrinfo | ✅ | [Chapter11/summary.md](Chapter11/summary.md) |
| Day 57 | 11.5-11.6 HTTP/CGI/Tiny Web 服务器 | ✅ | [Chapter11/summary.md](Chapter11/summary.md) |
| 补充 | TCP/UDP · DNS 排障 · backlog · HTTP 版本与方法 | ✅ | [Chapter11/summary.md](Chapter11/summary.md)；[tcp_udp.md](Chapter11/tcp_udp.md)；[libcurl_cpp.md](Chapter11/libcurl_cpp.md) |

### 第 12 章：并发编程 ✅

> §12.1 总结在 [Chapter12/12.1/summary.md](Chapter12/12.1/summary.md)；代码在 `Chapter12/12.1/experiments/`，包含基于进程的并发 echo 服务器/客户端、UDS/`mmap`/`eventfd` C 例程，以及可选 gRPC C++ 例程。§12.2 总结在 [Chapter12/12.2/summary.md](Chapter12/12.2/summary.md)，专题材料包括 [select](Chapter12/12.2/select.md)、[poll](Chapter12/12.2/poll.md)、[epoll](Chapter12/12.2/epoll.md)、[Reactor](Chapter12/12.2/reactor.md)、[Muduo one loop per thread](Chapter12/12.2/muduo_one_loop_per_thread.md)；配套可运行例程在 `Chapter12/12.2/experiments/`。§12.3 总结在 [Chapter12/12.3/summary.md](Chapter12/12.3/summary.md)，覆盖 POSIX 线程执行模型、生命周期管理和 thread-per-connection 的所有权规则。§12.4 总结在 [Chapter12/12.4/summary.md](Chapter12/12.4/summary.md)，覆盖共享变量判定、C++ 内存模型、同步错误根因和 one loop per thread 的所有权边界。§12.5 总结在 [Chapter12/12.5/summary.md](Chapter12/12.5/summary.md)，覆盖 semaphore/CV、生产者—消费者、死锁/饥饿，以及 SPSC/MPMC 队列和公平读写锁。§12.6 总结在 [Chapter12/12.6/summary.md](Chapter12/12.6/summary.md)，覆盖线程级并行、并行归约、speedup/efficiency、强弱扩展、Amdahl 上限和可复现测量。§12.7 总结在 [Chapter12/12.7/summary.md](Chapter12/12.7/summary.md)，覆盖四类线程不安全函数、可重入性、库函数封装、race 与 deadlock 的 API 契约。

| Day | 小节 | 状态 | 总结 |
|-----|------|------|------|
| Day 58 | 12.1 基于进程的并发编程 | ✅ | [Chapter12/12.1/summary.md](Chapter12/12.1/summary.md) |
| Day 59 | 12.2 基于 I/O 多路复用的并发编程 | ✅ | [Chapter12/12.2/summary.md](Chapter12/12.2/summary.md)；[experiments](Chapter12/12.2/experiments)（select/poll/epoll LT + epoll ET） |
| Day 60 | 12.3-12.4 基于线程的并发编程、共享变量 | ✅ | [§12.3](Chapter12/12.3/summary.md) · [§12.4](Chapter12/12.4/summary.md) |
| Day 61 | 12.5-12.7 同步、并行、线程安全、竞态/死锁 | ✅ | [§12.5](Chapter12/12.5/summary.md) · [§12.6](Chapter12/12.6/summary.md) · [§12.7](Chapter12/12.7/summary.md) |
| — | 12.8 小结 | ✅ | [BOOK_SUMMARY.md](BOOK_SUMMARY.md) 收口 |

---

## 实验记录

| 日期 | 章节 | 实验文件 | 实验内容 | 关键结论 |
|------|------|----------|----------|----------|
| 2026-03-30 | §2.4 | [Chapter2/2.4/exp_precision.cpp](Chapter2/2.4/exp_precision.cpp) | 用 `memcpy` 提取 double 位模式，对比 `0.3` 与 `0.1+0.2` 的 IEEE 754 布局 | 两者 frac 差 1 ULP，根源是两条路径的舍入累积方向不同 |
| 2026-06-07 | §8.5 | [Chapter8/8.5-8.6/sync/procmask.c](Chapter8/8.5-8.6/sync/procmask.c) | fork 前阻塞 SIGCHLD vs 不阻塞，观察 addjob/deletejob 竞态 | 不同步时 deletejob 会删到尚未 addjob 的作业；父进程保护 addjob 的 sigprocmask 若误把 oldset 传成 prev 会覆盖原掩码导致 SIGCHLD 永久阻塞、死循环 |
| 2026-06-07 | §8.5 | [Chapter8/8.5-8.6/shell/main.c](Chapter8/8.5-8.6/shell/main.c) | 用 `ps -o stat --ppid` 对比 §8.4 旧版与 §8.5 优化版 shell 的后台作业 | 旧版后台作业结束后变 `Z`（defunct 僵尸）；新版 SIGCHLD handler 用 `waitpid(WNOHANG)` 循环回收，无僵尸 |
| 2026-06-11 | §9.1-9.3 | [Chapter9/9.1-9.5/locality/main.c](Chapter9/9.1-9.5/locality/main.c) | perf stat 测时间局部性（pass 全扫 ×10 vs 1MB 分块 ×10），首次解读混合架构 perf 输出 | 未初始化的 256MB .bss 数组实际 RSS 仅 1.5MB（共享零页），原始数据全部失真；写入初始化后 pass 版 LLC-load-misses 是 blocked 版的 12.5 倍（626K vs 50K），但因硬件预取器掩盖，墙钟时间只差 20% |
| 2026-06-16 | 第 9 章 | [Chapter9/虚拟内存实战——日常开发场景串讲.md](Chapter9/虚拟内存实战——日常开发场景串讲.md) | 以 8 个日常场景（启动/malloc/读写文件/fork/爆栈/段错误/共享内存/TLB）+ 1 个综合排查横向串联全章，每场景配图 + 可编译代码 + 可观测命令；全部代码 gcc -Wall 验证通过 | first touch：malloc 后 RSS 1.1MB、触摸 256MB 后涨到 263MB、free 不降；COW：子进程只读 minflt 增量 0、改写 16384（=64MB/4KB）；段错误 si_code=1(SEGV_MAPERR) 对应缺页第一关；坑：子进程 `_exit` 前 printf 须 `fflush` 否则管道下全缓冲丢输出 |
| 2026-06-28 | §5.14 | [Chapter5/5.12-5.14/expirements/dedup.c](Chapter5/5.12-5.14/expirements/dedup.c) | profile 引导优化 + Amdahl 验证 + perf 三步法（record/report/annotate）vs gprof 对比。O(n²) 去重→哈希，分段计时 dedup/checksum | Amdahl 预测 7.36× vs 实测 7.30×（误差<1%）：dedup 自身加速 187× 但整体仅 7.3×，因 checksum 优化后占 96.6% 成新瓶颈、卡在 1/(1-α)=7.62× 天花板；**gprof 翻车**——把真热点 strcmp(libc 无符号)的 63% 错归给 `_init`，因 self time 不含被调库函数；perf 采 PC 正确钉到 `__strcmp_avx2` 65%，annotate 落到 `call strcmp@plt`(24%) 指令行 |
| 2026-06-29 | §9.13 | [Chapter9/9.13-kernel-vm-source/experiments/zero_page.c](Chapter9/kernel-vm-source/experiments/zero_page.c) | 读未初始化匿名内存（256MB/65536 页）分只读/写两阶段，量化 zero page 别名（statm RSS + stat minflt） | 只读遍历 RSS 仅 +132KB、minflt +65537（每页缺页映射共享 ZERO_PAGE 但不计 RSS）；写一遍后 RSS +256MB、minflt +65536；majflt 全程 0——「零页别名惩罚」根因坐实：`do_anonymous_page` 只读分支让所有虚拟页别名到同一物理零页 |
| 2026-06-29 | §9.13 | [Chapter9/9.13-kernel-vm-source/experiments/vmstat_fault.c](Chapter9/kernel-vm-source/experiments/vmstat_fault.c) | minor vs major fault 分离：malloc+memset 制造 minor；fadvise(DONTNEED)+mmap+MADV_RANDOM 制造 major | minor 段 minflt 32774/majflt 0；major 段 majflt 32768（恰好=页数）/minflt 0；**必须 MADV_RANDOM**——否则 readahead+fault-around 把顺序读的 major 几乎全转成 minor（加之前 major 仅 1） |
| 2026-06-29 | §9.13 | [Chapter9/9.13-kernel-vm-source/experiments/dirty_writeback.c](Chapter9/kernel-vm-source/experiments/dirty_writeback.c) | 持续写文件不 fsync，观察 /proc/meminfo Dirty/Writeback 水位 + 本轮吞吐曲线，看脏页回写子系统的限流（32GB/NVMe ext4 写 12GB） | 起步 ~6000MB/s（纯 page cache，Dirty 线性涨）→ ~1GB 处吞吐腰斩到 ~3000MB/s（balance_dirty_pages 软节流）→ ~2GB 后 Writeback 转非 0（flusher 介入）、Dirty 稳在 ~2.2-3.1GB 平台；**NVMe 快，全程稳在 background 水位、没撞 limit**，阈值基于可脏内存而非 MemTotal 故平台低于 32GB×10% |
| 2026-07-01 | §10.8 | [Chapter10/experiments/shared.c](Chapter10/experiments/shared.c) | 对比「两次 open 同一文件」与「open 后 fork」两种打开方式下 `read` 的偏移量语义（foobar.txt 内容 `foobar`） | 两次 open → 两个独立打开文件表项、各自 k=0，两次 read 都得 `f`；open 后 fork → 父子共享同一打开文件表项、同一个 k，子读 `f` 把 k 推到 1、父接着读 `o`。**偏移量绑定在打开文件表项上**：open 次数决定表项个数、fork 决定谁共享表项；strace `-f` 下版本 B 只一次 openat + 一次 clone |
| 2026-07-04 | 第 10 章·UDS 专题 | [Chapter10/uds_ipc.md](Chapter10/uds_ipc.md)；`experiments/` 下 `uds_server.c`·`uds_client.c`·`uds_passfd.c`·`passfd_send/recv.c`·`unix_socket.hpp`+`passfd_cpp.cpp` | UDS 本机 IPC：C/S 回显复用 RIO（`make uds`）；SCM_RIGHTS 传 fd 三版本——socketpair 父子（`passfd`）、命名 UDS 独立进程（`passfd2`）、C++17 RAII 封装（`passfd_cpp`）；server 健壮性收尾 | **传 fd 传的是内核对象非数字**：send 端 fd=5、recv 端 fd=4 却读到同一文件；机制只依赖一条已连通 AF_UNIX 连接，**与进程亲缘/建连方式无关**（socketpair 限父子，命名 UDS 任意独立进程），且 fd 不能跨机器。三个 cmsg 坑：必带 ≥1 字节数据、缓冲用 `CMSG_SPACE`/长度用 `CMSG_LEN` 别手算、收端先校验 level/type/len 再取。server 修 3 个真 bug（`!bind` 反判返回值、`buf_toupper` 越界传 RIO_BUFSIZE、路径误用常量）+ 补 SIGPIPE 忽略/accept EINTR 重试/`setvbuf` 防 `_exit` 丢日志；C++ 版结论：cmsg 机制省不掉，RAII 省掉的是 close/memset/errno 样板 |
| 2026-07-04 | UDS·抽象命名空间 | `experiments/uds_server.c`·`uds_client.c`（`make uds_abstract`） | `@` 前缀切换抽象命名空间 vs 路径式，`fill_uds_addr()` 按 `@` 统一分流并返回精确 addrlen | 抽象式 `sun_path[0]='\0'`+名字随后：**不落文件系统、内核自动回收、无需 unlink、无 EADDRINUSE**（实测 server 杀掉可即时重启同名）；`ss -xl` 与 `/proc/net/unix` 显示 `@csapp_uds`、`/tmp` 无文件。头号坑：**addrlen 必须 `offsetof+1+strlen` 精确算**，传 `sizeof(addr)` 会把尾随填零字节算进名字→能自连却拒正确外部客户端。代价：失去文件/目录权限管控（只能 `SO_PEERCRED`）、Linux 特有。路径选择：生产用 `/run` 或 `$XDG_RUNTIME_DIR`+靠目录权限，别用 `/tmp`（符号链接/抢占攻击） |
| 2026-07-08 | 第 11 章·TCP/UDP 专题 | [Chapter11/capture_tcp_udp.sh](Chapter11/capture_tcp_udp.sh)；[Chapter11/tcp_udp.md](Chapter11/tcp_udp.md)；`/tmp/netcap/*.pcap` | 用 tcpdump 抓 TCP 握手/HTTP 明文 payload/FIN 挥手、UDP DNS 一问一答、netem 丢包实验 | 三次握手本质是交换并确认双方 ISN，同时协商 MSS/SACK/timestamp/window scale；HTTP 请求 `seq 1:76` 被服务端 `ack 76` 确认，说明连接和请求发送成功，但当前网络把域名导向 `198.18.0.156` 且不回 body，故 `curl` 超时属于应用/代理路径问题而非 TCP 连接失败；UDP DNS 头只有端口/长度/校验和，transaction ID 和重试由 DNS 应用层负责 |
| 2026-07-08 | 第 11 章·libcurl C++ 专题 | [Chapter11/libcurl_cpp.md](Chapter11/libcurl_cpp.md)；[Chapter11/experiments/http_client.cpp](Chapter11/experiments/http_client.cpp) | 用 libcurl easy API 写 C++17 RAII HTTP 客户端，覆盖 GET/POST JSON/自定义 header/超时/错误处理 | `make http_client` 已真实编译链接通过，动态链接 `/lib/x86_64-linux-gnu/libcurl.so.4`；easy 生命周期是 global_init→easy_init→setopt→perform→getinfo→cleanup，核心是 WRITE/HEADER/READ 回调；错误要分两层：`CURLcode` 表示 DNS/连接/超时/TLS 等传输失败，HTTP 4xx/5xx 是传输成功后的业务状态 |
| 2026-07-12 | §12.1 | [Chapter12/12.1/experiments](Chapter12/12.1/experiments)；[Chapter12/12.1/summary.md](Chapter12/12.1/summary.md) | 基于进程的并发 echo 服务器 + 典型 IPC 例程：`make process-echo` 验证 fork-per-connection，`make ipc` 跑通 UDS/`mmap`/`eventfd`，另补 `grpc_echo/` 可选 gRPC 示例 | 父进程只保留 `listenfd` 并关闭 `connfd`，子进程关闭 `listenfd` 后服务当前连接；`SIGCHLD` handler 必须 `while waitpid(-1, WNOHANG)` 收干净僵尸；IPC 选择核心看本机控制面(UDS)、大块共享数据(`mmap`)、事件通知(`eventfd`)还是跨语言/跨机器服务接口(gRPC) |
| 2026-07-12 | §12.2 | [Chapter12/12.2/experiments](Chapter12/12.2/experiments)；[select](Chapter12/12.2/select.md)；[poll](Chapter12/12.2/poll.md)；[epoll](Chapter12/12.2/epoll.md)；[summary](Chapter12/12.2/summary.md) | `make clean all` 编译四个 server，`make demo` 依次跑 select/poll/epoll LT 并校验两条顺序回显；另有 `epoll_et_echo_server.c` 展示 nonblocking ET、accept/read 到 `EAGAIN`、输出缓冲和按需 `EPOLLOUT`，不在自动 demo 中 | 三者业务相同但等待机制不同：select 每轮复制/修改 `fd_set` 且受 `FD_SETSIZE` 约束；poll 分离 `events/revents` 但仍线性扫描；epoll 长期保存 interest set 并返回 ready event；基础 server 用单次 `read` 避免半行阻塞，但阻塞写仍非生产级，ET 版才补齐 drain、短写和背压边界 |
| 2026-07-19 | §12.3 | [Chapter12/12.3/experiments](Chapter12/12.3/experiments)；[summary](Chapter12/12.3/summary.md) | 实现教材 thread-per-connection echo server；`make clean all` 无新源码警告，`make demo` 先让一条连接延迟 2 秒发送，再用两个 2 秒超时客户端验证并发回显，并重复运行确认不遗留监听进程 | 每个 `connfd` 必须有独立堆参数；`pthread_create` 成功是所有权转移点，失败由 main `free+close`，成功后由 detached worker `free+echo+close`；线程共享 fd table，所以 main 不能照搬 fork 版提前关闭 `connfd` |
| 2026-08-16 | §3.11 | [Chapter3/3.11/experiments](Chapter3/3.11/experiments)（`fp_asm.c`·`negzero.c`·`vararg.c`）；[summary](Chapter3/3.11/summary.md) | `make asm` 生成 -Og/-O2 汇编逐条对照；`make avx` 对比 SSE2 与 VEX 机器码；`make rodata` dump 常数池；`make negzero` 实测 -0.0/NaN 行为；`make vararg` 验证变参 %al 与 xmm 溢出 | 默认基线生成 SSE2 两操作数 `addsd`（`f2 0f 58 c1`），`-mavx2` 才是三操作数 `vaddsd`（`c5 fb 58 c1`），同为 4 字节；**16 个 %xmm 全是 caller-saved**——`keep()` 必须 `movsd %xmm0,8(%rsp)` 跨调用保活，而整数版 `keep_int()` 只需 `pushq %rbx`；`fp_abs(-0.0)` 返回 `0x8000000000000000`、`fp_fabs(-0.0)` 返回 `0x0`，正是编译器不敢把三目优化成 `andpd` 的原因；常数池实测 `.LC0`=符号掩码、`.LC2`=绝对值掩码、`.LC3`=3.14、`.LC4`=1.0，两个掩码当 double 解释恰好是 -0.0 和 NaN |
| 2026-08-16 | §9.11 | [Chapter9/9.11/experiments](Chapter9/9.11/experiments)（`mem_errors.c`）；[summary](Chapter9/9.11/summary.md) | 十类内存错误逐个触发，`make demo` 对比普通编译与 ASan，`make demo-stdin` 跑需输入的两类，`make vg-uninit` 展示 ASan 盲区；另用 valgrind 逐类交叉验证 | **十类里有四类退出码为 0**（uninit/offbyone/ptr_arith/leak）——普通编译下完全无感；崩溃点≠错误点：`sizeof` 算错在第 59 行越界写，却崩在第 61 行 `free` 的 glibc 堆断言；工具边界实测坐实——**ASan 抓不到读未初始化内存**（完全静默，需 valgrind `--track-origins` 追溯到分配点），**valgrind 抓不到栈缓冲区溢出**（只有 canary 兜住），**界内指针错位两者都抓不到**；ASan 与 valgrind 报泄漏数一致（5120 字节 / 2 块） |
| 2026-07-26 | §12.4-§12.5 | [§12.4 summary](Chapter12/12.4/summary.md)；[§12.5 summary](Chapter12/12.5/summary.md) | 以变量引用图、进度图和 C++20 示例推导共享变量、happens-before、semaphore/CV、同步错误分类；拓展 one loop per thread、SPSC/MPMC ring 和三种读写锁，并对核心类做 `g++ -std=c++20 -Wall -Wextra -Wpedantic -pthread -fsyntax-only` 检查 | 同步正确性的核心是给 identity/ownership/lifetime/invariant/ordering 建立完整协议；one loop per thread 以连接单线程所有权缩小共享面；读者/写者偏好只能避免一侧饥饿，要靠 FIFO 到达顺序加 reader phase 才能让已入队的两侧请求都不被无限超越 |

---

## 待办

- 回填存量 summary 文档到 `CLAUDE.md` 新定义的标准格式 ✅（§3.6、§2.3、§3.4 全部完成）
- 全书总结 [BOOK_SUMMARY.md](BOOK_SUMMARY.md) ✅（2026-08-16，收口全部小结小节）
- ✅ **§3.11 浮点代码**（2026-08-16）：[summary](Chapter3/3.11/summary.md) + [experiments](Chapter3/3.11/experiments)
- ✅ **§9.11 常见的与内存有关的错误**（2026-08-16）：[summary](Chapter9/9.11/summary.md) + [experiments](Chapter9/9.11/experiments)
- ⏭️ 第 4 章 处理器体系结构：按计划跳过，不记录笔记

> **全书正文已无缺口。**

---

## 遗留问题

> 学习中遇到的疑问，已解决的标记 ✅，待解决的标记 ❓

- ✅ 二进制文件该怎么读（2026-07-01，§10.11 疑问）：文本函数（`fgets`/`%s`/`rio_readlineb`）以 `\n`(0x0a)/`\0`(0x00) 为边界，而二进制里这些只是普通数据字节，会被误当行尾/串尾截断，Windows 文本模式还会改写 `\r\n`。正解是**按字节数读、不按分隔符读**：C 用 `fread`+`fopen("rb")`、裸用 `read`/`rio_readn`（天然读满 n）、C++ 用 `ifstream(ios::binary).read()`（绝不用 `>>`/`getline`）。三个真实坑：字节序（读进 int 是文件原始序，跨机要 `ntohl`/`be32toh`）、结构体 padding（`fwrite(&struct)` 会写出填充字节，换架构布局变，需逐字段序列化或 `packed`）、短读是常态（靠返回值驱动循环）。工程一般不手写格式，交给 Protobuf/FlatBuffers/Cap'n Proto。展开见 [Chapter10/summary.md](Chapter10/summary.md) 文末「附：二进制文件到底怎么读」
- ✅ Reactor 模型是什么（2026-07-12，§12.2 疑问）：Reactor 是事件驱动服务器模式——一个或少量 event loop 通过 select/poll/epoll 等 demultiplexer 等待 fd ready，拿到事件后分发给对应 handler；内核只通知“可读/可写/错误”等 readiness，真正 `accept/read/write` 由用户态 handler 执行。它和 Proactor 的关键区别是 Reactor 收到的是“可以做 I/O”，Proactor 收到的是“I/O 已完成”。工程上 Nginx/Redis/libuv/Netty 都是 Reactor 思路的变体，核心坑是 handler 不能长时间阻塞，写路径要靠输出缓冲区和按需监听 `EPOLLOUT`。
- ✅ 网络线程如何安全地把工作交给 worker（2026-07-26，§12.4 疑问）：one loop per thread 下不把裸 fd 或 loop 内部 `Buffer*` 交给业务 worker；I/O loop 先把输入解析成拥有独立生命周期的 `Request`，worker 只做计算/阻塞业务，再通过线程安全 completion queue 把结果和 `weak_ptr<Connection>` 投递回连接所属 loop，由 loop 校验对象仍存活且处于 connected 状态后执行 `send_in_loop`。`shared_ptr` 只解决生命周期、不让对象字段自动线程安全；如果采用 prethreaded blocking server 真要转交 fd，则必须用 `UniqueFd` move 完成独占所有权转移，原线程成功移交后不再 read/write/close。

---

## 三句话日志

| 日期 | 最重要的概念 | 最容易错的点 | 对应的工程现象 |
|------|-------------|-------------|---------------|
| 2026-03-22 | 第1章全景：OS三大抽象（进程/虚存/文件）+ 编译系统四阶段 | 存储层次每级速度差距巨大，性能瓶颈几乎都是数据在哪一层 | 程序从源码到运行的每一步都对应后续一个章节的深讲内容 |
| 2026-03-22 | §2.1：信息只是字节，类型决定解释方式 | 有符号数右移是实现定义行为；`long` 大小依赖平台 | `show_bytes` 揭示小端字节序和 IEEE 754 布局；`htonl/ntohl` 处理网络字节序 |
| 2026-03-24 | §2.2：补码 MSB 权重为负，这一差异衍生所有有符号/无符号行为差异 | 混合运算时是有符号转为无符号，而非反方向；`-1 < 0U` 为 false | `strlen`/`.size()` 返回无符号类型，与有符号比较是 C/C++ 常见警告来源 |
| 2026-03-26 | §2.3：整数运算本质是模运算，有符号溢出是 UB，无符号溢出是合法的模运算 | 有符号溢出 UB 导致编译器消除溢出检查代码；负数右移需加偏置才等价于除法 | GCC `-O2` 下 `x+1>x` 被优化为恒真；`calloc` 比 `malloc` 安全因为内部做了大小溢出检查 |
| 2026-03-30 | §2.4 预习：IEEE 754 浮点是实数的近似，0.1/0.2/0.3 均无法精确表示为二进制小数 | `0.1+0.2 != 0.3` 不是 bug，是两条舍入路径差了 1 ULP，浮点数不能用 `==` 比较 | 金融计算必须用整数/定点数；并行求和因结合律不成立而结果不确定是正常现象 |
| 2026-03-30 | §3.2–3.3：`gcc -S` 产物含大量伪指令，`objdump -d` 才是真实执行指令；x86-64 后缀 b/w/l/q 对应 1/2/4/8 字节 | `%rbx` 存 dest 不是随意的：`call` 后 caller-saved 寄存器可能被破坏，必须用 callee-saved 的 `%rbx` | ABI 调用约定是跨模块链接的基础；`objdump -d` 是 crash/core dump 分析的第一工具 |
| 2026-04-02 | §3.4：读汇编先盯住数据流，寄存器/内存/立即数只是操作数的三种来源，关键是”从哪来，到哪去，宽度多少” | 写 `%eax` 会把 `%rax` 高 32 位清零，而写 `%ax`/`%al` 不会；普通 `mov` 也不能直接内存到内存 | 栈帧保存寄存器、函数序言/结尾、数组和结构体访问，本质都能还原成有效地址计算和 `mov`/`push`/`pop` 的组合 |
| 2026-04-06 | §3.5：`leaq` 不访问内存，是编译器做快速整数算术的标准工具（乘以小常数）；移位量只能是立即数或 `%cl` | AT&T 语法 `sub S, D` 是 D-=S 而不是 S-=D；`sar` 补符号位、`shr` 补 0，对负数结果天壤之别 | 性能分析看到 `leaq` 密集不是在访问内存；负数整数除法必有偏置修正序列，不要误读为独立操作 |
| 2026-04-06 | §3.6：cmov 消除分支预测失败（~15-20 cycles 代价），是编译器 -O1+ 对无副作用条件表达式的默认选择；switch 跳转表让分发代价 O(1) | `cmp a,b` 做的是 b-a，容易搞反方向；cmov 两个分支都会被求值，有副作用/访问可能非法内存时不能用；switch 超界检查用 `ja`（无符号比较）一次性排除负数和越界 | 编译器优化报告中的 if-conversion 就是 jmp→cmov 变换；稀疏 case 的 switch 和密集 case 产生完全不同的汇编，影响 CPU 分支预测行为 |
| 2026-05-21 | §3.7：一次调用 = `call` 压返回地址 + 跳转、`ret` 弹返回地址；参数寄存器顺序固定，值要跨调用存活就放 callee-saved 寄存器 | caller-/callee-saved 指的是"谁有保存义务"而非"谁去使用"，极易记反；不是每次调用都建栈帧 | `gdb bt`/perf 火焰图靠栈帧返回地址链回溯；`ret` 无条件信任栈顶是栈溢出/ROP 攻击的根；递归深度被 `ulimit -s` 物理栈大小卡死 |
| 2026-05-31 | §8.1：异常是为响应处理器状态变化而做的控制流突变，按同步/异步分为中断/陷阱/故障/终止四类 | 「返回到当前指令」是故障专属（要重试），陷阱和中断都返回下一条；中断是异步的、和当前指令无关 | 系统调用是陷阱、缺页是故障、SIGSEGV 源于不可修复的缺页故障；`strace` 看陷阱、`perf stat` 看 minor/major fault |
| 2026-05-31 | §8.2：进程提供两个核心假象——独立逻辑控制流（独占 CPU）+ 私有地址空间（独占内存），靠上下文切换和虚拟内存实现 | 并发 ≠ 并行（单核时间片轮转也算并发）；进程进内核态的唯一入口是异常，不能自己提权 | `/proc/<pid>/maps` 看私有地址空间布局；`perf stat -e context-switches` 测切换开销；`time` 的 user/sys 区分用户态/内核态时间 |
| 2026-06-06 | §8.3-8.4：进程控制四件套 fork（一调两返）/exit/waitpid（回收）/execve（一调不返），fork+execve 是「运行新程序」的标准模型 | fork 后父子是独立副本且执行顺序不确定；execve 后的代码只在失败时执行；僵尸是「已终止未回收」占 PID 而非内存 | 简易 shell 的 fork+exec+reap 就是 bash 跑命令的原理；`strace -f` 看 clone/execve/wait4；cd 必须内置因 fork 出去改不了父 shell 状态 |
| 2026-06-07 | §8.5：信号是内核的「软件中断」，靠 pending/blocked 两个位向量驱动生命周期；handler 异步打断主流程，配套 sigprocmask 同步、sigsuspend 显式等待 | 信号不排队（pending 不计数），多个同种信号会合并，回收子进程必须 `while + WNOHANG` 一次收干净；handler 里只能用 `sio_*` 不能用 printf；全局标志要 `volatile sig_atomic_t` | shell/守护进程靠 SIGCHLD handler 回收后台子进程，不回收就堆积僵尸；`strace` 看 rt_sigaction/rt_sigprocmask/wait4；`/proc/<pid>/status` 的 SigPnd/SigBlk 就是位向量快照 |
| 2026-06-07 | §8.6：setjmp/longjmp 是绕过正常调用-返回的非局部跳转，一步跳过多层栈帧；setjmp 一行返回两次（直接返回 0、被 longjmp 拽回返回非 0） | longjmp 只能跳进尚未返回的函数（否则跳进失效栈帧 UB）；跨 setjmp 还要保留新值的局部变量须加 volatile；从 handler 逃逸要用 siglongjmp 否则丢信号掩码 | C++ 异常/Go panic 是同源思想的高级封装；交互式程序「Ctrl-C 中断当前操作但不退出」用 sigsetjmp/siglongjmp 实现 |
| 2026-06-14 | 跨 §8.4↔§9.8 整理：fork/execve 的统一模型——`task_struct` 下挂三本账本（`mm_struct`/`files_struct`/`fs_struct`），fork 复制三本（mm 走 COW）、execve 只换 mm 一本，files/fs 原样保留 | 误以为 execve 也换 fd 表/cwd——正因 execve 保留 `files_struct`/`fs_struct`，重定向才能「穿过」execve 生效；fork 复制 fd 表是管道/重定向能成立的前提 | 重定向（dup2 夹在 fork-execve 间改 fd 表）、管道（共享 `files_struct` 指向同一管道缓冲、非地址空间共享）、cd 必须内置（改 `fs_struct` 否则随子进程销毁）；已交叉补入 §8.3-8.4 与 §9.7-9.8 两份 summary（含账本结构图/缺页流程/COW/克隆账本-换账本图） |
| 2026-06-08 | §8.7：进程观测工具链——strace 看系统调用、ps 看快照、top 看实时、pmap 看地址空间，背后数据源统一是虚拟文件系统 /proc | /proc/stat、/proc/diskstats 是自启动累计值，必须两次采样做差；load average 不是 CPU 百分比要和核数比；MemAvailable 才是真正可用内存而非 MemFree | 线上排障三板斧 top→ps/status→strace -p；node_exporter/vmstat/iostat 全部读 /proc；容器里 /proc/cpuinfo 可能是宿主机的导致误判资源 |
| 2026-06-10 | §9.1-9.3：虚拟内存第一重身份是用 DRAM 缓存磁盘——CPU 发虚拟地址、MMU 查页表翻译，PTE 有效位决定页命中还是缺页，缺页是 demand paging 的正常机制 | 虚拟内存 ≠ 交换区，每次访存都在做地址翻译；malloc 只登记映射不分配物理页，首次触摸才缺页；时间局部性看重用距离不看重复次数 | Linux 页表是 pgd→p4d→pud→pmd→pte 五级基数树，根在 mm_struct->pgd、切换进程就是改写 CR3；/proc/\<pid\>/stat 第 10/12 字段（minflt/majflt）直接观测缺页，分块（blocking）是压缩重用距离的标准工程手法 |
| 2026-06-16 | §9.9-9.10：动态分配器是用户态在「堆」上做空间记账的库（核心矛盾吞吐率 vs 峰值利用率，所有数据结构都服务于 free 时找块+合并）；GC 则把堆看成可达图，从根（寄存器/栈/全局）走不到的块即垃圾、自动回收 | malloc/free 是库、sbrk/mmap 才是系统调用，多数 malloc 不进内核；free 后堆不还内核故 RSS 不降；后向合并易、前向合并难（靠边界标记脚部 O(1)）；GC 的「垃圾」是不可达而非「没用」，引用计数收不了循环引用，C 只能保守式 GC（宁漏勿误） | glibc ptmalloc2=分离适配+arena+tcache；M_MMAP_THRESHOLD≈128KB 隔离大块；Java/Go 分代并发 mark-sweep、Python 引用计数+收环、Boehm GC 给 C 用；valgrind 的 definitely lost/still reachable 正是可达性分析 |
| 2026-06-14 | §9.4-9.5：虚拟内存的第二、三重身份——每进程独立页表简化链接/加载/共享/分配（§9.4），PTE 权限位让每次访存顺带做访问控制（§9.5） | execve 不读盘只建映射，靠 demand paging 换入；段错误和缺页同入口不同出口（合法→换页，越权→SIGSEGV）；x86-64 没有独立读位，写位是 _PAGE_RW、不可执行是 _PAGE_NX | COW 是「简化共享 + 写保护」的合成；写 .rodata 字符串字面量崩溃就是写保护；JIT 必须 W^X（先写后改可执行）；/proc/maps 的 rwxp、PSS vs RSS 都落在这两节机制上 |
| 2026-06-27 | §6.2~6.3：程序天生有局部性（时间=同址重访、空间=邻址顺访），所以"近期热点放更快更小的存储里"划算，于是 SRAM→DRAM→磁盘层层缓存，每层都是下层的缓存、以块为单位搬运 | 空间局部性看访问地址是否相邻而非代码行是否相邻，C 行优先数组要 stride-1 必须让内层循环对应最后一维；三种 miss 别混（冷 miss 跟容量无关、冲突 miss 缓存没满也发生、只有容量 miss 加大缓存能解）；硬件 cache 透明，程序员只能靠改善局部性间接命中 | 行优先 vs 列优先遍历同一矩阵，perf 看 LLC-load-misses 差几倍就是"逻辑相同性能差很多"的根因；循环分块(tiling)把容量不命中变块内命中；cachegrind 无需 PMU 即可定位 cache 不友好的循环 |
| 2026-06-27 | §6.1：存储层次源于物理鸿沟——SRAM 快贵小（cache）、DRAM 慢便宜大（主存）、闪存/磁盘非易失但更慢，且 CPU-内存速度差距持续拉大，这是整个第 6 章的理由 | 总线带宽/磁盘速率用十进制 10⁶·10⁹，内存容量/cache/页用二进制 2²⁰·2³⁰，两者易混；磁盘平均旋转延迟是半圈不是整圈；SSD 不能原地改写、写前须块级擦除且有寿命 | DRAM 行缓冲区命中是顺序访问快的物理根源；一次磁盘随机访问 ~10ms 抵数十万次访存，是 swap 抖动假死的根源；fio 4K 随机 IOPS、smartctl 擦写寿命、iostat await 都落在本节物理结构上 |
| 2026-06-27 | §6.4：cache 硬件把地址切成 `[tag\|组索引\|块偏移]` 三段定位，直接映射/组相联/全相联只是「每组放几行 E」的不同取值，写策略=命中(写直达/写回)×不命中(写分配/非写分配)两个正交选择 | 组索引取地址**中间位**不是高位（高位会让顺序访问全挤一组）；冲突不命中在 cache 没满时也发生；写命中策略和写不命中策略是两个正交问题；脏位≠有效位 | 直接映射抖动靠 padding/`alignas` 错开映射解决；写回+写分配让大量小写在 L1 合并、不打满带宽；全相联因要并行比所有行只能做小，TLB 是其现实身影；`perf c2c` 测的 false sharing 就是同一 line 的跨核写回竞争 |
| 2026-06-27 | §6.6：存储器山把访存性能画成两维曲面——工作集大小决定「站在哪级存储」（纵深台阶 L1→L2→L3→主存），步长决定「用了多少空间局部性」（横向斜坡）；本机(Ultra 7 255H)实测跑出 L1≈85→L2≈48→L3≈33→主存≈20 GB/s 四级断崖，断崖位置精确落在 48K/3M/24M 容量边界 | 山顶平山脚陡——工作集进 L1 时 stride 几乎不影响（没 miss 可摊），只有落到主存空间局部性才救命；stride 伤害到「每 line 只剩 1 个元素」(本机 s8=64B)就到顶、再大压平；矩阵乘 6 版本计算量完全相同、性能差几倍纯来自 miss 率 | 用软件量硬件——纵切面台阶反推各级 cache 容量、和 lscpu 对上；矩阵乘 kij/ikj(B、C 全 stride-1)比 jki/kji 快几倍，是 BLAS 循环重排+分块的最小模型；tiling 把工作集切进 cache = 往山顶爬，对应列存/im2col/分块卷积 |
| 2026-06-28 | §5.12-5.14：load/store 也是有延迟的功能单元——指针追逐 CPE≈load 延迟(4)、store→load 别名经存储转发 CPE 暴涨；优化分层施力(算法>消妨碍因素>循环展开/多累加器)，先 profile 再优化、Amdahl 定上限 | "store 比 load 快"是错觉(别名时转发链拉到~6cyc)；链表 CPE≈4 是 load 延迟串成关键路径而非 cache 慢；别名是编译器**无法证明不别名**时的悲观假设；gprof 采样估时对短函数失真；优化前必须先知道 α | `restrict` 解开别名悲观假设(BLAS/memcpy)；指针追逐 load-延迟受限是"数组优于链式"的微架构理由；store buffer 是 x86 TSO 重排与多线程内存屏障的根源；perf record+火焰图替代需重编译的 gprof |
| 2026-06-29 | §9.13 内核源码导览：把第 9 章机制对接 Linux 6.17 真实代码——一次缺页的代码之旅 `exc_page_fault→handle_mm_fault→handle_pte_fault` 按 PTE 状态分流(`do_anonymous_page`/`do_fault`/`do_swap_page`/`do_wp_page`)，每条分支对应一个书本概念；外加书本没讲的 rmap 反向映射、kswapd/LRU 回收、脏页回写 writeback（per-bdi flusher + balance_dirty_pages 限流）三块深水区 |
| 2026-07-01 | 第 10 章 §10.1-10.12：Unix「一切皆文件」落到 open/read/write/close/stat 五个系统调用，裸调用的坑（short count/EINTR）由 RIO 兜住；内核用三张表（描述符表/打开文件表/v-node）表示打开的文件，文件偏移量 k 绑定在打开文件表项上 | 偏移量 k 不在 fd 也不在 v-node——两次 open 同一文件各有独立 k，而 fork/dup2 共享同一个 k；short count 不是错误必须循环；标准 I/O 缓冲对 socket/双向流不友好故网络用 RIO | shell 的 `>`/`2>&1` 靠 dup2 改描述符表项指向实现；`strace -f -e openat,read,clone` 看 fork 只一次 openat；`FILE*` 重定向到文件后由行缓冲变全缓冲，崩溃前不 fflush 会丢输出 |
| 2026-07-01 | §10.11 疑问：二进制文件必须按字节数读（fread/read/rio_readn/ifstream.read），不能用按分隔符切的文本函数——0x0a/0x00 在二进制里只是普通数据 | fopen 记得带 `b`、ifstream 记得 `ios::binary`（Windows 上关 \r\n 转换）；`fread` 也会短读要靠返回值驱动；跨机器还要处理字节序 + 结构体 padding | 工程不手写二进制格式，交 Protobuf/FlatBuffers/Cap'n Proto 统一解决字节序·对齐·版本演进；`fwrite(&struct)` 直接落盘会把编译器填充字节写出，换架构即不兼容 |
| 2026-06-29 | §9.13 重构：把单文件 summary 改成对标参考 mm 文档的多文件「内核虚拟内存说明书」（00-index + 01 地址空间/02 页表/03 缺页主线/04 demand paging+COW/05 rmap/06 回收swap/07 脏页回写/08 观测实验），ARM64 架构基准 + 6.x 内核，实验迁入 experiments/ | 架构基准选 ARM64 但实验在 x86 本机跑——核心 mm 路径(handle_mm_fault 往后)架构无关、同一份 mm/*.c，只有异常入口(el0_da vs exc_page_fault)/页表级数/PTE 位架构相关；参考文档基于 5.x(链表+mm_rb)，本文以 6.x maple tree/folio 为准并注演进；行号沿用参考会偏移、新增内容不编行号 | 图文用 ASCII 调用链(带 文件:函数 注释)+ Mermaid(关系图/分流决策/LRU 状态机)；每条机制配 /proc 观测点 + bpftrace/ftrace 追踪靶子 | 旧教程的 `vm_next`链表/`mm_rb`红黑树在 6.1+ 已被 maple tree(`mm->mm_mt`)单结构取代，照搬会编译不过；`page`≠`folio`(后者保证指向复合页头页)；段错误与正常换页同入口、只在查 VMA/查权限两关分流；读未初始化匿名页不分配实页(共享 ZERO_PAGE)；COW 复制在 `do_wp_page`、按单页、第一次写才发生 | 免 root 三件套量化内核行为：statm/stat 的 RSS·minflt·majflt、`/proc/vmstat` 的 pgfault/pgmajfault/pgsteal_direct、`/proc/<pid>/smaps` 的 Anon/Shared；进阶用 bpftrace/trace-cmd(需 root)在 `do_wp_page`/`handle_mm_fault` 下探针，把调用链从读源码想象变成亲眼触发 |
| 2026-07-04 | 第 10 章 UDS 专题：UDS 是本机 IPC 正解（复用 socket API 但不进 TCP/IP 栈、按文件路径寻址）；SCM_RIGHTS 能在任意进程间传 fd（传的是打开文件表项这个内核对象、非数字），只依赖一条已连通的 AF_UNIX 连接 | 抽象命名空间的 addrlen 必须 `offsetof+1+strlen` 精确算，传 `sizeof(addr)` 会把尾随填零算进名字；传 fd 必带 ≥1 字节数据且 cmsg 只能用 `CMSG_*` 宏；`bind` 成功返回 0，别写成 `!bind`；`_exit` 不刷 stdio 缓冲，管道下丢日志（本专题第 3 次踩） | Docker/Postgres/systemd/Wayland/D-Bus 本机通信全走 UDS；nginx master/worker 与 privsep 靠传 fd 分发连接/委派特权；现代 C++ 无标准 socket，贴 syscall 就自己套 RAII(`Fd`/`UnixSocket`)，要异步/跨语言再上 Asio/Cap'n Proto-KJ/sdbus-c++ |
| 2026-07-12 | §12.1：基于进程的并发服务器 = 父进程负责 `accept`，每个 `connfd` 交给一个子进程处理；进程隔离让模型简单可靠，但共享状态必须靠 IPC | 父子 fd 关闭规则最容易错：父关 `connfd`、子关 `listenfd`；`SIGCHLD` 不排队，回收子进程必须 `while waitpid(-1, WNOHANG)` | 早期 Apache prefork、PostgreSQL/OpenSSH 的多进程 worker、shell pipeline、systemd socket activation 都是 fork + fd + IPC 组合出来的工程形态 |
| 2026-07-19 | §12.2：I/O 多路复用只通知 fd readiness，Reactor 再组织成“注册→等待→分发→handler 执行实际 I/O”的事件循环；select/poll 每轮全量提交与扫描，epoll 长期维护 interest set/ready list | ready 不等于有业务数据或 I/O 已完成；ET 必须 nonblocking 并把 accept/read/write 做到 `EAGAIN`；`EPOLLOUT` 只能在 outbuf 非空时按需开启，否则会 busy loop | Nginx/Redis/libuv 用少量 loop 管大量低活跃连接；Muduo one loop per thread 让每条连接固定归属一个 I/O loop，以单一所有权减少锁，重业务另交有界 worker pool并用 queue+eventfd 投递结果 |
| 2026-07-19 | §12.3：线程把执行流与资源容器分开；同进程线程各有寄存器和栈，却共享地址空间与 fd table；thread-per-connection 用一个 detached worker 服务一个连接 | 不能传会被 accept 循环覆盖的 `&connfd`；创建成功后 main 也不能像 fork 版一样 `close(connfd)`，否则会关闭 worker 共享的同一 fd | `make demo` 让一个 worker 阻塞在延迟连接上时，其他 worker 仍及时回显；生产中通常用有界线程池或 Reactor + worker pool 控制线程、队列和背压 |
| 2026-07-26 | §12.4-§12.5：同步正确性不是给每行补锁，而是围绕变量实例建立 identity、ownership、lifetime、invariant 和 happens-before；one loop per thread 用单连接单线程所有权主动缩小共享面 | atomic 只保证单次操作，不能自动维护 check-then-act；CV 必须等待受 mutex 保护的谓词；读者优先会饿死 writer、写者优先会饿死 reader，`std::shared_mutex` 也不承诺统一公平策略 | I/O loop + MPSC completion queue + `eventfd` 是 Reactor 跨线程回投的经典边界；SPSC 靠固定角色免 CAS，MPMC 先选 bounded mutex+CV，只有 profile 证明瓶颈后才考虑 per-slot sequence ring；公平读写锁用 FIFO 队列按 reader phase 放行 |
| 2026-08-16 | §3.11：浮点是并行的另一套世界——独立的 %xmm 寄存器组、靠指令后缀（ss/sd/ps/pd）而非寄存器名区分宽度、独立的参数计数队列，IEEE 754 的语义（NaN 无序、-0.0、常数不可精确表示）逐条落到了指令上 | 全部 16 个 %xmm 都是 caller-saved，浮点值跨调用只能溢出到栈；`x<0?-x:x` 不等价于 `fabs(x)`（-0.0 行为不同）所以编译器不给 `andpd`；浮点比较要用 `ja`/`jb` 无符号跳转，判等必须同时看 PF 和 ZF 否则 `NaN==NaN` 为真 | 热点循环里全是 `addsd` 而非 `addpd` 说明向量化失败，用 `-fopt-info-vec-missed` 查原因；`-ffast-math` 的本质就是授权编译器做这些「语义不完全等价」的变换；`long double` 走的是完全不同的 x87 栈 |
| 2026-08-16 | §9.11：内存错误最危险的不是它让程序崩溃，而是它经常不崩——实测十类错误里四类退出码为 0，且会崩的那几类崩溃点也不在错误点（堆越界要等下次 free 操作元数据才暴露） | 没有任何单一工具能抓全：ASan 抓不到读未初始化内存、valgrind 抓不到栈缓冲区溢出、界内指针错位两者都抓不到；`malloc` 不清零而 `calloc` 清零，「有时候读到 0」是巧合不是保证 | CI 用 `-fsanitize=address,undefined` 兜日常、valgrind `--track-origins` 线下深挖疑难、`-D_FORTIFY_SOURCE=2` 上生产；本节所有堆错误破坏的都是 §9.9 讲的分配器元数据，这也是堆溢出能被利用成安全漏洞的根源 |
| 2026-08-16 | 全书收口：三个假象（独占 CPU / 独占连续内存 / 访存等速）分别由上下文切换、虚拟内存、存储层次维持，几乎所有"性能与正确性玄学"都是某个抽象的实现细节漏了出来 | 状态标记会过时——第 2、5 章其实早已学完却一直挂着 ⬜，回顾时按目录里的 summary 实际覆盖范围核对，别信旧表格 | 排障与优化都有固定的下钻顺序：先 Amdahl 测 α → 算法 → 访存模式（cache miss）→ 依赖链（延迟界限）→ 分支 → 系统层（缺页/上下文切换/D 状态），跨过任何一层直接调底层参数都是浪费 |
| 2026-07-26 | §12.6-§12.7：多线程加速必须同时满足工作可并行、分块均衡和开销可控，并用 `S_p=T_1/T_p`、`E_p=S_p/p` 与 Amdahl 上限解释扩展曲线；线程安全最终是函数/API 的状态、所有权和调用时序契约 | 线程数不等于速度；单个方法线程安全不代表 check-then-act 安全；内部加锁也不自动可重入；返回静态对象的接口解锁后仍可能被下一次调用覆盖 | 计算任务用线程局部归约减少 atomic/cache-line 热点，并用 `perf stat` 区分串行比例、调度与带宽瓶颈；遗留 C API 优先改为显式 context、调用者缓冲区或锁内复制快照，跨模块统一锁顺序并避免锁内 callback |
