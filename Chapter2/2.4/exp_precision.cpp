#include <cstdint>
#include <cstring>
#include <iostream>

void printFloatBits(float f) {
  uint32_t bits{0};
  memcpy(&bits, &f, sizeof(uint32_t));
  std::cout << f << ", s: " << ((bits >> 31) & 1) << std::hex << ", exp: 0x"
            << ((bits >> 23) & 0xFF) << ", frac: 0x" << (bits & 0x7FFFFF)
            << std::dec << std::endl;
}

void printDoubleBits(double d) {
  uint64_t bits{0};
  memcpy(&bits, &d, sizeof(uint64_t));
  std::cout << d << ", s: " << ((bits >> 63) & 1) << std::hex << ", exp: 0x"
            << ((bits >> 52) & 0x7FF) << ", frac: 0x"
            << (bits & 0x000FFFFFFFFFFFFFULL) << std::dec << std::endl;
}

int main(int argc, char **argv) {
  std::cout << "0.1 + 0.2 == 0.3? " << ((0.1 + 0.2 == 0.3) ? "true" : "false")
            << std::endl;
  printDoubleBits(0.1);
  printDoubleBits(0.2);
  printDoubleBits(0.3);
  printDoubleBits((0.1 + 0.2));

  return 0;
}