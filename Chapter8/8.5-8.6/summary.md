# §8.5–8.6 信号与非局部跳转

这两节讲的是异常控制流在**用户层**的两种机制：**信号**让内核把系统事件通知到进程（一种"软件中断"），**非局部跳转**让程序绕过正常的调用-返回链直接跳回外层。前者难在异步、会并发打断主程序、且不排队；后者难在栈帧失效与 `volatile`。

信号（§8.5）：

- 生命周期：发送（写 pending 位）→ 阻塞（blocked 位决定是否暂缓）→ 接收（内核强制进程响应）
- 信号不排队：同一种信号待处理时最多记一个，多来的会丢
- 处理程序异步打断主流程，只能调用异步信号安全函数，必须保存 errno、用 volatile 标志
- 后台子进程回收、竞态规避、显式等待，是信号在真实程序里最常见的三个落点（对应本目录 `handler/`、`sync/`、`waitsig/`、`shell/`）

非局部跳转（§8.6）：

- `setjmp` 把当前调用环境（寄存器、栈指针、PC）拍照存进 `jmp_buf`，`longjmp` 用快照恢复现场，让 `setjmp` "第二次返回"
- 一次 `longjmp` 能跨越任意多层栈帧，是 C 版的 `try/throw/catch`
- 危险源于栈帧：跳回的目标函数必须还"活着"，局部变量要 `volatile`

---

## 信号是什么、信号术语

**🎯 信号 = 内核发给进程的一条小消息**

一个信号就是一个小整数编号，通知进程"系统里发生了某类事件"。它是比异常更高层的抽象：异常由硬件/内核处理，信号则把事件暴露给用户进程。

| 信号 | 编号 | 默认行为 | 触发场景 |
|------|------|----------|----------|
| `SIGINT` | 2 | 终止 | 键盘 Ctrl-C |
| `SIGKILL` | 9 | 终止 | **不可捕获、不可忽略、不可阻塞** |
| `SIGSEGV` | 11 | 终止+core | 非法内存访问（段错误） |
| `SIGCHLD` | 17 | 忽略 | 子进程停止或终止 |
| `SIGSTOP`/`SIGTSTP` | 19/20 | 停止 | `SIGSTOP` 不可捕获；`SIGTSTP` 是 Ctrl-Z |
| `SIGALRM` | 14 | 终止 | `alarm` 定时器到期 |

**🎯 两个内核位向量：pending 和 blocked**

每个进程的上下文里，内核维护两个用信号编号做下标的位向量：

- **pending（待处理）**：信号已发出但还没被接收，对应位置 1
- **blocked（被阻塞 / 信号掩码）**：进程暂时不想接收的信号，对应位置 1

```
发送信号 k  →  内核把 pending 的第 k 位置 1
接收信号 k  →  内核把 pending 的第 k 位清 0，然后执行响应动作
```

**⚠️ pending 是位向量，不是计数器**

第 k 位只有 0/1 两态，所以一个信号"待处理"期间，再来同种信号**直接丢弃**，不累加。这是后面所有"循环回收"写法的根源。

---

## 发送信号

**🎯 发送的几种途径**

- **`/bin/kill` 程序**：`kill -9 1234` 发 SIGKILL 给进程 1234；`kill -9 -1234` 中 PID 为负表示发给**整个进程组** 1234
- **`kill` 函数**：`kill(pid, sig)`
  - `pid > 0`：发给进程 `pid`
  - `pid == 0`：发给调用进程所在进程组的每个进程
  - `pid < 0`：发给进程组 `|pid|` 的每个进程
- **键盘**：Ctrl-C 让内核发 `SIGINT` 给前台**进程组**所有进程；Ctrl-Z 发 `SIGTSTP`
- **`alarm(secs)`**：让内核在 `secs` 秒后发一个 `SIGALRM` 给自己

