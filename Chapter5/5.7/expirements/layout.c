#include <stdio.h>

#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

extern void hot_path(int);
extern void cold_path_log_error(int);

/* 版本 A：没有任何提示 */
int handle_plain(int v) {
  if (v < 0) {              // 编译器不知道哪条常见
    cold_path_log_error(v); // 假设这是罕见的错误处理
    return -1;
  }
  hot_path(v); // 这才是 99% 走的路
  return 0;
}

/* 版本 B：告诉编译器 v<0 是罕见情况 */
int handle_hinted(int v) {
  if (unlikely(v < 0)) {
    cold_path_log_error(v);
    return -1;
  }
  hot_path(v);
  return 0;
}
