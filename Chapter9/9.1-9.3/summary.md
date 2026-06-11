# §9.1-9.3 虚拟内存：寻址、地址空间与 DRAM 缓存

这三节的主线是：**虚拟内存的第一重身份——把主存（DRAM）当作磁盘的缓存**。CPU 发出的是虚拟地址，MMU 查页表翻译成物理地址；页表条目（PTE）的有效位决定这次访问是页命中还是缺页。缺页不是错误，而是 demand paging 的正常工作方式——靠程序的局部性，这套"用 ms 级的磁盘做后备"的缓存才跑得起来。

- §9.1：物理寻址 vs 虚拟寻址，MMU 是中间翻译层
- §9.2：地址空间的本质——同一数据对象可以有多个独立的地址
- §9.3：虚拟页/物理页、页表、页命中、缺页、按需调页、工作集

---

## 物理寻址与虚拟寻址

**🎯 两种寻址方式**

- **物理寻址**：CPU 直接把物理地址放上总线访问内存。早期 PC、单片机、DSP 至今这么干
- **虚拟寻址**：CPU 生成**虚拟地址（VA）**，由芯片上的 **MMU（内存管理单元）** 查表翻译成**物理地址（PA）**后才访问主存。这个翻译过程叫**地址翻译**

```
CPU ──虚拟地址 VA──▶ MMU ──物理地址 PA──▶ 主存
                     ▲
                     │ 查页表（页表存放在主存中，由 OS 维护）
```

**🎯 关键分工**

- 硬件（MMU）：每次访存做翻译，纯查表，不懂策略
- 操作系统：维护页表内容，决定"哪个虚拟页映射到哪个物理页"
- 两者合作完成翻译——这是贯穿整个第 9 章的主线

**🔧 在 Linux 里的直接体现**

你在 C 程序里 `printf("%p", &x)` 打出来的、gdb 里看到的、`/proc/<pid>/maps` 里列出的，**全部是虚拟地址**。两个进程打印同一个变量地址可能完全相同，但物理上各占一块内存——这就是 §8.2 学过的"私有地址空间"假象的实现机制。

---

## 地址空间

**🎯 定义**

地址空间就是一组非负整数地址的有序集合：

- **虚拟地址空间**：N = 2ⁿ 个地址，n 是虚拟地址位数。x86-64 Linux 下 n = 48（新硬件可选 57），所以用户态能用到 256 TB 级别的虚拟地址
- **物理地址空间**：M = 2ᵐ 个地址，对应机器实际插了多少 DRAM

**🎯 本节最重要的一句话**

> 数据对象（字节）和它的属性（地址）是分开的，**同一个数据对象可以有多个独立的地址**。

一个字节可以同时拥有：一个虚拟地址（程序看到的）+ 一个物理地址（DRAM 里的实际位置）。后面 §9.8 还会看到：两个进程的不同虚拟地址可以映射到**同一个**物理页（共享库、COW），这全是"地址与数据分离"这一抽象的红利。

**⚠️ 虚拟地址空间通常远大于物理地址空间**

8 GB 内存的机器上，每个进程都"看到" 256 TB 的虚拟地址空间。这不矛盾——虚拟地址空间绝大部分是**未分配**的，不占任何资源。

---

## 虚拟内存 = 用 DRAM 缓存磁盘

**🎯 概念框架：VM 是存储层次中的一级缓存**

虚拟内存的内容（概念上）存放在磁盘上，主存 DRAM 是它的缓存。虚拟地址空间被切成固定大小的**虚拟页（VP）**，物理内存切成同样大小的**物理页（PP，页帧）**，Linux x86-64 默认页大小 P = 4 KB。

任意时刻，虚拟页的集合分成三类：

| 状态 | 含义 | 例子 |
|------|------|------|
| 未分配 | 还不存在，不占磁盘也不占内存 | 堆和栈之间的巨大空洞 |
| 已缓存 | 已分配，且当前在 DRAM 里 | 正在频繁访问的代码和数据 |
| 未缓存 | 已分配，但只在磁盘（或还没初始化） | 被换出的页、还没读进来的文件页 |