```c
/* 父进程给自己 fork 出的整个进程组发 SIGINT */
pid_t pid = fork();
if (pid == 0) {
    /* 子进程：默认和父进程同组 */
    while (1) {}            /* 一直跑，等着被信号干掉 */
}
kill(-getpgrp(), SIGINT);  /* 负号 = 发给整个进程组 */
```

**🎯 进程组**

每个进程属于唯一一个进程组（`getpgrp()` 取组 ID）。作业控制和"一次干掉一批进程"都依赖进程组——这也是 shell 要给前台作业 `setpgid` 单独分组的原因（避免 Ctrl-C 误伤 shell 自己）。

---

## 接收信号与处理程序

**🎯 接收发生在"从内核态返回用户态"的时刻**

内核每次要把控制权交还给进程 p（系统调用返回、上下文切换回来）时，都会检查
`pnb = pending & ~blocked`。若非零，就挑**编号最小**的那个信号强制 p 接收，触发对应动作。

**🎯 每个信号有默认行为，可被改写（除 SIGKILL/SIGSTOP）**

默认行为四选一：① 终止进程 ② 终止并 dump core ③ 停止直到收到 SIGCONT ④ 忽略。用 `signal` 安装处理程序覆盖默认行为：

```c
typedef void (*sighandler_t)(int);
sighandler_t signal(int signum, sighandler_t handler);
/* handler 可以是 SIG_IGN（忽略）、SIG_DFL（恢复默认）或自定义函数 */

void sigint_handler(int sig) {     /* 自定义：Ctrl-C 不再杀进程 */
    sio_puts("Caught SIGINT\n");
}
signal(SIGINT, sigint_handler);
```

**⚠️ 处理程序会被其它信号嵌套打断**

主程序被信号 A 打断进入 handler A，handler A 执行中又可能被信号 B 打断进入 handler B。所以处理程序本身也要可重入。

**🔧 用 sigaction 而非裸 signal**

`signal` 的语义在不同 Unix 上历史包袱很重（处理后是否自动恢复默认、是否重启被中断的系统调用都不一致）。可移植做法是用 `sigaction` 包一层，固定三条语义：处理本信号时阻塞同类信号、自动重启被中断的慢速系统调用（`SA_RESTART`）、handler 安装后不复位。本目录 `csapp.c` 里的 `try_signal` 就是这个包装：

```c
handler_t *try_signal(int signum, handler_t *handler) {
  struct sigaction action, old_action;
  action.sa_handler = handler;
  sigemptyset(&action.sa_mask);
  action.sa_flags = SA_RESTART;
  sigaction(signum, &action, &old_action);
  return old_action.sa_handler;
}
```

---

## 阻塞与解除阻塞信号

**🎯 两种阻塞**

- **隐式阻塞**：内核默认在执行 handler 期间阻塞"同类型"信号（防止自己打断自己）
- **显式阻塞**：用 `sigprocmask` 主动修改 blocked 位向量

```c
sigset_t mask, prev;
sigemptyset(&mask);
sigaddset(&mask, SIGCHLD);

sigprocmask(SIG_BLOCK,   &mask, &prev);  /* blocked |= mask（阻塞 SIGCHLD），旧掩码存 prev */
/* ... 临界区：此处不会被 SIGCHLD 打断 ... */
sigprocmask(SIG_SETMASK, &prev, NULL);   /* 把 blocked 恢复成 prev */
```

`sigprocmask` 的三种 `how`：`SIG_BLOCK`（加入阻塞集）、`SIG_UNBLOCK`（移出）、`SIG_SETMASK`（直接整体替换）。配套的集合操作：`sigemptyset` / `sigfillset` / `sigaddset` / `sigdelset`。

**⚠️ 阻塞不是丢弃**

被阻塞的信号仍会在 pending 里记一位，等解除阻塞那一刻立即投递。但因为不排队，阻塞期间来的多个同种信号合并成一个。

---

## 编写信号处理程序的安全规则

这是本节最容易翻车的地方。CSAPP 给出几条铁律：

**⚠️ G1：只调用异步信号安全函数**

