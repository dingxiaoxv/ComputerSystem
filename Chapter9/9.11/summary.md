# §9.11 常见的与内存有关的错误

C 把内存管理的全部责任交给程序员，于是就有了一整类**编译器不拦、运行时不报、但会在几小时后的另一个函数里崩溃**的 bug。这一节要建立的核心认知是：内存错误最危险的地方**不是它会让程序崩溃，而是它经常不崩**——错误现场和崩溃现场往往相隔十万八千里。本节所有结论均来自本机实测，代码见 [experiments/mem_errors.c](experiments/mem_errors.c)。

## 先看一张表：内存错误有多不可靠

**⚠️ 十类错误里有四类「跑完还返回 0」**

同一份代码用 `gcc -O0 -g` 编译（无任何 sanitizer），逐个触发，实测退出码：

| 错误类型 | 普通编译下的现象 | 退出码 |
|----------|-----------------|--------|
| 读未初始化内存 | 打印出垃圾值，一切"正常" | **0** |
| off-by-one 越界写堆 | 完全无感 | **0** |
| 误解指针运算（界内错位） | 读到错误的值，程序继续跑 | **0** |
| 内存泄漏 | 完全无感 | **0** |
| 引用指针而非对象 | 逻辑跑飞（死循环） | **0** |
| `sizeof` 算错 → 堆越界写 | glibc 堆元数据断言失败 | 134 (SIGABRT) |
| use-after-free + double free | `free(): double free detected in tcache 2` | 134 |
| 栈缓冲区溢出 | `*** stack smashing detected ***` | 134 |
| 返回栈地址（悬空） | 段错误 | 139 (SIGSEGV) |
| 间接引用坏指针 | 段错误 | 139 |

**🎯 会崩的那几个，崩溃点也不在错误点**

`sizeof` 算错那个例子最典型：真正的错误在**第 59 行的越界写**，但程序是在**第 61 行的 `free()`** 里崩的——

```
Fatal glibc error: malloc.c:2599 (sysmalloc): assertion failed:
  (old_top == initial_top (av) && old_size == 0) || ...
```

因为越界写破坏了堆块头部的元数据，glibc 直到下一次操作堆时才发现不对。**调用栈指向的是受害者，不是凶手**——这是内存错误排查最费时间的地方，也是必须用工具而不是用眼睛找的根本原因。

## 错误 1：间接引用坏指针

**🎯 经典形态：`scanf` 忘了 `&`**

```c
int val = 0;
scanf("%d", val);       /* 错：把 val 的值当作地址 */
scanf("%d", &val);      /* 对 */
```

`val` 的值是 0，于是 `scanf` 往地址 0 写 4 字节。实测：

```
普通运行  : 段错误（退出码 139）
valgrind : Invalid write of size 4
           Address 0x0 is not stack'd, malloc'd or (recently) free'd
```

**⚠️ 只有写到未映射区域才会立刻崩**

如果 `val` 恰好是个已映射的合法地址（比如从未初始化的栈变量里读到一个像样的值），这次写入会**静默成功**，破坏掉别人的数据。地址 0 崩得快，反而是运气好。

**🔧 现代编译器能拦一部分**

GCC 的 `-Wformat` 会对 `scanf("%d", val)` 直接报错（类型不匹配），所以这类错误在开启 `-Wall` 后基本绝迹。真正难缠的是从函数返回的、经过多层传递的坏指针。

## 错误 2：读未初始化的内存

**🎯 `malloc` 不清零，`calloc` 才清零**

```c
int *y = malloc(n * sizeof(int));
long sum = 0;
for (int i = 0; i < n; i++)
    sum += y[i];        /* 读的是上一次 free 留下的垃圾 */
```

实测两次运行结果完全不同：

```
普通编译       : sum = 0
ASan 编译      : sum = -8758364688
```

值取决于分配器复用了哪块内存、上面残留什么。**「有时候是 0」比「总是垃圾」更危险**——测试时碰巧全 0，上线后就不是了。

**⚠️ 这是 ASan 唯一抓不到的一类**

实测 ASan 对这个用例**完全静默**，因为它跟踪的是「地址是否可访问」，而这块地址完全合法。要抓未初始化读得用 Valgrind 或 MSan：

