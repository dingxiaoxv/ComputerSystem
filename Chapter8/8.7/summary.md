# §8.7 操作进程的工具

这一节是全章的"工具收口"：前面学的 fork/exec/wait、信号、上下文切换、进程状态，到底怎么**从外部观测**？Linux 提供了一套标准命令行工具——`strace` 看系统调用、`ps` 看进程快照、`top` 看实时资源、`pmap` 看地址空间，而它们背后的数据源几乎都来自同一个地方：内核导出的虚拟文件系统 `/proc`。这一节难点不在概念，而在于**知道遇到什么现象该掏出哪个工具、该读哪个文件**。

---

## strace：追踪系统调用

**🎯 它是什么**

`strace` 打印一个程序（及其子进程）执行过程中调用的**每一条系统调用**和接收到的信号，是把"进程和内核之间的对话"录下来的工具。本章学的 `fork`/`execve`/`wait4`/`rt_sigaction` 全都能在 strace 输出里一眼认出来。

```
$ strace -f ./shell           # -f 跟踪 fork/clone 出的子进程
clone(...)            = 4242   # fork 的真身
wait4(-1, ...)        = 4242   # waitpid 回收
rt_sigaction(SIGCHLD, ...)     # signal 安装 handler
execve("/bin/ls", ...) = 0     # 子进程换核心
```

**🔧 工程典型用法**

- `strace -f ./prog`：跟踪含子进程的程序（shell、守护进程必加 `-f`）
- `strace -e trace=openat,read,write ./prog`：只看文件相关调用，过滤噪音
- `strace -p 1234`：attach 到一个**已经在跑**的进程（卡死时的第一手段）
- `strace -c ./prog`：不打印细节，只统计每种系统调用的次数和耗时（找 syscall 热点）
- `strace -T ./prog`：每条 syscall 后面带耗时，定位是哪个调用阻塞了
- `strace -tt ./prog`：带微秒时间戳，看事件先后顺序

**🔧 `-e trace=` 的过滤写法**

`-e trace=` 既能写**具体 syscall 名**，也能写 strace 预定义的**系统调用分类**（新版用 `%` 前缀），不用一个个手列：

| 分类 | 覆盖的系统调用 | 本章/工程场景 |
|------|---------------|--------------|
| `%process` | `fork`/`clone`/`execve`/`wait4`/`exit_group` | §8.4 进程控制，跟踪 fork+exec 首选 |
| `%signal` | `rt_sigaction`/`rt_sigprocmask`/`kill`/`sigreturn` | §8.5 信号，看 handler 安装和掩码操作 |
| `%file` | 带文件名的调用：`openat`/`stat`/`unlink`/`access` | 排查"程序找哪个配置文件/找不到文件" |
| `%desc` | 按 fd 操作：`read`/`write`/`close`/`fcntl`/`epoll` | 第 10 章 I/O，看读写流量 |
| `%network`(`%net`) | `socket`/`connect`/`bind`/`sendto`/`recvfrom` | 排查网络连接、连了哪个 IP |
| `%memory` | `mmap`/`munmap`/`brk`/`mprotect` | 第 9 章虚拟内存，看堆/映射变化 |

```bash
strace -f -e trace=%process,%signal ./shell   # 最贴合第 8 章的组合
strace -e trace=!openat,mmap ./prog           # 取反：去掉启动期库加载噪音
strace -e trace=/^rt_sig ./prog               # 正则：只看 rt_sig* 信号调用
```

配套的其他 `-e` 选项：`-e signal=SIGCHLD,SIGINT`（只显示收到的某几种信号）、`-e read=3 -e write=1`（dump 出指定 fd 上读写的实际数据）、`-e status=failed`（新版，只显示返回出错的 syscall，快速找 `ENOENT` 之类的失败点）、`-e inject=openat:error=ENOENT`（故障注入，强行让某 syscall 失败以测错误处理）。

**⚠️ 书里特别提醒**：要让 strace 输出最干净，编译时建议加 `-static`（静态链接），否则启动阶段会被一大堆动态链接器加载 `.so` 的 `openat`/`mmap` 刷屏。真实排障时一般用 `-e` 过滤掉这些启动噪音即可。

