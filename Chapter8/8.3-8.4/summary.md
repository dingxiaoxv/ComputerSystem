# §8.3-8.4 系统调用错误处理与进程控制

§8.2 把「进程」这个抽象讲清楚了——但那是内核视角的概念。本节下沉到**程序员视角**：C 程序怎么用系统调用去**创建、回收、替换**进程？答案就是 Unix 进程控制的那几个核心系统调用：`fork`（创建）、`exit`（终止）、`waitpid`（回收）、`execve`（替换程序映像）。再加上 §8.3 的「每个系统调用都可能失败，必须检查返回值」这条铁律，就构成了写任何 Unix 程序的地基。理解了 `fork` 的「一次调用、两次返回」和 `fork + execve` 的「先复制再替换」模型，就理解了 shell 是怎么跑起每一条命令的、`bash` 敲一个 `ls` 背后发生了什么、以及为什么会出现僵尸进程。

---

## 系统调用错误处理（§8.3）

🎯 **铁律**：Unix 系统级函数遇到错误时，通常返回 **-1**，并设置全局变量 `errno` 指明具体原因。**每一次系统调用都必须检查返回值**——这不是可选项，是写健壮 Unix 程序的前提。

```c
if ((pid = fork()) < 0) {
    fprintf(stderr, "fork error: %s\n", strerror(errno));
    exit(0);
}
```

🎯 **`errno` + `strerror`**：`errno` 是个整数错误码，`strerror(errno)` 把它翻成人类可读的字符串（如 `"No child processes"`）。注意 `errno` **只在函数失败时才有意义**，成功时它的值是未定义的——不要在没失败时去读它。

🔧 **错误处理包装函数（error-handling wrapper）**：CSAPP 用两类工具把样板代码收敛掉，本目录的 `csapp.c` 就是这套：

- **错误报告函数** `unix_error`：打印 `errno` 对应的消息并退出。
  ```c
  void unix_error(char *msg) {
      fprintf(stderr, "%s: %s\n", msg, strerror(errno));
      exit(0);
  }
  ```
- **错误处理包装函数** `Fork`（本目录里叫 `try_fork`）：调用真实函数，失败就报错退出，成功才返回。约定**首字母大写**表示「带检查的版本」。
  ```c
  pid_t try_fork(void) {
      pid_t pid;
      if ((pid = fork()) < 0)
          unix_error("Fork error");
      return pid;
  }
  ```

⚠️ 包装函数让正文代码干净（`pid = Fork();` 一行搞定），但**代价是任何错误都直接 `exit`**。这在教学和小工具里没问题，真实服务程序里往往需要更细的错误恢复（重试、降级、记录日志后继续），不能无脑套用。

---

## 获取进程 ID（§8.4.1）

🎯 **两个最基本的查询**：

| 函数 | 返回 |
|------|------|
| `pid_t getpid(void)` | 调用进程自己的 PID |
| `pid_t getppid(void)` | 调用进程**父进程**的 PID |

`pid_t` 在 Linux 上定义为 `int`。每个进程有唯一的正整数 PID。这两个调用**永远成功**，不需要检查错误。

🔧 `getppid` 在排查「孤儿进程被谁收养」时很有用——父进程先死，子进程的 `getppid()` 会变成 1（或现代 systemd 下的某个 subreaper），表示已被 init 收养。

---

## 创建与终止进程（§8.4.2）

🎯 **进程的四种状态**：
- **运行（Running）**：正在 CPU 上执行，或在等待被调度（就绪）。
- **停止（Stopped）**：被信号（`SIGSTOP`/`SIGTSTP`/`SIGTTIN`/`SIGTTOU`）挂起，直到收到 `SIGCONT` 才恢复。
- **终止（Terminated）**：永久停止。三种原因：① 收到默认行为是终止的信号；② 从 `main` 返回；③ 调用 `exit`。
- **僵尸（Zombie）**：已终止但还没被父进程回收——见下文 §8.4.3。

🎯 **`exit` 与退出状态**：
```c
void exit(int status);   // status 的低 8 位是退出状态，0 表示成功
```
`exit` 不返回。从 `main` 里 `return n` 等价于 `exit(n)`。

🎯 **`fork`——本节最重要、也最反直觉的调用**：
```c
pid_t fork(void);
```
`fork` 创建一个新的**子进程**，它是父进程的**几乎完全的副本**：

