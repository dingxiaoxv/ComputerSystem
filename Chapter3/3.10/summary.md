# §3.10 在机器级程序中将控制与数据结合起来

这一节的主线是：**把前面学的"控制流 + 数据访问"合在一起看真实程序，重点是指针的本质、用 GDB 把抽象映射到机器、以及缓冲区溢出这个最经典的安全漏洞如何在汇编层面发生与防御**

- 指针在机器层面就是地址（一个 `long`），类型只活在编译期
- GDB 是把"C 抽象"和"机器现实"对齐的唯一可靠工具
- 缓冲区溢出 = 栈上局部数组越界写 → 覆盖返回地址 → 控制流被劫持
- 现代 Linux 用栈保护（canary）、不可执行栈（NX）、ASLR 三件套对抗
- 变长栈帧需要 `%rbp` 帧指针，因为 `%rsp` 在运行时才能确定偏移

---

## 指针的机器级本质

**🎯 指针就是地址，类型是编译期信息**

C 里 `int *p` 和 `char *p` 在运行时都是一个 8 字节地址（x86-64）。类型差异只影响：

- `*p` 读多少字节、按什么解释
- `p + 1` 加多少（按 `sizeof(*p)` 缩放）
- `&p[i]` 的偏移计算（`i * sizeof(*p)`）

机器指令本身不带类型，所有"类型语义"都被编译器翻译成具体的字节数。

**🎯 五条指针原则（书上原文）**

| 原则 | 机器层含义 |
|------|----------|
| 每个指针都有类型 | 类型只存在于编译器，运行时只剩字节宽度 |
| 每个指针都有一个值 | 值就是地址，特殊值 `NULL = 0` |
| 指针用 `&` 创建 | 通常对应 `leaq` 指令 |
| 指针用 `*` 解引用 | 对应一次内存访问（`movq (%rax), %rdx`） |
| 数组与指针紧密联系 | `a[i]` 等价于 `*(a+i)`，都编译成同一种地址计算 |

**🎯 函数指针**

```c
int (*fp)(int, int *) = &foo;   // 也可以写 = foo
int r = fp(3, &x);              // 也可以写 (*fp)(3, &x)
```

函数指针的值就是函数代码的入口地址。调用时编译成 `call *%rax` 这种**间接调用**——`call` 后面跟寄存器或内存而非立即地址。

```asm
movq foo@GOTPCREL(%rip), %rax   # 取函数地址
call *%rax                       # 间接调用
```

`*` 在声明和调用里位置容易混，记住：声明里 `(*fp)` 的括号是必须的，否则 `int *fp(...)` 会被解析成"返回 int* 的函数"。

---

## 用 GDB 阅读机器级行为

**🎯 GDB 在嵌入式/服务端的真实使用场景**

教科书演示常用"打断点→单步执行"，但工程现场更多是两类场景：

- **事后分析**：进程崩了，拿 coredump + 二进制 + 符号表，离线复盘崩在哪、为什么
- **在线分析**：进程没崩但表现异常（hang、CPU 飙高、死锁），`gdb -p` 挂上去看每个线程在干什么

这两类场景几乎不用 `b/r/s/n`，主要用的是 `bt` / `info threads` / `x` / `info reg` / `frame` / `p`。

**🎯 通用命令骨架（任何场景都要用）**

| 类别 | 命令 | 用途 |
|------|------|------|
| 调用栈 | `bt` / `bt full` | 当前线程栈；`full` 附带每帧的局部变量 |
| 切栈帧 | `f N` / `up` / `down` | 跳到第 N 帧 / 上一帧 / 下一帧 |
| 看变量 | `p expr` / `p/x` / `p *p@N` | 表达式求值；`*p@N` 把指针当数组打印 N 个 |
| 看寄存器 | `info reg` / `p $rax` | 所有 / 单个 |
| 看内存 | `x/<n><f><u> addr` | n 个单位，f 格式（x/d/s/i），u 大小（b/h/w/g） |
| 反汇编 | `disas` / `disas /m` / `disas func` | 当前函数 / 混合源码 / 指定函数 |
| 符号 | `info symbol 0xADDR` / `info line *0xADDR` | 地址 → 函数名 / 源码行 |
| 共享库 | `info sharedlibrary` | 加载的 .so 列表与地址范围 |