**🎯 DRAM 缓存的组织结构由不命中代价决定**

DRAM 比 SRAM 慢约 10 倍，但磁盘比 DRAM 慢约 **100,000 倍**。这个悬殊的代价差决定了 DRAM 缓存（相对 L1/L2/L3 SRAM 缓存）的所有设计选择：

| 设计维度 | SRAM 缓存（L1-L3） | DRAM 缓存（虚拟内存） | 原因 |
|----------|------------------|---------------------|------|
| 块大小 | 64 B | 4 KB（甚至 2 MB 大页） | miss 代价大 → 一次多搬点，摊薄成本 |
| 相联度 | 4/8/16 路组相联 | **全相联**（任意 VP 可放任意 PP） | miss 太贵，必须最大化命中率 |
| 替换策略 | 硬件近似 LRU | OS 用复杂软件算法 | 替换错了代价是 ms 级，值得花软件开销 |
| 写策略 | 写穿/写回都有 | **永远写回** | 每次写都同步磁盘不可接受 |

**⚠️ 不要把虚拟内存等同于"交换区"**

"虚拟内存 = 内存不够时用硬盘凑"是最常见的误解。交换（swap）只是 VM 作为缓存这一面的一个表现；VM 同时还是内存管理工具（§9.4）和保护工具（§9.5）。即使机器永不 swap，每一次访存也都在走虚拟地址翻译。

---

## 页表、页命中与缺页

**🎯 页表：VP → PP 的映射表**

页表是一个 **PTE（页表条目）数组**，常驻主存，每个虚拟页对应一条 PTE。CSAPP 的简化模型里每条 PTE = 有效位 + 物理页号（或磁盘地址）：

```
PTE = [ valid | 地址字段 ]
  valid=1 → 地址字段是物理页号（DRAM 中）
  valid=0 且地址非空 → 页在磁盘上
  valid=0 且地址为空 → 未分配
```

**🎯 页命中（page hit）**

MMU 查 PTE，valid = 1，直接拿物理页号拼出物理地址，访存继续。全程硬件完成，无 OS 介入。

**🎯 缺页（page fault）= valid 为 0 的 PTE 被访问**

缺页是一种**故障（fault）**——回忆 §8.1：故障处理后**重新执行当前指令**。完整流程：

1. MMU 发现 PTE valid = 0，触发缺页异常，陷入内核
2. 内核缺页处理程序选一个**牺牲页**，若它是脏页则先写回磁盘
3. 把目标页从磁盘读入腾出的物理页，更新两条 PTE（牺牲页置 invalid，新页置 valid）
4. 返回，**重新执行引发缺页的那条指令**——这次就是页命中了

**🎯 按需调页（demand paging）**

系统不会预先把页加载进内存，而是**一直等到缺页才搬**。`malloc`/`mmap` 分配内存时只是登记了映射（创建/修改 PTE），不分配物理页；第一次真正读写才缺页、才拿到物理页。这就是"malloc 1 GB 秒回，但 RSS 不涨"的原因。

**🎯 抖动（thrashing）**

只要程序的**工作集**（当前活跃使用的页集合）小于物理内存，缺页只在冷启动时出现，之后全是命中。一旦工作集超过物理内存，页面被换出又马上被换回，程序时间全花在等磁盘上——这就是抖动，表现为机器"卡死"但 CPU 占用不高、磁盘灯狂闪。

---

## 脏页

**🎯 定义**

脏页 = **被写过、但内容还没同步回后备存储的页**。"脏"指的是不一致：DRAM 里的副本比磁盘上的新。它是写回（write-back）策略的直接产物——写操作只改 DRAM 副本、不立刻落盘，所以从写下第一个字节起这个页就是脏的，直到内核把它写回才重新变"干净"。

**🎯 为什么驱逐时必须区分干净/脏**

这就是缺页流程第 2 步"牺牲页若是脏页则先写回"的原因：

| 牺牲页状态 | 驱逐动作 | 成本 |
|-----------|---------|------|
| 干净 | 磁盘副本就是最新的，直接丢弃 | 零 I/O |
| 脏 | 内存版本更新，必须先写回磁盘再驱逐 | 多一次磁盘 I/O |

