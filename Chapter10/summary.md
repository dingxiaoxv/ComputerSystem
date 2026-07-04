# 第 10 章 系统级 I/O

这一章的主线是：**Linux「一切皆文件」的抽象怎么落到几个最朴素的系统调用（`open`/`read`/`write`/`close`/`stat`）上，以及裸系统调用有哪些坑（short count、EINTR、元数据解析），CSAPP 用 RIO 包把这些坑兜起来**。后半章往上层走：内核用三张表表示打开的文件（§10.8 共享文件、§10.9 重定向），标准 I/O 在其上再加一层缓冲流（§10.10），最后给出「该用哪套 I/O」的选择准则（§10.11）。读完要能回答：为什么 `read` 返回的字节数可能比你要的少、文件描述符到底是什么、`fork`/`dup2` 为什么共享文件偏移量、什么场景该用 `FILE*`、什么场景该用 RIO 或裸系统调用。

> 本章代码组织：`rio/` 只放 RIO 实现（`rio.c` / `rio.h`），实验代码统一在 `experiments/`（`make race` 跑 TSan 竞态实验）。

---

## Unix I/O：一切皆文件

**🎯 核心抽象**

Linux 把所有 I/O 设备（普通文件、目录、磁盘、终端、网络、管道）都建模成文件，统一用一组系统调用操作。应用程序看到的就是一个字节序列加一个「当前位置」。

- 打开：`open` 返回一个**文件描述符**（非负小整数），后续所有操作都用它
- 改变位置：`lseek` 设置文件偏移量 `k`
- 读写：`read`/`write` 从偏移量 `k` 处传输字节并推进 `k`
- 关闭：`close` 释放描述符

**🎯 三个默认打开的描述符**

每个进程启动时内核自动打开三个，定义在 `<unistd.h>`：

| fd | 宏 | 含义 |
|----|----|------|
| 0 | `STDIN_FILENO` | 标准输入 |
| 1 | `STDOUT_FILENO` | 标准输出 |
| 2 | `STDERR_FILENO` | 标准错误 |

这就是 shell 里 `2>&1`（把 fd 2 重定向到 fd 1 指向的地方）的由来。

## 文件类型

**🎯 `stat` 里 `st_mode` 编码的文件类型**

`ls -l` 第一个字符就是它：

- **普通文件 (`-`)**：内核不解释内容。又分文本文件（ASCII/UTF-8）和二进制文件——这个区分只在应用层有意义，内核一视同仁
- **目录 (`d`)**：一张「文件名 → inode」的表，至少含 `.`（自身）和 `..`（父目录）
- **符号链接 (`l`)**、**字符设备 (`c`)**、**块设备 (`b`)**、**FIFO 命名管道 (`p`)**、**socket (`s`)**

**🎯 文本行的行尾约定**

Linux 文本行以换行符 `\n`（ASCII `0x0a`）结尾，**没有 Windows 的 `\r\n`**。`rio_readlineb` 正是靠扫描 `\n` 来切行的。

## 打开与关闭文件

**🎯 `open` 的形态**

```c
int open(const char *filename, int flags, mode_t mode);
```

- 返回当前进程**最小的未使用描述符**——这是 shell 重定向能工作的关键机制（先 `close(1)` 再 `open`，新文件就占到 fd 1）
- `flags`：访问模式 `O_RDONLY`/`O_WRONLY`/`O_RDWR`，可按位或上 `O_CREAT`/`O_TRUNC`/`O_APPEND`
- `mode`：仅在 `O_CREAT` 时有意义，是新文件权限，会被进程的 `umask` 掩掉一部分

**⚠️ 描述符是有限资源**

每个进程能打开的描述符数有上限（`ulimit -n`，常见 1024）。忘记 `close` 会泄漏描述符，长跑服务最终 `open` 返回 `EMFILE`。

## 读和写文件

**🎯 `read` / `write` 的签名与返回值**

```c
ssize_t read(int fd, void *buf, size_t n);    // 返回实际读到的字节数，0 表示 EOF，-1 出错
ssize_t write(int fd, const void *buf, size_t n);  // 返回实际写出的字节数，-1 出错
```

返回类型是 `ssize_t`（有符号），才能用 `-1` 表示错误；参数 `n` 是 `size_t`（无符号）。

