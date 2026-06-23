#include <stdlib.h>

int *g;                                  // 全局根
void lose(void) { int *p = malloc(64); } // ① p 出作用域即不可达 → definitely lost
int main(void) {
  lose();
  g = malloc(64); // ② 始终被全局 g 指着 → still reachable
  return 0;       // 两块都没 free
}