- 子进程得到与父进程**相同但独立**的用户级虚拟地址空间副本（代码、数据、堆、栈）。
- 子进程得到父进程**打开文件描述符的副本**——所以子进程能读写父进程打开的文件。
- 子进程与父进程的 PID 不同。

⚠️ **「一次调用，两次返回」**——这是 `fork` 最容易绕晕的地方：
- 在**父进程**中，`fork` 返回**子进程的 PID**（一个正数）。
- 在**子进程**中，`fork` 返回 **0**。
- 据此区分自己身处哪个进程：`if (fork() == 0) { /* 子进程 */ }`。

⚠️ **并发执行，顺序不确定**：`fork` 之后父子两个进程**并发运行**，谁先跑、跑多少完全由内核调度决定。不要假设任何执行顺序——`fork.c` 里父子都打印 `x`，输出 `parent` 和 `child` 谁先出现是不确定的。

⚠️ **写时复制语义下的「独立副本」**：父子共享同一份变量初值，但**此后各改各的，互不影响**。`fork.c` 里 `x` 初值为 1：
```c
int x = 1;
pid = try_fork();
if (pid == 0) printf("child: x = %d\n", ++x);   // 子进程：x → 2
printf("parent: x = %d\n", --x);                // 父进程：x → 0
```
子进程打印 `2`，父进程打印 `0`——同一个变量名，两份独立内存。物理上内核用**写时复制（copy-on-write）**优化：`fork` 时不真的拷贝整个地址空间，而是父子共享同一批物理页并标记只读，**谁先写哪一页才真正复制那一页**。所以 `fork` 很轻，`fork + execve` 的「先复制再替换」也不浪费——复制出来的页大多还没改就被 `execve` 丢弃了。

🔧 **进程图（process graph）**：分析 `fork` 程序输出时，画一张有向图——每个 `fork` 是一个分叉点，节点是「语句执行」，边是「先后顺序」。任何不违反图中偏序（拓扑序）的输出顺序都是可能的。这是 CSAPP 推荐的、判断「N 个 fork 会打印几次、可能有哪些顺序」的标准方法。

---

## 回收子进程与僵尸进程（§8.4.3）

🎯 **为什么需要回收**：进程终止后**不会立即从系统消失**，而是保持「已终止」状态，直到被父进程**回收（reap）**。这种已终止待回收的进程叫**僵尸进程（zombie）**。内核保留它的退出状态等信息，等父进程来取。

🎯 **谁来回收**：
- 父进程用 `wait`/`waitpid` 回收子进程，回收后内核才彻底删除该进程（释放 PID 等）。
- 如果**父进程先于子进程终止**，这些子进程（含僵尸）由 **init/systemd（PID 1）收养并回收**。所以长期运行的进程（shell、服务器）必须主动回收子进程，否则僵尸堆积、PID 耗尽。

🎯 **`waitpid`——回收的主力**：
```c
pid_t waitpid(pid_t pid, int *statusp, int options);
```
默认行为（`options = 0`）：**挂起调用进程**，直到 `pid` 指定的子进程**终止**；返回被回收子进程的 PID，并把退出状态写入 `*statusp`。

🎯 **`pid` 参数选谁回收**：
- `pid > 0`：只等指定 PID 的那一个子进程。
- `pid == -1`：等**任意一个**子进程（这就是 `wait(&status)` 的等价写法，`wait(s)` ≡ `waitpid(-1, s, 0)`）。

🎯 **从 `status` 解读子进程结局**——用一组宏（不要手动位运算）：

| 宏 | 含义 |
|------|------|
| `WIFEXITED(status)` | 子进程是否正常终止（`exit`/`return`） |
| `WEXITSTATUS(status)` | 正常终止时的退出状态（仅在上一条为真时有效） |
| `WIFSIGNALED(status)` | 是否因**未捕获的信号**而终止 |
| `WTERMSIG(status)` | 导致终止的信号编号 |
| `WIFSTOPPED(status)` | 子进程当前是否被停止 |
| `WSTOPSIG(status)` | 导致停止的信号编号 |

🎯 **回收顺序不确定**：用 `waitpid(-1, ...)` 回收多个子进程时，**回收顺序由子进程终止的先后决定，无法预测**。`wait.c` 的 `no_order_wait` 就演示了这点：fork 出 10 个子进程，`waitpid(-1, ...)` 循环回收，打印出来的 PID 顺序每次运行都可能不同。若要**按 fork 顺序**回收，得自己存下每个 PID 再 `waitpid(pid[i], ...)` 逐个等（`order_wait`）。