**⚠️ 不足值 short count——本章最重要的坑**

`read`/`write` 实际传输的字节数**可能小于请求的 `n`**，且这不是错误。常见原因：

- 读到 **EOF** 之前剩余字节不足
- 从**终端**读：一次通常只返回一行
- 从**网络 socket** 读写：内核缓冲、对端节奏、`MSG` 边界都可能导致提前返回
- 被**信号处理程序中断**：返回 -1 且 `errno == EINTR`

对磁盘上的普通文件读写，遇不到 short count（除非碰到 EOF 或信号）；但只要涉及网络/管道/终端，就必须循环处理。

## RIO 健壮 I/O 包

CSAPP 自己封装的 **R**obust **I/O**，底层全是 `read`/`write`，专门兜住 short count 和 EINTR。分两组，对应 `rio/rio.c`：

**🎯 无缓冲组：`rio_readn` / `rio_writen`**

无状态，内部循环重试直到读满/写满 `n` 字节，把 short count 和 EINTR 吃掉。适合二进制数据（如网络上读定长包头）。

```c
ssize_t rio_readn(int fd, void *usrbuf, size_t n);   // 读满 n（或到 EOF），EOF 则返回值 < n
ssize_t rio_writen(int fd, void *usrbuf, size_t n);  // 写满 n
```

**🎯 带缓冲组：`rio_initb` + `rio_readlineb` / `rio_readnb`**

维护一个 `rio_t` 结构体，内含一块 `RIO_BUFSIZE`（8192）的用户态缓冲。一次 `read` 拉一大块进缓冲，之后从缓冲挑字节，**大幅减少陷入内核的次数**——按行读尤其明显（否则裸 `read` 一次一字节找 `\n` 会触发海量系统调用）。本质是 `FILE*` 缓冲流的教学版简化。

```c
typedef struct {
  int rio_fd;                /* 关联的描述符 */
  int rio_cnt;              /* 缓冲中未读字节数 */
  char *rio_bufptr;         /* 下一个待读字节 */
  char rio_buf[RIO_BUFSIZE];/* 内部缓冲 */
} rio_t;
```

**⚠️ 缓冲组与无缓冲组可混用同一描述符，但缓冲组之间共享 `rio_t` 状态**

`rio_readlineb` 和 `rio_readnb` 可以对**同一个 `rio_t`** 交替调用（带缓冲，互相看得到缓冲剩余）；但**不能和裸 `read` 混用**同一 fd，否则缓冲里的数据会被绕过。

## 读取文件元数据

**🎯 `stat` / `fstat`**

```c
int stat(const char *filename, struct stat *buf);  // 按路径名
int fstat(int fd, struct stat *buf);               // 按已打开的描述符
```

关键字段：

- `st_mode`：文件类型 + 权限位
- `st_size`：文件字节数
- `st_uid` / `st_gid`：属主

**🎯 类型判定用宏，不要手写位运算**

```c
struct stat s;
stat("foo", &s);
if (S_ISREG(s.st_mode))  /* 普通文件 */ ;
if (S_ISDIR(s.st_mode))  /* 目录 */ ;
if (S_ISLNK(s.st_mode))  /* 符号链接 */ ;
```

## 读取目录内容

**🎯 `opendir` / `readdir` / `closedir`**

目录是「文件名 → inode」表，用专门的一组函数遍历，而不是 `read`：

```c
DIR *dirp = opendir(path);
struct dirent *dp;
while ((dp = readdir(dirp)) != NULL) {   // 每次返回下一个目录项
    printf("%s\n", dp->d_name);          // d_name 是文件名
}
closedir(dirp);
```

**⚠️ 用 `errno` 区分「读完」和「出错」**

`readdir` 返回 `NULL` 既可能是遍历结束，也可能是出错。区分方法：调用前把 `errno` 置 0，返回 `NULL` 后检查 `errno` 是否非 0。

## 共享文件（§10.8）

**🎯 内核用三张表表示打开的文件**

一个「打开的文件」在内核里由三级结构串起来，理解这一节的所有现象都靠这张图：

