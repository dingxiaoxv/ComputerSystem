void addvec(int *x, int *y, int *z, int n);
void multvec(int *x, int *y, int *z, int n);

/* 两个调用计数器，仅作"探针"，用来观察静态库的选择性抽取：
 * main 只引用 add_cnt、从不引用 mult_cnt（也不调用 multvec）。
 * 链接后 `nm prog | grep cnt` 只能看到 add_cnt，看不到 mult_cnt，
 * 说明 multvec.o 因无人引用而未被链入。 */
extern int add_cnt;
extern int mult_cnt;   /* 故意声明但不在 main 中引用 */