🎯 **循环回收的终止条件**：`while ((pid = waitpid(-1, &status, 0)) > 0) { ... }`——当没有子进程可回收时，`waitpid` 返回 -1 且 `errno == ECHILD`，循环正常退出。所以回收循环后应检查 `if (errno != ECHILD) unix_error(...)` 来区分「正常收完」和「真出错」。

⚠️ **僵尸 ≠ 占内存**：僵尸进程几乎不占资源（用户级内存早已释放），它占的是**进程表项 + PID**。危害是 PID 是有限资源，僵尸长期不回收会耗尽 PID 让系统无法创建新进程。`ps` 里状态为 `Z`、命令带 `<defunct>` 的就是僵尸。

---

## 让进程休眠（§8.4.4）

🎯 **`sleep`**：
```c
unsigned int sleep(unsigned int secs);
```
挂起调用进程 `secs` 秒。正常睡满返回 0；若被信号提前打断，返回**剩余未睡的秒数**。

🎯 **`pause`**：
```c
int pause(void);
```
挂起调用进程，**直到收到一个信号**。常用于「啥也不干，等信号来」的场景（配合 §8.5 信号处理）。

🔧 这两个调用都会让进程进入「睡眠/阻塞」态，内核趁机**上下文切换**到别的进程——这正是 §8.2 讲的「阻塞型系统调用触发上下文切换」的具体例子，CPU 不会空转等时间。

---

## 加载并运行程序：execve（§8.4.5）

🎯 **`execve`——把当前进程「整个换成」另一个程序**：
```c
int execve(const char *filename, const char *argv[], const char *envp[]);
```
- `filename`：可执行目标文件（或脚本）的路径。
- `argv`：参数列表，`argv[0]` 约定是程序名，列表以 `NULL` 结尾。
- `envp`：环境变量列表，形如 `"NAME=VALUE"`，同样以 `NULL` 结尾。

⚠️ **「调用一次，从不返回」**——这是 `execve` 与 `fork` 的镜像对照：
- `fork` 调用一次返回两次；`execve` 调用一次**正常情况下永不返回**。
- 因为 `execve` 用新程序**覆盖了当前进程的地址空间**（代码、数据、栈、堆全部替换），原来的代码已经不存在了，自然「无处可返回」。
- **只有出错时 `execve` 才返回 -1**（比如文件不存在、没有执行权限）。所以 `execve` 后面紧跟的代码**只有失败时才会执行**：
  ```c
  if (execve(argv[0], argv, environ) < 0) {
      printf("%s: command not found.\n", argv[0]);
      exit(0);
  }
  ```

🎯 **`execve` 保留了什么**：地址空间被换掉，但进程的「身份」延续——**PID 不变**、打开的文件描述符默认保留（除非设了 close-on-exec）、信号处理设置部分重置。这正是「`fork` 后子进程能继承父进程重定向好的文件描述符，再 `execve` 执行新程序」这一 I/O 重定向机制的基础（§10.9）。

🔧 `environ` 是个全局变量 `extern char **environ`，指向当前进程的环境变量数组。`getenv`/`setenv` 操作的就是它。shell 把它原样传给 `execve` 的 `envp`，子程序就继承了 shell 的环境。

---

## fork + execve：进程的执行模型（§8.4.6）

🎯 **核心组合拳**：Unix 里「运行一个新程序」标准动作是 **fork + execve 两步**：

1. **`fork`** 复制出一个子进程（得到一条新的逻辑控制流）。
2. 子进程里调用 **`execve`** 把自己替换成目标程序。
3. 父进程根据需要 `waitpid` 回收子进程（前台），或直接返回继续（后台）。

🔧 **这就是 shell 的工作原理**——本目录 `shell/main.c` 是一个最小可运行的 shell，把这套模型完整演示了一遍：

```c
void eval(char *cmdline) {
    bg = parseline(buf, argv);           // 解析命令行，判断是否后台(&)
    if (builtin_command(argv)) return;   // 内置命令在 shell 进程内直接执行
    if ((pid = try_fork()) == 0) {       // 子进程
        if (execve(argv[0], argv, environ) < 0) {  // 替换成目标程序
            printf("%s: command not found.\n", argv[0]);
            exit(0);
        }
    }
    if (!bg) {                           // 前台作业：父进程等待回收
        waitpid(pid, &status, 0);
    } else {                             // 后台作业：打印 pid 后直接返回
        printf("%d %s", pid, cmdline);
    }
}
```

