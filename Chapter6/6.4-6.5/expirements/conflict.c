#include <stdio.h>

#define N (1 << 12) // 让 stride 恰好等于 cache 一路的大小
float x[N], y[N];

double dotprod(void) {
  double sum = 0;
  for (int i = 0; i < N; i++)
    sum += x[i] * y[i];
  return sum;
}

int main(int argc, char **argv) {
  // 关键：先写一遍，破掉 demand-zero 的零页别名，
  // 让每一页都拿到独立的物理内存，访问模式才真实可测。
  for (int i = 0; i < N; i++) {
    x[i] = i;
    y[i] = i;
  }

  printf("sum result %lf\n", dotprod());
  return 0;
}
