/* 被打桩的目标程序：一次 malloc + 一次 free。
 * 它对自己被打桩毫不知情，源码、二进制都不需要任何改动。
 *
 * 编译：gcc -o intr int.c
 * 运行（带打桩）：LD_PRELOAD=./mymalloc.so ./intr
 */
#include <stdlib.h>

int main() {
  int *p = malloc(32);
  free(p);
  return 0;
}
