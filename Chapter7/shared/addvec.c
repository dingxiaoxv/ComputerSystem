/* add_cnt：调用计数探针。main 引用它，所以 addvec.o 必然被链入，
 * 链接后 nm 能在可执行文件里看到 add_cnt。 */
int add_cnt = 0;

void addvec(int *x, int *y, int *z, int n) {
  int i;
  add_cnt++;
  for (i = 0; i < n; ++i) {
    z[i] = x[i] + y[i];
  }
}