handler 可能在主程序执行任意函数的"半中间"被插进来。如果 handler 调用了不可重入的函数（`printf`、`malloc` 用了全局缓冲区/锁），就会破坏其内部状态甚至死锁。`write` 是异步信号安全的，所以要用基于 `write` 的 `sio_*`：

```c
/* 不要在 handler 里 printf！用 sio_* */
ssize_t sio_puts(char s[]) { return write(STDOUT_FILENO, s, sio_strlen(s)); }
void    sio_error(char s[]) { sio_puts(s); _exit(1); }  /* _exit 而非 exit */
```

**⚠️ G2：进入时保存 errno，退出时恢复**

handler 里调用的系统调用（如 `waitpid`）会改写 `errno`，可能覆盖主程序正在用的 `errno`：

```c
void handler(int sig) {
    int olderrno = errno;       /* 保存 */
    while (waitpid(-1, NULL, WNOHANG) > 0) ;
    errno = olderrno;           /* 恢复 */
}
```

**⚠️ G3：访问全局数据时阻塞所有信号**

handler 和主程序共享的数据结构（如作业表），访问时要用 `sigprocmask(SIG_BLOCK, &mask_all, ...)` 临时阻塞所有信号，保证操作的原子性。

**⚠️ G4/G5：全局标志用 `volatile sig_atomic_t`**

`volatile` 阻止编译器把变量缓存进寄存器（否则主程序循环可能永远看不到 handler 的修改）；`sig_atomic_t` 保证读写是单条不可分割的指令。

```c
volatile sig_atomic_t pid;   /* handler 写，主程序读 */
```

**⚠️ 信号不排队 → handler 必须一次清干净**

因为 pending 不计数，N 个子进程同时退出可能只触发一次 `SIGCHLD`。所以回收必须用 `while` 循环 + `WNOHANG`，一次把所有僵尸全收掉：

```c
while ((pid = waitpid(-1, NULL, WNOHANG)) > 0)
    deletejob(pid);          /* 用 if 或不循环就会漏收，留下僵尸 */
```

**🔧 慢速系统调用被中断：EINTR**

`read`/`accept`/`sleep` 等慢速系统调用阻塞期间被信号打断时，可能返回 `-1` 并置 `errno = EINTR`。不带 `SA_RESTART` 时要手动重启：

```c
while ((n = read(fd, buf, len)) < 0)
    if (errno != EINTR) unix_error("read error");  /* EINTR 只是被打断，重试 */
```

---

## 同步流以避免并发 bug（§8.5.6）

**⚠️ 经典竞态：addjob 与 deletejob 谁先**

shell fork 子进程后要 `addjob`，子进程结束的 `SIGCHLD` handler 要 `deletejob`。若不同步，子进程可能在父进程 `addjob` **之前**就结束，handler 先 `deletejob` 一个还不存在的 job：

```c
/* 错误：fork 和 addjob 之间没有任何保护 */
if ((pid = fork()) == 0) { execve(...); }
addjob(pid);   /* 子进程若已结束，handler 早就 deletejob 过了 → 删空 */
```

**🎯 正确做法：fork 前阻塞 SIGCHLD，addjob 后再解除**

保证 `addjob` 一定先于对应的 `deletejob`（对应 `sync/procmask.c`）：

```c
sigprocmask(SIG_BLOCK, &mask_one, &prev);   /* fork 前阻塞 SIGCHLD */
if ((pid = fork()) == 0) {
    sigprocmask(SIG_SETMASK, &prev, NULL);  /* 子进程恢复掩码（别把阻塞带进 exec） */
    execve(...);
}
sigprocmask(SIG_BLOCK, &mask_all, NULL);    /* 注意 oldset 传 NULL，不要覆盖 prev */
addjob(pid);
sigprocmask(SIG_SETMASK, &prev, NULL);      /* addjob 完成后才解除，挂起的 SIGCHLD 此刻投递 */
```

---

## 显式等待信号（§8.5.7）