🎯 **内置命令 vs 外部程序——判据是「是否要改 shell 自身状态」**：
- **需要改 shell 自己** → **必须内置**。因为 `fork` 出去改的是子进程，对父 shell 无效。
  - `cd`：要 `chdir` 改 shell 自己的工作目录（系统里根本没有 `/bin/cd` 这个程序）。
  - `exit`/`quit`：要终止 shell 自身。
  - `export`：改 shell 自己的环境变量。
  - `jobs`/`fg`/`bg`：读写只存在于 shell 进程内存里的「作业表」。
- **不需要改 shell 自己** → 交给 `fork + execve` 当外部程序跑（`ls`、`gcc`、`cat`……）。

⚠️ **前台 vs 后台**：命令末尾有 `&` 是后台作业，shell **不等待**它，打印 PID 后立刻返回接收下一条命令；否则是前台作业，shell `waitpid` 阻塞等它结束。本例的简化 shell **没有回收后台作业**——这正是它相比真实 shell（及 tsh lab）缺的一块：真实 shell 要在 `SIGCHLD` 信号处理程序里异步回收后台子进程，否则后台作业全变僵尸（这是 §8.5 信号的内容）。

---

## 易错点

- `fork` 是「一次调用两次返回」：父进程拿到子进程 PID（正数），子进程拿到 0；判断 `fork()==0` 才知道自己是子进程。
- `fork` 后父子变量是**独立副本**，改一个不影响另一个——别以为 `fork` 后还共享变量；它们只是初值相同。
- `fork` 后父子执行顺序**完全不确定**，任何依赖「父先跑」或「子先跑」的代码都是错的，必须用信号/管道等显式同步。
- `execve` 与 `fork` 相反，是「调用一次永不返回」——它后面的代码只有在 `execve` **失败**时才执行，别把正常逻辑写在 `execve` 后面。
- 僵尸进程不是「卡死的进程」，是「已终止但没被父进程回收」的进程；它占的是 PID/进程表项而非内存，长期不回收会耗尽 PID。
- `waitpid(-1,...)` 的回收顺序由子进程**终止先后**决定，不等于 fork 顺序；要按序回收必须自己存 PID 数组逐个等。
- 回收循环靠 `waitpid` 返回 -1 且 `errno==ECHILD` 结束，别把这个「正常收完」当成错误。
- `cd` 这类命令必须内置，不能 `fork+execve`——子进程 `chdir` 改的是子进程的工作目录，shell 自己的目录没变。
- 系统调用返回 -1 才看 `errno`；成功时 `errno` 的值是未定义的，不要去读。

---

## 工程关联

- **`strace -f ./shell` 观察 fork/execve/wait 三件套**：跑一条命令能看到 `clone`(fork 的底层实现) → `execve` → `wait4`(waitpid 底层) 的完整调用链，`-f` 跟踪子进程。这是排查「程序到底启动了什么子进程、传了什么参数」的利器。
- **写时复制的可见性**：`fork` 一个占大内存的进程几乎瞬间完成，正是因为 COW 没有真复制物理页；`perf stat -e minor-faults` 能看到子进程首次写共享页时触发的缺页（minor fault），那才是真正发生物理复制的时刻。
- **僵尸进程排查**：`ps aux | grep defunct` 或 `ps -el` 看状态 `Z` 的进程；僵尸的父进程没正确 `wait` 是常见服务端 bug，修复方式通常是装 `SIGCHLD` 处理程序或把子进程二次 fork 后让 init 收养。
- **PID 不变是重定向的基础**：shell 的 I/O 重定向（`ls > out.txt`）靠「fork 后、execve 前，在子进程里 `dup2` 改文件描述符」实现——execve 不动文件描述符表，所以新程序继承了重定向（§10.9）。
- **`fork` 与缓冲区**：`printf` 带缓冲，若 `fork` 前有未刷新的 stdout 缓冲，父子各持一份副本，可能导致输出重复——这是 `fork` 程序里 `printf` 看似「多打印」的经典坑，`fflush` 或改用无缓冲 `write` 可避免。
- **容器/进程管理**：`runc`、`systemd` 等本质都是「fork + 设置命名空间/cgroup + execve」的放大版；理解最小 shell 的 fork+exec 模型，是理解容器启动流程的起点。