脏页驱逐成本是干净页的两倍量级，所以换页算法优先挑干净页下手。

**🎯 硬件怎么知道页脏了：`_PAGE_DIRTY`**

就是 PTE 第 6 位 `_PAGE_BIT_DIRTY`：CPU 对某页执行写操作时由 MMU **硬件自动置位**，软件零开销；内核换页时读这一位决定要不要写回。它和 `_PAGE_ACCESSED`（读过就置位，供 LRU 近似用）是一对，都是"硬件汇报、软件决策"契约的典型。

**🎯 两类脏页，写回去向不同**

- **文件页**（mmap 的文件、页缓存）：写回**对应的文件**。`write()` 写文件后数据先变成页缓存里的脏页，由内核刷写线程异步落盘——这是"断电丢数据"风险的来源，也是 `sync`/`fsync` 存在的意义
- **匿名页**（堆、栈，没有对应文件）：没有原文件可写回，只能写到 **swap 分区**；机器没配 swap 时匿名脏页根本无法驱逐，内存紧张只能走 OOM killer

**🔧 在 Linux 里直接观察**

```bash
grep Dirty /proc/meminfo          # 全系统当前脏页总量
cat /proc/sys/vm/dirty_ratio      # 脏页占比超过阈值，写进程被强制同步刷盘
```

`vm.dirty_ratio`/`vm.dirty_background_ratio` 是数据库、存储服务最常调的内核参数之一：脏页攒太多，集中刷盘造成 I/O 风暴和写延迟毛刺；攒太少又浪费写合并机会。"服务周期性卡顿 + iostat 显示写突发"，第一个该怀疑的就是脏页刷写策略。

---

## Linux 内核中的页表数据结构（关注点 ①）

**🎯 真实页表不是一张大数组，而是 4/5 级基数树**

CSAPP 本节的"单级页表"是教学简化：x86-64 下若真用单级表，每个进程要 2⁴⁸/2¹² × 8 B = 512 GB 的 PTE 数组。真实方案是多级页表（详细原理在 §9.6.3），Linux 把它建模成最多 5 级，每级是一个 4 KB 页、装 512 个 8 字节条目，虚拟地址每 9 位索引一级：

```
虚拟地址（48 位）:  [ PGD 9位 | PUD 9位 | PMD 9位 | PTE 9位 | 页内偏移 12位 ]
级别（5 级时多一层 P4D）:  pgd_t → p4d_t → pud_t → pmd_t → pte_t
```

**🎯 内核源码里的类型定义**（Linux 6.x，`arch/x86/include/asm/pgtable_64_types.h`）

每一级条目本质就是一个 64 位整数，内核刻意用单成员结构体包一层，防止不同级别的条目被混用（编译期类型检查）：

```c
typedef unsigned long   pteval_t;
typedef unsigned long   pmdval_t;
typedef unsigned long   pudval_t;
typedef unsigned long   pgdval_t;

typedef struct { pteval_t pte; } pte_t;     /* 最后一级条目，对应 CSAPP 的 PTE */
typedef struct { pmdval_t pmd; } pmd_t;
typedef struct { pudval_t pud; } pud_t;
typedef struct { pgdval_t pgd; } pgd_t;     /* 顶级目录条目 */
```

**🎯 PTE 内部的位布局**（`arch/x86/include/asm/pgtable_types.h`）

CSAPP 图里的"有效位"在 x86 上就是第 0 位 `_PAGE_PRESENT`；§9.5 要讲的权限位、§9.4 的脏位也全在这一个 64 位里：

```c
#define _PAGE_BIT_PRESENT   0   /* 在内存中（CSAPP 的 valid 位） */
#define _PAGE_BIT_RW        1   /* 可写 */
#define _PAGE_BIT_USER      2   /* 用户态可访问 */
#define _PAGE_BIT_ACCESSED  5   /* 被访问过（CPU 硬件置位，换页算法用） */
#define _PAGE_BIT_DIRTY     6   /* 被写过（CPU 硬件置位，决定换出时是否写回） */
#define _PAGE_BIT_NX       63   /* 不可执行（栈不可执行保护就靠它） */
```

