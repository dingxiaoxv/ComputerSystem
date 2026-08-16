/* §3.11 -0.0 陷阱：为什么编译器不敢把 x<0?-x:x 优化成 andpd
 *
 *   make negzero
 */
#include <stdio.h>
#include <string.h>
#include <math.h>

double fp_abs(double x);    /* x < 0 ? -x : x   —— 编译成分支 */
double fp_fabs(double x);   /* __builtin_fabs   —— 编译成 andpd */

static void show(const char *name, double v)
{
    unsigned long long bits;
    memcpy(&bits, &v, sizeof bits);
    printf("  %-18s = %+g\tbits = 0x%016llx\n", name, v, bits);
}

int main(void)
{
    double nz = -0.0;
    printf("── -0.0 ──（andpd 清符号位，三目表达式原样返回）\n");
    show("fp_abs(-0.0)",  fp_abs(nz));
    show("fp_fabs(-0.0)", fp_fabs(nz));

    /* NaN 也值得一看：NaN < 0 为假，三目版原样返回带符号位的 NaN */
    double nnan = -NAN;
    printf("── -NaN ──（比较结果为「无序」，两条分支都不走）\n");
    show("fp_abs(-NaN)",  fp_abs(nnan));
    show("fp_fabs(-NaN)", fp_fabs(nnan));
    return 0;
}
