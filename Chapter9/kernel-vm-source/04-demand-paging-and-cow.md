# 4. demand paging、文件页缺页与写时复制

接 `03` 的 `handle_pte_fault` 分流，本章钻进三条最常见的缺页分支：匿名页首次访问
（`do_anonymous_page`，demand paging 的核心）、文件页缺页（`do_fault`）、写时复制
（`do_wp_page`，COW）。它们对应 CSAPP §9.8 的内存映射机制——fork、execve、mmap 的行为全由这三条
分支拼出来。三个实验（[08](08-observation-and-experiments.md) 的题 2/3/4）正好量化这三条分支。

## 4.1 匿名页首次访问：do_anonymous_page

匿名 VMA（堆、栈、`.bss`、`malloc` 大块、`MAP_ANONYMOUS`）第一次缺页走这里。它按「读 / 写」分两条路，
这是理解 demand paging 与 zero page 的关键。

```
mm/memory.c:do_anonymous_page(vmf)              [行 3898]
  │
  ├── if (!(vmf->flags & FAULT_FLAG_WRITE))           # 只读缺页
  │     → entry = pte_mkspecial(pfn_pte(my_zero_pfn(addr), prot))   # 指向全局 ZERO_PAGE（只读）
  │     → set_pte_at(...)                              # 不分配实页！一次 minor fault，但不占 RSS
  │     → return                                       # 所有「只读未写」的匿名页共享同一物理零页
  │
  └── else                                             # 写缺页
        → page = alloc_zeroed_user_highpage_movable(vma, addr)  # 分配一个清零的真实物理页
        → __SetPageUptodate(page)
        → inc_mm_counter_fast(mm, MM_ANONPAGES)
        → page_add_new_anon_rmap(page, vma, addr)      # 建立反向映射（→ 05）
        → lru_cache_add_inactive_or_unevictable(page)  # 加入 inactive LRU（→ 06）
        → set_pte_at(mm, addr, pte, mk_pte(可写))      # 建立可写映射
```

🎯 **demand paging**：`malloc`/`mmap` 只建 VMA、不给物理页（§9.4「execve 只建映射不读盘」同理）。
真正分配发生在第一次**写**缺页这条路——分配清零页、加 rmap、加 LRU、建可写 PTE。

🎯 **zero page 优化**：第一次**读**一个还没写过的匿名页，内核不分配实页，而是让 PTE 指向全局共享的
`ZERO_PAGE`（只读）。这是一次 minor fault，但 `ZERO_PAGE` 是 special page，`vm_normal_page` 不认它，
**不计入进程 RSS**。

🔧 **实测对照（[08 实验 2](08-observation-and-experiments.md)，`zero_page.c`）**：只读遍历 256MB 匿名内存
RSS 仅涨 132KB、minflt 涨 65537（每页缺页一次走只读分支）；写一遍后 RSS 才涨满 256MB。
**这就解答了「零页别名惩罚」**——拿未初始化数组做 cache 基准，测的根本不是内存带宽，而是所有虚拟页别名到
同一物理零页。基准前必须先写一遍数组。

## 4.2 文件页缺页：do_fault

文件映射 VMA（`vm_file != NULL`，有 `vm_ops`）的缺页走 `do_fault`，按访问类型再分三路：

```
mm/memory.c:do_fault(vmf)                     [行 4495]
  │
  ├── if (读访问)
  │     → do_read_fault()
  │       ├── do_fault_around()               # fault-around：顺带预映射周围已在 cache 的页
  │       └── __do_fault()
  │             → vma->vm_ops->fault(vmf)      # 文件系统回调，如 filemap_fault
  │               → 命中 page cache：minor；未命中：从磁盘读 → MAJOR fault
  │
  ├── if (写访问 && 私有映射 MAP_PRIVATE)
  │     → do_cow_fault()                       # 私有文件映射写 → COW 到匿名页
  │       ├── alloc 新页 → __do_fault() 读原文件页 → copy_user_highpage()
  │       └── finish_fault()                   # 映射 COW 副本（改动不回原文件）
  │
  └── if (写访问 && 共享映射 MAP_SHARED)
        → do_shared_fault()
          ├── __do_fault() 读文件页
          ├── vma->vm_ops->page_mkwrite()      # 通知文件系统准备写
          ├── finish_fault()                   # 映射为可写
          └── balance_dirty_pages_ratelimited()  # 写脏后做脏页限流（→ 07）
```