**🎯 父进程怎么"等一个信号到来"，四种写法层层递进**

| 写法 | 问题 |
|------|------|
| `while (!flag) ;` | 逻辑对，但空转烧 CPU |
| `while (!flag) pause();` | **致命竞态**：信号若在判断后、pause 前到达，pause 永久睡死 |
| `while (!flag) sleep(1);` | 能用，但响应慢或仍偏忙 |
| `while (!flag) sigsuspend(&mask);` | **正确**：原子地"解除阻塞 + 挂起" |

**🎯 sigsuspend 的原子性是关键**

`sigsuspend(&mask)` 等价于不可分割地执行下面三步，正因为不可分割，才关掉了 `pause` 版的竞态窗口（对应 `waitsig/waitforsignal.c`）：

```c
sigprocmask(SIG_SETMASK, &mask, &saved);  /* 临时换掩码（解除目标信号阻塞） */
pause();                                  /* 挂起等信号 */
sigprocmask(SIG_SETMASK, &saved, NULL);   /* 恢复原掩码 */
```

---

## setjmp 与 longjmp 的基本机制

**🎯 两个函数的分工**

```c
#include <setjmp.h>
int  setjmp(jmp_buf env);              /* 保存调用环境到 env */
void longjmp(jmp_buf env, int retval); /* 恢复 env，让对应的 setjmp 返回 retval */
```

- `setjmp` 把当前**调用环境**（栈指针、各保存寄存器、程序计数器）打包存进 `env`，**直接返回 0**
- `longjmp` 从 `env` 里恢复这套环境，效果是让那个 `setjmp` 调用**再返回一次**，这次返回 `retval`（若传 0 会被强制改成 1）

**🎯 一个 setjmp，两种返回**

这是理解非局部跳转的关键——同一行 `setjmp` 会"返回"两次，靠返回值区分是哪一次：

```c
jmp_buf env;

int main() {
    if (setjmp(env) == 0) {        /* 第一次：直接调用，返回 0 */
        printf("正常路径，开始干活\n");
        work();                    /* work 内部某处会 longjmp(env, 1) */
    } else {                       /* 第二次：被 longjmp 拽回来，返回非 0 */
        printf("从深层跳回来了\n");
    }
    return 0;
}

void work() {
    deep();                        /* 多层嵌套调用 */
}
void deep() {
    longjmp(env, 1);               /* 一步跳回 main 里的 setjmp，跳过 work 的返回 */
}
```

**🎯 longjmp 跨越多层栈帧**

`deep` 里的 `longjmp` 不会逐层 `return`，而是**直接把栈指针拨回 `setjmp` 时的位置**，`work`、`deep` 的栈帧瞬间作废。这正是它"非局部"的含义——跳出了正常的函数调用链。

---

## 非局部跳转的典型用途：深层错误的集中处理

**🔧 把多层嵌套里的错误一次性甩回顶层**

C 没有异常机制。要从深层调用里报告错误，传统做法是层层检查返回值、层层向上 `return`，繁琐且容易漏。`setjmp`/`longjmp` 让深层代码直接跳回顶层的统一错误处理点：

```c
jmp_buf env;

int main() {
    int rc = setjmp(env);
    if (rc == 0) {
        parse_config();            /* 深处任何一步出错都能直接跳回这里 */
        printf("配置加载成功\n");
    } else {
        printf("配置加载失败，错误码 %d\n", rc);  /* 集中处理 */
    }
}

void parse_token() {
    if (/* 出错 */) longjmp(env, ERR_BAD_TOKEN);  /* 跳过中间所有层 */
}
```

这本质上就是 `try { ... } catch (rc) { ... }` 的 C 版实现。`retval` 携带的就是"异常类型"。

---

## sigsetjmp 与 siglongjmp：信号处理程序里的非局部跳转

**⚠️ 普通 longjmp 不保存信号掩码**

