#include <stdio.h>

#define N 4096
static int a[N][N];

long sum_row(void) { // stride-1
  long s = 0;
  for (int i = 0; i < N; i++)
    for (int j = 0; j < N; j++)
      s += a[i][j];
  return s;
}

long sum_col(void) { // stride-N
  long s = 0;
  for (int j = 0; j < N; j++)
    for (int i = 0; i < N; i++)
      s += a[i][j];
  return s;
}

int main(int argc, char **argv) {
  // 关键：先写一遍，破掉 demand-zero 的零页别名，
  // 让每一页都拿到独立的物理内存，访问模式才真实可测。
  for (int i = 0; i < N; i++)
    for (int j = 0; j < N; j++)
      a[i][j] = i + j;

  // 用命令行参数选函数，避免编译器把没用到的那个优化掉，
  // 也方便 perf 分别测：./a.out row  /  ./a.out col
  long s;
  if (argc > 1 && argv[1][0] == 'c')
    s = sum_col();
  else
    s = sum_row();

  printf("sum result %ld\n", s);
  return 0;
}
