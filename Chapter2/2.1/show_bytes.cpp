#include <cstddef>
#include <iomanip>
#include <iostream>
#include <cstring>

using byte_ptr = const unsigned char *;

void show_bytes(byte_ptr start, std::size_t len) {
  for (std::size_t i = 0; i < len; ++i) {
    std::cout << std::hex << std::setw(2) << std::setfill('0')
              << static_cast<unsigned>(start[i]) << ' ';
  }
  std::cout << '\n';
}

void show_int(int x) { show_bytes(reinterpret_cast<byte_ptr>(&x), sizeof(x)); }

void show_float(float x) {
  show_bytes(reinterpret_cast<byte_ptr>(&x), sizeof(x));
}

void show_pointer(void *x) {
  show_bytes(reinterpret_cast<byte_ptr>(&x), sizeof(x));
}

void show_string(const char *s) {
  show_bytes(reinterpret_cast<byte_ptr>(s), strlen(s));
}

int main() {
  int i = 12345;
  float f = 12345.0f;
  int *p = &i;
  const char *s = "abcdef";

  std::cout << "int    12345: ";
  show_int(i);
  std::cout << "float  12345: ";
  show_float(f);
  std::cout << "pointer &i  : ";
  show_pointer(p);
  std::cout << "string abcdef: ";
  show_string(s);

  return 0;
}
