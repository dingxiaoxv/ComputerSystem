#include <stdio.h>
#include <unistd.h>

int main() {
  long sum = 0;
  for (long i = 0; i < 100000000L; i++)
    // sum += i;           // 纯用户态计算
    write(1, ".", 1);   // 系统调用
  printf("%ld\n", sum); // printf 内部触发 write 陷阱
  return 0;
}