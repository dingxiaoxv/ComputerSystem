/* §5.11.1 实验题 2：寄存器溢出拐点
 *
 * 整数乘法 combine，K 路累加器（K = 1..20）。
 * K 增大时 CPE 先降（并行度上升），超过可用寄存器后回升（累加器溢出到栈）。
 * 用 __rdtsc 测 TSC 周期（相对比较即可，绝对值因 TSC≠核心频率会偏小）。
 *
 * 编译：gcc -O2 -o reg_spill reg_spill.c
 *      gcc -O2 -S reg_spill.c   # 对比 acc6 vs acc20，看高路数版是否出现 (%rsp) load/store
 */
#include <stdio.h>
#include <stdlib.h>
#include <x86intrin.h>

#define N 4096
#define ITERS 200000

/* 用宏批量生成 K 路累加器版本，避免手写 20 份 */
#define MAKE_COMBINE(K)                                       \
__attribute__((noinline))                                     \
long combine##K(long *d, long n) {                            \
    long acc[K];                                              \
    for (int j = 0; j < K; j++) acc[j] = 1;                   \
    long i;                                                   \
    for (i = 0; i + K <= n; i += K)                           \
        for (int j = 0; j < K; j++) acc[j] *= d[i + j];       \
    for (; i < n; i++) acc[0] *= d[i];                        \
    long r = 1;                                               \
    for (int j = 0; j < K; j++) r *= acc[j];                  \
    return r;                                                 \
}

MAKE_COMBINE(1)  MAKE_COMBINE(2)  MAKE_COMBINE(3)  MAKE_COMBINE(4)
MAKE_COMBINE(5)  MAKE_COMBINE(6)  MAKE_COMBINE(8)  MAKE_COMBINE(10)
MAKE_COMBINE(12) MAKE_COMBINE(16) MAKE_COMBINE(20)

typedef long (*combine_fn)(long *, long);

static void bench(const char *name, combine_fn fn, long *d) {
    /* 预热 */
    volatile long s = fn(d, N);
    unsigned long long best = ~0ULL;
    for (int rep = 0; rep < 5; rep++) {
        unsigned long long c0 = __rdtsc();
        for (int it = 0; it < ITERS; it++) s ^= fn(d, N);
        unsigned long long c1 = __rdtsc();
        if (c1 - c0 < best) best = c1 - c0;
    }
    double cpe = (double)best / ((double)N * ITERS);
    printf("%-10s CPE(TSC) = %.3f\n", name, cpe);
    (void)s;
}

int main(void) {
    long *d = malloc(N * sizeof(long));
    for (int i = 0; i < N; i++) d[i] = (i % 7) + 1;   /* 避免溢出/0 */

    printf("K 路累加器整数乘法 CPE（TSC 周期，相对比较）：\n");
    bench("K=1",  combine1,  d);
    bench("K=2",  combine2,  d);
    bench("K=3",  combine3,  d);
    bench("K=4",  combine4,  d);
    bench("K=5",  combine5,  d);
    bench("K=6",  combine6,  d);
    bench("K=8",  combine8,  d);
    bench("K=10", combine10, d);
    bench("K=12", combine12, d);
    bench("K=16", combine16, d);
    bench("K=20", combine20, d);
    free(d);
    return 0;
}