内核用一组内联函数读这些位，命名一目了然：

```c
/* arch/x86/include/asm/pgtable.h（简化） */
static inline int pte_present(pte_t a) { return pte_flags(a) & _PAGE_PRESENT; }
static inline int pte_write(pte_t pte) { return pte_flags(pte) & _PAGE_RW; }
static inline int pte_dirty(pte_t pte) { return pte_flags(pte) & _PAGE_DIRTY; }
```

**🎯 页表根挂在哪：`mm_struct->pgd`**（`include/linux/mm_types.h`）

每个进程一套页表，根指针存在进程的内存描述符里：

```c
struct mm_struct {
    ...
    pgd_t *pgd;          /* 顶级页目录的虚拟地址，整套页表的根 */
    unsigned long total_vm;   /* 映射的总页数 */
    ...
};
/* 访问链：task_struct->mm->pgd */
```

上下文切换时，内核把新进程 `mm->pgd` 对应的**物理地址**写进 **CR3 寄存器**——MMU 永远从 CR3 出发逐级查表。"切换页表"就是改写一个寄存器，这就是每个进程拥有私有地址空间的全部秘密。

**🔧 内核自己怎么"手动查页表"**（`mm/memory.c` 中缺页处理的逐级下钻，简化）

```c
/* __handle_mm_fault() 的骨架：和 MMU 硬件做的事一模一样 */
pgd = pgd_offset(mm, address);       /* mm->pgd + 取 VA 第 39-47 位做索引 */
p4d = p4d_offset(pgd, address);
pud = pud_offset(p4d, address);
pmd = pmd_offset(pud, address);
pte = pte_offset_map(pmd, address);  /* 走到最后一级，拿到 pte_t */
```

MMU 硬件查表和这段软件代码逻辑完全相同；区别只是硬件查表发生在每次访存（有 TLB 加速），软件查表只在缺页处理等慢路径上执行。

---

## 时间局部性的好坏怎么判断（关注点 ②）

**🎯 判定标准：重用距离（reuse distance）**

时间局部性 = 同一数据被访问后，**很快**再次被访问。"很快"的精确度量是**重用距离：两次访问同一数据之间，访问了多少个不同的数据**。

> **重用距离 < 缓存容量 → 第二次访问命中；重用距离 > 缓存容量 → 数据早被逐出，重访等于没访问过。**

所以判断时间局部性不看"数据被重复用了多少次"，而看**两次重用隔了多远**。

**🎯 具体代码：同样的访问总量，局部性天差地别**

两个版本都把一个 256 MB 数组的每个元素累加 10 遍——访问的元素集合、总访问次数完全相同，只是顺序不同：

```c
#define N      (64 * 1024 * 1024)   /* 64M 个 float = 256 MB，远大于 LLC */
#define BLOCK  (256 * 1024)         /* 1 MB，稳稳放进 LLC */
float a[N];

/* 版本 A：时间局部性差
 * a[i] 两次被访问之间隔了整整 N 个其他元素（重用距离 = N = 256 MB）
 * 远超 LLC（典型 8-32 MB）→ 第 2~10 遍来时 a[i] 早被逐出，每遍都从内存重读 */
float pass_then_repeat(void) {
    float sum = 0;
    for (int pass = 0; pass < 10; pass++)
        for (long i = 0; i < N; i++)
            sum += a[i];
    return sum;
}

/* 版本 B：时间局部性好
 * 把数组切成 1 MB 的块，块内连刷 10 遍再前进
 * a[i] 的重用距离 = BLOCK = 1 MB < LLC → 第 2~10 遍全部 cache 命中 */
float blocked_repeat(void) {
    float sum = 0;
    for (long blk = 0; blk < N; blk += BLOCK)
        for (int pass = 0; pass < 10; pass++)
            for (long i = blk; i < blk + BLOCK; i++)
                sum += a[i];
    return sum;
}
```

**🎯 怎么实测验证**

