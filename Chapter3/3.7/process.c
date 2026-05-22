#include <stdio.h>

long leaf(long y) { return y + 2; }

long g(long y) {
  long a = y + 2;
  long *p = &a; // 取值
  return *p + 1;
}

long top(long x) {
  long tmp = x - 5;
  long ret = leaf(tmp);
  return tmp + ret;
}

int main() {
  int result = top(100);
  return result;
}