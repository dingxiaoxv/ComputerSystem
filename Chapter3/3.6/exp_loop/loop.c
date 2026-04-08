/* 实验 C（选做）：循环的汇编变换
 *
 * 编译命令：
 *   gcc -O0 -S loop.c -o loop_O0.s
 *   gcc -O1 -S loop.c -o loop_O1.s
 *
 * 观察问题：
 *   1. sum_while 和 sum_for 在 -O1 下的汇编是否相同？
 *   2. 找到 guarded-do 模式：先 jmp 到循环末尾测试的那一跳
 *   3. -O0 vs -O1 的控制流结构有何差异？
 */

long sum_while(long n) {
    long s = 0, i = 1;
    while (i <= n) {
        s += i;
        i++;
    }
    return s;
}

long sum_for(long n) {
    long s = 0;
    for (long i = 1; i <= n; i++)
        s += i;
    return s;
}
