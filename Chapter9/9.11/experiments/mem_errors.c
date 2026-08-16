/* §9.11 常见的与内存有关的错误：每类错误一个函数，命令行选择触发
 *
 *   编译运行： make            # 普通版，多数错误"看起来正常"
 *             make asan       # ASan 版，精确定位
 *             make msan-note  # 说明为什么 uninit 要用 valgrind/MSan
 *
 *   用法：./mem_errors <类型>
 *   类型：badptr uninit stack_overflow ptrsize offbyone deref_ptr
 *        ptr_arith dangling_stack use_after_free leak
 *
 * 注意：本文件是**故意写错**的教学样本，不要当代码范例。
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── 错误 1：间接引用坏指针 ───────────────────────────────────
 * scanf 要的是地址，传了值 → 把 val 的内容当地址写，多半 SIGSEGV */
static void err_badptr(void)
{
    int val = 0;
    printf("在下面输入一个整数（scanf 少写了 &）：\n");
    scanf("%d", (int *)(long)val);   /* 正确写法：scanf("%d", &val) */
    printf("val = %d\n", val);
}

/* ── 错误 2：读未初始化的内存 ─────────────────────────────────
 * malloc 不清零（calloc 才清零），累加前必须自己初始化 */
static void err_uninit(void)
{
    int n = 8;
    int *y = malloc(n * sizeof(int));      /* 内容是垃圾 */
    long sum = 0;
    for (int i = 0; i < n; i++)
        sum += y[i];                       /* 读未初始化内存 */
    printf("sum = %ld （每次运行可能不同）\n", sum);
    free(y);
}

/* ── 错误 3：栈缓冲区溢出 ─────────────────────────────────────
 * 缓冲区只有 8 字节，却读入任意长度 */
static void err_stack_overflow(void)
{
    char buf[8];
    printf("输入一串长文本（缓冲区只有 8 字节）：\n");
    if (scanf("%s", buf) == 1)             /* 正确写法：scanf("%7s", buf) */
        printf("buf = %s\n", buf);
}

/* ── 错误 4：把指针大小当成它指向对象的大小 ───────────────────
 * sizeof(int *) == 8，sizeof(int) == 4 → 在 64 位上分配了 2 倍，
 * 但如果反过来写（指向 double 的指针数组）就会分配不足 */
static void err_ptrsize(void)
{
    int n = 4;
    /* 想要 n 个 int* 的数组，却按 int 的大小算 */
    int **a = malloc(n * sizeof(int));     /* 正确：n * sizeof(int *) */
    for (int i = 0; i < n; i++)
        a[i] = NULL;                       /* 后半段越界写 */
    printf("a[%d] 写完，未崩溃不代表没错\n", n - 1);
    free(a);
}

/* ── 错误 5：错位错误（off-by-one）────────────────────────────
 * 循环写到 a[n]，正好越过末尾一个元素 */
static void err_offbyone(void)
{
    int n = 4;
    int *a = malloc(n * sizeof(int));
    for (int i = 0; i <= n; i++)           /* 正确：i < n */
        a[i] = i;
    printf("a[0..%d] 写完\n", n);
    free(a);
}

/* ── 错误 6：引用指针本身，而不是它指向的对象 ─────────────────
 * *size-- 先算 *size 再让指针自减，不是让计数自减 */
static void err_deref_ptr(int *size)
{
    int cnt = 0;
    while (*size > 0) {                    /* size 指向一个计数器 */
        cnt++;
        if (cnt > 3) {                     /* 防止真的跑飞，演示到此为止 */
            printf("*size-- 让**指针**动了，计数器纹丝不动 → 死循环\n");
            return;
        }
        /* 正确写法：(*size)-- */
        *size--;                           /* 实际是 *(size--) */
    }
}

/* ── 错误 7：误解指针运算 ─────────────────────────────────────
 * p + 1 加的是 sizeof(*p) 字节，不是 1 字节 */
static void err_ptr_arith(void)
{
    int a[4] = {10, 20, 30, 40};
    int *p = a;
    /* 想跳过 1 个 int（4 字节），却按字节算 */
    int *wrong = (int *)((char *)p + 1);   /* 未对齐，跨了元素边界 */
    printf("p[1]     = %d （正确：+1 个元素）\n", *(p + 1));
    printf("byte+1   = %d （错误：+1 字节，读到跨界的垃圾）\n", *wrong);
}

/* ── 错误 8：引用不存在的变量（返回栈地址）───────────────────
 * 函数返回后栈帧失效，指针悬空 */
static int *err_dangling_stack(void)
{
    int local = 42;
    return &local;                         /* 返回后 local 生命周期已结束 */
}

/* ── 错误 9：引用空闲堆块中的数据（use-after-free / double free）*/
static void err_use_after_free(void)
{
    int *a = malloc(4 * sizeof(int));
    for (int i = 0; i < 4; i++) a[i] = i;
    free(a);
    printf("free 之后读 a[0] = %d\n", a[0]);   /* use-after-free */
    free(a);                                   /* double free */
}

/* ── 错误 10：内存泄漏 ────────────────────────────────────────
 * 早退路径漏掉 free；循环里反复分配 */
static void err_leak(void)
{
    for (int i = 0; i < 3; i++) {
        char *buf = malloc(1024);
        if (buf == NULL) return;
        memset(buf, 'x', 1024);
        if (i == 1) continue;              /* 早退路径漏了 free */
        free(buf);
    }
    char *never = malloc(4096);            /* 直接不释放 */
    (void)never;
    printf("泄漏了 1024 + 4096 字节\n");
}

int main(int argc, char *argv[])
{
    if (argc != 2) {
        fprintf(stderr,
            "用法: %s <badptr|uninit|stack_overflow|ptrsize|offbyone|\n"
            "          deref_ptr|ptr_arith|dangling_stack|use_after_free|leak>\n",
            argv[0]);
        return 1;
    }
    const char *w = argv[1];
    if      (!strcmp(w, "badptr"))         err_badptr();
    else if (!strcmp(w, "uninit"))         err_uninit();
    else if (!strcmp(w, "stack_overflow")) err_stack_overflow();
    else if (!strcmp(w, "ptrsize"))        err_ptrsize();
    else if (!strcmp(w, "offbyone"))       err_offbyone();
    else if (!strcmp(w, "deref_ptr"))    { int n = 5; err_deref_ptr(&n); }
    else if (!strcmp(w, "ptr_arith"))      err_ptr_arith();
    else if (!strcmp(w, "dangling_stack")) {
        int *p = err_dangling_stack();
        printf("悬空指针读到 %d （值可能仍是 42，但栈帧已失效）\n", *p);
    }
    else if (!strcmp(w, "use_after_free")) err_use_after_free();
    else if (!strcmp(w, "leak"))           err_leak();
    else { fprintf(stderr, "未知类型: %s\n", w); return 1; }
    return 0;
}
