# 8. 观测与实验

前面 01–07 讲机制，本章上机验证：5 个可跑的程序（`experiments/`）+ `/proc` 观测点 + 内核侧追踪，把
「读源码想象」变成「亲眼看到数字」。每个实验都注明它实证了哪条机制。

## 8.1 平台边界（务必先读）

⚠️ **实验在 x86-64 / 内核 6.17 本机跑，文档主体按 ARM64 写——这不矛盾**：

- 实验测的全是**架构无关行为**：缺页计数（minflt/majflt）、COW 复制页数、zero page 别名、脏页水位。
  这些由通用 `mm/*.c` 决定（`handle_pte_fault`/`do_anonymous_page`/`do_wp_page`/`balance_dirty_pages`），
  ARM64 与 x86 是同一份代码，**机制相同、数值因平台/内存大小而异**。
- 架构相关的只是进内核的入口（x86 `exc_page_fault` / ARM64 `el0_da→do_page_fault`，见 `03`），实验不依赖它。
- 所以：把实验结果当「这条通用机制确实如此」的证据，别当「ARM64 上的精确数值」。

编译运行（在 `experiments/` 目录内）：

```bash
cd experiments
gcc -O2 -Wall -Wextra zero_page.c    -o zero_page    && ./zero_page
gcc -O2 -Wall -Wextra vmstat_fault.c -o vmstat_fault && ./vmstat_fault
gcc -O2 -Wall -Wextra cow_trace.c    -o cow_trace    && ./cow_trace
gcc -O2 -Wall -Wextra dirty_writeback.c -o dirty_writeback && ./dirty_writeback 12
bash check_kernel_structs.sh
```

## 8.2 实验 1：核对内核结构 + 看清版本演进（`check_kernel_structs.sh`，免 root）

**实证**：`01` 的数据结构 + 「5.10 → 6.x」的版本演进——同一批符号，在**本机 6.17 头文件**和
**kernel-src（5.10）**里查，结果不同，正好把演进看清。

本机（x86-64 / 6.17）跑脚本的结果：

```
grep 本机 /lib/modules/$(uname -r)/build 头文件树（6.17）：
  mm_struct.mm_mt (maple tree)   → 命中    ← 6.x 已用 maple tree
  旧红黑树 mm_rb                  → 缺失    ← 6.x 已删
  vm_area_struct.vm_next         → 缺失    ← 6.x 已删链表
  struct folio                   → 命中    ← 6.x 已引入 folio
  pgd_offset / pte_offset_kernel → 命中    （02 的逐级下钻宏）
  struct anon_vma / anon_vma_chain → 命中  （05 的 rmap）
```

⚠️ **关键对照**：本说明书主体对照的 `kernel-src`（RK3562 / **5.10**）里查同样的符号，结果**相反**——
`mm_rb` 命中、`mm_mt` 缺失、`vm_next` 命中、无 `struct folio`：

```
grep kernel-src（5.10）：
  grep -n 'struct rb_root mm_rb'   kernel-src/include/linux/mm_types.h   → 命中（mm_struct 用红黑树）
  grep -n 'vm_next'                kernel-src/include/linux/mm_types.h   → 命中（VMA 还有链表）
  grep -n 'struct maple_tree mm_mt' kernel-src/include/linux/mm_types.h  → 缺失（5.10 还没 maple tree）
  grep -n 'struct folio {'         kernel-src/include/linux/mm_types.h   → 缺失（5.10 还没 folio）
```

🎯 **结论**：两棵树一对照，`01`/`05`/`06` 里讲的「演进注」就具象了——maple tree（6.1）、folio（5.16）是
本机 6.17 的现实，而文档正文与行号对照的 5.10 源码用的还是 `mm_rb`+链表+`struct page`。**看文档配 kernel-src
是 5.10；想看 6.x 新结构就 grep 本机头文件。**

## 8.3 实验 2：zero page 别名（`zero_page.c`，免 root）

**实证**：`04` 的 `do_anonymous_page` 只读分支 / zero page；并关掉「零页别名惩罚」遗留问题。

`mmap` 256MB 匿名内存，分「只读遍历」「逐页写」两阶段，各读 `/proc/self/statm` 的 RSS 与
`/proc/self/stat` 的 minflt。本机实测：

```
                     RSS 增量        minflt 增量
映射后(未触碰)        基线            —
只读遍历 256MB       +132 KB         +65537      ← 几乎不涨：全部别名到共享 ZERO_PAGE，不计 RSS
逐页写 256MB        +262144 KB      +65536      ← 写一遍才分配真实页，RSS 涨满
majflt 全程 0（匿名页不读盘）
```

🎯 **结论**：只读匿名页全部别名到同一物理 ZERO_PAGE（`do_anonymous_page` 只读分支），不占 RSS 却每页
一次 minor fault。**拿未初始化数组做 cache/内存基准，测的是 zero page 别名而非真实内存流量——基准前必须
先写一遍数组。**（这正是 §9.1-9.5 locality 实验里 blocked 版本数据失真的根因。）

