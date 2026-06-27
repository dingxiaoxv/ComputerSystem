// wss_scan.c —— 工作集大小(Working Set Size)扫描
// 用法: ./wss_scan <每个数组的元素个数 N> <重复次数 REP>
//   总工作集 = N * 4字节 * 2个数组
//   REP 用来把总运行时间拉长, 让 perf 计数器复用占比接近 100%
//
// 实验: 固定总访问量 (N*REP 不变), 只改 N, 观察 miss 率
//   小 N (工作集 < L1): miss 率极低, 几乎全命中
//   大 N (工作集 > L3): miss 率飙升, 每次访问都要去内存
#include <stdio.h>
#include <stdlib.h>

double dotprod(const float *x, const float *y, long n) {
  double sum = 0;
  for (long i = 0; i < n; i++)
    sum += x[i] * y[i];
  return sum;
}

int main(int argc, char **argv) {
  if (argc != 3) {
    fprintf(stderr, "用法: %s <N> <REP>\n", argv[0]);
    return 1;
  }
  long n = atol(argv[1]);
  int rep = atoi(argv[2]);

  float *x = malloc(n * sizeof(float));
  float *y = malloc(n * sizeof(float));
  // 先写一遍, 真正分配物理页 + 预热(破掉 demand-zero 零页别名)
  for (long i = 0; i < n; i++) {
    x[i] = (float)i;
    y[i] = (float)i;
  }

  double sum = 0;
  for (int r = 0; r < rep; r++)
    sum += dotprod(x, y, n);

  printf("N=%ld 工作集=%ldKB REP=%d sum=%g\n",
         n, n * (long)sizeof(float) * 2 / 1024, rep, sum);
  free(x);
  free(y);
  return 0;
}