| 表 | 归属 | 关键内容 |
|----|------|----------|
| **描述符表** | 每进程一张 | `fd → 指向某个打开文件表项`。fork 会连表一起复制 |
| **打开文件表** | 全系统共享 | **文件位置 `k`（偏移量）**、引用计数、访问模式；指向某个 v-node |
| **v-node 表** | 全系统共享 | `stat` 里的元数据：`st_mode`、`st_size` 等（对应磁盘 inode 的内存态） |

一句话记法：**文件位置存在「打开文件表项」里，不在 fd 里，也不在 v-node 里**。谁共享同一个打开文件表项，谁就共享同一个偏移量 `k`。

**🎯 两次 `open` 同一文件 → 两个独立偏移量**

```c
fd1 = open("foobar.txt", O_RDONLY, 0);
fd2 = open("foobar.txt", O_RDONLY, 0);
```

`fd1`、`fd2` 各自指向一个**不同的打开文件表项**（各有独立的 `k`），但两个表项指向**同一个 v-node**（同一物理文件）。所以两次 `read` 各自从 `k=0` 开始，都读到第一个字节 `'f'`。

```
描述符表        打开文件表              v-node 表
fd1 ──────────► [表项A  k=0] ─┐
fd2 ──────────► [表项B  k=0] ─┴──► [v-node  foobar.txt]
```

**🎯 `fork` 后父子 → 共享同一个偏移量**

`fork` 复制的是**描述符表**，子进程 `fd` 的每个表项和父进程指向**同一个打开文件表项**——于是父子共享同一个 `k`，且引用计数 +1（所以父子都要各自 `close` 才真正释放）。

```
父描述符表  ─┐
             ├─► [同一个表项  k 共享] ──► [v-node]
子描述符表  ─┘
```

子进程读一个字节把 `k` 推到 1，父进程 `wait` 之后接着读，看到的是第 2 个字节。

## 重定向（§10.9）

**🎯 `dup2(oldfd, newfd)`：让 `newfd` 指向 `oldfd` 的打开文件表项**

```c
int dup2(int oldfd, int newfd);   // 先 close(newfd)，再让 newfd 复制 oldfd 的指向
```

`dup2` 不新建打开文件表项，而是把 `newfd` 的描述符表项**改指到 `oldfd` 已有的那个表项**（引用计数 +1）。这样往 `newfd` 写就等于往 `oldfd` 的目标写——`k` 也是共享的。

**🔧 shell 的 `cmd > out.txt` 就是这么实现的**

```c
int fd = open("out.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644);
dup2(fd, STDOUT_FILENO);   // 之后 printf / write(1,...) 全落到 out.txt
close(fd);
```

`2>&1` 则是 `dup2(1, 2)`——让 fd 2 和 fd 1 指向**同一个打开文件表项**，所以标准错误和标准输出交错写进同一个文件、共享同一个偏移量（这也是 `2>&1` 必须写在 `>out` 之后才生效的原因）。

## 标准 I/O（§10.10）

**🎯 C 标准库在 Unix I/O 之上加了一层用户态缓冲流**

`<stdio.h>` 提供的 `fopen`/`fread`/`fgets`/`fprintf`/`fclose` 等，把一个描述符包装成 `FILE*`（流），内部维护一块缓冲区——和 RIO 的 `rio_t` 是同一种思路，只是它是 ANSI C 标准、可移植到所有平台。

- `FILE*` 里存着底层 fd、缓冲区指针、缓冲计数、以及错误/EOF 标志位
- 三个标准流自动打开：`stdin`、`stdout`、`stderr`（对应 fd 0/1/2）

**🎯 三种缓冲模式，决定数据何时真正下陷到 `write`**

| 模式 | 触发 flush 的时机 | 默认场景 |
|------|-------------------|----------|
| 全缓冲 | 缓冲写满 | 流指向普通文件时 |
| 行缓冲 | 遇到 `\n` 或缓冲满 | 流指向终端时（如 `stdout` 连 tty） |
| 无缓冲 | 每次调用立即写 | `stderr`（保证错误信息不丢） |

这解释了一个经典现象：`printf("hi")` 不带 `\n` 且输出被重定向到文件时，程序若崩溃，`hi` 可能还在缓冲里没落盘——因为重定向后 `stdout` 从行缓冲变成了全缓冲。用 `fflush` 或 `setvbuf` 可显式控制。

