# §5.12–5.14 内存性能、优化策略清单与瓶颈定位

这三节是第 5 章的收尾：**§5.12** 把"延迟/吞吐"的分析从算术单元推进到内存单元——load 和 store 也有自己的功能单元、自己的延迟，而真正的坑是 **store→load 之间的数据相关（write/read dependency）**；**§5.13** 把全章手段收成一张分层的优化清单；**§5.14** 回答"该优化谁"——用 profiler 找热点、用 Amdahl 定律决定值不值得。

主线一句话：**会算 CPE 只是上半场，下半场是知道把力气花在哪、以及内存别名怎么悄悄拖慢你。**

---

## load 操作的性能：指针追逐撞上 load 延迟

**🎯 load 单元也是一种功能单元，有自己的延迟与吞吐**

现代处理器（书中以 Haswell 为例）有 **2 个 load 单元**，每个 load 延迟 **4 cycle**、每周期可发起 1 个（issue 1/cycle）。所以纯 load 的吞吐界限是 0.5 cycle/load，但**延迟界限是 4**。

**🎯 关键路径由 load 构成时，CPE = load 延迟**

最干净的例子是链表遍历——指针追逐（pointer chasing）：

```c
typedef struct ELE {
    struct ELE *next;
    long data;
} list_ele, *list_ptr;

long list_len(list_ptr ls) {
    long len = 0;
    while (ls) {
        len++;
        ls = ls->next;   // ← 本次 load 的地址，依赖上一次 load 的结果
    }
    return len;
}
```

实测 **CPE ≈ 4.00**，正好等于 load 延迟。原因：`ls = ls->next` 这条 load 的**地址**来自上一次 load 的**数据**，每次 load 必须等上一次彻底完成，形成一条全由 load 串起来的关键路径。两个 load 单元在这里毫无用处——没有可并行的独立 load。

⚠️ 对比 §5.7 的 `combine4`（整数加法）CPE≈1：那里 load 不在关键路径上（关键路径是累加器的算术依赖），load 单元的吞吐足够喂饱它。**同样是 load，在不在关键路径上，性能含义完全不同。**

---

## store 操作与 store buffer：写本身不产生依赖

**🎯 store 单元也独立，但 store 不产生"被后续指令读取的寄存器值"**

store 把寄存器值写进内存，不像 load 那样产出一个寄存器结果供下游使用。所以一串**互不读取**的 store，彼此完全独立，CPE 可以压到吞吐界限（Haswell 1 个 store 单元，≈1 cycle/store）。

```c
void clear_array(long *dest, long n) {
    for (long i = 0; i < n; i++)
        dest[i] = 0;        // 一连串独立 store，CPE ≈ 1
}
```

**🔧 store buffer：store 不会立刻落到内存/cache**

处理器把 store 拆成"地址 + 数据"先丢进 **store buffer（存储缓冲区）**，地址算好就能继续，数据何时真正写回内存与流水线解耦。这让 store 看起来"很快"。但它埋下了下一节的雷：后续 load 必须检查 store buffer，看自己要读的地址是不是还躺在某个未落盘的 store 里。

---

## write/read 相关：别名（aliasing）是隐形杀手

**🎯 当 store 写过的地址马上被 load 读到，就形成 store→load 数据相关**

经典实验函数：

```c
void write_read(long *src, long *dest, long n) {
    long cnt = n;
    long val = 0;
    while (cnt--) {
        *dest = val;        // store
        val = (*src) + 1;   // load —— 它读的地址会不会正是上面 store 的地址？
    }
}
```

同一份代码，喂不同指针，CPE 天差地别：

| 调用方式 | src 与 dest 关系 | CPE | 关键路径 |
|----------|------------------|-----|----------|
| `write_read(&a[0], &a[1], n)` | 不别名（指向不同元素） | **≈1.3** | store 与 load 互相独立，吞吐界限 |
| `write_read(&a[0], &a[0], n)` | **别名**（同一地址） | **≈7.3** | load 必须拿到上一次 store 的数据 → store-to-load forwarding 链 |

**🔧 store-to-load forwarding（存储转发）形成的关键路径**

别名时，`val = *src + 1` 这个 load 读的就是上一句 `*dest = val` 刚写的地址。处理器必须：load 算地址 → 在 store buffer 里匹配到那条 store → 把 store 的数据**转发**给 load → load 出结果 → `+1` → 作为下一轮 store 的数据。这条 `s_data → load → +1 → s_data` 的环就是关键路径，延迟≈6，于是 CPE≈7。

⚠️ 编译器无法静态判定两个指针是否别名时，必须**保守地假设它们可能别名**，因而不敢把 load 提到循环外、不敢复用寄存器——这正是 §5.6"消除不必要的内存引用"被别名卡住的底层原因，也是 `restrict` 关键字存在的意义（程序员向编译器承诺"我不别名，放心优化"）。

---

## §5.13 优化策略清单：从高层到底层的分层施力

**🎯 优化是分层的，越往上收益越大、越该先做**

书中把全章手段整理成一张可执行清单：

