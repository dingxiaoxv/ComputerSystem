/* mult_cnt：调用计数探针。main 从不引用 multvec 和 mult_cnt，
 * 没有任何未解析符号指向 multvec.o，所以它不会被从库中抽取，
 * 链接后 nm 在可执行文件里看不到 mult_cnt（对照 add_cnt）。 */
int mult_cnt = 0;

void multvec(int *x, int *y, int *z, int n) {
  int i;
  mult_cnt++;
  for (i = 0; i < n; ++i) {
    z[i] = x[i] * y[i];
  }
}