如果想从**信号处理程序**里跳回主程序（比如收到 `SIGINT` 后放弃当前操作、回到主循环），必须用专门的 `sigsetjmp`/`siglongjmp`。原因是：进入 handler 时内核会阻塞当前信号，普通 `longjmp` 不恢复信号掩码，跳回去后该信号就一直被阻塞了。

```c
#include <setjmp.h>
int  sigsetjmp(sigjmp_buf env, int savesigs);  /* savesigs 非 0 时连信号掩码一起保存 */
void siglongjmp(sigjmp_buf env, int retval);
```

**🔧 用它实现"软重启"——Ctrl-C 不退出，而是回到主循环**

```c
sigjmp_buf buf;

void handler(int sig) {
    siglongjmp(buf, 1);            /* 从 handler 一步跳回主循环 */
}

int main() {
    signal(SIGINT, handler);
    if (sigsetjmp(buf, 1) != 0)    /* savesigs=1：同时保存/恢复信号掩码 */
        printf("重新开始\n");
    while (1) {
        /* 主循环：Ctrl-C 会被 handler 接住并跳回 sigsetjmp，而不是杀死进程 */
    }
}
```

很多交互式程序（如 shell、REPL）的"Ctrl-C 中断当前命令但不退出"就是这么实现的。

---

## 非局部跳转的危险与限制

**⚠️ 跳回的目标函数必须还在栈上**

`longjmp` 只能跳到一个**尚未返回**的函数里的 `setjmp`。如果包含 `setjmp` 的函数已经返回，它的栈帧早被回收，再 `longjmp` 过去就是访问失效内存——未定义行为。

```c
jmp_buf env;
void setup() { setjmp(env); }      /* 危险：setup 一返回，env 就失效了 */
int main() {
    setup();
    longjmp(env, 1);               /* UB：跳进一个已经销毁的栈帧 */
}
```

**⚠️ 局部变量要加 volatile**

`longjmp` 恢复的是 `setjmp` 时刻的寄存器快照。`setjmp` 之后被修改、且被编译器分配在寄存器里的局部变量，其修改在 `longjmp` 回来后可能"丢失"（恢复成旧值）。需要在跨越 `setjmp`/`longjmp` 后还能保留新值的局部变量，必须声明为 `volatile`：

```c
int main() {
    volatile int count = 0;        /* 不加 volatile，longjmp 回来后 count 可能被还原 */
    if (setjmp(env) != 0) count++;
    ...
}
```

**⚠️ handler 里只能用 siglongjmp**

从信号处理程序逃逸用 `siglongjmp`，普通 `longjmp` 会丢失信号掩码导致后续该信号被永久阻塞。

---

## 易错点

信号（§8.5）：

- 信号 pending 是位向量不是计数器，待处理期间多来的同种信号会丢，所以回收子进程必须 `while + WNOHANG` 一次收干净
- `SIGKILL`(9) 和 `SIGSTOP` 无法被捕获、忽略或阻塞，写 handler 拦不住它们
- handler 里绝不能用 `printf`/`malloc`（非异步信号安全），要用基于 `write` 的 `sio_*`
- handler 进入要存 `errno`、退出要还原，否则会污染主程序的 `errno`
- 全局标志不加 `volatile`，编译器可能把它缓存进寄存器，导致主程序循环永远看不到 handler 的修改
- 阻塞信号不等于丢弃，解除阻塞瞬间会投递，但合并后只剩一个
- 前台作业被 SIGCHLD handler 回收后，再裸 `waitpid` 会撞 `ECHILD`，要改用 `sigsuspend` 显式等待
- 子进程 fork 后若不恢复信号掩码，会把"阻塞 SIGCHLD"的状态泄漏给 `execve` 出的新程序

非局部跳转（§8.6）：