**⚠️ 标准 I/O 流是全双工受限的**

同一个流交替读写有坑：写之后、读之前（反之亦然）必须夹一个 `fflush`/`fseek`/`fsetpos`/`rewind`，否则行为未定义。这个限制加上「缓冲对 socket 不友好」，正是 CSAPP 不在网络编程里用 `FILE*` 而自造 RIO 的原因。

## 该用哪套 I/O（§10.11）

**🎯 三套 I/O 的定位**

CSAPP 把可用的读写手段分成三层，各有适用面：

- **Unix I/O（`open`/`read`/`write`）**：最底层，异步信号安全（可在信号处理程序里调用），能拿到 `stat` 全部元数据。缺点是要自己处理 short count。
- **标准 I/O（`FILE*`）**：带缓冲、可移植、接口丰富，日常读写磁盘文件的首选。缺点是缓冲语义对网络/双向流不友好，且不是异步信号安全。
- **RIO**：专为网络编程补位——既兜住 short count/EINTR，又提供带缓冲的按行读，且缓冲组和无缓冲组能安全共用一个 fd。

**🎯 CSAPP 给的选择准则**

1. 能用标准 I/O 就用标准 I/O（磁盘文件、终端的日常读写）。
2. 别在网络 socket 上用标准 I/O——缓冲语义 + 双向流限制会出各种诡异行为；这里用 RIO 或直接 Unix I/O。
3. 信号处理程序里只能用 Unix I/O（`write` 是异步信号安全的，`printf` 不是）。

**⚠️ 二进制文件不要用「面向文本行」的函数**

`fgets`/`scanf`/`rio_readlineb` 这类函数以 `\n` 为边界，而二进制数据里 `0x0a`（换行）只是普通字节，会被误当成行尾提前截断；`0x00` 也会被字符串函数当成结尾。二进制文件必须用**按字节数**读的函数（`fread` / `rio_readn` / `read`），详见本章末尾「二进制文件怎么读」的展开。

---

## 易错点

- `read` 返回值比请求的 `n` 小**不是错误**——是正常的 short count，必须循环（或用 RIO），尤其在 socket/管道/终端上。
- 把 `read`/`write` 返回值存进 `int` 或无符号类型会丢掉 `-1` 错误语义——必须用 `ssize_t`。
- EOF 不是一个特殊字符，没有「EOF 字节」——它是 `read` 返回 0 这一事件。
- 忘记 `close` 会泄漏描述符，受 `ulimit -n` 限制，长跑服务最终 `EMFILE`。
- 带缓冲的 `rio_readlineb` 和裸 `read` 不能混用同一 fd——缓冲里预读的数据会被裸 `read` 跳过。
- RIO 的 `rio_t` 缓冲组**不是线程安全的**：多个线程共享同一个 `rio_t` 会在 `rio_cnt`/`rio_bufptr`/`rio_buf` 上数据竞争（见实验题）。CSAPP 的设计前提是「一连接一 `rio_t`」，靠不共享回避锁。
- `readdir` 返回 `NULL` 要靠 `errno` 区分「读完」与「出错」，不能直接当成读完。
- 文件偏移量 `k` **存在打开文件表项里，不在 fd、也不在 v-node 里**——两次 `open` 同一文件有各自独立的 `k`，而 `fork`/`dup2` 共享同一个 `k`。凭「同一个文件」直觉去猜是否共享位置一定错。
- `fork` 后父子共享打开文件表项、引用计数为 2，**父子都要各自 `close`** 才真正释放该表项；只在一方 `close` 不会回收。

## 工程关联