---

### 场景一：coredump 离线分析

**🔧 前置条件清单**

coredump 能用的前提常被忽略，先确认这一组开关：

```bash
# 1. shell 的 core size 限制
ulimit -c unlimited

# 2. 内核 core 文件路径模板（很多发行版默认走 systemd-coredump 或 apport，文件不在 cwd）
cat /proc/sys/kernel/core_pattern
# 常见值：
#   |/lib/systemd/systemd-coredump %P %u %g %s %t %c %h   → 用 coredumpctl 取
#   |/usr/share/apport/apport ...                         → Ubuntu，去 /var/crash
#   core.%e.%p                                            → 直接落在 cwd

# 3. systemd 体系下查 core
coredumpctl list
coredumpctl gdb <PID>     # 自动调起 gdb，免去找 core 文件路径

# 4. 二进制必须带符号（编译时 -g，部署时不要 strip；或留 .debug 文件配 build-id）
file ./a.out             # 看 "with debug_info, not stripped"
```

**⚠️ 嵌入式特有坑**

- 嵌入式系统经常 `ulimit -c 0` 默认禁用 core，需要在启动脚本里显式打开
- flash 空间紧张时 core 写不下，要么设 `core_pattern` 重定向到外部存储，要么用 `setrlimit` 截断
- 交叉编译场景：分析 ARM core 需要 `gdb-multiarch` 或 `arm-linux-gnueabihf-gdb`，并用 `set sysroot` 指向目标机的根文件系统拷贝

**🔧 标准启动流程**

```bash
gdb /path/to/binary /path/to/core
# 或
gdb -c core.1234 ./binary
```

进入后先做三件事：

```
(gdb) bt                          # 看崩在哪条调用链上
(gdb) info threads                # 看是不是多线程程序，崩的是哪个线程
(gdb) info registers              # 看现场寄存器
```

**🔧 定位崩溃根因的常用动作**

```
(gdb) bt full                     # 看每一帧的局部变量
(gdb) f 2                         # 切到怀疑的帧
(gdb) info args                   # 这一帧的函数参数
(gdb) info locals                 # 这一帧的局部变量
(gdb) p some_struct               # 打印整个结构体
(gdb) p *ptr                      # 解引用看内容（NULL/野指针会报错——这本身就是答案）
(gdb) x/64bx ptr                  # 当内存还能读时，按字节看 raw 内容
```

判断常见崩因：

| 现象 | 大概率原因 |
|------|----------|
| `bt` 顶端是 `??` 或地址全是 `0x4141...` | 栈被踩飞 / 缓冲区溢出 / 返回地址被覆盖 |
| `bt` 全是 `??` 没有任何函数名 | 缺符号；找对应的 `-dbg` 包或 `.debug` 文件 |
| 崩在 `memcpy`/`memset`/`strlen` 等 libc 函数 | 多半是上层传了野指针/越界长度，看 `f 1` 调用方 |
| 崩在 `free`/`malloc` 内部 | 堆破坏（double free、heap overflow），核心信息可能已在更早 |
| `SIGABRT` + 栈顶是 `__stack_chk_fail` | 栈金丝雀被破坏，缓冲区溢出 |
| `SIGABRT` + `__assert_fail` | 显式 assert，看上一帧的表达式 |

**🔧 符号缺失的补救**

```
(gdb) info sharedlibrary           # 看哪些 .so 状态是 "No"（没符号）
(gdb) set debug-file-directory /path/to/symbols
(gdb) set sysroot /path/to/target-rootfs    # 交叉调试关键
(gdb) file /correct/path/to/binary           # 二进制错了重新指
```

build-id 匹配：core 里记录了原始二进制的 build-id，错版本的二进制 gdb 会警告，回溯结果不可信。

---

### 场景二：挂活进程分析多线程问题

**🔧 attach 与 detach**

```bash
gdb -p <PID>           # 挂上去，进程被 SIGSTOP 暂停
# 或在 gdb 里：
(gdb) attach <PID>
(gdb) detach           # 务必 detach 再退出，否则被调试进程会被 kill
(gdb) quit             # 退出时若未 detach 会询问
```

**⚠️ 安全/权限注意**

