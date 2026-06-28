/* §5.11.2 实验题 4：cmov vs 分支，可预测 vs 随机数据
 *
 * minmax1：分支版（if 交换）         —— 数据随机时预测失败率高
 * minmax2：功能式版（三元表达式）    —— 编译器倾向生成 cmov，无预测失败
 *
 * 编译：gcc -O2 -o cmov_vs_branch cmov_vs_branch.c
 *      gcc -O2 -S cmov_vs_branch.c   # 看 minmax2 是否生成 cmovq，minmax1 是否是跳转
 * 运行：taskset -c 0 ./cmov_vs_branch <branch|cmov> <pred|rand>
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define N (1 << 14)
#define ITERS 50000

/* 分支版：编译器倾向用条件跳转 */
__attribute__((noinline))
void minmax1(long *a, long *b, long n) {
    for (long i = 0; i < n; i++) {
        if (a[i] > b[i]) {
            long t = a[i]; a[i] = b[i]; b[i] = t;
        }
    }
}

/* 功能式版：两边都算、按条件选，编译器倾向 cmov */
__attribute__((noinline))
void minmax2(long *a, long *b, long n) {
    for (long i = 0; i < n; i++) {
        long lo = a[i] < b[i] ? a[i] : b[i];
        long hi = a[i] < b[i] ? b[i] : a[i];
        a[i] = lo; b[i] = hi;
    }
}

int main(int argc, char **argv) {
    int use_cmov = (argc > 1 && strcmp(argv[1], "cmov") == 0);
    int rnd      = (argc > 2 && strcmp(argv[2], "rand") == 0);

    long *a = malloc(N * sizeof(long));
    long *b = malloc(N * sizeof(long));
    srand(999);
    for (long i = 0; i < N; i++) {
        if (rnd) {                 /* 随机：a[i]>b[i] 不可预测 */
            a[i] = rand(); b[i] = rand();
        } else {                   /* 可预测：a[i] 总是 < b[i]，分支几乎不触发 */
            a[i] = i; b[i] = i + N;
        }
    }

    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (int it = 0; it < ITERS; it++) {
        if (use_cmov) minmax2(a, b, N);
        else          minmax1(a, b, N);
    }
    clock_gettime(CLOCK_MONOTONIC, &t1);

    double ns = (t1.tv_sec - t0.tv_sec) * 1e9 + (t1.tv_nsec - t0.tv_nsec);
    printf("%-6s %-5s  %.3f ns/elem  (a[0]=%ld b[0]=%ld)\n",
           use_cmov ? "cmov" : "branch", rnd ? "rand" : "pred",
           ns / ((double)N * ITERS), a[0], b[0]);
    free(a); free(b);
    return 0;
}
