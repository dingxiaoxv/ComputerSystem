// 实验题 2：亲眼看 COW 触发（minor fault 计数）
//
// 主线：fork 不复制物理页，只把父子双方私有页的 PTE 标只读 + 区域标 COW。
//   - 子进程"只读"遍历这块内存：命中共享的父页，几乎不触发 minor fault；
//   - 子进程"全改写"这块内存：每页第一次写触发一次保护故障 → handler 复制
//     该页（minor fault，因为页本就在内存、无需读盘）。
// 用 getrusage(RUSAGE_SELF).ru_minflt 在子进程读/写前后取差值，把
// "复制发生在第一次写、按页（4KB）粒度"量化出来。
//
// 编译：gcc -O0 -Wall -o cow_minflt cow_minflt.c
// 注意 -O0：避免编译器把"只读遍历"的循环优化掉（结果未被使用时会被删）。

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

#define MB (1024UL * 1024UL)
#define REGION_BYTES (64UL * MB)

// 取当前进程到目前为止累计的 minor fault 次数
static long minflt(void) {
  struct rusage ru;
  getrusage(RUSAGE_SELF, &ru);
  return ru.ru_minflt;
}

int main(void) {
  long page = sysconf(_SC_PAGESIZE);
  unsigned long npages = REGION_BYTES / (unsigned long)page;
  printf("页大小 = %ld B，区域 = %lu MB，约 %lu 个页\n", page, REGION_BYTES / MB, npages);

  // 1) 父进程分配并写满 64MB，确保每个虚拟页都已分配到物理页
  //    （malloc 只是建匿名映射，不写就不会真正分配物理页 / 触发缺页）
  char *buf = malloc(REGION_BYTES);
  if (!buf) {
    perror("malloc");
    return 1;
  }
  memset(buf, 0xAB, REGION_BYTES); // 触发父进程自己的匿名缺页，物理页就位

  pid_t pid = fork();
  if (pid < 0) {
    perror("fork");
    return 1;
  }

  if (pid == 0) {
    // ===== 子进程 =====
    // fork 后：子进程的这块内存与父进程共享同一批物理页，PTE 全标只读。

    // --- 阶段 A：只读遍历 ---
    long before_read = minflt();
    volatile unsigned long sum = 0; // volatile 防止整段循环被优化掉
    for (unsigned long i = 0; i < REGION_BYTES; i += (unsigned long)page)
      sum += (unsigned char)buf[i]; // 每页读一个字节
    long after_read = minflt();

    // --- 阶段 B：全部改写 ---
    long before_write = after_read;
    for (unsigned long i = 0; i < REGION_BYTES; i += (unsigned long)page)
      buf[i] = 0xCD; // 每页写一个字节 → 第一次写触发 COW
    long after_write = minflt();

    printf("\n[子进程] sum=%lu（仅为阻止优化）\n", sum);
    printf("[子进程] 只读阶段 minor fault 增量 = %ld 次\n", after_read - before_read);
    printf("[子进程] 改写阶段 minor fault 增量 = %ld 次（理论 ≈ %lu）\n",
           after_write - before_write, npages);
    fflush(stdout); // _exit 不刷新 stdio 缓冲；stdout 是管道时为全缓冲，必须手动刷
    _exit(0);
  }

  // ===== 父进程 =====
  waitpid(pid, NULL, 0);
  printf("\n结论：只读阶段几乎不增（命中共享父页）；\n");
  printf("      改写阶段每页第一次写各触发一次 COW 复制，增量 ≈ 区域页数。\n");
  free(buf);
  return 0;
}