```
1. 高层设计（收益最大，最先做）
   └─ 为问题选对算法和数据结构。再多底层技巧救不了一个 O(n²) 的烂算法。

2. 基本编码原则（消除编译器被挡住的妨碍因素 optimization blocker）
   ├─ 消除循环内的连续函数调用：能移出循环就移出（可能牺牲一点模块性）
   └─ 消除不必要的内存引用：引入临时变量累加，最后一次性写回
      （这一步同时绕开了上面 write/read 别名的悲观假设）

3. 底层优化（针对处理器微架构）
   ├─ 循环展开（unrolling）：减少循环开销，暴露更多可并行指令
   ├─ 多累加器 + 重结合（reassociation）：突破延迟界限，逼近吞吐界限
   └─ 用功能式写法改写条件，诱导编译器生成 cmov，避开分支预测失败
```

**⚠️ 同时盯住"妨碍因素"**

编译器优化是保守的，下面三件事会让它直接放弃优化：
- 内存别名（aliasing）——见上节
- 函数调用的潜在副作用——编译器不敢假设无副作用，除非内联
- 浮点运算的结合律不成立——`-O2` 默认不敢重排浮点累加（要 `-ffast-math` 才放开）

---

## §5.14 用 profiler 定位瓶颈：先量，再优化

**🎯 程序剖析（profiling）：插桩运行，统计每个函数的耗时与调用次数**

不要凭直觉猜热点。Unix 经典工具 **gprof**：

```bash
gcc -O2 -pg prog.c -o prog   # -pg 插入计时桩
./prog                       # 运行后产出 gmon.out
gprof prog gmon.out          # 解析出每个函数的累计时间 + 调用次数
```

gprof 给两类信息：① 每个函数的 CPU 时间占比；② 函数调用次数（精确计数）。书中案例（统计文本中的 n-gram）就是靠它一轮轮发现热点：先把 `insert_string` 的插入排序换成快排、再把线性查找换成哈希表、再降低 `strlen`/`lower` 的调用——每一步都先 profile 再动手。

**⚠️ gprof 的局限**
- 时间靠 ~10ms 周期采样估计，**运行很短的函数测不准**
- 默认看不进内联函数和库函数内部
- `-pg` 改变了程序行为、需要重新编译

**🔧 现代替代：perf（采样式，无需重编译）**

```bash
perf record -g ./prog        # 采样调用栈，产出 perf.data
perf report                  # 按热度排序，可下钻到指令
perf report -g | flamegraph  # 火焰图：宽度=耗时占比，一眼看出热点
```

`perf` 不需要 `-pg` 重编译、能看到内核态/库函数、采样开销低，是当前定位热点的首选；要"精确插桩计数"（而非采样）时用 `valgrind --tool=callgrind` + `kcachegrind`。

**🎯 Amdahl 定律：决定一个优化值不值得做**

若某部分占总时间比例 α，把它加速 k 倍，整体加速比为：

$$S = \frac{1}{(1-\alpha) + \alpha/k}$$

极端情形 k→∞（把这部分干到 0 耗时），整体上限 `S_max = 1/(1-α)`。

⚠️ 推论：**只优化占比大的部分**。一个只占 5% 的函数，哪怕优化到无穷快，整体也只快 1/(1-0.05)≈1.05 倍。这就是为什么要先 profile——把力气花在 α 大的地方，否则做无用功。这与 §1.9 第一次见到的 Amdahl 定律首尾呼应。

---

## 易错点

- "store 比 load 快"是错觉——孤立看 store 因 store buffer 解耦确实流畅，但一旦后续 load 读到刚写的地址（别名），store→load 转发会把延迟拉到 ~6 cycle，CPE 暴涨。
- 链表遍历 CPE≈4 不是"内存慢"，而是 load 延迟串成了关键路径、两个 load 单元无独立 load 可并行；命中 L1 也救不了，问题在依赖不在 cache。
- 别名是编译器层面的悲观假设，不是运行时一定发生的事——只要编译器**无法证明**不别名，它就不敢优化，于是该提出循环的 load 留在了循环里。
- gprof 的函数时间是采样估计、不是精确测量，对短函数和高频小函数会严重失真；要精确计数得用 callgrind。
- Amdahl 定律的结论不是"优化能无限加速"，而是"被你忽略的那 (1-α) 部分决定了天花板"——优化前必须先知道 α。
- 优化顺序不能颠倒：先选对算法（高层），再消妨碍因素，最后才上循环展开/多累加器；底层技巧救不了烂算法。

## 工程关联

