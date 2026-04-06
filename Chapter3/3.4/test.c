#include <stdio.h>

unsigned long func(unsigned long x) {
  unsigned y = (unsigned)x;
  return y;
}

long extend_sign(char x) { return x; }

long extend_zero(unsigned char x) { return x; }

void copy(long *src, long *dst) { *dst = *src; }

long g(long x) {
  long y = x + 1;
  return y;
}

int main(int argc, char **argv) {
  printf("%ld\n", extend_sign(0xff)); // 0xffffffffffffffff
  printf("%ld\n", extend_zero(0xff)); // 0x00000000000000ff

  return 0;
}