🎯 **fault-around**：`do_read_fault` 里 `do_fault_around` 会在一次缺页时，把故障页**周围**若干页（默认 16 页）
中已经在 page cache 里的，一并建立 PTE 映射。好处：把多次 minor fault 合并成一次，减少缺页次数。

⚠️ **fault-around + readahead 会掩盖 major fault**：顺序 `mmap` 读文件时，预读（readahead）把后续页提前
读进 cache、fault-around 提前建好 PTE，于是大量本该是 major 的访问变成了 minor、甚至零 fault。
[08 实验 3](08-observation-and-experiments.md) 实测：不加干预时 32768 页只有 1 次 major；加 `MADV_RANDOM`
关掉预读/fault-around 后才看到 32768 次干净的 major fault。

🎯 **mmap 为什么可能比 read 快**：`do_fault` 直接把内核 page cache 的物理页映射进用户地址空间（建 PTE），
省掉 `read()` 的「page cache → 用户缓冲区」那次 CPU 拷贝；多进程映射同一文件还能共享这份 page cache。

## 4.3 写时复制：do_wp_page

写一个「存在但只读」的页（`pte_present && 写 && !pte_write`）触发 COW，`handle_pte_fault` 分流到这里：

```
mm/memory.c:do_wp_page(vmf)                    [行 3408]
  │
  ├── old_page = vm_normal_page(vma, addr, orig_pte)
  │
  ├── if (page_mapcount(old_page) == 1)        # 只有一个映射者：独占
  │     → wp_page_reuse()                      # 复用：无需复制，直接解写保护
  │       → pte = pte_mkwrite(pte_mkdirty(orig_pte))
  │       → set_pte_at()
  │
  └── else (多个映射者共享)
        → wp_page_copy()                       [行 3146]   # 复制：给自己一份私有副本
          ├── new_page = alloc_page_vma()      # 分配新页
          ├── cow_user_page(new_page, old_page) # 复制内容
          ├── page_add_new_anon_rmap(new_page) # 新页反向映射（→ 05）
          ├── set_pte_at_notify(new_page, 可写) # 本进程 PTE 改指新页
          └── page_remove_rmap(old_page)       # 旧页映射计数--，其他映射者继续共享
```

🎯 **复用 vs 复制**：COW 不总是复制——若发现这页**只剩自己在用**（别的进程已各自 COW 走或退出），直接
`wp_page_reuse` 解写保护、零复制。只有真有多个共享者时才 `wp_page_copy`。这避免了「最后一个用户也白复制一次」。

## 4.4 fork 怎么布置 COW

`do_wp_page` 的触发前提是「页被标只读且共享」，这是 fork 布置的。fork 不复制数据，只复制页表并把私有页
标只读：

```
fork → copy_process → dup_mmap                # 逐个复制 VMA，插入子进程的 mm_rb 红黑树 + mmap 链表
  └── copy_page_range → ... → copy_present_pte
        对可写的私有页：
          → ptep_set_wrprotect()              # 父、子两边 PTE 都改只读
          → 两边指向同一批物理页，page->_mapcount++
```

于是父子初始共享全部物理页（PTE 全只读）；任一方写某页 → 触发 `do_wp_page` → 复制那一页。
**复制在第一次写发生、按单页（4KB）粒度**，没写过的页一直共享。

🔧 **实测对照（[08 实验 4](08-observation-and-experiments.md)，`cow_trace.c`）**：父进程写满 64MB 后 fork，
子进程只读阶段 minflt 增量 0（共享父页），改写阶段 minflt 增量 16384（= 64MB/4KB，每页一次 `do_wp_page`）。
用 `bpftrace` 在 `do_wp_page` 下探针，计数也是 16384——内核侧、用户侧两个独立计数对上。

## 4.5 三条分支拼出 fork / execve / mmap

| CSAPP 机制 | 由哪些分支实现 |
|-----------|--------------|
| `fork` 快、父子隔离 | `copy_page_range` 标只读 + 写时 `do_wp_page` 复制单页 |
| `execve` 启动快 | 建新 VMA，代码/数据靠 `do_fault` 首次访问从 a.out 换入，`.bss` 靠 `do_anonymous_page` |
| `malloc` 大块 | `mmap(MAP_ANONYMOUS)` 建 VMA，`do_anonymous_page` 写缺页时才给实页 |
| `mmap` 文件读 | `do_fault`/`do_read_fault` 把 page cache 页映射进来，省一次拷贝 |
| `mmap` 共享写盘 | `do_shared_fault` + `page_mkwrite` + 脏页回写（`07`）|