```
$ valgrind --track-origins=yes ./mem_errors uninit
Conditional jump or move depends on uninitialised value(s)
   at ... printf ...
   by 0x10931E: err_uninit (mem_errors.c:36)
Uninitialised value was created by a heap allocation
   at 0x4846828: malloc
   by 0x1092C6: err_uninit (mem_errors.c:32)     ← 直接指出是哪次 malloc
```

`--track-origins=yes` 能一路追溯到**产生这个未初始化值的分配点**，这是排查此类问题最有效的手段。

**🔧 GCC 的静态警告也能拦一部分**：本例中 `-Wall -Wextra` 报了 `-Wmaybe-uninitialized`。但它只在编译器能看清数据流时有效，跨函数、跨编译单元就无能为力。

## 错误 3：栈缓冲区溢出

**🎯 `scanf("%s")` 不知道缓冲区多大**

```c
char buf[8];
scanf("%s", buf);       /* 错：读入长度不受限 */
scanf("%7s", buf);      /* 对：留一个字节给 '\0' */
```

实测输入 40 个 `A`：

```
普通编译  : *** stack smashing detected ***: terminated   （退出码 134）
ASan     : ERROR: AddressSanitizer: stack-buffer-overflow
           #0 ... in err_stack_overflow mem_errors.c:43
Valgrind : 抓不到，只有 canary 兜住
```

**⚠️ Valgrind 检测不到栈缓冲区溢出**

这是重要的工具边界：Valgrind 的 memcheck 主要跟踪**堆**分配的红区，对栈帧内部的越界基本无能为力。栈上的越界要靠 ASan（它给每个栈变量也插了红区）或编译器的 canary。

**🎯 canary 是 §3.10 那套栈保护的直接体现**

`*** stack smashing detected ***` 来自 `__stack_chk_fail`，是 GCC 的 `-fstack-protector-strong`（Ubuntu 默认开启）插入的检查。**它只在函数返回时检查**，所以只能发现「溢出到 canary 之外」的情况——溢出到同一栈帧内其他局部变量它一无所知。

## 错误 4：把大小算错的三种方式

**🎯 (a) 用指针的大小代替对象的大小**

```c
int **a = malloc(n * sizeof(int));      /* 错：应为 sizeof(int *) */
```

64 位上 `sizeof(int *)` 是 8、`sizeof(int)` 是 4，于是只分配了需要量的一半。实测：

```
普通编译  : Fatal glibc error: malloc.c:2599 assertion failed  （崩在后面的 free）
ASan     : heap-buffer-overflow ... in err_ptrsize mem_errors.c:59
Valgrind : Invalid write of size 8
           Address 0x4a81050 is 0 bytes after a block of size 16 alloc'd
```

Valgrind 那句 **"0 bytes after a block of size 16"** 是最精确的描述：刚好越过 16 字节块的末尾。

**🔧 防御写法**：`malloc(n * sizeof(*a))` —— 用**解引用后的类型**算大小，改类型时不用同步改 `sizeof`。

**🎯 (b) 错位错误（off-by-one）**

```c
for (int i = 0; i <= n; i++)    /* 错：应为 i < n */
    a[i] = i;
```

实测**普通编译完全无感，退出码 0**。原因是 `malloc(16)` 实际给的块通常有 24 字节可用空间，多写 4 字节落在了分配器的对齐填充里。ASan 用红区精确抓到：

```
ERROR: AddressSanitizer: heap-buffer-overflow on address 0x502000000020
    #0 ... in err_offbyone mem_errors.c:71
```

**🎯 (c) 误解指针运算：加的是元素不是字节**

```c
int *p = a;
*(p + 1);                        /* 跳过 1 个 int = 4 字节 —— 对 */
*(int *)((char *)p + 1);         /* 跳过 1 字节 —— 错位读 */
```

实测输出：

```
p[1]   = 20         （正确）
byte+1 = 335544320  （= 0x14000000，20 的字节错位）
```

**⚠️ 这一类 ASan 和 Valgrind 都抓不到**——地址仍在数组内部，没有越界，只是**读错了位置**。工具只能发现「访问了不该访问的内存」，发现不了「访问了不该访问的位置上的合法内存」。这类错误只能靠代码审查和单元测试。

## 错误 5：操作指针而不是它指向的对象

**🎯 `*size--` 的优先级陷阱**