```bash
gcc -O1 locality.c -o locality        # 别用 -O2/-O3，避免循环被向量化重排干扰对比
perf stat -e LLC-load-misses,LLC-loads,instructions,cycles ./locality
```

预期：两版本 `instructions` 几乎相同，但版本 A 的 `LLC-load-misses` 约是版本 B 的 10 倍（每遍全 miss vs 只有第一遍 miss）。**指令数相同、cache miss 数倍差距——这就是"时间局部性好坏"在性能数据上的样子。**（注意：本实验是顺序访问，硬件预取器会掩盖大部分 miss 延迟，墙钟时间差距实测只有 ~20%；随机访问才会让时间差距追上 miss 差距，见题 2 实测结果。）

**⚠️ 时间局部性和空间局部性是两个独立维度**

版本 A 的**空间局部性其实很好**（顺序扫描，每条 cache line 的 16 个 float 都被用上），坏的是时间局部性（重访太晚）。分析程序时要分开问两个问题：相邻数据有没有一起用（空间）？同一数据重访够不够快（时间）？

**🔧 这个判定思路对应的真实工程手法**

矩阵分块（blocking/tiling）、数据库的批量处理、`fread` 按块读文件，本质都是同一招：**把"对全量数据各做一轮的多趟操作"重排成"对一小块数据做完所有操作再前进"**，压缩重用距离到缓存容量以内。第 6 章的矩阵乘法实验会再次用到。

---

## perf 缓存事件怎么读：load 的漏斗模型（关注点 ② 配套）

`LLC-loads`、`LLC-load-misses` 这类事件名的含义，要放在"一次 load 逐级穿透缓存层次"的漏斗里才能读懂——每个事件名标记的是漏斗的一个断面。

**🎯 LLC 是谁：Last-Level Cache = L3**

以 Core Ultra 7 255H 的 P-core 为例，一次 load（如 `addss (%rax),%xmm0` 发出的读请求）逐级向下找数据：

```
load 请求
  ↓ L1d 命中？（48 KB/核，~5 周期）────命中 → 结束（绝大多数停在这）
  ↓ miss
  ↓ L2 命中？（~3 MB/核，~16 周期）───命中 → 结束
  ↓ miss                          ←── 走到这一步，计入 LLC-loads
  ↓ L3/LLC 命中？（24 MB 全核共享，~50-70 周期）─命中 → 结束
  ↓ miss                          ←── 走到这一步，计入 LLC-load-misses
  ↓ DRAM（~300-400 周期）
```

**🎯 三个指标的精确定义**

- `LLC-loads`：L1、L2 都没找到、不得不来问 L3 的 load 次数——不是"总访存次数"，而是**穿透了前两级的漏网之鱼**
- `LLC-load-misses`：连 L3 也没找到、必须去 DRAM 的次数——整个层次里**代价最高的事件**，性能优化的头号观察对象
- LLC miss rate = misses ÷ loads，即 perf 注释里的 `# xx% of all LL-cache accesses`

用题 2 实测数据验证漏斗形状：总 load ≈ 7 亿（`L1-dcache-loads`）→ LLC-loads 84 万 → LLC-load-misses 66 万。**99.9% 的 load 在 L1/L2 就解决了**，逐级递减是健康程序的常态；性能问题 = 漏斗下游突然变粗。

**⚠️ LLC-loads 少 ≠ 访存少**

blocked 版照样执行了 6.4 亿次 load 指令，只是几乎全被 L1/L2 消化，轮不到 L3 出场（LLC-loads 仅 12 万）。看缓存行为要从 L1 往下逐级看，每一级都是上一级的漏网。

**⚠️ miss rate 的分母陷阱**

题 2 里 blocked 版 miss rate 47.7% 看似不低，但分母只有 pass 版的 1/7，绝对量才 5.7 万次。**分母不同量级时比率没有对比意义，对比实验先看绝对计数。**

**⚠️ 这些事件默认只统计 demand load，不含硬件预取**

pass 版扫了 2.56 GB 内存，按 cache line 算理论应有约 4 亿次 miss，实测 demand miss 只有 66 万——预取器抢先把数据搬到位，miss 记到了预取请求头上。所以 demand miss 低只说明"延迟被藏住了"，不代表内存流量小。

