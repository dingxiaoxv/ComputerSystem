#include <stdio.h>

#define likely(x)   __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

volatile int sink;

/* 内联的一段“处理代码”，故意写大一点，方便观察它被整体搬到哪里 */
static inline void block_A(int v) { sink = v*3+1; sink ^= v<<2; sink += v*7; }
static inline void block_B(int v) { sink = v-9;   sink |= v>>1; sink -= v*5; }

/* 版本 A：认为 cond 通常成立 -> A 是热块 */
int expect_true(int v) {
    if (likely(v > 0)) { block_A(v); return 1; }
    else               { block_B(v); return 0; }
}

/* 版本 B：认为 cond 通常不成立 -> B 是热块 */
int expect_false(int v) {
    if (unlikely(v > 0)) { block_A(v); return 1; }
    else                 { block_B(v); return 0; }
}