---

## ps / top：进程快照与实时监控

**🎯 ps = 一张静态快照**

`ps` 列出当前系统的进程，**包括僵尸进程**（这正好印证 §8.4 学的：僵尸"已终止未回收"，`ps` 里状态列是 `Z`/`defunct`）。

```
$ ps aux                       # 看所有用户的所有进程，含 CPU/MEM%
$ ps -o pid,ppid,stat,cmd      # 自定义列：父子关系 + 状态
$ ps -o stat --ppid 4242       # 看某父进程的子进程状态（前几节验证僵尸用过）
```

STAT 列字母对应进程状态，是本章进程状态机的直接体现：

| STAT | 含义 |
|------|------|
| `R` | 运行 / 可运行（在运行队列里） |
| `S` | 可中断睡眠（等事件，如等 I/O、等信号） |
| `D` | 不可中断睡眠（通常卡在磁盘 I/O，`kill` 都打不动） |
| `T` | 已停止（收到 SIGSTOP/SIGTSTP） |
| `Z` | 僵尸（defunct，等父进程 wait 回收） |

**🎯 top = 动态刷新的实时视图**

`top` 打印进程的资源占用并**周期刷新**，是观察"谁在吃 CPU/内存"的第一工具。

- 顶部 `load average` 三个数 = 1/5/15 分钟平均负载（见下文 `/proc/loadavg`）
- 按 `P` 按 CPU 排序、`M` 按内存排序、`1` 展开每个核
- `top -H -p 1234`：把进程 1234 的**线程**逐个列出（第 12 章并发会用到）

**🔧 工程上**：`top` 适合"现在卡了，谁的锅"的实时定位；`ps` 适合脚本里抓快照、grep 特定进程。现代机器上 `htop`（交互更友好）、`pidstat -p <pid> 1`（按进程逐秒采样）是常用增强版。

---

## pmap：进程地址空间映射

**🎯 它是什么**

`pmap` 显示一个进程的**内存映射**——代码段、数据段、堆、栈、共享库、mmap 区各自的起止地址和大小。它是第 9 章虚拟内存的预热工具，本质是把 `/proc/<pid>/maps` 整理成人类可读格式。

```
$ pmap -x 1234
Address           Kbytes     RSS   Dirty Mode  Mapping
000055...000        132       80       0 r-x-- prog        # 代码段（只读可执行）
000055...000          8        8       8 rw--- prog        # 数据段（可写）
00007f...000       1804      300       0 r-x-- libc.so.6   # 共享库代码
00007ff...000        132      24      24 rw---   [ stack ] # 栈
 total            xxxxK
```

**🔧 工程典型用法**

- `pmap -x <pid>`：带 RSS（实际驻留物理内存）和 Dirty（脏页）列，排查内存到底耗在哪个映射
- 看一个进程加载了哪些 `.so`、共享库占多少、是否有异常大的匿名 mmap（内存泄漏线索）
- 对照 `ps` 里的 RSS：`pmap` 能告诉你那块内存**具体是什么**

---

## /proc：内核状态的文件接口

**🎯 它是什么**

`/proc` 是一个**虚拟文件系统**——它不在磁盘上，每次读取时由内核现场生成内容，把大量内核数据结构以 ASCII 文本形式导出给用户程序。`ps`/`top`/`pmap`/`free`/`uptime` 这些工具本质都是在解析 `/proc` 下的文件。书里给的入门例子就是：

```
$ cat /proc/loadavg
0.52 0.48 0.45 2/431 8921
```

**🎯 两类路径：`/proc/<pid>/*`（单进程）和 `/proc/*`（全系统）**

- `/proc/<pid>/...`：某个进程的私有信息
- `/proc/<其他名字>`：整机层面的内核状态

**🔧 分析"单个进程"的常用路径**