- 内核 `ptrace_scope` 限制 attach：`cat /proc/sys/kernel/yama/ptrace_scope`，值为 1 时只能 attach 自己启动的子进程；调试他人进程需要 root 或 `CAP_SYS_PTRACE`
- attach 会暂停目标进程，**线上服务慎用**——hang 超过几秒会影响 SLA、被守护进程踢掉、心跳超时被 K8s 重启
- 嵌入式系统上 attach 可能阻塞看门狗喂狗线程，导致 WDT reset；attach 前要么先关 watchdog，要么用非侵入式的 `gcore`

**🔧 不停进程的折中：`gcore`**

```bash
gcore <PID>                  # 生成 core.<PID>，不杀进程，只暂停极短时间
gdb ./binary core.<PID>      # 然后离线分析，等价场景一
```

线上排查 hang/死锁的标准做法：`gcore` 取快照 + 离线分析，避免长时间挂住进程。

**🔧 多线程现场分析的核心命令**

```
(gdb) info threads
  Id   Target Id                  Frame
* 1    Thread 0x7f... (LWP 1234)  __lll_lock_wait () at ...
  2    Thread 0x7f... (LWP 1235)  pthread_cond_wait@@GLIBC_...
  3    Thread 0x7f... (LWP 1236)  __lll_lock_wait () at ...

(gdb) thread 2                    # 切到 2 号线程
(gdb) bt                          # 看它的栈
(gdb) thread apply all bt         # 一次性打印所有线程的栈（最常用！）
(gdb) thread apply all bt full    # 加上局部变量
```

`thread apply all bt` 的输出是分析死锁、hang、CPU 飙高的第一手资料，等价于 Java 的 `jstack`。

**🎯 识别常见多线程现象的栈特征**

| 栈顶函数 | 含义 |
|---------|------|
| `__lll_lock_wait` / `pthread_mutex_lock` | 在等互斥锁——找谁持有 |
| `pthread_cond_wait` / `pthread_cond_timedwait` | 等条件变量，正常 idle 也长这样 |
| `futex_wait` / `do_futex` | 等 futex，是各种同步原语的底层 |
| `epoll_wait` / `poll` / `select` | 在等 I/O，事件循环空闲 |
| `read` / `recv` / `recvfrom` | 阻塞读，可能对端没发数据 |
| `nanosleep` / `clock_nanosleep` | 主动 sleep，正常 |
| `__GI___libc_malloc` 内部很深 | 可能在 arena 锁上，多线程分配争用 |

**🔧 死锁定位实战流程**

1. `thread apply all bt` 找出所有卡在 `__lll_lock_wait` 的线程
2. 每个等锁的线程切过去 `frame N` 找到 `pthread_mutex_lock(&m)` 那一帧，`p &m` 拿到锁地址
3. 用 `p m.__data.__owner` 查这把锁的持有者 LWP（glibc 的 `pthread_mutex_t` 有 `__owner` 字段，记录持有线程的 TID）
4. 切到持有者线程看它在等什么 → 形成等待环就是死锁

```
(gdb) p mtx
$1 = {__data = {__lock = 2, __count = 0, __owner = 1235, ...}, ...}
                                          ^^^^^^^^^^^^^
                                          这把锁被 LWP 1235 持有
```

**🔧 CPU 飙高定位**

非侵入式方法（**优先**）：

```bash
top -H -p <PID>              # 找出 CPU 占用最高的线程 LWP
# 然后 attach 或 gcore 后：
(gdb) thread find <LWP>      # 找到 gdb 内对应的 thread id
(gdb) thread <id>
(gdb) bt
```

反复 `gcore` 几次（间隔 1-2 秒），对比同一线程的栈：如果总是停在同一个函数，那就是热点；如果在变化，说明在正常推进只是慢。

**🔧 嵌入式高频用法补充**

- **看寄存器观察硬件交互**：`info reg`、`p/x *(volatile uint32_t*)0x40021000`（直接读 MMIO 地址；用户态 Linux 程序需要 mmap `/dev/mem` 才能读）
- **远程调试**：目标板跑 `gdbserver :1234 ./app`，宿主机 `gdb-multiarch ./app`，`target remote target-ip:1234`；或 `target extended-remote` 支持 attach/run
- **核对中断/信号上下文**：信号处理器栈帧顶部会看到 `<signal handler called>`，下面就是被中断时的现场
- **打印结构体宏一键化**：在 `~/.gdbinit` 里写自定义命令，把常用的状态机/数据结构 dump 包装成一行命令