**🎯 通用事件名是跨平台抽象**

`LLC-loads` 是 perf 的 generic event，运行时翻译成本机微架构的原始事件（`perf list --details` 可看映射）。不同 CPU 上语义有细微差别，绝对值跨机器不可比；同机对比两个程序版本才是它的主战场。

**🔧 工程判读口诀**

- IPC 低 + LLC-load-misses 高 → memory-bound，优化数据布局和访问顺序（第 6 章）
- IPC 低 + LLC miss 少 → 瓶颈在别处（分支预测、依赖链、TLB……），换事件继续排查（第 5 章 topdown 方法）
- IPC ≥ 1.5 且 miss 少 → 计算瓶颈，去看向量化和指令级并行

---

## 用 /proc 统计缺页次数（关注点 ③）

**🎯 数据源：`/proc/<pid>/stat` 的第 10/12 字段**

内核为每个进程维护两个累计计数器，暴露在 `/proc/<pid>/stat`（按空格分隔的字段，1 起数）：

| 字段序号 | 名称 | 含义 |
|---------|------|------|
| 10 | `minflt` | **minor fault**：缺页但不需要磁盘 I/O——物理页其实已在内存（零页、页缓存、COW），只需修一下 PTE |
| 12 | `majflt` | **major fault**：缺页且需要读磁盘——真正的"从磁盘换入"，代价 ms 级 |

```bash
# 看任意进程的累计缺页数（注意：comm 字段可能含空格，字段序号可能被打偏，
# 稳妥做法是先用 sed 去掉前两个字段再数）
sed 's/.*) //' /proc/<pid>/stat | awk '{print "minflt="$7, "majflt="$9}'

# 更省事的等价方式：ps 直接帮你解析好
ps -o pid,min_flt,maj_flt,comm -p <pid>
```

**🎯 在自己的程序里读：观察 demand paging 的现场**

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* 读 /proc/self/stat 的 minflt/majflt
 * 坑：第 2 个字段 comm 形如 (a.out)，本身可能含空格和括号，
 *     必须用 strrchr 找最后一个 ')' 再往后解析，不能傻 split */
static void show_faults(const char *tag) {
    char buf[512];
    FILE *f = fopen("/proc/self/stat", "r");
    fgets(buf, sizeof buf, f);
    fclose(f);
    char *p = strrchr(buf, ')') + 2;            /* 跳到第 3 个字段 state */
    long minflt, majflt;                        /* 跳过 state ppid pgrp session tty tpgid flags */
    sscanf(p, "%*c %*d %*d %*d %*d %*d %*u %ld %*u %ld", &minflt, &majflt);
    printf("[%-12s] minflt=%-8ld majflt=%ld\n", tag, minflt, majflt);
}