| 路径 | 看什么 | 关联本章/工程场景 |
|------|--------|------------------|
| `/proc/<pid>/status` | 人类可读的进程状态：状态、VmRSS、线程数、`SigPnd`/`SigBlk`/`SigCgt` | §8.5 的 pending/blocked 位向量快照就在这里 |
| `/proc/<pid>/maps` | 地址空间映射（pmap 的原始数据） | §8.2 私有地址空间、第 9 章虚拟内存 |
| `/proc/<pid>/fd/` | 进程打开的所有文件描述符（符号链接指向真实文件/socket/pipe） | 第 10 章 I/O；排查"句柄泄漏"、看进程连了哪些 socket |
| `/proc/<pid>/cmdline` | 启动命令行（`\0` 分隔） | 反查某 PID 到底是什么程序 |
| `/proc/<pid>/stat` | 机器可读的一行状态（utime/stime/starttime/状态等） | `ps`/`top` 解析它算 CPU 占用 |
| `/proc/<pid>/io` | 该进程读写的字节数（rchar/wchar、read_bytes/write_bytes） | 找"哪个进程在猛刷磁盘" |
| `/proc/<pid>/smaps` | maps 的精细版，每段的 Pss/Swap/脏页 | 精确算共享内存分摊（Pss） |

**🔧 分析"系统性能与负载"的常用路径**

| 路径 | 看什么 | 怎么用来判断瓶颈 |
|------|--------|-----------------|
| `/proc/loadavg` | 1/5/15 分钟平均负载 + 运行/总进程数 + 最近 PID | 负载持续 > 核数说明 CPU/IO 排队；三个数递增=越来越忙 |
| `/proc/stat` | 全系统 CPU 累计时间（user/nice/system/idle/iowait/irq…）、上下文切换数、启动后总 fork 数 | `iowait` 高=卡在磁盘；`ctxt` 涨太快=切换开销大；对两次采样做差才有意义 |
| `/proc/meminfo` | 内存全景：MemTotal/MemFree/MemAvailable/Buffers/Cached/SwapFree | `MemAvailable` 才是"真正可用"；Swap 在掉=内存吃紧 |
| `/proc/cpuinfo` | 每个逻辑核的型号、主频、cache 大小、标志位 | 第 5/6 章算 CPE、看 cache 层级容量的依据 |
| `/proc/uptime` | 系统已运行秒数 + 累计空闲秒数 | 算整机平均 CPU 利用率 |
| `/proc/vmstat` | 虚拟内存细粒度统计：pgfault/pgmajfault、pgpgin/out、pswpin/out | major fault 高=频繁换页（第 9 章）；区分 minor/major 缺页 |
| `/proc/interrupts` | 各 CPU 各类中断（含设备中断）计数 | §8.1 异步中断的实测；定位中断不均衡 |
| `/proc/diskstats` | 每块磁盘的 I/O 次数、扇区数、耗时 | `iostat` 的数据源，判断磁盘是否饱和 |
| `/proc/net/dev` | 每个网卡收发字节/包/错误数 | 网络吞吐和丢包初筛 |

**⚠️ 关键认知**：`/proc/stat`、`/proc/diskstats` 这类是**自启动以来的累计值**，单次 `cat` 没意义——必须**间隔采样做差**（这正是 `top`/`vmstat 1`/`iostat 1` 每秒刷新背后做的事）。

---

## 解读 /proc/<pid>/status

**⚠️ 先分清 status 和 stat 两个文件**

同一进程目录下有两个名字像的文件，别搞混：

- `/proc/<pid>/status`：**多行、人类可读**，字段带名字（`State:`/`VmRSS:`/`SigBlk:`），手动排障看它
- `/proc/<pid>/stat`：**单行、机器可读**，几十个空格分隔的字段、无字段名，`ps`/`top` 解析它算 CPU 占用

下面讲的是 `status`，给一台进程做"体检"时最常 `cat` 的文件。

**🎯 第一眼看 State**

```
State:  S (sleeping)
```

