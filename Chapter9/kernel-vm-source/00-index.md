# Linux 内核虚拟内存子系统说明书

> 配套 CSAPP 第 9 章。这不是书本章节总结，而是一份**按子系统分类、代码对照**的说明书：
> 把「虚拟内存的每个机制」落到 Linux 内核里**具体的数据结构与函数调用链**，让你看代码时知道
> 「哪段代码在做书上讲的哪件事」。

## 基准与边界声明（先读）

| 维度 | 取值 | 说明 |
|------|------|------|
| **架构基准** | ARM64 | 与源码树 (RK3562/ARM64) 对齐。异常入口、页表级数、PTE 位、Zone 类型按 ARM64 写。 |
| **内核基准** | **5.10.226** | 与本目录 `kernel-src/`（软链接到 RK3562 内核树）**完全一致**：VMA 用 `mm_rb` 红黑树 + `mmap` 链表、物理页是 `struct page`（无 folio）、`try_to_unmap(struct page *)`。6.x 的演进（maple tree、folio）在相关处以「演进注」点出，不作主体。 |
| **实验平台** | x86-64 / 6.17 本机 | `experiments/` 的程序在 x86-64 本机实测。测的是**架构无关行为**（缺页计数、COW 页数、zero page、脏页水位），ARM64/5.10 上机制相同、数值因平台而异。 |
| **行号** | 对应 `kernel-src/`，可直接定位 | 调用链里的 `[行 N]` 就是 `kernel-src/<文件>` 的第 N 行（5.10.226）。看文档时 `vim kernel-src/mm/memory.c +5307` 即可跳到对应代码。 |

🔗 **源码就在手边**：`kernel-src/` 是软链接，指向 RK3562 的 Linux 5.10.226 内核树。文档里凡是
`mm/memory.c:handle_mm_fault() [行 5307]` 这样的引用，都能直接 `kernel-src/mm/memory.c` 第 5307 行找到原文——
边读文档边对照真实代码。（该软链接已 gitignore，不入库。）

⚠️ **核心要点**：内核 mm 的「上半身」是架构无关的——`handle_mm_fault → handle_pte_fault → do_anonymous_page / do_wp_page / do_swap_page`、`balance_dirty_pages`、`try_to_unmap`、LRU/kswapd 全在通用的 `mm/*.c`，ARM64 与 x86 是同一份代码。架构差异只在「下半身」：异常如何进内核、页表几级、PTE 位怎么排。所以**文档讲 ARM64、实验跑 x86，对核心路径不矛盾**。

## 文档索引

| 文档 | 主题 | 对应 CSAPP |
|------|------|-----------|
| [01-address-space.md](01-address-space.md) | 地址空间组织：`mm_struct` / `vm_area_struct` / 红黑树+链表 / `struct page` | §9.7 进程地址空间 |
| [02-page-table.md](02-page-table.md) | 页表与地址翻译：ARM64 四级页表、逐级下钻、PTE 位 | §9.3.2 / §9.6 |
| [03-page-fault.md](03-page-fault.md) | 缺页主线：异常入口 → `handle_mm_fault` → `handle_pte_fault` 分流 | §9.7.2 缺页处理 |
| [04-demand-paging-and-cow.md](04-demand-paging-and-cow.md) | demand paging（zero page）、文件页缺页、写时复制 COW | §9.8 内存映射 |
| [05-rmap.md](05-rmap.md) | 反向映射：从物理页反查谁在用它 | （书本未讲，回收前提） |
| [06-reclaim-and-swap.md](06-reclaim-and-swap.md) | 页回收：LRU、kswapd、水位、swap 换入 | §9.3 DRAM 缓存磁盘 |
| [07-dirty-writeback.md](07-dirty-writeback.md) | 脏页回写：per-bdi flusher、`balance_dirty_pages` 限流 | §6.4 写回策略的内核落地 |
| [08-observation-and-experiments.md](08-observation-and-experiments.md) | 观测与实验：4 个可跑程序 + 实测数据 + `/proc` + 追踪 | 全部机制的实证 |

阅读顺序：先 `01`→`02` 建立「地址空间 + 页表」静态视图，再 `03`→`04` 看「缺页」这条动态主线，然后 `05`→`06`→`07` 进回收/回写的深水区，最后 `08` 上机验证。

## 涉及的内核源码地图

以下路径均在 `kernel-src/` 下（如 `kernel-src/mm/memory.c`）：

```
通用代码（架构无关，ARM64 与 x86 共用）
├── mm/mmap.c            # VMA 创建/合并/拆分、do_mmap、地址空间维护       → 01
├── mm/memory.c          # 缺页核心：handle_mm_fault / handle_pte_fault    → 03,04
│                        #          do_anonymous_page / do_wp_page / do_fault / do_swap_page
├── mm/rmap.c            # 反向映射：try_to_unmap / page_referenced        → 05
├── mm/vmscan.c          # 页回收：kswapd / shrink_node / shrink_page_list  → 06
├── mm/swap.c            # LRU 链表管理（active/inactive 流转）             → 06
├── mm/swapfile.c        # swap 分区/槽位管理                              → 06
├── mm/page-writeback.c  # 脏页回写：balance_dirty_pages / 水位            → 07
├── fs/fs-writeback.c    # flusher worker：wb_workfn / __mark_inode_dirty  → 07
└── include/linux/
    ├── mm_types.h       # mm_struct / vm_area_struct / struct page        → 01
    ├── mm.h             # struct vm_fault / handle_mm_fault 声明          → 03
    ├── rmap.h           # anon_vma / anon_vma_chain                       → 05
    └── pgtable.h        # pgd_offset 等下钻宏                             → 02

架构相关代码（ARM64）
├── arch/arm64/kernel/entry.S    # 异常向量表 el0_sync                      → 03
├── arch/arm64/mm/fault.c        # do_mem_abort / do_page_fault（C 入口）   → 03
└── arch/arm64/include/asm/
    ├── pgtable.h                # ARM64 PTE 构造/判定内联函数              → 02
    └── pgtable-hwdef.h          # PTE 位定义（AF/AP/PXN/...）              → 02
```