- `setjmp` 一行会"返回"两次：直接调用返回 0，被 `longjmp` 触发返回非 0，靠返回值区分两条路径
- `longjmp(env, 0)` 的 0 会被强制改成 1，因为 `setjmp` 用返回 0 表示"第一次直接返回"
- `longjmp` 只能跳进**尚未返回**的函数，跳进已销毁的栈帧是未定义行为
- 跨 `setjmp`/`longjmp` 还要保留新值的局部变量必须加 `volatile`，否则可能被恢复成旧值
- 从信号处理程序跳回主程序必须用 `sigsetjmp`/`siglongjmp`（且 `savesigs` 传非 0），普通 `longjmp` 会丢信号掩码
- `setjmp` 的返回值只能用于简单判断，不能存进变量再做复杂运算（标准对其使用上下文有限制）

---

## 工程关联

- shell / 守护进程回收后台子进程，靠的就是 `SIGCHLD` handler + `waitpid(WNOHANG)`，不回收就会堆积僵尸进程占满进程表（对应 `shell/`，可用 `ps` 看 `Z`/defunct 状态验证）
- `strace -f` 能看到 `kill`、`rt_sigaction`、`rt_sigprocmask`、`wait4` 这些信号相关系统调用，是观察 fork/exec/signal 交互的利器
- `kill -l` 列出系统所有信号编号；`/proc/<pid>/status` 里的 `SigPnd`/`SigBlk`/`SigCgt` 字段就是 pending/blocked/caught 位向量的十六进制快照
- 网络服务里 `accept`/`read` 被信号打断返回 `EINTR` 是经典坑，要么用 `SA_RESTART`，要么循环重试
- 优雅退出（收到 `SIGTERM` 时先清理再退出）、配置热重载（`SIGHUP`）都是 handler + volatile 标志的典型应用
- C++ 异常（`throw`/`catch`）、Go 的 `panic`/`recover` 在底层都是同一类"非局部控制转移"思想的高级封装，理解了 `setjmp`/`longjmp` 就理解了异常的本质：拨回栈指针、跳过中间栈帧
- 协程 / 用户态线程的早期实现（如 `ucontext`、某些 setjmp-based coroutine 库）就靠保存/恢复调用环境来切换执行流
- 交互式程序（shell、Python REPL、数据库客户端）的"Ctrl-C 中断当前操作回到提示符而不退出"普遍用 `sigsetjmp`/`siglongjmp` 实现
- 调试时若看到栈回溯突然"断层"、跳过了若干本应存在的帧，往往就是发生过 `longjmp`

---

## 实验题

**🧪 题 1：观察信号的发送与默认行为**

```c
/* loop.c：一个死循环程序 */
int main() { while (1) {} }
```

要求：

- 编译运行，另开终端用 `kill -SIGINT <pid>` 和 `kill -SIGKILL <pid>` 分别终止它
- 给程序装一个 `SIGINT` handler（打印一句话后不退出），再发 `SIGINT`，确认 Ctrl-C 杀不死它
- 验证：装了 handler 后 `SIGKILL` 仍然能秒杀，解释为什么 `SIGKILL` 拦不住

**🧪 题 2：信号不排队导致的丢失（对应 `handler/`）**

```c
/* 父进程一次 fork 出 5 个子进程，子进程立即 exit；
   handler 故意只 waitpid 一次（不循环），观察漏收 */
void handler(int sig) {
    waitpid(-1, NULL, 0);          /* 故意不用 while 循环 */
}
```

要求：

- 把回收改成 `if (waitpid 一次)` 和 `while (waitpid ... WNOHANG)` 两个版本
- 用 `ps` 或在父进程末尾打印，确认非循环版本会残留僵尸
- 解释为什么 5 个子进程可能只触发不到 5 次 `SIGCHLD`

**🧪 题 3：竞态与同步对比（对应 `sync/procmask.c`）**

```c
/* 子进程立即退出以放大竞态窗口 */
if ((pid = try_fork()) == 0) { try_execve("/bin/true", argv, NULL); }
addjob(pid);
```

要求：