int main(void) {
    show_faults("start");

    size_t sz = 256 * 1024 * 1024;              /* 256 MB = 65536 个 4KB 页 */
    char *p = malloc(sz);
    show_faults("after malloc");                /* minflt 几乎不变：只登记映射 */

    for (size_t i = 0; i < sz; i += 4096)
        p[i] = 1;                               /* 逐页触摸，每页第一次写触发一次缺页 */
    show_faults("after touch");                 /* minflt 暴涨约 65536 */

    for (size_t i = 0; i < sz; i += 4096)
        p[i] = 2;
    show_faults("touch again");                 /* 第二遍几乎不涨：页已在内存 */
    return 0;
}
```

预期输出（数量级）：

```
[start       ] minflt=150      majflt=0
[after malloc] minflt=152      majflt=0      ← malloc 不分配物理页
[after touch ] minflt=65690    majflt=0      ← 每 4KB 页恰好一次 minor fault
[touch again ] minflt=65692    majflt=0      ← demand paging 只发生一次
```

**🎯 配套工具交叉验证**

```bash
/usr/bin/time -v ./a.out      # 输出含 Major/Minor page faults 两行（注意不是 shell 内置 time）
perf stat -e page-faults,minor-faults,major-faults ./a.out
```

程序内还可以用 `getrusage(RUSAGE_SELF, &ru)` 拿 `ru.ru_minflt`/`ru.ru_majflt`，数据源相同。

**⚠️ 怎么才能看到 major fault**

日常程序 majflt 几乎总是 0，因为文件大概率已在页缓存里。想制造 major fault：`sync && echo 3 | sudo tee /proc/sys/vm/drop_caches` 清掉页缓存后，再 mmap 读一个大文件——首次访问每个页都要真正读磁盘。

---

## 易错点

- 虚拟内存不是"内存不够拿硬盘凑"的交换区，它首先是地址空间抽象，每次访存都在做地址翻译，与是否 swap 无关
- 缺页是正常机制不是错误，demand paging 全靠它工作；只有访问未分配或无权限的页，缺页处理程序才升级成 SIGSEGV
- `malloc` 成功只是登记了虚拟页映射，物理页要等第一次触摸时缺页才分配，所以 malloc 大块内存秒回且 RSS 不涨
- minor fault 不碰磁盘（修 PTE 即可），major fault 才要等磁盘 I/O，看缺页统计必须区分这两列
- DRAM 缓存是全相联 + 写回 + 大页块，所有设计都由"miss 代价是 ms 级"这一点逆推出来，不要拿 SRAM 缓存的直觉套
- 时间局部性看的是重用距离（两次重访之间隔了多少不同数据）而不是重复次数，访问集合相同、顺序不同，局部性可以天差地别
- 脏页的"脏"是相对后备存储而言（内存副本比磁盘新），不是相对其他进程；匿名脏页只能写回 swap，无 swap 时不可驱逐，内存紧张只能 OOM
- CSAPP 的单级页表是教学简化，真实 x86-64 是 4/5 级基数树，否则每进程仅页表就要 512 GB
- 解析 `/proc/<pid>/stat` 不能直接按空格 split，第 2 个字段 `(comm)` 可能含空格，要从最后一个 `)` 之后再数字段
- 只读不写的全局大数组（.bss）所有页都映射到内核共享零页，物理内存几乎为零，做 cache/内存 benchmark 必须先写入初始化，否则测的根本不是真实内存流量
- 混合架构 CPU（P-core + E-core）上 perf stat 会按 PMU 分三组输出，括号里是该组计数的时间覆盖率，覆盖率极低（如 1.25%）的行是按比例外推的噪声，只能信覆盖率高的那组

## 工程关联

- 每个进程一套页表，根指针在 `task_struct->mm->pgd`；上下文切换就是把它写进 CR3 寄存器——"私有地址空间"假象的硬件落点
- `_PAGE_DIRTY`/`_PAGE_ACCESSED` 由 CPU 硬件置位，内核换页算法（LRU 近似）和"换出前是否写回"全靠这两位，这是 PTE 作为软硬件契约的典型例子
- 栈不可执行（防代码注入攻击）的实现就是 PTE 第 63 位 `_PAGE_NX`，`readelf -l` 里 GNU_STACK 段的 RW 标志最终落到这一位
- 线上服务"越跑越卡、CPU 不高、磁盘狂闪"优先怀疑抖动：`ps -o min_flt,maj_flt` 或 `sar -B` 看 majflt 速率，工作集超物理内存就是实锤
- `perf stat -e minor-faults` 数量异常大的服务，常见原因是频繁 malloc/free 大块内存导致页面反复归还内核又重新缺页（glibc 的 `M_MMAP_THRESHOLD` 行为）
- 数据库/科学计算的 blocking、批处理本质都是压缩重用距离到缓存容量内，是时间局部性判定标准的直接应用

## 实验题

**🧪 题 1：用 /proc/self/stat 观察 demand paging**

使用上文 `show_faults` 完整程序：

要求：

- 编译运行，确认四个观测点的 minflt 变化符合预期（malloc 后不涨、首次触摸暴涨约 sz/4096、二次触摸不涨）
- 把触摸步长从 4096 改成 8192，验证 minflt 增量减半——证明缺页以页为粒度
- 用 `/usr/bin/time -v ./a.out` 交叉验证 minor fault 总数

**🧪 题 2：时间局部性 perf 对比**

使用上文 `pass_then_repeat` / `blocked_repeat` 两个版本：

要求：

- `gcc -O1` 编译，命令行参数选择跑哪个版本（避免一次进程里互相污染缓存）
- `perf stat -e LLC-load-misses,LLC-loads,instructions,cycles` 分别测量
- 验证：instructions 基本相同，LLC-load-misses 相差约 10 倍；用"重用距离 vs LLC 容量"解释这个倍数（`getconf -a | grep CACHE` 查本机 LLC 大小）

**🧪 题 2 实测结果（2026-06-11，Core Ultra 7 255H，gcc -O1，数组已写入初始化）**

只取 `cpu_core` 组（覆盖率 ~92%，`cpu_atom` 覆盖率 <9% 为外推噪声）：

| 指标 | pass（局部性差） | blocked（局部性好） | 倍数 |
|------|------|------|------|
| LLC-load-misses | 663,558 | 56,606 | **11.7×** |
| LLC-loads | 842,994 | 118,679 | 7.1× |
| LLC miss rate | 78.7% | 47.7% | — |
| instructions | 3.25 G | 3.31 G | ≈1（前提成立） |
| IPC | 1.49 | 1.80 | 1.2× |
| 墙钟时间 | 0.437 s | 0.371 s | 1.18× |

结论与解读：

- **LLC-load-misses 绝对值差 11.7 倍**是局部性差异的直接证据：pass 版重用距离 256 MB ≫ LLC（24 MB），每遍重访都 miss；blocked 版重用距离 1 MB ≪ LLC，第 2~10 遍全命中
- **miss rate（78.7% vs 47.7%）反而有误导性**：两边分母差 7 倍，blocked 的 47.7% 是"小分母上的大比率"，绝对量才 5.7 万次。对比实验先看绝对计数，比率只做辅助
- **instructions 几乎相同（差 2%）是对比实验的合法性前提**：两版做同样多的工作，差异只来自访存模式
- **墙钟只差 18%，远小于 miss 数的 11.7 倍**：顺序访问被硬件预取器掩盖，miss 的延迟代价没有暴露在关键路径上（第 6 章 Memory Mountain 实验会用随机访问打败预取器）
- 输出 16777216.000000 = 2²⁴：float 有效位数 24 bit，sum 累加到 2²⁴ 后再 `+1.0f` 因舍入丢失，结果不再增长——呼应 §2.4 浮点精度

**🧪 题 3：制造并观察 major fault**

```c
/* mmap 读一个大文件的第一个字节 × 每页 */
int fd = open("bigfile", O_RDONLY);
char *m = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
long sum = 0;
for (off_t i = 0; i < st.st_size; i += 4096) sum += m[i];
```

要求：

- 用 `dd if=/dev/urandom of=bigfile bs=1M count=512` 造一个 512 MB 文件
- 第一次直接跑，记录 majflt（文件刚写完，大概率在页缓存里，majflt ≈ 0、minflt 很大）
- `sync && echo 3 | sudo tee /proc/sys/vm/drop_caches` 后再跑，majflt 应暴涨到约 512MB/4KB 量级（实际会因预读 readahead 少一些），对比两次运行的耗时差距
- 解释：同一段代码，minor 和 major 的差别只在"页缓存里有没有"

**🧪 题 4：在内核头文件里找到页表类型定义**

要求：

- `dpkg -l | grep linux-headers` 确认本机装了内核头文件（没有则 `sudo apt install linux-headers-$(uname -r)`）
- 在 `/usr/src/linux-headers-$(uname -r)/arch/x86/include/asm/` 下 grep 出 `pgd_t`、`pte_t` 的 typedef 和 `_PAGE_BIT_PRESENT` 的定义，和本文核对
- 数一数 `pgtable_64_types.h` 里 PGDIR_SHIFT/PUD_SHIFT/PMD_SHIFT/PAGE_SHIFT 的值（48 位下应为 39/30/21/12），验证"每级 9 位索引 + 12 位页内偏移"的拆分
