#include <stdio.h>

/***** expiremen 2 *****/
int sum_index(int *a, long n) {
  int s = 0;
  for (long i = 0; i < n; i++)
    s += a[i];
  return s;
}

int sum_ptr(int *a, long n) {
  int s = 0;
  int *end = a + n;
  while (a < end)
    s += *a++;
  return s;
}

/***** expiremen 3 *****/
int get_3x5(int A[3][5], long i, long j) { return A[i][j]; }
int get_4x7(int A[4][7], long i, long j) { return A[i][j]; }
int get_3x6(int A[3][6], long i, long j) { return A[i][j]; }

/***** expiremen 4 *****/
#define N 2048
int A[N][N];

long sum_row_major() {
  long s = 0;
  for (int i = 0; i < N; i++)
    for (int j = 0; j < N; j++)
      s += A[i][j];
  return s;
}

long sum_col_major() {
  long s = 0;
  for (int j = 0; j < N; j++)
    for (int i = 0; i < N; i++)
      s += A[i][j];
  return s;
}

int main(int argc, char **argv) {
  if (argc < 2)
    return 1;
  long s = (argv[1][0] == 'r') ? sum_row_major() : sum_col_major();
  printf("%ld\n", s);
  return 0;
}
