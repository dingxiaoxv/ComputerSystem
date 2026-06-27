// ptr_chase.c —— 指针追逐, 打败硬件预取器的工作集扫描
// 用法: ./ptr_chase <数组槽数 N> <访问次数 ITERS>
//   工作集 = N * 8 字节
//
// 原理: 在数组里构造一个覆盖全部 N 个槽的【单环随机置换】(Sattolo 算法),
//   每次访问 p = idx[p] 跳到下一个随机位置. 地址完全不可预测,
//   硬件预取器失效 -> 每次访问的真实延迟暴露出来.
//   工作集 < L1: 每跳 ~4 cycle;  > L3: 每跳 上百 cycle.
// 这才是 §6.6 Memory Mountain / lat_mem_rd 的内核.
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
  if (argc != 3) {
    fprintf(stderr, "用法: %s <N> <ITERS>\n", argv[0]);
    return 1;
  }
  long n = atol(argv[1]);
  long iters = atol(argv[2]);

  long *idx = malloc(n * sizeof(long));
  for (long i = 0; i < n; i++)
    idx[i] = i;
  // Sattolo 算法: 保证生成单一 n-环(不会提前回到起点)
  for (long i = n - 1; i > 0; i--) {
    long j = rand() % i; // j ∈ [0, i)
    long t = idx[i];
    idx[i] = idx[j];
    idx[j] = t;
  }

  long p = 0;
  for (long k = 0; k < iters; k++)
    p = idx[p]; // 真·数据依赖链, 无法乱序/预取跳过

  printf("N=%ld 工作集=%ldKB ITERS=%ld (最后 p=%ld, 防优化)\n",
         n, n * 8 / 1024, iters, p);
  free(idx);
  return 0;
}
