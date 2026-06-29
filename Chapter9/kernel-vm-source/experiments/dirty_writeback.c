// dirty_writeback.c —— 亲眼看脏页回写子系统的「水位线 + 限流」（免 root）
//
// 对应概念「脏页回写：writeback 子系统」。持续向一个文件写入（不 fsync），
// 每隔一段读 /proc/meminfo 的 Dirty / Writeback，并测本轮瞬时写吞吐：
//   · 起步           → 写全被 page cache 吸收，Dirty 直线上涨，吞吐 = 内存带宽
//   · 过 background  → 内核唤醒 per-bdi flusher（wb_workfn）后台回写，
//                      Writeback 变非 0，Dirty 上涨变缓
//   · 过 dirty_ratio → 写进程在 balance_dirty_pages 里被同步限流（节流到接近
//                      磁盘回写带宽），瞬时吞吐明显掉下来，Dirty 进入平台
//
// 阈值（本机内核默认）：background = 可脏内存 × dirty_background_ratio(10%)，
// limit = 可脏内存 × dirty_ratio(20%)。"可脏内存"≈ free + 可回收，略小于
// MemTotal。机制设计的排查视角见 §9.1-9.5「iowait/回写风暴」两节。
//
// 写入的是真实块设备上的临时文件，结束直接 unlink（脏页随 inode 作废，不必
// 等回写完）。注意别指向 tmpfs——tmpfs 在内存里，不产生脏页回写。
//
// 编译：gcc -O2 -Wall dirty_writeback.c -o dirty_writeback
// 运行：./dirty_writeback [写入上限GB，默认 10]

#define _GNU_SOURCE
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static long meminfo_kb(const char *key) {
  FILE *f = fopen("/proc/meminfo", "r");
  if (!f)
    return -1;
  char name[64];
  long val;
  char unit[16];
  long ret = -1;
  while (fscanf(f, "%63[^:]: %ld %15s\n", name, &val, unit) >= 2) {
    if (strcmp(name, key) == 0) {
      ret = val;
      break;
    }
  }
  fclose(f);
  return ret;
}

static double now_sec(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return ts.tv_sec + ts.tv_nsec / 1e9;
}

int main(int argc, char **argv) {
  const size_t MB = 1UL << 20;
  size_t limit_gb = (argc > 1) ? strtoul(argv[1], NULL, 10) : 10;
  const size_t MAX = limit_gb * 1024 * MB;
  const size_t ROUND = 64 * MB;     // 每轮写 64MB
  const size_t REPORT = 256 * MB;   // 每累计 256MB 打印一行

  const char *path = "./dirty_test.tmp";
  int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (fd < 0) {
    perror("open");
    return 1;
  }
  char *buf = malloc(MB);
  if (!buf) {
    perror("malloc");
    return 1;
  }
  memset(buf, 0xcd, MB);

  printf("MemTotal=%ld MB  写入上限=%zu GB  （Ctrl-C 可随时中止）\n",
         meminfo_kb("MemTotal") / 1024, limit_gb);
  printf("%10s | %12s | %10s | %12s\n", "已写(MB)", "本轮吞吐MB/s", "Dirty(MB)", "Writeback(MB)");
  printf("-----------+--------------+------------+--------------\n");

  size_t total = 0, since_report = 0;
  while (total < MAX) {
    double t0 = now_sec();
    for (size_t w = 0; w < ROUND; w += MB) {
      if (write(fd, buf, MB) != (ssize_t)MB) {
        perror("write");
        goto done;
      }
    }
    double dt = now_sec() - t0;
    total += ROUND;
    since_report += ROUND;
    if (since_report >= REPORT) {
      since_report = 0;
      double rate = (ROUND / (double)MB) / (dt > 0 ? dt : 1e-9);
      printf("%10zu | %12.0f | %10ld | %12ld\n", total / MB, rate,
             meminfo_kb("Dirty") / 1024, meminfo_kb("Writeback") / 1024);
      fflush(stdout);
    }
  }

done:
  free(buf);
  close(fd);
  unlink(path);  // 删除 inode，脏页作废，无需等回写
  printf("-----------+--------------+------------+--------------\n");
  printf("结束：临时文件已删除。看吞吐何时从「内存带宽」掉到「磁盘回写带宽」——\n");
  printf("      那一刻就是写进程撞上 dirty_ratio、被 balance_dirty_pages 限流。\n");
  return 0;
}
