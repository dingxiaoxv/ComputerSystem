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
  char *p = strrchr(buf, ')') + 2; /* 跳到第 3 个字段 state */
  long minflt, majflt;             /* 跳过 state ppid pgrp session tty tpgid flags */
  sscanf(p, "%*c %*d %*d %*d %*d %*d %*u %ld %*u %ld", &minflt, &majflt);
  printf("[%-12s] minflt=%-8ld majflt=%ld\n", tag, minflt, majflt);
}

int main(void) {
  show_faults("start");

  size_t sz = 256 * 1024 * 1024; /* 256 MB = 65536 个 4KB 页 */
  char *p = malloc(sz);
  show_faults("after malloc"); /* minflt 几乎不变：只登记映射 */

  for (size_t i = 0; i < sz; i += 4096)
    p[i] = 1;                 /* 逐页触摸，每页第一次写触发一次缺页 */
  show_faults("after touch"); /* minflt 暴涨约 65536 */

  for (size_t i = 0; i < sz; i += 4096)
    p[i] = 2;
  show_faults("touch again"); /* 第二遍几乎不涨：页已在内存 */
  return 0;
}