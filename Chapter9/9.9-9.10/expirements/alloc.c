/* §9.9 实验：把「题1 brk/mmap 分界」和「题3 chunk 头/对齐可视化」整合在一起。
 *
 * 题3 看 printf 输出：      ./alloc
 * 题1 看系统调用：          strace -e trace=brk,mmap,munmap ./alloc
 *   （题3 的小 malloc 至多触发一次 brk，不干扰题1 对大块 mmap 的观察）
 */
#include <malloc.h> /* malloc_usable_size */
#include <stdio.h>
#include <stdlib.h>

/* glibc chunk 的 size 字段就在 payload 指针的前一个字（8 字节）里，
 * 低 3 位是 PREV_INUSE 等标志位，&~0x7 清掉后才是 chunk 总大小（含头部）。*/
static size_t chunk_size(void *p) { return *((size_t *)p - 1) & ~0x7UL; }

int main(void) {
  /* ===== 题3：块头与对齐填充（趁堆还干净时观察）===== */
  puts("== 题3：chunk 大小 / 对齐 / 内部碎片 ==");

  void *a = malloc(1), *b = malloc(17), *c = malloc(32);
  printf("malloc(1)  返回=%p  chunk=%zu  usable=%zu\n", a, chunk_size(a), malloc_usable_size(a));
  printf("malloc(17) 返回=%p  chunk=%zu  usable=%zu\n", b, chunk_size(b), malloc_usable_size(b));
  printf("malloc(32) 返回=%p  chunk=%zu  usable=%zu\n", c, chunk_size(c), malloc_usable_size(c));
  printf("返回地址都按 16 对齐? a%%16=%zu b%%16=%zu c%%16=%zu\n", (size_t)a % 16, (size_t)b % 16,
         (size_t)c % 16);
  printf("相邻两次返回地址差: b-a=%ld  c-b=%ld\n", (char *)b - (char *)a, (char *)c - (char *)b);
  free(a);
  free(b);
  free(c);

  /* 请求从 1 涨到 40，观察 chunk 大小「阶梯式跳变」——每跨一个对齐档跳一级，
   * 台阶内多出来的字节就是对齐造成的内部碎片。*/
  puts("\nreq -> chunk（观察阶梯跳变）:");
  for (int req = 1; req <= 40; req++) {
    void *p = malloc(req);
    printf("  req=%2d  chunk=%zu  内部碎片=%zu\n", req, chunk_size(p), chunk_size(p) - req);
    free(p);
  }

  /* ===== 题1：brk vs mmap 分界（配合 strace 看）===== */
  puts("\n== 题1：1000 次小 malloc + 一次大 malloc ==");
  for (int i = 0; i < 1000; i++) {
    void *p = malloc(64); /* 留住不 free，逼堆增长 */
    (void)p;
  }
  void *big = malloc(1 << 20); /* 1MB，超过 mmap 阈值，单独走 mmap */
  free(big);                   /* 大块 free 直接 munmap 还给内核 */
  return 0;
}