## 8.4 实验 3：minor vs major fault（`vmstat_fault.c`，免 root）

**实证**：`03` 的缺页分类、`04` 的 `do_fault` + fault-around/readahead。

① `malloc`+`memset` 128MB 造 minor；② 写 128MB 文件 → `fsync` → `posix_fadvise(DONTNEED)` 丢缓存 →
`mmap` + `madvise(MADV_RANDOM)` 后逐页读造 major。本机实测：

```
minor 段（匿名写）:  minflt +32774   majflt 0          ← do_anonymous_page 写分支
major 段（文件读）:  majflt +32768   minflt 0          ← do_fault 从磁盘读回，恰好 = 页数
```

⚠️ **关键观察**：必须 `MADV_RANDOM`！不加时 32768 页只触发 1 次 major——readahead 预读 + fault-around
（`04`）把顺序读的 major 几乎全转成 minor。这本身就是个值得记住的真实优化。

## 8.5 实验 4：COW 计数（`cow_trace.c`，免 root + 进阶追踪）

**实证**：`04` 的 `do_wp_page` + fork 的 `copy_page_range`——COW 在第一次写、按单页发生。

父进程写满 64MB 后 fork，子进程留 2 秒 attach 窗口再改写，用 `getrusage` 读 minflt：

```
[child] 只读阶段 minflt 增量 :     0      ← 共享父页，零复制
[child] 改写阶段 minflt 增量 : 16384      ← = 64MB/4KB，每页第一次写一次 do_wp_page
```

**进阶（需 root）**：另开终端在 `do_wp_page` 下探针，跑 `cow_trace` 后看计数 ≈ 16384，与用户侧 minflt 对上：

```bash
! sudo bpftrace -e 'kprobe:do_wp_page { @[comm] = count(); }'
```

## 8.6 实验 5：脏页回写水位（`dirty_writeback.c`，免 root）

**实证**：`07` 的 balance_dirty_pages 反馈限流 + per-bdi flusher。

持续写文件不 fsync，每 256MB 打印本轮吞吐 + `/proc/meminfo` 的 Dirty/Writeback。本机（32GB/NVMe ext4，写 12GB）：

```
已写(MB) | 吞吐MB/s | Dirty(MB) | Writeback(MB)
     768 |   6068   |    772    |     0      ← 纯 page cache 吸收，Dirty 线性涨
    1024 |   2983   |   1016    |     0      ← 吞吐腰斩：balance_dirty_pages 软节流
    2304 |   4498   |   2164    |     4      ← Writeback 非 0：flusher 介入
    6144 |   5262   |   2681    |    24      ← Dirty 稳在 ~2.2-3.1GB 平台，写入与回写平衡
   12288 |   4856   |   3097    |    16
```

⚠️ NVMe 回写够快，全程稳在 background 水位、没撞 limit（慢盘才会看到吞吐被硬拽到磁盘带宽）。阈值基于
可脏内存而非 MemTotal，故平台低于 32GB×10%。

## 8.7 观测点速查

| 想看什么 | 命令 / 文件 | 对应章节 |
|---------|------------|---------|
| 地址空间的 VMA | `cat /proc/<pid>/maps` | 01 |
| 每 VMA 的 RSS/共享/私有/swap | `cat /proc/<pid>/smaps` | 01 / 06 |
| 进程缺页计数 | `/proc/<pid>/stat` 的 minflt/majflt、`getrusage` | 03 / 04 |
| 全局缺页 | `/proc/vmstat` 的 `pgfault`/`pgmajfault` | 03 |
| 回收压力 | `/proc/vmstat` 的 `pgsteal_direct`/`pgscan_direct`/`pswpin`/`pswpout` | 06 |
| 脏页/回写 | `/proc/meminfo` 的 `Dirty`/`Writeback`；`/proc/vmstat` 的 `nr_dirty`/`nr_writeback` | 07 |
| 被脏页限流的进程 | `ps -eo pid,stat,wchan:32,comm \| grep balance_dirty` | 07 |

## 8.8 内核侧追踪（进阶，需 root）

文档里的调用链可以用动态追踪亲眼看到触发：

```bash
# 数某内核函数被调次数
! sudo bpftrace -e 'kprobe:handle_mm_fault { @=count(); }'
! sudo bpftrace -e 'kprobe:do_wp_page      { @=count(); }'
! sudo bpftrace -e 'kprobe:try_to_unmap    { @=count(); }'
! sudo bpftrace -e 'kprobe:balance_dirty_pages { @=count(); }'

# 抓一次缺页的完整内核调用栈，对照 03/04 的调用链
! sudo trace-cmd record -p function_graph -g handle_mm_fault ./zero_page
! sudo trace-cmd report | head -60
```

> x86 本机的函数名与本文（ARM64 视角）的通用层函数一致（`handle_mm_fault`/`do_wp_page`/`try_to_unmap`/
> `balance_dirty_pages` 都在 `mm/*.c`）；只有最外层异常入口名不同（x86 `exc_page_fault`）。
