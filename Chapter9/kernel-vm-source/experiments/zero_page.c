// zero_page.c —— 亲眼看「读未初始化匿名内存」不分配真实物理页（共享 ZERO_PAGE）
//
// 对应概念 5（do_anonymous_page 与 zero page），也是 PROGRESS.md 里
// 「零页别名惩罚」遗留问题的根因实验。
//
// 一块 MAP_ANONYMOUS|MAP_PRIVATE 的内存是 demand-zero 的：
//   · 未触碰        → 不占物理页（RSS 不涨）
//   · 第一次「读」  → 缺页进内核走 do_anonymous_page 的非写路径，PTE 指向
//                     全局共享的 ZERO_PAGE（只读）。这是一次 minor fault
//                     （minflt++），但 ZERO_PAGE 是 special page，不计入本
//                     进程 RSS —— 所以读完一整块，RSS 几乎不涨。
//   · 第一次「写」  → 写只读的 zero 映射触发再次缺页，内核分配真实物理页，
//                     RSS 这才暴涨。
//
// 工程教训：拿「未初始化数组」做内存/cache 基准测试会严重失真——你以为在测
// 内存带宽，其实所有虚拟页都别名到同一个物理 zero page。基准前必须先写一遍。
//
// 编译：gcc -O2 -Wall zero_page.c -o zero_page    运行：./zero_page

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

// /proc/self/statm 第 2 个字段是 resident（单位：页），换算成 KB
static long read_rss_kb(void) {
  FILE *f = fopen("/proc/self/statm", "r");
  long size = 0, res = 0;
  if (f) {
    if (fscanf(f, "%ld %ld", &size, &res) != 2)
      res = 0;
    fclose(f);
  }
  return res * (sysconf(_SC_PAGESIZE) / 1024);
}

// /proc/self/stat 字段：pid (comm) state ppid pgrp session tty tpgid flags
//                      minflt(10) cminflt(11) majflt(12) ...
// comm 可能含空格/括号，用最后一个 ')' 定位之后的字段最稳。
static void read_faults(unsigned long *minflt, unsigned long *majflt) {
  *minflt = *majflt = 0;
  FILE *f = fopen("/proc/self/stat", "r");
  if (!f)
    return;
  char buf[4096];
  if (fgets(buf, sizeof buf, f)) {
    char *p = strrchr(buf, ')');  // 跳过 pid 与 (comm)
    if (p)
      sscanf(p, ") %*c %*d %*d %*d %*d %*d %*u %lu %*u %lu", minflt, majflt);
  }
  fclose(f);
}

int main(void) {
  const size_t MB = 1UL << 20;
  const size_t N = 256 * MB;  // 256 MB 测试块
  const long pg = sysconf(_SC_PAGESIZE);
  const size_t npages = N / (size_t)pg;

  unsigned long mf_a, jf_a, mf_b, jf_b, mf_c, jf_c;
  long rss_map, rss_read, rss_write;

  char *a = mmap(NULL, N, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (a == MAP_FAILED) {
    perror("mmap");
    return 1;
  }
  rss_map = read_rss_kb();
  read_faults(&mf_a, &jf_a);

  // 阶段一：只读遍历每一页的首字节（触发 zero page 映射）
  volatile unsigned long sink = 0;
  for (size_t i = 0; i < N; i += (size_t)pg)
    sink += (unsigned long)a[i];
  rss_read = read_rss_kb();
  read_faults(&mf_b, &jf_b);

  // 阶段二：逐页写首字节（触发真实物理页分配）
  for (size_t i = 0; i < N; i += (size_t)pg)
    a[i] = 1;
  rss_write = read_rss_kb();
  read_faults(&mf_c, &jf_c);

  printf("测试块大小            : %zu MB（%zu 页, 页大小 %ld B）\n", N / MB, npages, pg);
  printf("------------------------------------------------------------\n");
  printf("映射后(未触碰) RSS    : %8ld KB\n", rss_map);
  printf("只读遍历后     RSS    : %8ld KB   (增量 %+ld KB)\n", rss_read, rss_read - rss_map);
  printf("逐页写后       RSS    : %8ld KB   (增量 %+ld KB)\n", rss_write, rss_write - rss_read);
  printf("------------------------------------------------------------\n");
  printf("只读阶段 minflt 增量  : %8lu   (≈页数 %zu, 每页一次缺页映射 zero page)\n",
         mf_b - mf_a, npages);
  printf("写阶段   minflt 增量  : %8lu   (≈页数 %zu, 每页一次缺页分配真实页)\n",
         mf_c - mf_b, npages);
  printf("majflt 总增量         : %8lu   (匿名页不读盘, 应为 0)\n", jf_c - jf_a);
  printf("------------------------------------------------------------\n");
  printf("结论：只读遍历 256MB，RSS 几乎不涨 —— 全部别名到共享 ZERO_PAGE；\n");
  printf("      写一遍后 RSS 才 ≈ 256MB。未初始化数组做基准 = 测 zero page。\n");

  (void)sink;
  munmap(a, N);
  return 0;
}
