#include <stdio.h>

void multsore(long, long, long*);

int main() {
  long d;
  multsore(2, 3, &d);
  printf("2 * 3 = %ld\n", d);
  return 0;
}

long mult2(long a, long b) {
  long s = a * b;
  return s;
}