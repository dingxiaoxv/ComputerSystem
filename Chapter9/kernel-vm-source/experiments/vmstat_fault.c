// vmstat_fault.c —— 区分 minor fault（页在内存，只建映射）与 major fault（需读盘）
//
// 对应概念 4（缺页代码之旅）的两条出口分支，以及 §9.1-9.5 的 minor/major 概念：
//   · minor fault：malloc 一大块并写满 → 匿名页缺页 do_anonymous_page，
//                  分配物理页但不碰磁盘。计入 /proc/self/stat 的 minflt
//                  与 /proc/vmstat 的 pgfault。
//   · major fault：mmap 一个文件、先用 posix_fadvise(DONTNEED) 丢掉它的
//                  page cache，再访问 → 文件页缺页 do_fault 必须从磁盘读回，
//                  计入 majflt 与 /proc/vmstat 的 pgmajfault。
//
// 注意：major fault 依赖「目标文件页确实不在 page cache」。免 root 用
// posix_fadvise(DONTNEED) 尽力丢弃；若本机内存充裕、刚写完缓存未回收，可能
// 丢不干净导致 major 偏少——最可靠的清缓存是 `echo 3 >/proc/sys/vm/drop_caches`
// （需 root）。脚本会如实打印实测值。
//
// 编译：gcc -O2 -Wall vmstat_fault.c -o vmstat_fault   运行：./vmstat_fault

#define _GNU_SOURCE
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

static void read_faults(unsigned long *minflt, unsigned long *majflt) {
  *minflt = *majflt = 0;
  FILE *f = fopen("/proc/self/stat", "r");
  if (!f)
    return;
  char buf[4096];
  if (fgets(buf, sizeof buf, f)) {
    char *p = strrchr(buf, ')');
    if (p)
      sscanf(p, ") %*c %*d %*d %*d %*d %*d %*u %lu %*u %lu", minflt, majflt);
  }
  fclose(f);
}

// 取 /proc/vmstat 里某一项（如 "pgfault"、"pgmajfault"）的全局累计值
static unsigned long read_vmstat(const char *key) {
  FILE *f = fopen("/proc/vmstat", "r");
  if (!f)
    return 0;
  char name[64];
  unsigned long val, ret = 0;
  while (fscanf(f, "%63s %lu", name, &val) == 2) {
    if (strcmp(name, key) == 0) {
      ret = val;
      break;
    }
  }
  fclose(f);
  return ret;
}

int main(void) {
  const size_t MB = 1UL << 20;
  const size_t N = 128 * MB;
  const long pg = sysconf(_SC_PAGESIZE);
  const size_t npages = N / (size_t)pg;
  unsigned long mf0, jf0, mf1, jf1, mf2, jf2;
  unsigned long vpf0, vpmf0, vpf1, vpmf1;

  // ---------- minor fault：匿名页写入 ----------
  read_faults(&mf0, &jf0);
  vpf0 = read_vmstat("pgfault");
  vpmf0 = read_vmstat("pgmajfault");

  char *anon = malloc(N);
  if (!anon) {
    perror("malloc");
    return 1;
  }
  memset(anon, 1, N);  // 写满 → 每页一次匿名缺页
  read_faults(&mf1, &jf1);
  vpf1 = read_vmstat("pgfault");
  vpmf1 = read_vmstat("pgmajfault");

  printf("=== minor fault：malloc + memset %zu MB（%zu 页）===\n", N / MB, npages);
  printf("  /proc/self/stat minflt 增量 : %8lu  (≈页数 %zu)\n", mf1 - mf0, npages);
  printf("  /proc/self/stat majflt 增量 : %8lu  (匿名不读盘, 应为 0)\n", jf1 - jf0);
  printf("  /proc/vmstat pgfault 增量   : %8lu  (全局, 含其他进程, 仅作佐证)\n", vpf1 - vpf0);
  printf("  /proc/vmstat pgmajfault增量 : %8lu\n", vpmf1 - vpmf0);
  free(anon);

  // ---------- major fault：文件页缺页 ----------
  const char *path = "./bigfile.tmp";
  int fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0644);
  if (fd < 0) {
    perror("open");
    return 1;
  }
  // 写出一个 N 字节文件并回写到磁盘
  char *chunk = malloc(MB);
  memset(chunk, 0xab, MB);
  for (size_t off = 0; off < N; off += MB) {
    if (write(fd, chunk, MB) != (ssize_t)MB) {
      perror("write");
      return 1;
    }
  }
  free(chunk);
  fsync(fd);
  // 尽力丢弃该文件的 page cache（免 root）
  posix_fadvise(fd, 0, (off_t)N, POSIX_FADV_DONTNEED);

  char *fmap = mmap(NULL, N, PROT_READ, MAP_PRIVATE, fd, 0);
  if (fmap == MAP_FAILED) {
    perror("mmap");
    return 1;
  }
  // 关键：禁用 readahead 与 fault-around。否则一次缺页会被内核预读 + 顺带建立
  // 周围多页 PTE，把本该逐页发生的 major fault 几乎全转成 minor，看不出效果。
  madvise(fmap, N, MADV_RANDOM);
  read_faults(&mf1, &jf1);
  vpf0 = read_vmstat("pgmajfault");

  volatile unsigned long sink = 0;
  for (size_t i = 0; i < N; i += (size_t)pg)  // 逐页读 → 文件页缺页
    sink += (unsigned long)fmap[i];
  read_faults(&mf2, &jf2);
  vpf1 = read_vmstat("pgmajfault");

  printf("\n=== major fault：mmap 文件 + fadvise(DONTNEED) 后逐页读 ===\n");
  printf("  /proc/self/stat majflt 增量 : %8lu  (从磁盘读回的页数)\n", jf2 - jf1);
  printf("  /proc/self/stat minflt 增量 : %8lu  (仍在 page cache 的页走 minor)\n", mf2 - mf1);
  printf("  /proc/vmstat pgmajfault增量 : %8lu  (全局)\n", vpf1 - vpf0);
  if (jf2 - jf1 == 0)
    printf("  ⚠ majflt=0：文件页仍在 page cache（fadvise 没丢干净）。\n"
           "     可改用 root：sudo sh -c 'echo 3 >/proc/sys/vm/drop_caches' 后重试。\n");

  (void)sink;
  munmap(fmap, N);
  close(fd);
  unlink(path);
  return 0;
}
