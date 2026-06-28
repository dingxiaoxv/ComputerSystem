/* §5.11.2 实验题 3：分支预测失败——排序 vs 未排序
 *
 * 同一份随机数据，一份原样、一份排序后，跑同一个含数据相关分支的循环。
 * 排序后分支高度可预测，未排序时预测器频频失手。
 *
 * 编译：gcc -O2 -o branch_predict branch_predict.c
 * 运行：taskset -c 0 ./branch_predict sorted
 *       taskset -c 0 ./branch_predict unsorted
 * perf：taskset -c 0 perf stat -e cpu_core/branches/,cpu_core/branch-misses/,cpu_core/cycles/,cpu_core/instructions/ ./branch_predict unsorted
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define N (1 << 16)     /* 65536 个元素 */
#define ITERS 20000     /* 重复多轮放大信号 */

static int cmp_int(const void *a, const void *b) {
    return (*(const int *)a) - (*(const int *)b);
}

int main(int argc, char **argv) {
    int sorted = (argc > 1 && strcmp(argv[1], "sorted") == 0);

    int *data = malloc(N * sizeof(int));
    srand(12345);
    for (int i = 0; i < N; i++)
        data[i] = rand() % 256;     /* 0..255，阈值 128 时一半概率走分支 */

    if (sorted)
        qsort(data, N, sizeof(int), cmp_int);

    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);

    long sum = 0;
    for (int it = 0; it < ITERS; it++) {
        for (int i = 0; i < N; i++) {
            if (data[i] >= 128)     /* ← 待观察的数据相关分支 */
                sum += data[i];
        }
    }

    clock_gettime(CLOCK_MONOTONIC, &t1);
    double ns = (t1.tv_sec - t0.tv_sec) * 1e9 + (t1.tv_nsec - t0.tv_nsec);
    double per = ns / ((double)N * ITERS);

    printf("%-9s sum=%ld  time=%.3f s  %.3f ns/elem\n",
           sorted ? "sorted" : "unsorted", sum, ns / 1e9, per);
    free(data);
    return 0;
}
