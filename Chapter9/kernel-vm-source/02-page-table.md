# 2. 页表与地址翻译

CSAPP §9.6 讲多级页表把虚拟地址切成几段、逐级索引、最后拼上页内偏移得到物理地址。本章把这套机制落到
ARM64 内核：四级页表的位域切分、内核逐级下钻的 `*_offset` 宏、以及一个 ARM64 PTE 的真实位布局。
MMU 硬件每次访存都走这条翻译路径（有 TLB 加速），内核只在缺页等慢路径上用软件走一遍同样的逻辑。

## 2.1 ARM64 四级页表

ARM64（4KB 页、48 位虚拟地址）把 VA 切成 4 个 9 位索引 + 12 位页内偏移：

```
虚拟地址 (48 位, 4KB 页)
 47        39 38        30 29        21 20        12 11                0
┌──────────┬──────────┬──────────┬──────────┬────────────────────────┐
│  PGD idx │  PUD idx │  PMD idx │  PTE idx │      Page Offset       │
│  (9 bit) │  (9 bit) │  (9 bit) │  (9 bit) │      (12 bit)          │
└────┬─────┴────┬─────┴────┬─────┴────┬─────┴────────────────────────┘
     │          │          │          │
     ▼          ▼          ▼          ▼
   TTBR0 → PGD[512] → PUD[512] → PMD[512] → PTE[512] ──→ 物理页 + offset
            (4KB)      (4KB)      (4KB)      (4KB)

每级页表：512 项 × 8 字节 = 4KB（恰好一页）；9 位索引正好选 512 项中之一。
```

🎯 **TTBR0 / TTBR1 双页表根**：ARM64 用两个页表基址寄存器分隔用户态与内核态——`TTBR0_EL1` 指向当前进程
用户空间页表（即 `mm->pgd`），`TTBR1_EL1` 指向内核空间页表（全进程共享）。进程切换只改 `TTBR0`，
内核映射不动。这就是 §9.7「内核虚拟内存映射进每个进程页表顶部」的 ARM64 实现——不是塞进同一棵树，
而是另一个寄存器指另一棵树，高位地址走 `TTBR1`、低位走 `TTBR0`。

> 对比 x86-64：只有一个 `CR3` 指向统一的页表根，内核/用户在同一棵树里用 PTE 的 U/S 位区分。这是
> `02` 里少数几个真正架构相关的点之一。

## 2.2 内核逐级下钻：`*_offset` 宏

§9.6 的「逐级索引」在代码里就是一串 `*_offset` 宏，从 `mm->pgd` 一路走到 PTE。这段逻辑**架构无关**
（`include/linux/pgtable.h`），ARM64 与 x86 共用：

```c
// 给定 mm 与虚拟地址 addr，走到最后一级 PTE（概念版，省略 *_none/巨页/分配判断）
pgd = pgd_offset(mm, addr);          // 1) mm->pgd + PGD 索引位
p4d = p4d_offset(pgd, addr);         // 2) 第 4 级（ARM64 4 级时此级折叠，恒等下传）
pud = pud_offset(p4d, addr);         // 3) PUD 索引
pmd = pmd_offset(pud, addr);         // 4) PMD 索引
pte = pte_offset_kernel(pmd, addr);  // 5) 最后一级，得到 pte_t*
```

缺页处理里这条链由 `__handle_mm_fault()` 走（见 `03`），但用的是「分配版」`p4d_alloc/pud_alloc/
pmd_alloc/pte_alloc`——缺页时中间级页表可能还不存在，要边走边分配：

```
mm/memory.c:__handle_mm_fault()          [行 4859]
  ├── pgd = pgd_offset(mm, address)      # 取 PGD 条目（PGD 随 mm 一起在 fork 时建）
  ├── p4d = p4d_alloc(mm, pgd, address)  # ARM64 折叠为恒等
  ├── pud = pud_alloc(mm, p4d, address)  # 不存在则分配一页做 PUD
  ├── pmd = pmd_alloc(mm, pud, address)  # 不存在则分配一页做 PMD
  └── handle_pte_fault(vmf)              [行 4728]   # 进入 PTE 级处理（→ 03）
```

⚠️ **P4D 折叠**：Linux 页表抽象统一为五级（PGD→P4D→PUD→PMD→PTE）以兼容 x86-64 的 5 级页表（LA57）。
ARM64 用 4 级，第 4 级（P4D）在编译期被「折叠」成恒等操作（`p4d_offset` 直接返回传入的 pgd 指针），
不占实际内存。所以你在 ARM64 代码里看到 P4D 一层但它是个透明的空壳。