```c
void f(int *size) {
    while (*size > 0) {
        ...
        *size--;        /* 实际是 *(size--)：让指针后退，计数器纹丝不动 */
        (*size)--;      /* 对：让计数器减一 */
    }
}
```

单目运算符 `*` 和 `--` 优先级相同、右结合，所以 `*size--` 先算 `size--`。后果是指针一路向低地址退，循环永不终止。实测：

```
普通编译  : 死循环（示例里加了计数上限才没跑飞），退出码 0
ASan     : stack-buffer-underflow ... in err_deref_ptr mem_errors.c:81
Valgrind : Conditional jump depends on uninitialised value(s)
```

两个工具从**不同角度**看到了同一件事：ASan 说「你读到了栈变量下方的红区」，Valgrind 说「你读到的值从来没被初始化过」。

## 错误 6：引用已失效的内存

**🎯 (a) 返回局部变量地址**

```c
int *f(void) {
    int local = 42;
    return &local;      /* 函数返回后栈帧失效 */
}
```

实测普通编译直接段错误（退出码 139），GCC 也给了静态警告 `-Wreturn-local-addr`。**这个错误相对幸运**：编译器和运行时都容易发现。

**⚠️ 危险的变体是返回栈上数组的指针**，值可能在一段时间内"看起来还对"——直到下一次函数调用覆盖了那片栈空间。

**🎯 (b) use-after-free 与 double free**

```c
free(a);
printf("%d\n", a[0]);   /* use-after-free */
free(a);                /* double free */
```

实测：

```
普通编译  : free(): double free detected in tcache 2   （退出码 134）
ASan     : heap-use-after-free on address 0x502000000010
           #0 ... in err_use_after_free mem_errors.c:118
           freed by thread T0 here: ... mem_errors.c:117
           previously allocated by thread T0 here: ...
```

ASan 的报告同时给出**三个位置**：出错点、释放点、分配点——这是它比 glibc 那句干巴巴的 "double free detected" 有价值得多的地方。

**🔧 `free` 的三条契约**

- 传进去的必须是 `malloc`/`calloc`/`realloc` **返回的那个地址**，不能加偏移（分配器靠 `p` 前面的块头找元数据）。
- 不能重复 `free`（现代 glibc 的 tcache 有检测，但不保证全部拦住）。
- 不能 `free` 栈地址或全局地址。
- 防御写法：`free(p); p = NULL;` —— 对 `NULL` 调 `free` 是合法空操作，能把 double free 变成无害操作。

## 错误 7：内存泄漏

**🎯 两种典型形态**

```c
for (int i = 0; i < 3; i++) {
    char *buf = malloc(1024);
    if (i == 1) continue;       /* 早退路径漏了 free */
    free(buf);
}
char *never = malloc(4096);     /* 干脆忘了释放 */
```

实测普通编译**毫无异常，退出码 0**。两个工具都能精确定位：

```
ASan     : SUMMARY: AddressSanitizer: 5120 byte(s) leaked in 2 allocation(s).
           Direct leak of 4096 byte(s) ... err_leak mem_errors.c:133
           Direct leak of 1024 byte(s) ... err_leak mem_errors.c:127
Valgrind : definitely lost: 5,120 bytes in 2 blocks
```

**⚠️ 短命进程的泄漏不致命，长跑服务的泄漏是灾难**

进程退出时内核会回收全部页，所以命令行小工具漏一点无所谓。但守护进程、服务器每处理一个请求漏 1 KB，跑一天就是几个 GB —— 表现为 RSS 单调上升，最终 OOM。

**🎯 Valgrind 的四档分类要会读**

| 分类 | 含义 | 要不要修 |
|------|------|---------|
| definitely lost | 没有任何指针指向它 | **必须修** |
| indirectly lost | 它的父块泄漏了，它跟着漏 | 修父块即可 |
| possibly lost | 只有指向块内部的指针 | 要看，可能是自定义分配器 |
| still reachable | 退出时仍有指针指着（全局缓存等） | 通常可接受 |

## 工具能力边界：一张实测对照表

**🎯 没有一个工具能抓全部**

