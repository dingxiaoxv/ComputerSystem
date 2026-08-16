/* §3.11 浮点代码：用最小函数集覆盖浮点汇编的 6 个话题
 *
 *   编译观察：gcc -Og -S fp_asm.c -o fp_asm.s
 *   反汇编  ：gcc -Og -c fp_asm.c && objdump -d fp_asm.o
 *   看常数池：objdump -s -j .rodata fp_asm.o
 */

/* ---- 话题 1：参数传递与返回值都走 %xmm ---- */
double fp_add(double a, double b) { return a + b; }

/* ---- 话题 2：整型和浮点各自独立计数寄存器 ---- */
/* i1→%edi  d1→%xmm0  i2→%esi  d2→%xmm1  i3→%edx  返回值→%xmm0 */
double fp_mix(int i1, double d1, int i2, double d2, int i3)
{
    return d1 * i1 + d2 * i2 + i3;
}

/* ---- 话题 3：转换指令的方向和"截断"语义 ---- */
double  i2d(int x)      { return (double)x; }   /* vcvtsi2sd  int   → double */
double  f2d(float x)    { return (double)x; }   /* vcvtss2sd  float → double */
float   d2f(double x)   { return (float)x;  }   /* vcvtsd2ss  double→ float  */
int     d2i(double x)   { return (int)x;    }   /* vcvttsd2si 截断，不是舍入 */
long    d2l(double x)   { return (long)x;   }
unsigned d2u(double x)  { return (unsigned)x; } /* 无符号转换要绕道 64 位 */

/* ---- 话题 4：符号操作是位运算，不是算术 ---- */
double fp_neg(double x) { return -x; }          /* xorpd  掩码 0x8000...0 */
/* 手写三目：编译器**不敢**优化成 andpd，因为 -0.0 和 NaN 的行为不一样 */
double fp_abs(double x) { return x < 0 ? -x : x; }
/* 内建 fabs：语义就是"清符号位"，编译器直接给 andpd */
double fp_fabs(double x) { return __builtin_fabs(x); }

/* ---- 话题 5：比较用 vucomisd，NaN 走 PF（奇偶标志） ---- */
int fp_lt(double a, double b) { return a < b; }
int fp_eq(double a, double b) { return a == b; }
/* 三路比较：把 NaN 单独分出来，能看清 PF 的作用 */
int fp_cmp3(double a, double b)
{
    if (a < b)  return -1;
    if (a > b)  return 1;
    if (a == b) return 0;
    return 2;               /* 只有 NaN 参与时才会走到这里（无序） */
}

/* ---- 话题 6：浮点常数从 .rodata 加载，没有立即数形式 ---- */
double fp_scale(double x) { return x * 3.14 + 1.0; }

/* 特例：0.0 和 1.0 也一样要加载（0.0 常被优化成 vxorpd 自异或） */
double fp_zero(void) { return 0.0; }
double fp_one(void)  { return 1.0; }

/* ---- 附：float 与 double 的向量寄存器是同一套，只是用低位宽度不同 ---- */
float flt_add(float a, float b) { return a + b; }
