/* dedup.c —— 实验题3：profile 引导的逐步优化 + Amdahl 验证
 *
 * 程序对 N 个（含大量重复的）字符串去重，并算一个校验和。
 *   - dedup_quadratic : O(n^2) 线性查找去重 —— 故意埋的热点
 *   - dedup_hash      : O(n) 哈希去重       —— 优化版
 *   - checksum        : 累加所有串长度      —— 占比小，用来演示 Amdahl“优化小头没用”
 *
 * 用法: ./dedup <n2|hash> <N> <R>
 *   mode = n2 | hash ；N = 输入串个数 ；R = 重复去重轮数（拉长运行时间，便于采样）
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAXLEN 24
#define SEED 12345 /* 固定种子：两个版本喂完全相同的输入，对比才公平 */

static char **g_words; /* 输入数组 */
static int g_n;        /* 输入个数 */

/* 生成输入：从 dict 个不同的词里随机取，制造大量重复 */
static void build_input(int n, int dict) {
  g_words = malloc((size_t)n * sizeof(char *));
  g_n = n;
  srand(SEED);
  for (int i = 0; i < n; i++) {
    char buf[MAXLEN];
    snprintf(buf, sizeof buf, "word_%06d", rand() % dict);
    g_words[i] = strdup(buf);
  }
}

/* “小头”函数：对所有串做 djb2 哈希求和。
 * CHECK_REPS 把它放大到一个可观的固定占比，好让 Amdahl 的 (1-α) 天花板看得见；
 * 每轮种子 +rep 让结果随轮变化，阻止 -O2 把它当循环不变量外提掉。 */
#ifndef CHECK_REPS
#define CHECK_REPS 80
#endif
static long checksum(void) {
  long s = 0;
  for (int rep = 0; rep < CHECK_REPS; rep++)
    for (int i = 0; i < g_n; i++) {
      unsigned long h = 5381 + (unsigned)rep;
      for (char *p = g_words[i]; *p; p++)
        h = h * 33 + (unsigned char)*p;
      s += (long)(h & 0xff);
    }
  return s;
}

/* 热点：O(n^2) 去重。每个输入都线性扫描已收集的 uniq 列表逐个 strcmp */
static int dedup_quadratic(char **uniq) {
  int u = 0;
  for (int i = 0; i < g_n; i++) {
    int found = 0;
    for (int j = 0; j < u; j++) {
      if (strcmp(g_words[i], uniq[j]) == 0) {
        found = 1;
        break;
      }
    }
    if (!found)
      uniq[u++] = g_words[i];
  }
  return u;
}

/* 优化版：O(n) 哈希去重（djb2 + 线性探测的开放定址集合） */
static int dedup_hash(char **uniq) {
  int cap = 1;
  while (cap < g_n * 2)
    cap <<= 1; /* 容量取 2 的幂，便于用 &(cap-1) 取模 */
  int *tab = malloc((size_t)cap * sizeof(int));
  for (int i = 0; i < cap; i++)
    tab[i] = -1;

  unsigned long mask = (unsigned long)cap - 1;
  int u = 0;
  for (int i = 0; i < g_n; i++) {
    unsigned long h = 5381;
    for (char *p = g_words[i]; *p; p++)
      h = h * 33 + (unsigned char)*p;
    unsigned long k = h & mask;
    int dup = 0;
    while (tab[k] != -1) {
      if (strcmp(uniq[tab[k]], g_words[i]) == 0) {
        dup = 1;
        break;
      }
      k = (k + 1) & mask;
    }
    if (!dup) {
      tab[k] = u;
      uniq[u++] = g_words[i];
    }
  }
  free(tab);
  return u;
}

static double now_sec(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return ts.tv_sec + ts.tv_nsec * 1e-9;
}

int main(int argc, char **argv) {
  if (argc != 4) {
    fprintf(stderr, "usage: %s <n2|hash> <N> <R>\n", argv[0]);
    return 1;
  }
  int use_hash = (strcmp(argv[1], "hash") == 0);
  int N = atoi(argv[2]);
  int R = atoi(argv[3]);

  build_input(N, /*dict=*/2000);
  char **uniq = malloc((size_t)N * sizeof(char *));

  /* 计时分两段：dedup 段（被优化的热点） vs checksum 段（不动的小头） */
  double t_dedup = 0, t_check = 0;
  long sink = 0;
  int u = 0;
  for (int r = 0; r < R; r++) {
    double t0 = now_sec();
    u = use_hash ? dedup_hash(uniq) : dedup_quadratic(uniq);
    double t1 = now_sec();
    sink += checksum();
    double t2 = now_sec();
    t_dedup += t1 - t0;
    t_check += t2 - t1;
  }

  printf("mode=%-4s N=%d R=%d uniq=%d  | dedup=%.4fs  checksum=%.4fs  total=%.4fs  (sink=%ld)\n",
         argv[1], N, R, u, t_dedup, t_check, t_dedup + t_check, sink);
  return 0;
}