| 错误类型 | 编译器警告 | 普通运行 | ASan | Valgrind |
|----------|-----------|---------|------|----------|
| 间接引用坏指针 | `-Wformat` 可拦 | SIGSEGV | ✅ | ✅ |
| 读未初始化内存 | `-Wmaybe-uninitialized` | 静默 | **❌** | ✅ 能追溯分配点 |
| 栈缓冲区溢出 | ❌ | canary 中止 | ✅ | **❌** |
| `sizeof` 算错 | ❌ | 堆元数据崩溃 | ✅ | ✅ |
| off-by-one（堆） | ❌ | 静默 | ✅ | ✅ |
| 引用指针而非对象 | ❌ | 静默死循环 | ✅ | ✅ |
| 指针运算错位（界内） | ❌ | 静默读到垃圾 | **❌** | **❌** |
| 返回栈地址 | `-Wreturn-local-addr` | SIGSEGV | ✅ | ✅ |
| use-after-free / double free | `-Wuse-after-free` | glibc abort | ✅ | ✅ |
| 内存泄漏 | ❌ | 静默 | ✅ | ✅ |

**🔧 实践中的分工**

- **ASan**：日常开发和 CI 的默认选项。运行速度约 2× 慢、内存约 3× 大，但报告精确到行号且带分配/释放栈。抓不到未初始化读。
- **Valgrind**：慢 10-50×，不适合 CI 全量跑，但**不需要重新编译**、能抓未初始化读、能追溯值的来源。适合线下深挖疑难杂症。
- **MSan**（`-fsanitize=memory`）：专抓未初始化读，但要求**整个程序（含所有库）都用 MSan 重新编译**，实践中门槛很高。
- **编译器警告**：零成本，必开 `-Wall -Wextra`。本节十类错误里它拦下了三类。
- **三者互补，谁也不能替代代码审查**——「界内错位」那一类只有人能发现。

## 易错点

- 内存错误**最常见的表现是没有表现**：本节十类错误里有四类在普通编译下退出码为 0，测试全绿不等于没有内存错误。
- 崩溃点几乎不是错误点——堆越界写要等到下次 `malloc`/`free` 操作元数据时才暴露，调用栈指向的是受害者。
- `malloc` **不清零**，只有 `calloc` 清零；「有时候读到 0」是巧合，不是保证。
- ASan **抓不到读未初始化内存**，这是它唯一的重大盲区，需要 Valgrind 或 MSan 补位。
- Valgrind **抓不到栈缓冲区溢出**，它主要跟踪堆；栈上越界要靠 ASan 或 canary。
- canary 只在**函数返回时**检查，溢出到同一栈帧内其他变量它发现不了（与 §3.10 结论一致）。
- 指针算术加的是**元素个数**不是字节数，`(char *)p + 1` 和 `p + 1` 是完全不同的两件事。
- `*size--` 减的是**指针**不是它指向的值，要写 `(*size)--`；单目 `*` 和 `--` 优先级相同且右结合。
- `free` 只接受 `malloc` 系列**返回的那个地址**，加了偏移就会破坏堆元数据；`free(NULL)` 是合法空操作。
- 「界内错位」这类逻辑错误 ASan 和 Valgrind **都抓不到**——工具只能发现越界，发现不了越界之内的错误位置。
- `free` 之后 RSS 通常不降（见 §9.9），所以**不能用 RSS 不降来判断泄漏**，要用工具的分配/释放配对分析。
- Valgrind 的 `still reachable` 通常不是 bug（全局缓存、单例），别把它和 `definitely lost` 一起当泄漏报。

## 工程关联

- **CI 流水线的标准配置**：Debug 构建默认开 `-fsanitize=address,undefined -fno-omit-frame-pointer`，把 ASan 和 UBSan 一起挂上；ASan 与 TSan 互斥不能同时开，通常分两条流水线跑。
- **生产环境的低成本替代**：ASan 的开销上不了线，生产上常用 `MALLOC_CHECK_=3`（glibc 内置的轻量堆检查）、`GLIBC_TUNABLES=glibc.malloc.perturb=...`（free 后填充固定字节，让 UAF 更快暴露），或换成带审计能力的分配器（jemalloc/tcmalloc 的 debug 模式）。
- **`-D_FORTIFY_SOURCE=2`**：让 `memcpy`/`sprintf`/`strcpy` 等函数在编译期能推断出目标大小时插入运行时检查，代价极低，发行版默认开启。本节 `printf` 被替换成 `__printf_chk` 就是它的痕迹。
- **内核态没有这些工具的等价物**：内核用 KASAN（内核 ASan）、KMSAN、kmemleak，原理相同但需要重新编译内核并开启对应 CONFIG。
- **C++ 的解法是不手写 `new`/`delete`**：`unique_ptr`/`shared_ptr`/`vector`/`string_view`（注意 `string_view` 本身不管生命周期，是新的悬空来源）把生命周期问题交给类型系统，本节大部分错误在现代 C++ 里根本没有机会出现。
- **Rust 的借用检查器**解决的正是本节第 6 类（引用已失效内存）——它把 use-after-free 从运行时错误提升成了编译错误，代价是所有权规则的心智负担。
- **与 §9.9 的联系**：本节所有堆错误的破坏对象都是**分配器的元数据**（块头、边界标记、空闲链表指针）。理解了隐式/显式空闲链表的结构，就能理解为什么越界写 4 个字节能让下一次 `free` 直接 abort。

