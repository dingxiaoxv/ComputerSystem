/* 实验 A：观察 cmov vs jmp 的编译选择
 *
 * 编译命令：
 *   gcc -O1 -S branch.c -o branch_O1.s
 *   gcc -O2 -S branch.c -o branch_O2.s
 *
 * 观察问题：
 *   1. simple_branch 在 -O1/-O2 下是否出现 cmov 指令？
 *   2. side_effect_branch 为什么不能用 cmov？汇编里的控制流是什么样的？
 *   3. expensive_branch 编译器选了 cmov 还是 jmp？为什么？
 */

/* 场景1：简单三元表达式——编译器倾向 cmov */
long simple_branch(long x, long y) {
    return x > y ? x - y : y - x;  /* abs(x-y) */
}

/* 场景2：有副作用——编译器必须用 jmp */
long *gp;
long side_effect_branch(long x, long y) {
    return x > y ? (*gp = x, x - y) : y - x; // 使用cmov会在每个分支执行*gp = x，这不符合代码
}

/* 场景3：计算代价大——编译器通常选 jmp（避免白算） */
long expensive_branch(long x, long y) {
    long a = x * x * x;
    long b = y * y * y;
    return x > 0 ? a : b;
    // o1 o2 都选择里jump
}