**🔧 `~/.gdbinit` 推荐配置**

```
set print pretty on            # 结构体换行打印
set print array on             # 数组按行打印
set print elements 0           # 不截断长字符串/数组
set pagination off             # 不要 --More-- 分页
set history save on            # 保存命令历史
set logging file gdb.log
set logging on                 # 把会话存盘，方便复盘
set confirm off                # 减少二次确认
# Python 增强：
source /usr/share/gdb-dashboard/.gdbinit   # 可视化面板（可选）
```

强烈推荐安装 [`pwndbg`](https://github.com/pwndbg/pwndbg) 或 [`gef`](https://github.com/hugsy/gef)：自动美化栈/寄存器/反汇编显示，对崩溃和溢出分析体验提升巨大。

---

**🎯 `x` 命令的格式参数（速查）**

```
x/4gx $rsp     # 从 rsp 开始 4 个 giant(8字节) 按 hex 显示
x/16bx $rdi    # 16 字节按 hex
x/s $rdi       # 当字符串解释
x/i $rip       # 当指令反汇编
x/20i $pc-40   # 看 PC 前后的指令上下文（崩溃现场很常用）
```

格式字母：`x`(hex) `d`(dec) `u`(udec) `s`(string) `i`(inst) `c`(char) `f`(float)；单位：`b`(byte) `h`(2) `w`(4) `g`(8)。

---

## 内存的越界引用与缓冲区溢出

**🎯 为什么栈上数组溢出最危险**

C 不做数组边界检查。栈上局部数组溢出会覆盖**同一栈帧内更高地址**的内容，包括：

- 其他局部变量
- 保存的寄存器（callee-saved）
- **返回地址**（`call` 时压栈，`ret` 时弹出跳转）

覆盖返回地址 = 攻击者控制 `ret` 跳转目标 = 程序控制流被劫持。

**🎯 经典脆弱函数：`gets`**

```c
char *gets(char *s) {
    int c;
    char *dest = s;
    while ((c = getchar()) != '\n' && c != EOF)
        *dest++ = c;          // 完全没检查 s 的容量
    *dest++ = '\0';
    return s;
}
```

调用者无法告诉 `gets` 缓冲区有多大，所以 `gets` 没有安全使用方式。同类危险函数：`strcpy` / `strcat` / `sprintf` / `scanf("%s")`。替代品：`fgets` / `strncpy` / `snprintf`。

**🔧 栈布局示意（书上 echo 例子）**

```
高地址
┌────────────────────┐
│ 返回地址（call 压入）│  ← gets 越界写超过这里就劫持控制流
├────────────────────┤
│ 保存的 %rbx 等      │
├────────────────────┤
│ char buf[8]         │  ← gets 从这里开始写
└────────────────────┘
低地址  ← %rsp
```

输入超过 8 字节时，多出的字节按地址递增方向继续写，依次覆盖保存寄存器、返回地址。

**🎯 攻击的完整链条**

1. 找一个调用了 `gets`/`strcpy` 等的函数
2. 构造 payload：填充字节 + 想覆盖的返回地址（小端序写入）
3. 返回地址指向：
   - 栈上注入的 shellcode（已被 NX 禁掉）
   - 或现有代码片段（ROP / return-to-libc，仍然可行）

---

## 对抗缓冲区溢出的三件套

**🎯 栈随机化（ASLR, Address Space Layout Randomization）**

每次运行时栈的起始地址随机化，让"猜返回地址值"变难。Linux 默认开启，可以验证：

```bash
cat /proc/sys/kernel/randomize_va_space   # 2 = 完全随机
```

绕过手段：信息泄露（让程序先打印某个栈地址），或 NOP sled（构造大片 `nop` 作"着陆区"扩大命中范围）。

**🎯 栈破坏检测（Stack Canary / Stack Protector）**

GCC 的 `-fstack-protector` 在栈帧里插入一个**金丝雀值**，函数返回前检查它是否被改动。汇编层面看到的模式：

```asm
# 函数入口
movq %fs:40, %rax           # 从 %fs:40 取 canary（每个线程独立）
movq %rax, -8(%rbp)         # 存到栈上 buf 之后

# 函数出口
movq -8(%rbp), %rax
xorq %fs:40, %rax           # 比较
je   .L_ok
call __stack_chk_fail       # 不等 → 进程立即中止
```

金丝雀值含 `\0` 字节，让 `strcpy` 之类的字符串函数无法整段越过它而不破坏它。

**🎯 限制可执行代码区（NX, Non-Executable Stack）**

页表项里加上 NX 位（书上称 "no-execute"），栈和堆默认不可执行。攻击者无法直接在栈上注入 shellcode 然后跳过去执行。

绕过手段：**ROP**（Return-Oriented Programming），用现有代码段里的小片段（gadget）拼出攻击逻辑，全程只跳到可执行区。

**⚠️ 三件套各自不是银弹**

- ASLR 能被信息泄露绕过
- canary 只在函数返回时检查，溢出到其他变量不会触发
- NX 防不住 ROP

现代攻防是组合战，但这三个一起开能让相当大比例的入门级溢出失效。

---

## 支持变长栈帧

**🎯 什么时候栈帧大小是变长的**

```c
long vframe(long n, long idx, long *q) {
    long i;
    long *p[n];          // 变长数组 VLA：长度运行时才知道
    p[0] = &i;
    for (i = 1; i < n; i++) p[i] = q;
    return *p[idx];
}
```

`p[n]` 需要 `8*n` 字节栈空间，`n` 在编译期未知 → 栈帧大小动态确定。

**🎯 为什么需要 `%rbp`**

固定栈帧里，所有局部变量相对 `%rsp` 的偏移是常量，可以直接 `movq -16(%rsp), %rax` 访问。

但变长栈帧里 `%rsp` 在 `subq %rax, %rsp` 后位置不固定，编译器需要一个**稳定的参考点**——这就是帧指针 `%rbp`：

```asm
pushq %rbp               # 保存旧 rbp
movq  %rsp, %rbp         # rbp 锁定当前栈顶位置
subq  $16, %rsp          # 固定大小局部变量区
# ... 动态分配 ...
subq  %rax, %rsp         # 栈再增长 8n 字节
# 访问固定局部变量仍可用 -16(%rbp)
# 访问 VLA 用 (%rsp) 起算
leave                    # 等价 movq %rbp,%rsp; popq %rbp
ret
```

**⚠️ 帧指针不是必须的**

GCC `-fomit-frame-pointer`（`-O1` 以上默认开）会省掉 `%rbp`，多出一个通用寄存器可用。只有需要变长栈帧或调试时才强制使用。所以你看现代 `-O2` 汇编通常没有 `pushq %rbp / movq %rsp, %rbp` 这种序言。

**🎯 `leave` 指令**

专为退出栈帧设计的合并指令：

```asm
leave   等价于:
    movq %rbp, %rsp     # 折叠掉整个栈帧（包括 VLA 部分）
    popq %rbp           # 恢复旧 rbp
```

后面通常紧跟 `ret`。

---

## 易错点

- 指针的类型是编译期信息，运行时只剩一个地址值——`int*` 和 `char*` 在机器层完全一样宽
- `&` 通常翻译成 `leaq` 而不是访存指令，`*` 才是真正的访存
- `gets` 没有任何安全用法，因为它不知道缓冲区有多大；任何含 `gets` 的代码都是漏洞
- 缓冲区溢出真正的危险不是覆盖普通变量，而是覆盖**返回地址**劫持控制流
- canary 检查只发生在函数返回时，溢出到栈帧内其他变量（不跨过 canary）不会被发现
- NX 禁掉的是"在栈上执行代码"，ROP 用现有可执行段的 gadget 不受影响
- `%rbp` 不是 x86-64 必须使用的帧指针，`-O1` 起 GCC 默认省略；只有变长栈帧才强制需要它
- VLA（`int a[n]`）是 C99 特性，C++ 标准并不支持（GCC 作为扩展接受），生产代码一般避免

---

## 工程关联

- `gdb` 的 `disas` / `info reg` / `x/Ngx $rsp` 三连击是阅读 core dump、定位 segfault 的标准动作
- 看到段错误（SIGSEGV）→ 大多对应野指针、空指针、栈溢出三类之一；通过 `bt` 看调用栈先定位现场
- `__stack_chk_fail` 出现在崩溃栈里 = 栈金丝雀被破坏，几乎一定是缓冲区溢出
- ELF 程序头里 `GNU_STACK` 段的标志位决定栈是否可执行：`readelf -l a.out | grep STACK`，应为 `RW`（无 `E`）
- ASLR 状态：`cat /proc/sys/kernel/randomize_va_space`；调试时可临时关掉（`setarch -R`）让地址稳定
- 编译器加固选项常见组合：`-fstack-protector-strong -D_FORTIFY_SOURCE=2 -Wl,-z,relro,-z,now -fPIE -pie`
- Linux 内核漏洞利用文章里常说的 "smashing the stack" / "ret2libc" / "ROP chain" 都对应 §3.10.3-3.10.4 的内容
- `-fomit-frame-pointer` 对 perf 影响：开了之后 `perf record` 用基于 `%rbp` 的回溯会断；要改用 `--call-graph dwarf` 或 `lbr`

---

## 实验题

**🧪 题 1：观察 `&` 编译成 `leaq`**

```c
long take_addr(long x) {
    long *p = &x;
    return (long)p;
}
```

要求：

- `gcc -O1 -S` 生成汇编
- 找出对应 `&x` 的 `leaq` 指令
- 解释为什么需要先把 `x` 放到栈上才能取地址（提示：寄存器没有地址）

**🧪 题 2：GDB 实战——观察栈帧布局**

```c
#include <stdio.h>
void inner(int a, int b) {
    int local = a + b;
    printf("%d\n", local);
}
int main(void) { inner(3, 4); return 0; }
```

要求：

- `gcc -O0 -g -o demo demo.c`
- `gdb ./demo`，`b inner`，`r`
- 用 `disas`、`info reg rsp rbp`、`x/8gx $rsp`、`bt` 把这些信息对应起来
- 找出返回地址在栈上的位置，并用 `disas main` 验证它确实是 `call inner` 后面那条指令的地址

**🧪 题 3：手动触发栈金丝雀**

```c
#include <string.h>
void victim(const char *s) {
    char buf[8];
    strcpy(buf, s);     // 故意不限长度
}
int main(int argc, char **argv) {
    victim(argv[1]);
    return 0;
}
```

要求：

- 编译两次：`gcc -O0 -fno-stack-protector` 和 `gcc -O0 -fstack-protector-strong`
- 分别 `objdump -d` 找 `victim`，对比有无 canary 相关的 `%fs:40` 指令
- 给两个程序传一个 32 字节字符串作为参数，观察行为差异（一个可能 segfault 或继续跑，一个会输出 `*** stack smashing detected ***`）
- **不要**对系统服务做这类实验，只在自己的 demo 里玩

**🧪 题 4：观察 NX 的体现**

要求：

- `readelf -l /bin/ls | grep -A1 GNU_STACK`，确认 `Flags` 是 `RW`（没有 `E`）
- 写一个最小程序，强制让链接器开启可执行栈：`gcc -z execstack demo.c`，再次 `readelf`，观察 Flags 变成 `RWE`
- 不要在生产二进制里这么做；这个实验只是为了看 ELF 头怎么记录 NX 状态

**🧪 题 5：对比有无帧指针的汇编**

```c
long sum(long *a, long n) {
    long s = 0;
    for (long i = 0; i < n; i++) s += a[i];
    return s;
}
```

要求：

- 分别用 `gcc -O0 -S` 和 `gcc -O2 -S` 生成汇编
- 观察 `-O0` 版本有 `pushq %rbp; movq %rsp, %rbp; ...; leave; ret`，`-O2` 版本通常没有
- 再加一个变长数组版本：`long buf[n];`，用 `gcc -O2 -S` 编译，确认 `%rbp` 又回来了

**🧪 题 6：coredump 离线分析全流程**

```c
// crash.c
#include <string.h>
#include <stdlib.h>
struct Node { int id; char name[16]; struct Node *next; };
static void walk(struct Node *n) {
    while (n) { strcpy(n->name, "X"); n = n->next; }   // 故意不判 n 是否合法
}
int main(void) {
    struct Node *bad = (struct Node*)0xdeadbeef;       // 野指针
    struct Node head = {1, "head", bad};
    walk(&head);
    return 0;
}
```

要求：

- `ulimit -c unlimited && gcc -O0 -g -o crash crash.c && ./crash`
- 找到 core 文件（必要时 `coredumpctl gdb` 或检查 `/proc/sys/kernel/core_pattern`）
- 按下面的"参考分析流程"把现场看完整

**🔧 参考分析流程**

让 core 落到可执行文件目录（一次性方案）：

```bash
sudo sysctl -w kernel.core_pattern='core.%e.%p'
cd Chapter3/3.10
ulimit -c unlimited
./crash                            # 段错误，生成 core.crash.<pid>
gdb ./crash ./core.crash.*         # 或直接 coredumpctl gdb crash
```

GDB 内分析步骤：

```
(gdb) bt
#0  walk (n=0xdeadbeef) at crash.c:11
#1  0x000055...    in main () at crash.c:19
```

栈顶 `n=0xdeadbeef`，第一直觉就是野指针解引用。继续验证：

```
(gdb) f 0                  # 切到崩溃帧（walk）；注意 n 是 walk 的形参，main 里没有
(gdb) info args            # n = 0xdeadbeef
(gdb) p *n                 # Cannot access memory at address 0xdeadbeef ← 物理崩因
(gdb) x/16bx n             # 同样 Cannot access memory
(gdb) info reg rip rdi     # rdi (第一个参数) 应等于 0xdeadbeef；rip 是崩溃指令地址
(gdb) x/i $rip             # 看崩在哪条指令，多半是 mov ...(%rdi)... 解引用
```

跨帧查看父帧上下文，确认 0xdeadbeef 的来源：

```
(gdb) f 1                  # main 帧
(gdb) info locals
bad = 0xdeadbeef
head = {id = 1, name = "X\000ad", '\000' <repeats 11 times>, next = 0xdeadbeef}
```

注意 `head.name = "X\000ad"`：第一次 `strcpy(n->name, "X")` 写了 `'X'` `'\0'` 两字节，原始 `"head"` 的 `a` `d` 残留——直观演示 `strcpy` 写到结束符就停、不清空缓冲区剩余字节（很多信息泄露漏洞的根源）。

最后从虚拟内存层确认为什么 `0xdeadbeef` 不可访问：

```
(gdb) info proc mappings   # 列出该进程所有 VMA
```

会看到 `0xdeadbeef` 既不在 `[heap]` `[stack]` 也不在任何 ELF/so 段——属于未映射地址，MMU 翻译失败 → 内核发 SIGSEGV → core dump。这就把"野指针 → 段错误"从 C 层一路追到了虚拟内存层。

常见误操作：在 frame 1（main）里执行 `p n` 会报 `No symbol "n" in current context`——`n` 是 `walk` 的形参，只在 frame 0 可见；或用 `p walk::n` 跨帧限定访问。

**🧪 题 7：多线程死锁现场捕获**

```c
// deadlock.c —— 经典 AB-BA 死锁
#include <pthread.h>
#include <unistd.h>
static pthread_mutex_t A = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t B = PTHREAD_MUTEX_INITIALIZER;
static void *t1(void *_) {
    pthread_mutex_lock(&A); sleep(1);
    pthread_mutex_lock(&B);   // 卡在这
    pthread_mutex_unlock(&B); pthread_mutex_unlock(&A); return 0;
}
static void *t2(void *_) {
    pthread_mutex_lock(&B); sleep(1);
    pthread_mutex_lock(&A);   // 卡在这
    pthread_mutex_unlock(&A); pthread_mutex_unlock(&B); return 0;
}
int main(void) {
    pthread_t a, b;
    pthread_create(&a, 0, t1, 0); pthread_create(&b, 0, t2, 0);
    pthread_join(a, 0); pthread_join(b, 0);
    return 0;
}
```

要求：

- `gcc -O0 -g -pthread -o dl deadlock.c && ./dl &`，记下 PID
- 等 2 秒后 `gcore <PID>`（**用 gcore 而不是 gdb attach，模拟线上不停服务的做法**）
- `gdb ./dl core.<PID>`，执行 `thread apply all bt`，确认两个 worker 线程都卡在 `__lll_lock_wait`
- 在两个线程里分别 `p A.__data.__owner` 和 `p B.__data.__owner`，画出"线程 X 持有锁 Y、等待锁 Z"的依赖环
- 收尾 `kill <PID>`

**🔧 参考排查流程**

前置：`gcore` 和 `gdb -p` 都依赖 ptrace。Ubuntu/Debian 默认 `kernel.yama.ptrace_scope=1`，**不允许 ptrace 非父子进程**，会报 `ptrace: 不允许的操作`。临时放开：

```bash
sudo sysctl -w kernel.yama.ptrace_scope=0
# 实验结束恢复
sudo sysctl -w kernel.yama.ptrace_scope=1
```

抓快照（**用 `$!` 拿后台 PID 最可靠**，避免抓到包装它的 bash）：

```bash
./dl &
PID=$!
sleep 2                      # 等死锁形成
gcore $PID                   # 生成 core.$PID，不杀进程
kill -9 $PID
gdb ./dl core.$PID
```

**踩坑识别**：如果 gcore 时栈顶是 `wait4()`，或 gdb 加载时提示 `Core was generated by '/usr/bin/bash'`，说明抓到了 shell 包装进程而不是 `./dl`——PID 找错了。

进入 gdb 后的标准 4 步分析：

**Step 1：全线程栈快照**

```
(gdb) info threads
(gdb) thread apply all bt
```

典型死锁现场会看到多个线程栈顶都是：

```
#0  futex_wait (... futex_word=0x... <X>) at futex-internal.h
#1  __GI___lll_lock_wait (futex=0x... <X>, ...)
#2  ___pthread_mutex_lock (mutex=0x... <X>)
#3  <你的代码> at deadlock.c:行号
```

`futex_word` / `mutex` 后面的符号名（如 `<A>` `<B>`）直接告诉你它们在等哪把锁。

**Step 2：找到每把锁的持有者**

`pthread_mutex_t.__data.__owner` 记录持有线程的 LWP（内核 tid），是定位死锁的关键字段：

```
(gdb) p A.__data.__owner       # → 例如 554830
(gdb) p B.__data.__owner       # → 例如 554831
```

`pthread_mutex_t.__data` 主要字段：

| 字段 | 含义 |
|------|------|
| `__lock` | futex 状态：0=空闲，1=有人持有，2=有人持有且有等待者 |
| `__owner` | 持有线程的 LWP（与 `info threads` 里的 LWP 对得上） |
| `__count` | 递归锁重入计数（普通锁恒为 0） |
| `__nusers` | 关联用户数 |
| `__kind` | 锁类型：0=NORMAL, 1=RECURSIVE, 2=ERRORCHECK |

`__lock == 2` 印证"被持有且有等待者"，与 `__lll_lock_wait` 栈顶对应。

**Step 3：把"持有 / 等待"关系列成表**

```
LWP 554830 (t1)  持有 A    等待 B   ← Step 2 的 __owner + Step 1 的栈顶
LWP 554831 (t2)  持有 B    等待 A
```

t1 等 t2 释放 B、t2 等 t1 释放 A → **等待环成立 = 死锁**。如果只有"等待"没有"环"，那是普通阻塞而非死锁。

**Step 4：理解 main 线程在做什么**

```
Thread 1: __pthread_clockjoin_ex (threadid=...)  at pthread_join_common.c
          main () at deadlock.c:29
```

main 卡在 `pthread_join` 等 t1 退出，但 t1 永远等不到 B → main 永远 join 不回来 → 进程对外表现为完全 hang 死。`expected=<LWP>` 字段直接指出 main 在等哪个线程。

**生产环境注意事项**：

- 线上服务**优先用 `gcore`** 而不是 `gdb -p`，避免长时间挂起进程被守护进程或 K8s 重启
- 看门狗（watchdog）系统上，gcore 也会暂停喂狗线程几百毫秒到几秒，需评估是否在 WDT 超时窗口内
- core 文件大小约等于进程 RSS，提前确认目标分区剩余空间
- core 含进程完整内存，可能泄露密钥/token，传出设备前压缩加密

**🧪 题 8：观察 ASLR**

要求：

- 写一个程序打印某个栈上局部变量的地址：`printf("%p\n", &x);`
- 连续运行 5 次，观察地址每次不同（ASLR 生效）
- 用 `setarch $(uname -m) -R ./a.out` 关掉 ASLR 再跑 5 次，地址固定
- 临时全局关闭：`echo 0 | sudo tee /proc/sys/kernel/randomize_va_space`（实验后记得改回 `2`）
