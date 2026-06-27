/* mountain.c —— 自己机器上的「存储器山」
 *
 * 思路（CSAPP §6.6.1）：用一个 test(elems, stride) 反复读数组，
 *   - 工作集大小 size = elems * sizeof(long) 控制「数据落在哪一级存储」
 *   - 步长 stride 控制「空间局部性好不好」（stride=1 最好，越大越差）
 * 读吞吐量(MB/s) = 读到的字节数 / 耗时。把 (size, stride) 两维扫一遍，
 * 输出一张二维表——这就是存储器山的等高线数据。
 *
 * 编译：gcc -O2 -o mountain mountain.c
 * 运行：taskset -c 0 ./mountain > mountain.csv   # 绑核，避免迁移污染数据
 */
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define MINBYTES (1L << 14) /* 16 KB：最小工作集 */
#define MAXBYTES (1L << 27) /* 128 MB：最大工作集 */
#define MAXSTRIDE 16        /* 步长 1..16（单位：long，即 ×8 字节） */
#define MAXELEMS (MAXBYTES / sizeof(long))

static long data[MAXELEMS]; /* 全局大数组 */

static void init_data(long *d, long n) {
    for (long i = 0; i < n; i++) d[i] = i;
}

/* 4 路展开累加，制造足够多的独立 load、压满访存流水线 */
static long test(long elems, long stride) {
    long sx2 = stride * 2, sx3 = stride * 3, sx4 = stride * 4;
    long acc0 = 0, acc1 = 0, acc2 = 0, acc3 = 0;
    long limit = elems - sx4, i;
    for (i = 0; i < limit; i += sx4) {
        acc0 += data[i];
        acc1 += data[i + stride];
        acc2 += data[i + sx2];
        acc3 += data[i + sx3];
    }
    for (; i < elems; i += stride) acc0 += data[i];
    return (acc0 + acc1) + (acc2 + acc3);
}

static double now_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

/* 返回读吞吐量 MB/s；自适应重复以拿到稳定计时 */
static double run(long size, long stride) {
    long elems = size / sizeof(long);
    long touched = elems / stride; /* 实际读到的元素个数 */
    if (touched <= 0) return 0;

    test(elems, stride); /* 预热 */

    long reps = 1;
    double t;
    /* 小工作集一遍太快，倍增重复次数直到耗时 >= 50ms */
    for (;;) {
        double start = now_sec();
        volatile long sink = 0;
        for (long r = 0; r < reps; r++) sink += test(elems, stride);
        (void)sink;
        t = now_sec() - start;
        if (t >= 0.05) break;
        reps *= 2;
    }
    double bytes = (double)touched * sizeof(long) * reps;
    return bytes / t / 1e6; /* MB/s（十进制 MB） */
}

int main(void) {
    init_data(data, MAXELEMS);

    /* CSV 表头：第一列工作集，其余每列一个 stride */
    printf("size");
    for (long s = 1; s <= MAXSTRIDE; s++) printf(",s%ld", s);
    printf("\n");

    for (long size = MINBYTES; size <= MAXBYTES; size <<= 1) {
        if (size >= (1 << 20)) printf("%ldM", size >> 20);
        else printf("%ldK", size >> 10);
        for (long s = 1; s <= MAXSTRIDE; s++)
            printf(",%.0f", run(size, s));
        printf("\n");
        fflush(stdout);
    }
    return 0;
}