- **`restrict` 关键字**：C99 的 `restrict` 就是程序员对编译器承诺"此指针不与其他指针别名"，直接解开 §5.12 的悲观假设，让 load 能被提出循环、寄存器能被复用——数值库（BLAS、memcpy 实现）大量使用。
- **指针追逐是真实的反模式**：链表、树、哈希链遍历都是 load-延迟受限，CPE 卡在 load latency，无法靠 ILP 加速；这是工程上"数组/扁平结构优于链式结构"的微架构理由（cache 友好之外的第二条理由）。
- **store buffer 与内存序**：store buffer 让本核 store 延迟落盘，是 x86 TSO 内存模型里 store-load 重排的硬件根源，也是多线程下需要内存屏障的原因（第 12 章 false sharing / 可见性问题的底层机制）。
- **perf 工作流**：`perf record -g` → `perf report` → 火焰图是线上性能排障的标准三步，替代了需要重编译的 gprof；`perf annotate` 能下钻到具体汇编指令看哪条热。
- **Amdahl 在分布式/并行同样成立**：并行化只能加速可并行部分，串行部分（α'=1-α）决定多核加速天花板，是第 12 章 speedup 曲线压平的根因。

## 实验题

**🧪 题 1：write/read 别名的 CPE 鸿沟**

```c
void write_read(long *src, long *dest, long n) {
    long val = 0;
    while (n--) { *dest = val; val = (*src) + 1; }
}
```

要求：
- 写 driver，用大数组 `a`，分别以 `write_read(&a[0], &a[1], N)`（不别名）与 `write_read(&a[0], &a[0], N)`（别名）各跑 N=10⁸。
- 用 `rdtsc` 或 `perf stat -e cycles,instructions` 测两种情形的 CPE，验证别名版本 CPE 约为不别名版本的 4~6 倍。
- `gcc -O2 -S` 看两种调用对应的汇编是否相同（应相同——差异纯在运行时数据相关），并用 `perf stat -e cycle_activity.stalls_*` 观察别名版本的停顿来源。

**🧪 题 2：指针追逐 vs 数组求和**

```c
// 版本 A：链表遍历（指针追逐）
long sum_list(list_ptr p)  { long s=0; while(p){ s+=p->data; p=p->next; } return s; }
// 版本 B：等长数组顺序求和
long sum_arr(long *a, long n){ long s=0; for(long i=0;i<n;i++) s+=a[i]; return s; }
```

要求：
- 让链表节点在内存中**连续分配**（排除 cache miss 干扰），N 相同，对比两者 CPE。
- 验证版本 A 的 CPE≈load 延迟（本机 Ultra 7 255H 实测填入），版本 B 因 load 可并行 + 累加是关键路径而明显更低。
- 结论：A 的瓶颈是 load-延迟串成关键路径，与 cache 命中与否无关。

**🧪 题 3：profile 引导的逐步优化 + Amdahl 验证（✅ 已完成，本机 Ultra 7 255H）**

源码 [`expirements/dedup.c`](expirements/dedup.c)：对 N 个含大量重复的字符串去重 + 算校验和。
`dedup_quadratic`（O(n²) 线性查找，热点）vs `dedup_hash`（O(n) 哈希，优化版）；`checksum`（djb2 求和，故意放大成"小头"，演示 Amdahl 天花板）。

**① 分段计时（`gcc -O2`，N=20000 R=80）**

```
n2  : dedup=2.6765s  checksum=0.4045s  total=3.0810s
hash: dedup=0.0143s  checksum=0.4076s  total=0.4219s
```

**② Amdahl 验证**——热点占比 α = 2.6765/3.0810 = **0.869**，dedup 自身加速 k = 2.6765/0.0143 = **187×**：

$$S_{预测} = \frac{1}{(1-0.869)+0.869/187} = 7.36\times \qquad S_{实测} = \frac{3.081}{0.422} = 7.30\times$$

误差 <1%。**关键**：dedup 快了 187 倍，整体只快 7.3 倍——优化后 checksum 占 96.6% 成新瓶颈，卡死在天花板 1/(1-α)=7.62×。**没碰的那 13% 决定了上限。**

**③ gprof（`gcc -O2 -fno-inline -pg`，`-fno-inline` 保住函数边界）翻车**：

```
40.37%  dedup_quadratic      36.70%  checksum      22.94%  _init  ← 神秘的 23%
```

与 wall-clock 87:13 严重不符。**根因**：gprof 的 self time 不含被调用的库函数；`dedup` 的真实成本全在 `strcmp`（libc 无符号），被错归给最近符号 `_init`；`checksum` 因 djb2 是自身指令反而虚高。

**④ perf 三步法（`gcc -O2 -g -fno-omit-frame-pointer`）正确归因**：

```bash
perf record -g --call-graph fp --all-user ./dedup_perf n2 20000 80   # 1.采样(paranoid=1 用 --all-user)
perf report --stdio --no-children                                    # 2.flat self 排序
perf annotate --stdio main                                           # 3.下钻到指令行
```

结果：`__strcmp_avx2` **65%**（gprof 完全漏掉），annotate 钉到 `call strcmp@plt`(24%)+`jne`(20%) 即 O(n²) 内循环比较，`add/jne`(12%) 是 checksum 的 djb2。perf 采 PC 能落进 libc，是 gprof 的根本优势。

**⑤ 火焰图**（perf 无内置脚本，需 [FlameGraph](https://github.com/brendangregg/FlameGraph)）：
```bash
perf script | stackcollapse-perf.pl | flamegraph.pl > flame.svg   # 宽度=耗时占比
```