- **shell 重定向的底层机制**：`open` 总返回最小空闲 fd，配合 `close(1)` / `dup2` 就能把进程的 stdout 接到文件——`cmd > out.txt`、`2>&1` 全靠这个（第 10.8/10.9 节展开）。
- **`strace` 观察**：`strace -e trace=openat,read,write,close ./prog` 能直接看到每次系统调用的 fd、请求字节数和返回值，short count 在这里一目了然——这是把本章概念和真实进程对上号的最快方式。
- **EINTR 的现代处理**：与其在每个 I/O 调用点写 `if (errno == EINTR)` 重试（RIO 的做法），生产代码更常在信号层用 `sigaction` 的 `SA_RESTART` 标志让内核自动重启慢速系统调用，一次性解决。
- **RIO vs 标准库 vs 工业封装**：带缓冲读这件事，C 用 `FILE*`（`fgets`/`getline`），C++ 用 `std::ifstream`，网络场景用 Asio 的 `asio::read`（「读满或出错」等价 `rio_readn`）。它们都有明确的线程模型：`FILE*` 单次调用有隐式锁但跨调用会交错，Asio 要求「单个 socket 不并发」——和 RIO「一连接一 `rio_t`」是同一种思路。
- **描述符是内核资源**：`/proc/<pid>/fd/` 能看到进程当前打开的所有描述符，排查 fd 泄漏直接 `ls -l /proc/<pid>/fd | wc -l`。

## 实验题

**🧪 题 1：用 strace 看 short count 和 RIO 的循环**

对一个从管道/终端读数据的小程序：

```c
char buf[4096];
ssize_t n = read(STDIN_FILENO, buf, sizeof(buf));
printf("requested %zu, got %zd\n", sizeof(buf), n);
```

要求：
- `echo hello | strace -e trace=read ./a.out`，观察 `read(0, ..., 4096)` 实际返回值远小于 4096
- 对比直接 `read` 文件（`./a.out < bigfile`）时是否还有 short count
- 结论：short count 与数据源类型强相关，磁盘普通文件基本读满，管道/终端不一定

**🧪 题 2：带缓冲 vs 不带缓冲的系统调用次数**

分别用裸 `read`（一次一字节找 `\n`）和 `rio_readlineb` 读同一个上万行的文件，各自按行读完。

要求：
- `strace -c -e trace=read ./naive_readline < data.txt` 看 `read` 调用次数（应≈文件字节数）
- `strace -c -e trace=read ./rio_readline < data.txt` 看 `read` 调用次数（应≈文件大小 / 8192）
- 结论：用户态缓冲把系统调用次数从 O(字节数) 降到 O(字节数/缓冲大小)，这正是 `rio_t` 存在的意义

**🧪 题 3：两次 open vs fork 的偏移量共享**（已实现，见 `experiments/shared.c`）

`foobar.txt` 内容为 `foobar`。同一份逻辑，两种打开方式结果不同：

```c
// 版本 A：两次 open —— 两个独立打开文件表项，各自 k=0
fd1 = open("foobar.txt", O_RDONLY, 0);
fd2 = open("foobar.txt", O_RDONLY, 0);
read(fd1, &c, 1);  // 'f'
read(fd2, &c, 1);  // 'f'  ← 各读各的 k

// 版本 B：open 后 fork —— 父子共享同一个打开文件表项、同一个 k
fd = open("foobar.txt", O_RDONLY, 0);
if (fork() == 0) { read(fd, &c, 1); ... }  // 子读 'f'，把共享的 k 推到 1
wait(NULL);
read(fd, &c, 1);   // 父读 'o'  ← 接着子进程留下的 k
```

要求：
- 分别放开两段代码编译运行，验证 A 输出 `f/f`、B 输出 `son c=f / parent c=o`
- 用 `strace -f -e trace=openat,read,lseek,clone ./shared` 观察：版本 A 有两次 `openat`（两个 fd），版本 B 只有一次 `openat` + 一次 `clone`（fork）
- 结论：偏移量绑定在「打开文件表项」上——`open` 次数决定表项个数，`fork` 决定谁共享表项

**🧪 题 4：用 ThreadSanitizer 抓 `rio_t` 的数据竞争**（已实现，见 `experiments/`）

两个线程共享同一个 `rio_t` 并发调用 `rio_readlineb`，竞态点是 `rio_cnt` / `rio_bufptr` / `rio_buf`。

要求：
- `cd experiments && make race` 一键构建并运行（自动生成 `data.txt`，用 `setarch -R` 关 ASLR 绕开 TSan 影子内存冲突）
- 观察 TSan 报告的三类竞态：`size 4` 的 `rio_cnt`、`size 8` 的 `rio_bufptr`、`memcpy` vs `read` 同时读写 `rio_buf`
- 读懂报告结构：`Read/Write of size N`（字段身份）+ `Location is global 'shared_rio'`（变量名）+ 两个无同步的线程栈（race 定义）
- 进阶：写「每线程独立 `rio_t`」版本，验证 TSan 转为静默——这就是 CSAPP「一连接一 `rio_t`」设计的正确性证明