- 写一个**不阻塞 SIGCHLD** 的版本，多跑几次，观察是否出现 "deletejob: pid not found"
- 改成 fork 前阻塞 SIGCHLD、addjob 后解除的版本，确认竞态消失
- 故意把父进程那行 `sigprocmask(SIG_BLOCK, &mask_all, ...)` 的 oldset 写成 `&prev`（而非 `NULL`），观察程序为何死循环

**🧪 题 4：sigsuspend 对比 pause（对应 `waitsig/waitforsignal.c`）**

```c
pid = 0;
while (!pid)
    pause();          /* 有竞态的版本 */
```

要求：

- 在 `while (!pid)` 和 `pause()` 之间插入一句耗时操作（放大窗口），观察程序偶尔卡死
- 换成 `while (!pid) sigsuspend(&prev);`，确认不再卡死
- 用 `strace` 对比两个版本陷入的系统调用（`pause` vs `rt_sigsuspend`）

**🧪 题 5：shell 僵尸回收验证（对应 `shell/`）**

```
# 喂给 shell 的命令序列
/bin/sleep 0.3 &
/bin/sleep 5
quit
```

要求：

- 分别用 §8.4 旧版和 §8.5 优化版 shell 跑上面的序列
- 在后台 `sleep 0.3` 结束、前台 `sleep 5` 还在跑时，用 `ps -o pid,stat,comm --ppid <shell_pid>` 查子进程
- 确认旧版后台作业变成 `Z`（defunct），新版被 handler 回收、无僵尸

**🧪 题 6：观察 setjmp 的两次返回**

```c
#include <setjmp.h>
#include <stdio.h>
jmp_buf env;

void deep(void) {
    printf("进入 deep，准备跳回\n");
    longjmp(env, 42);
    printf("这行永远不会执行\n");
}

int main() {
    int rc = setjmp(env);
    printf("setjmp 返回 %d\n", rc);
    if (rc == 0)
        deep();
    else
        printf("被 longjmp 拽回，错误码 %d\n", rc);
    return 0;
}
```

要求：

- 编译运行，确认 "setjmp 返回" 这句打印了两次（一次 0，一次 42）
- 确认 `longjmp` 后面那行 printf 从不执行
- 把 `longjmp(env, 42)` 改成 `longjmp(env, 0)`，观察第二次返回值变成 1，解释原因

**🧪 题 7：验证 volatile 的必要性**

```c
#include <setjmp.h>
#include <stdio.h>
jmp_buf env;

int main() {
    int count = 0;          /* 故意不加 volatile */
    if (setjmp(env) == 0) {
        count = 100;        /* setjmp 之后修改 */
        longjmp(env, 1);
    }
    printf("count = %d\n", count);
    return 0;
}
```

要求：

- 分别用 `-O0` 和 `-O2` 编译运行，观察 `count` 的打印值是否不同
- 给 `count` 加 `volatile`，确认两种优化级别下结果一致
- 用 `gcc -O2 -S` 看汇编，解释为什么不加 `volatile` 时 `count` 的修改会"丢失"

**🧪 题 8：用 siglongjmp 实现 Ctrl-C 软重启**

```c
#include <setjmp.h>
#include <signal.h>
#include <stdio.h>
#include <unistd.h>
sigjmp_buf buf;

void handler(int sig) { siglongjmp(buf, 1); }

int main() {
    signal(SIGINT, handler);
    if (sigsetjmp(buf, 1) != 0)
        printf("\n[已中断，回到主循环]\n");
    while (1) {
        printf("工作中... (按 Ctrl-C 中断但不退出)\n");
        sleep(1);
    }
}
```

要求：

- 运行后多次按 Ctrl-C，确认程序回到主循环而不是退出
- 把 `sigsetjmp(buf, 1)` 的第二个参数改成 0，连续按 Ctrl-C，观察第二次起 SIGINT 是否还能被接住（掩码未恢复，SIGINT 被永久阻塞）
- 把 `siglongjmp` 换成普通 `longjmp`、`sigsetjmp` 换成 `setjmp`，对比信号掩码行为的差异