## 2.3 页表项类型：用 struct 包一层裸值

页表项类型不是裸 `unsigned long`，而是单字段结构体（`arch/arm64/include/asm/pgtable-types.h`）：

```c
typedef struct { pteval_t pte; } pte_t;   // 一个 PTE
typedef struct { pmdval_t pmd; } pmd_t;   // 一个 PMD 条目
typedef struct { pgdval_t pgd; } pgd_t;   // 一个顶级条目
```

🎯 **类型安全设计**：故意包成结构体，让编译器**拒绝**把 `pmd_t` 当 `pte_t` 用、或把页表项与普通整数
随意互转。各级条目底层都是 64 位但语义不同，混用是历史上常见的内核 bug 源。取值用 `pte_val(x)`、
构造用 `__pte(v)`，绕不过类型检查。这是 C 项目「零成本封装换类型安全」的经典手法。

## 2.4 ARM64 的 PTE 位布局

一个 ARM64 PTE（4KB 粒度）的关键位：

```
PTE (页表项, 64 位)
 63    55 54        12 11 10  9  8  7  6  5      2  1  0
┌──────┬─────────────┬──┬──┬───┬────┬───┬────────┬──┬──┐
│ 保留  │  输出物理地址 │  │AF│SH │ AP │NS │ ATTRidx│  │ V│
└──────┴─────────────┴──┴──┴───┴────┴───┴────────┴──┴──┘
  V    : Valid，页是否在内存（对应 §9.3 的有效位 / x86 的 _PAGE_PRESENT）
  AF   : Access Flag，硬件在首次访问时置位（LRU 回收用它判断「最近被访问」→ 06）
  AP   : Access Permission（00=RW@EL1, 01=RW@all, 10=RO@EL1, 11=RO@all；只读/可写 → COW 靠它）
  SH   : Shareability（多核间缓存一致性域）
  ATTRidx: 内存属性索引（查 MAIR_EL1，决定 cacheable/device 等）
  PXN/UXN(高位): 特权/非特权「永不执行」（对应 §3.10 的 NX 位、x86 的 _PAGE_NX）
```

🎯 **PTE 是软硬件的契约**：硬件在翻译时读 V/AP/AF 决定能不能访问、置 AF；内核在缺页/回收时改 V/AP
（建/撤映射、设只读触发 COW）、读 AF（判冷热）。CSAPP §9.5 的「权限位 + MMU 越权检查 → 故障」
就发生在这里：AP 不允许写却写了 → MMU 产生权限故障 → 进 `do_page_fault`（→ 03）。

内核操作 PTE 的内联函数（`arch/arm64/include/asm/pgtable.h`，架构相关，但与 x86 同名同义）：

```c
pte_present(pte)   // V 位：页在内存？
pte_write(pte)     // AP：可写？        do_wp_page 判断 COW 用它（→ 04）
pte_dirty(pte)     // 脏页？           回写判断用它（→ 07）
pte_young(pte)     // AF：最近访问过？   LRU 回收用它（→ 06）
pte_mkwrite(pte)   // 置可写
pte_mkdirty(pte)   // 置脏
pte_mkyoung(pte)   // 置访问位
set_pte_at(mm, addr, ptep, pte)  // 把构造好的 PTE 写入页表
```

## 2.5 大页：减少 TLB 压力

每个 VA 翻译要走 4 级、最坏 4 次访存（TLB 未命中时）。一个 TLB 条目只覆盖一个 4KB 页，访问大数据集
TLB 频繁失效。ARM64 支持在中间级直接落地，跳过下面的层、一个条目覆盖更大范围：

```
普通 4KB 页:   PGD → PUD → PMD → PTE → 4KB 物理页
PMD 大页(2MB): PGD → PUD → PMD ─────→ 2MB 物理块   (PMD 条目直接指物理块, 省一级)
PUD 大页(1GB): PGD → PUD ───────────→ 1GB 物理块   (PUD 条目直接指物理块, 省两级)
```

收益：一个 TLB 条目覆盖 2MB/1GB，大幅提高 TLB 命中率。代价：分配要连续大块物理内存（依赖 compaction）。
透明大页（THP）在缺页时由 `create_huge_pmd()` 尝试直接建 2MB 大页（见 `03` 的 `__handle_mm_fault`）。
观测：`/proc/meminfo` 的 `AnonHugePages`、`/sys/kernel/mm/transparent_hugepage/`。