---

## 实验题

**🧪 题 1：验证 fork 的「独立副本」与不确定顺序**

基于本目录 `fork/fork.c`：

```c
int x = 1;
pid = try_fork();
if (pid == 0) { printf("child: x = %d\n", ++x); exit(0); }
printf("parent: x = %d\n", --x);
exit(0);
```

要求：
- 编译运行，确认子进程打印 `x = 2`、父进程打印 `x = 0`，解释为什么同名变量 `x` 出现两个值。
- 多跑几十次（`for i in {1..50}; do ./fork; done`），观察 `parent` 与 `child` 两行的**先后顺序是否固定**，记录是否出现两种顺序。
- 把变量扩展成嵌套两层 fork（`fork(); fork();`），先画进程图预测会打印几行，再用 `getpid`/`getppid` 标注每行来自哪个进程，验证预测。

**🧪 题 2：观察僵尸进程的产生与消失**

```c
#include "csapp.h"
int main(void) {
    if (try_fork() == 0) { exit(0); }   // 子进程立即退出
    while (1) ;                         // 父进程死循环、故意不回收
}
```

要求：
- 编译运行，另开终端 `ps -el | grep defunct` 或 `ps aux | grep Z`，找到状态为 `Z`、命令带 `<defunct>` 的僵尸子进程。
- 用 `kill` 杀掉父进程，再 `ps` 观察僵尸是否消失——解释「父死后子进程（僵尸）被 init/systemd 收养并回收」。
- 在父进程死循环前加一行 `waitpid(-1, NULL, 0);`，重新运行，确认僵尸不再出现。

**🧪 题 3：waitpid 的回收顺序**

基于本目录 `wait/wait.c` 的两个函数：

要求：
- 跑 `no_order_wait`（`waitpid(-1, ...)`），多次运行，记录回收 PID 的顺序，确认每次可能不同。
- 跑 `order_wait`（自己存 `pid[]` 后逐个 `waitpid(pid[i], ...)`），确认回收顺序固定为 fork 顺序。
- 注意 `wait.c` 里 `order_wait` 的 `printf("...%d...", pid, ...)` 误把数组名 `pid` 当成单个值传了——修正为 `pid[i-1]`（或 `retpid`），重新验证输出的 PID 与退出状态对应正确。
- 用 `WEXITSTATUS(status)` 验证每个子进程的退出码确实是 `100 + i`。

**🧪 题 4：execve「永不返回」与最小 shell**

基于本目录 `shell/main.c`：

要求：
- 编译运行这个 shell，用**绝对路径**执行命令（如 `/bin/ls -l`、`/bin/echo hi`），观察前台命令会阻塞等待、加 `&` 的后台命令立即返回并打印 PID。
- 故意敲一个不存在的程序（`/bin/nonexist`），确认走进了 `execve` 返回 -1 的分支、打印 "command not found"——验证「execve 后面的代码只在失败时执行」。
- 用 `strace -f ./tsh` 跟踪，敲一条 `/bin/ls`，在输出里找到 `clone`/`execve`/`wait4` 三个系统调用，对应 fork→exec→reap 三步。
- 思考题：在这个 shell 里敲 `cd /tmp` 为什么无效？（提示：`cd` 不是内置命令，被 fork+execve 当外部程序找，而系统里没有 `/bin/cd`）尝试把 `cd` 加为内置命令（调用 `chdir`）。

**🧪 题 5：fork 与 stdout 缓冲的坑**

```c
#include <stdio.h>
#include <unistd.h>
int main(void) {
    printf("hello");   // 注意：没有 '\n'，行缓冲下不会立即刷新
    fork();
    return 0;          // 退出时刷新缓冲，父子各刷一次
}
```

要求：
- 直接运行 `./a.out`（输出到终端），再 `./a.out | cat`（输出重定向到管道），对比 `hello` 被打印了几次。
- 解释：终端是行缓冲、管道是全缓冲；重定向后 `printf("hello")` 留在缓冲区里被 `fork` 复制成两份，父子退出各刷一次 → 打印两次。
- 在 `fork()` 前加 `fflush(stdout)`（或给字符串加 `\n`），确认重定向后只打印一次——理解「fork 复制的是包括 stdio 缓冲区在内的整个用户地址空间」。
