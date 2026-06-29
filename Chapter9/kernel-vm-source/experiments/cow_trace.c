// cow_trace.c —— fork 后子进程写共享页，触发写时复制（COW），作为内核追踪靶子
//
// 对应概念 6（do_wp_page 与 copy_page_range）。fork 后父子共享全部私有页且
// PTE 标只读；子进程一旦「写」某页，触发写保护故障，内核走：
//     缺页 → handle_mm_fault → handle_pte_fault → do_wp_page → wp_page_copy
// 复制那一页给子进程、改可写、更新它的 PTE。复制是「第一次写、按单页」发生的。
//
// 本程序自身用 getrusage 把这件事量化（只读阶段 vs 改写阶段的 minflt 增量），
// 同时打印子进程 PID 并留出 attach 窗口，方便用 bpftrace / trace-cmd 从内核侧
// 观察 do_wp_page 的实时触发（见 summary 实验题 4/5，需 root）。
//
// 编译：gcc -O2 -Wall cow_trace.c -o cow_trace
// 运行：./cow_trace            （纯量化）
// 追踪：另开终端 ! sudo bpftrace -e 'kprobe:do_wp_page { @[comm] = count(); }'
//       然后运行本程序，结束后看 @[cow_trace] 的计数 ≈ 页数

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

static unsigned long minflt_self(void) {
  struct rusage ru;
  getrusage(RUSAGE_SELF, &ru);
  return (unsigned long)ru.ru_minflt;
}

int main(void) {
  const size_t MB = 1UL << 20;
  const size_t N = 64 * MB;
  const long pg = sysconf(_SC_PAGESIZE);
  const size_t npages = N / (size_t)pg;

  // 父进程申请并写满 → 物理页全部就位（fork 时才有东西可共享/复制）
  char *buf = malloc(N);
  if (!buf) {
    perror("malloc");
    return 1;
  }
  memset(buf, 0x5a, N);

  pid_t pid = fork();
  if (pid < 0) {
    perror("fork");
    return 1;
  }

  if (pid == 0) {
    // 子进程：与父共享 buf 的全部物理页（PTE 只读, COW 待触发）
    fprintf(stderr, "[child] pid=%d 已就绪，%zu MB / %zu 页待写。\n", getpid(), N / MB, npages);
    fprintf(stderr, "[child] 2s 后开始改写——如需内核侧追踪，现在 attach。\n");
    sleep(2);  // 留 attach 窗口

    unsigned long m0 = minflt_self();
    // 只读阶段：不触发复制（仍共享父页）
    volatile unsigned long sink = 0;
    for (size_t i = 0; i < N; i += (size_t)pg)
      sink += (unsigned long)buf[i];
    unsigned long m1 = minflt_self();
    // 改写阶段：每页第一次写触发 do_wp_page 复制一页
    for (size_t i = 0; i < N; i += (size_t)pg)
      buf[i] = 0x01;
    unsigned long m2 = minflt_self();
    (void)sink;

    fprintf(stderr, "[child] 只读阶段 minflt 增量 : %8lu  (共享父页, 几乎不增)\n", m1 - m0);
    fprintf(stderr, "[child] 改写阶段 minflt 增量 : %8lu  (≈页数 %zu, 每页一次 COW)\n", m2 - m1,
            npages);
    _exit(0);
  }

  // 父进程等子进程结束
  waitpid(pid, NULL, 0);
  free(buf);
  return 0;
}