## 实验题

**🧪 题 1：确认「内存错误经常不崩」**

```bash
make all
for t in uninit offbyone ptr_arith leak; do
    ./mem_errors $t; echo "  $t 退出码 = $?"
done
```

要求：
1. 记录哪些类型退出码为 0。
2. 对退出码为 0 的每一类，说明它到底破坏了什么（或者为什么没破坏成）。
3. 把 `-O0` 改成 `-O2` 重跑，看现象是否变化——理解优化会改变栈布局和内存复用，从而改变错误的可观测性。

**🧪 题 2：崩溃点 ≠ 错误点**

```c
int **a = malloc(n * sizeof(int));   /* 应为 sizeof(int *) */
for (int i = 0; i < n; i++) a[i] = NULL;
free(a);                              /* ← 程序在这里崩 */
```

要求：
1. 用 `gdb ./mem_errors` 跑 `ptrsize`，看崩溃时的调用栈落在哪个函数。
2. 用 ASan 版跑同一个用例，对比它报的行号。
3. 解释为什么普通版的调用栈对定位问题毫无帮助。

**🧪 题 3：ASan 与 Valgrind 的盲区**

要求：
1. 对 `uninit` 分别跑 ASan 版和 `valgrind --track-origins=yes`，确认 ASan 完全静默。
2. 对 `stack_overflow`（输入 40 个 A）分别跑 ASan 和 Valgrind，确认 Valgrind 抓不到。
3. 对 `ptr_arith` 跑两个工具，确认都抓不到，并解释原因。
4. 用这三个结果画一张「什么时候用哪个工具」的决策表。

**🧪 题 4：堆元数据是怎么被破坏的**

```c
int *a = malloc(4 * sizeof(int));
a[4] = 0xdeadbeef;      /* 越界写一个字 */
free(a);
```

要求：
1. 在 `a[4] = ...` 前后用 `gdb` 打印 `a[-1]`（块头里的 size 字段）和 `a[4]` 的地址关系。
2. 结合 §9.9 的隐式空闲链表结构，说明 `a[4]` 打到了什么位置（提示：下一个块的头部）。
3. 把越界值改成一个「看起来合法的 size」，看 `free` 是否还会 abort——理解为什么堆溢出能被利用成安全漏洞。

**🧪 题 5：泄漏的四种分类**

```c
char *p1 = malloc(100);              /* 什么都不做 */
char *p2 = malloc(100); p2 += 10;    /* 只留指向块内部的指针 */
static char *g;  g = malloc(100);    /* 全局变量持有 */
struct node { struct node *next; };  /* 构造父子块，只漏父块 */
```

要求：
1. 写出上述四种情况，用 `valgrind --leak-check=full --show-leak-kinds=all` 跑。
2. 对照输出确认它们分别落在 definitely / possibly / still reachable / indirectly 哪一档。
3. 说明为什么 `still reachable` 通常不需要修。

**🧪 题 6：把防御手段一次性加上**

要求：
1. 给 `mem_errors.c` 逐个加上防御写法：`scanf("%7s")`、`malloc(n * sizeof(*p))`、`free(p); p = NULL;`、`calloc` 替换 `malloc`。
2. 重新跑 `make demo`，确认 ASan 全部静默。
3. 加 `-D_FORTIFY_SOURCE=2 -O2` 重新编译原始版本，看哪些错误在**没有 sanitizer** 的情况下被拦下了。
