/* 实验 B：跳转表的真实样子
 *
 * 编译命令：x
 *   gcc -O1 -S switch_demo.c           # 查看汇编中的 jmp * 和跳转表
 *   gcc -O1 -o switch_demo switch_demo.c
 *   objdump -d switch_demo | grep -A 40 "<switch_demo>"
 *
 * 观察问题：
 *   1. 汇编中超界检查（cmp + ja）的 bound 是多少？ --> cmpq	$5, %rdi
 *   2. 找到 jmp * 间接跳转指令，它用的是什么寻址方式？
 *   3. 如果把 case 改成 1, 100, 1000（稀疏），汇编会变成什么？
 */

#include <stdio.h>

long switch_demo(long x) {
    long result = 0;
    switch (x) {
        case 1:  result = 10; break;
        case 2:  result = 20; break;
        case 3:  result = 30; break;
        case 4:  result = 40; break;
        case 5:  result = 50; break;
        default: result = -1;
    }
    return result;
}

int main(void) {
    for (long i = 0; i <= 6; i++)
        printf("switch(%ld) = %ld\n", i, switch_demo(i));
    return 0;
}