字母含义和 §8.7 的 `ps` STAT 表完全一致：`R` 运行/可运行、`S` 可中断睡眠（等事件，信号能唤醒，绝大多数进程在这）、`D` 不可中断睡眠（通常卡在磁盘 I/O，`kill -9` 都打不动）、`T` 停止（收到 SIGSTOP/SIGTSTP 或被调试器停住）、`Z` 僵尸（§8.4，已终止未回收）。看到 `S` 是正常等待，想知道"等在哪"就接着看 `wchan` 或 `strace -p`。

**🎯 身份与血缘**

```
Tgid:   22087      # 线程组 ID = 主线程 PID，多线程进程对外的"进程号"
Pid:    22087      # 本任务（线程）ID
PPid:   1          # 父进程；PPid=1 = 父进程已退出、被 init/systemd 收养（孤儿进程）
TracerPid: 0       # 谁在 ptrace 它，非 0 = 正被 strace/gdb 附着
Uid: 0 0 0 0       # 实际/有效/保存/文件系统 UID 四元组，排查权限看这
```

`PPid: 1` 很有信息量：本该有守护父进程管理的服务若 PPid 变 1，说明它成了孤儿被 init 接管。

**🎯 信号四件套（直接对应 §8.5 的位向量）**

```
SigPnd: 0000000000000000   # 本线程待处理信号（pending 位向量）
ShdPnd: 0000000000000000   # 整个进程共享的待处理信号
SigBlk: 0000000000000000   # 被阻塞的信号（blocked 掩码，sigprocmask 设的）
SigIgn: 0000000000000000   # 被忽略的信号
SigCgt: 0000000180000000   # 已安装 handler 捕获的信号
```

这些是十六进制位掩码，**第 k 位（从 1 数）对应信号 k**。实操要点：**SigBlk 非 0** = 进程主动屏蔽了某些信号，发了没反应先查它有没有挡；**SigCgt 里置 1 的位** = 装了 handler 的信号。不用手算，看哪些位是 1 再对 `kill -l` 的编号即可。

**🎯 内存占用（性能排查核心）**

```
VmPeak: 历史峰值虚拟内存       VmSize: 当前虚拟内存总量（地址空间，含未驻留）
VmRSS:  ★实际占用的物理内存★   VmSwap: 被换出到 swap 的量（持续涨=内存吃紧）
VmData/VmStk/VmExe/VmLib:  数据段/栈/代码/库 各占多少
```

判断"这进程吃多少内存"看 **VmRSS**，不是 VmSize——VmSize 含大量已映射未使用的地址空间，会虚高。

**🎯 线程与上下文切换**

```
Threads: 8                          # 线程数；和预期对不上要查线程泄漏
voluntary_ctxt_switches:    1234    # 主动让出 CPU（等 I/O/锁）
nonvoluntary_ctxt_switches:   56    # 被调度器抢占
```

两个 ctxt_switches 对两次采样做差很有用：**主动切换多** = 频繁等 I/O 或锁（I/O 密集型）；**被动切换多** = CPU 竞争激烈被频繁抢占。能区分一个"看起来卡"的进程是真在算还是在等。

**🔧 给单进程做体检的标准动作**

```bash
cat /proc/<pid>/status | grep -E 'State|VmRSS|Threads|Sig(Blk|Cgt)|ctxt'  # 一把抓关键字段
cat /proc/<pid>/wchan       # 它睡在哪个内核函数上（如 ep_poll、sk_wait_data）
ls -l /proc/<pid>/fd        # 打开的 fd，0/1/2 指向哪、有无 socket/句柄泄漏
strace -p <pid>             # 实时看它卡在哪个 syscall（最直接）
```

---

## 易错点

