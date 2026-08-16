/* §3.11 两个 ABI 细节：
 *   1. 调用变参函数前 %al 要存「实际使用的向量寄存器个数」
 *   2. 16 个 %xmm 全是 caller-saved，浮点值跨调用只能溢出到栈
 *
 *   make vararg
 */
#include <stdio.h>

/* 故意不给定义：只看调用点的汇编 */
extern double work(double);
extern long   iwork(long);

void p0(void)               { printf("none\n"); }        /* 无浮点参数 → 不设 %al */
void p1(double x)           { printf("%f\n", x); }       /* → movl $1, %eax */
void p2(double x, double y) { printf("%f %f\n", x, y); } /* → movl $2, %eax */

/* 浮点版：x 必须 movsd 到栈上才能跨过 call 存活 */
double keep(double x) { return work(x) + x; }

/* 整数版对照：x 可以待在 callee-saved 寄存器里 */
long keep_int(long x) { return iwork(x) + x; }
