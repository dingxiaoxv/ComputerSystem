#include <iostream>
#include <string>
#include <variant>

template <class... Ts> struct overloaded : Ts... {
  using Ts::operator()...;
};
template <class... Ts> overloaded(Ts...) -> overloaded<Ts...>;

using Shape = std::variant<int, double, std::string>;

void describe(const Shape &s) {
  std::visit(overloaded{
                 [](int n) { std::cout << "整数 " << n << '\n'; },
                 [](double d) { std::cout << "小数 " << d << '\n'; },
                 [](const std::string &s) { std::cout << "字符串 " << s << '\n'; },
             },
             s);
}

int main() {
  describe(42);      // 整数 42
  describe(3.14);    // 小数 3.14
  describe("hello"); // 字符串 hello
}