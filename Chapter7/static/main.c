#include <stdio.h>

#include "vector.h"

int x[2] = {1, 2};
int y[2] = {3, 4};
int z[2];

int main() {
  addvec(x, y, z, 2);
  printf("z = [%d %d]\n", z[0], z[1]);

  /* 引用 add_cnt：探针被读取，addvec.o 必然在最终可执行文件里。
   * 这里刻意【不】打印 mult_cnt——一旦引用它，链接器就会把
   * multvec.o 也抽进来，选择性抽取的演示就被破坏了。 */
  printf("add_cnt = %d\n", add_cnt);
  return 0;
}