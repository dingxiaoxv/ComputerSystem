#include <cstddef>
#include <cstring>
#include <iostream>
#include <vector>

void exp_mixed_compare() {
  int a = -1;
  unsigned int b = 0;
  std::cout << "a < b? " << (a < b) << std::endl; // unsigned与其他类型数运算，都隐式转换为unsigned
  std::cout << "(unsigned int) a = 0x" << std::hex << (unsigned int)a << std::dec << std::endl;
}

void exp_strlen_sub() {
  const char *s = "abc";
  const char *t = "adbc";
  // 直接打印 size_t 原始值，才能看到绕回后的 SIZE_MAX
  std::cout << "strlen(s) - strlen(t) = " << (strlen(s) - strlen(t)) << std::endl;
  std::cout << "strlen(s) - strlen(t) > 0? " << (strlen(s) - strlen(t) > 0)
            << std::endl; // s 比 t 短，期望 false，实际 true（绕回导致）
}

void exp_unsigned_loop() {
  std::vector<int> v = {10, 20, 30};

  size_t cnt = 0;
  for (size_t i = v.size() - 1; i >= 0; i--) {
    if (++cnt > 5) {
      break;
    }

    std::cout << "i is " << i << std::endl;
  }
}

void exp_sizet_int() {
  std::vector<int> v(3);
  int n = -1;
  std::cout << "n < (int)v.size()?" << (n < (int)v.size()) << std::endl;
  std::cout << "n < v.size()?" << (n < v.size())
            << std::endl; // unsigned与其他类型数运算，都隐式转换为unsigned
  std::cout << "(size_t)n = " << ((size_t)n) << std::endl;
}

int main(int argc, char **argv) {
  exp_mixed_compare();
  exp_strlen_sub();
  exp_unsigned_loop();
  exp_sizet_int();

  return 0;
}