- 以为 `strace` 输出乱是程序有问题——其实启动时刷屏的 `openat`/`mmap` 是动态链接器在加载 `.so`，用 `-static` 或 `-e trace=` 过滤即可。
- 跟踪 shell/守护进程忘了加 `-f`，结果 fork 出的子进程一行都看不到——`-f` 才会跟进子进程。
- 把 `ps` 当实时监控、把 `top` 当快照——`ps` 是一次性快照（适合脚本 grep），`top` 是周期刷新（适合实时定位）。
- 误读 `load average`：它不是 CPU 百分比，而是"平均有多少进程在运行或等待运行"，要和核数比较才有意义。
- 直接 `cat /proc/stat` 看 CPU 占用——里面是累计时间，必须两次采样做差，否则得到的是开机至今的总账。
- 把 `/proc/meminfo` 的 `MemFree` 当可用内存——Linux 会把空闲内存拿去做 page cache，`MemAvailable` 才是回收后真正可用的量。
- 以为 `D` 状态的进程 `kill -9` 能杀掉——不可中断睡眠（通常卡在内核 I/O）连 SIGKILL 都得等它醒来。

---

## 工程关联

- **线上排障三板斧**：`top` 看谁占资源 → `ps`/`/proc/<pid>/status` 看目标进程状态 → `strace -p <pid>` 看它卡在哪个系统调用。这套流程几乎是所有"服务无响应"问题的起手式。
- **句柄泄漏定位**：`ls /proc/<pid>/fd | wc -l` 看 fd 数量是否持续增长，逐个 readlink 看泄漏的是文件还是 socket（第 10 章）。
- **监控系统的底层**：Prometheus 的 node_exporter、`sar`、`vmstat`、`iostat`、`free` 全部读 `/proc`（和 `/sys`），理解 `/proc` 就理解了这些工具的数据来源。
- **容器与 `/proc`**：容器里看到的 `/proc/cpuinfo`/`meminfo` 可能是宿主机的（cgroup 限制不一定反映在这里），这是容器内程序误判可用资源的经典坑。
- **性能分析衔接**：`/proc/cpuinfo` 的 cache 大小、`/proc/vmstat` 的 major fault、`/proc/stat` 的 iowait，分别是第 5、6、9 章性能分析要反复回看的指标。

---

## 实验题

**🧪 题 1：用 strace 看清 fork+execve+wait 的完整对话**

写一个最小的"运行子程序"程序：

```c
#include <unistd.h>
#include <sys/wait.h>
int main(void) {
    pid_t pid = fork();
    if (pid == 0) {
        char *argv[] = {"/bin/echo", "hello", NULL};
        execve("/bin/echo", argv, NULL);   // 不返回
    }
    int status;
    waitpid(pid, &status, 0);
    return 0;
}
```

要求：
- `gcc -static -o runchild runchild.c` 后用 `strace -f ./runchild`
- 在输出里**逐条圈出** `clone`（fork 的真身）、`execve`、`wait4`，确认子进程的 syscall 缩进/PID 和父进程不同
- 对比加 `-static` 和不加时，输出里启动阶段 `openat .so` 的行数差异
- 再跑一次 `strace -c ./runchild`，看哪个 syscall 次数最多

**🧪 题 2：手算 /proc/stat 的 CPU 利用率**

要求：
- 间隔 1 秒读两次 `/proc/stat` 的第一行（`cpu` 行），记下各字段
- 对两次做差，用 `1 - Δidle / Δtotal` 算出这 1 秒的整机 CPU 利用率
- 同时跑 `top`，对比 `top` 顶部显示的 CPU% 是否一致——验证 `top` 就是这么算出来的
- 思考：为什么单次 `cat /proc/stat` 算不出"当前"利用率

**🧪 题 3：用 /proc 给一个进程做体检**

挑一个正在运行的进程（如自己的 shell 或一个 `sleep 1000 &`），要求：
- `cat /proc/<pid>/status`，找出 `State`、`VmRSS`、`Threads`、`SigBlk`/`SigCgt`，对照 §8.5 解释 SigCgt（已捕获信号掩码）里有哪些位
- `ls -l /proc/<pid>/fd`，确认 0/1/2 分别指向哪里（stdin/stdout/stderr 的真身）
- `cat /proc/<pid>/maps` 和 `pmap -x <pid>` 对比，确认 pmap 就是 maps 的可读化
- 给这个进程发 `kill -STOP <pid>`，再看 `ps -o stat` 的状态变成 `T`，然后 `kill -CONT` 恢复
