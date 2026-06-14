#include <stdio.h>

#define N (64 * 1024 * 1024) /* 64M 个 float = 256 MB，远大于 LLC */
#define BLOCK (256 * 1024)   /* 1 MB，稳稳放进 LLC */
float a[N];

/* 版本 A：时间局部性差
 * a[i] 两次被访问之间隔了整整 N 个其他元素（重用距离 = N = 256 MB）
 * 远超 LLC（典型 8-32 MB）→ 第 2~10 遍来时 a[i] 早被逐出，每遍都从内存重读 */
float pass_then_repeat(void) {
  float sum = 0;
  for (int pass = 0; pass < 10; pass++)
    for (long i = 0; i < N; i++)
      sum += a[i];
  return sum;
}

/* 版本 B：时间局部性好
 * 把数组切成 1 MB 的块，块内连刷 10 遍再前进
 * a[i] 的重用距离 = BLOCK = 1 MB < LLC → 第 2~10 遍全部 cache 命中 */
float blocked_repeat(void) {
  float sum = 0;
  for (long blk = 0; blk < N; blk += BLOCK)
    for (int pass = 0; pass < 10; pass++)
      for (long i = blk; i < blk + BLOCK; i++)
        sum += a[i];
  return sum;
}

int main() {
  /* 必须先写入：只读 .bss 数组的所有页会映射到内核共享零页，
   * 物理内存只占 ~4KB，cache/内存实验全部失真（实测 RSS 仅 1.5MB） */
  for (long i = 0; i < N; i++)
    a[i] = 1.0f;

  printf("calculate result: %f\n", blocked_repeat());
  return 0;
}