---

## 附：二进制文件到底怎么读（§10.11 疑问展开）

书上说「不能用读取文本的函数读二进制文件」，核心原因是**文本函数以特殊字节为边界，而二进制里这些字节只是普通数据**：

- `fgets` / `rio_readlineb` 以 `\n`（`0x0a`）为行尾——二进制里 `0x0a` 遍地都是，会被误当行尾**提前截断**
- `strlen` / `strcpy` / `%s` 以 `\0`（`0x00`）为串尾——二进制里 `0x00` 随处可见，字符串一族全部失效
- Windows 上还有一层：文本模式会把 `\r\n` ↔ `\n` 自动转换，二进制数据被悄悄改写

**🔧 正确姿势：按「字节数」读，不按「分隔符」读**

C 工程实践——用 `fread`/`fwrite`，且 `fopen` 必须带 `"b"`：

```c
FILE *fp = fopen("data.bin", "rb");           // "rb"：Windows 上关掉 \r\n 转换，POSIX 上 b 被忽略但要写上
struct Header h;
size_t got = fread(&h, sizeof h, 1, fp);      // 读 1 个 sizeof(Header) 的对象
if (got != 1) { /* 短读或 EOF：检查 feof(fp)/ferror(fp) */ }

// 读变长负载：用返回的元素个数判断，别假设一次读满
unsigned char buf[4096];
size_t n;
while ((n = fread(buf, 1, sizeof buf, fp)) > 0) {
    process(buf, n);                          // fread 也可能短读，靠 n 驱动
}
fclose(fp);
```

裸系统调用版（或网络场景）——用 `read`/`rio_readn`，`rio_readn` 天然「读满 n 或到 EOF」，正是为定长二进制包设计的：

```c
Header h;
if (rio_readn(fd, &h, sizeof h) != sizeof h) { /* 连接过早关闭 */ }
```

**⚠️ 三个二进制读写的真实坑（本节重点）**

1. **字节序**：`fread` 直接读进 `int` 拿到的是文件里的字节序。跨机器/网络的格式要显式定字节序，读完用 `ntohl`/`be32toh` 之类转换，别依赖本机默认。
2. **结构体 padding 与对齐**：`fwrite(&struct)` 会把编译器插入的填充字节一起写出去，换个编译器/架构布局就变了。可移植的二进制格式要么**逐字段序列化**，要么对结构体加 `#pragma pack`/`__attribute__((packed))` 并接受非对齐访问代价。
3. **短读是常态**：`fread`/`read` 返回的元素/字节数可能小于请求值（EOF、信号、网络节奏），必须用**返回值**驱动循环，不能假设一次读满——这和本章开头的 short count 是同一件事。

**🔧 C++ 工程实践：`std::ifstream` 开二进制模式 + `read`**

```cpp
#include <fstream>
std::ifstream in("data.bin", std::ios::binary);   // 关键：ios::binary
Header h;
in.read(reinterpret_cast<char*>(&h), sizeof h);
if (!in) { /* in.gcount() 给出实际读到的字节数 */ }

// 读整个文件到 vector<uint8_t>
std::vector<uint8_t> data(
    (std::istreambuf_iterator<char>(in)),
    std::istreambuf_iterator<char>());
```

- 千万别用 `operator>>` 或 `std::getline` 读二进制——它们会跳空白、按行切，语义和 `fgets` 一样错
- `in.read(...)` 是「尽量读满」，`in.gcount()` 拿实际字节数；短读时流进入 `fail` 状态
- 现代 C++ 反序列化通常交给库：Protobuf、Cap'n Proto、FlatBuffers、Boost.Serialization——它们统一解决字节序、对齐、版本演进三件事，工程里几乎不手写二进制格式

**一句话总结**：文本 vs 二进制的分界不在文件后缀，而在**「你按分隔符切，还是按字节数切」**。二进制一律走 `fread`/`read`/`istream::read` 这类按字节数的接口，`fopen`/`ifstream` 记得带 `b`/`ios::binary`，并对字节序、padding、短读三件事保持警惕。
