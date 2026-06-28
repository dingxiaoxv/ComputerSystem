# §5.4-5.6 三招基础优化：搬出循环、绕过封装、消除内存引用

这三节是对 §5.3 基线 `combine1` 的连续改造，每一节干掉一个低效点：**循环不变的函数调用**（5.4）、**逐元素的过程调用开销**（5.5）、**每次迭代的内存读写**（5.6）。目标是把 CPE 从 ~10 一路压到接近 1，且全程不依赖任何对处理器微架构的了解——这些是"任何机器上都该先做"的通用优化。

---

## 5.4 消除循环低效率：代码移动（code motion）

**🎯 问题：循环不变量被反复计算**

`combine1` 的循环条件 `i < vec_length(v)` 每次迭代都调用一次 `vec_length`。对长度 n 的向量，`vec_length` 被调了 n+1 次，而它的返回值根本不变。

**🎯 combine2：把不变量提到循环外**

```c
void combine2(vec_ptr v, data_t *dest) {
    long i;
    long length = vec_length(v);    // 只算一次，移出循环
    *dest = IDENT;
    for (i = 0; i < length; i++) {
        data_t val;
        get_vec_element(v, i, &val);
        *dest = *dest OP val;
    }
}
```

这种把循环内不随迭代变化的计算挪到循环外的变换，叫**代码移动 / 循环不变量外提（loop-invariant code motion）**。

**⚠️ 为什么编译器不替你做**

`vec_length` 是个**函数调用**，编译器无法确定它每次返回值相同（可能有副作用、可能受全局状态影响——又回到 §5.1 的函数调用障碍）。所以这一步通常得靠程序员手动完成。

**🔧 真实灾难案例：strlen 在循环条件里**

```c
// lower1：把字符串转小写，每次循环都调 strlen —— O(n²)
void lower1(char *s) {
    for (long i = 0; i < strlen(s); i++)
        if (s[i] >= 'A' && s[i] <= 'Z')
            s[i] -= ('A' - 'a');
}
// lower2：strlen 提到循环外 —— O(n)
void lower2(char *s) {
    long len = strlen(s);
    for (long i = 0; i < len; i++) ...
}
```

`strlen` 自身是 O(n)，放进循环条件就让整个函数变成 **O(n²)**。字符串短时无感，长度到几十万时 `lower1` 比 `lower2` 慢成千上万倍。这是 code motion 最经典、危害最大的现实场景。

---

## 5.5 减少过程调用：绕过抽象封装

**🎯 问题：每个元素一次函数调用 + 边界检查**

`combine2` 内层每次迭代调用 `get_vec_element`，它做边界检查再返回元素。n 个元素就是 n 次函数调用开销和 n 次边界检查。

**🎯 combine3：直接拿数组指针，循环里下标访问**

```c
void combine3(vec_ptr v, data_t *dest) {
    long i;
    long length = vec_length(v);
    data_t *data = get_vec_start(v);    // 一次性拿到底层数组首地址
    *dest = IDENT;
    for (i = 0; i < length; i++) {
        *dest = *dest OP data[i];        // 直接访问，不再每次调用函数
    }
}
```

**⚠️ 反直觉结果：CPE 几乎没变**

按理说省掉 n 次函数调用应该快很多，但实测 `combine3` 相比 `combine2` 提升很小。原因是：**瓶颈不在过程调用，而在内层那条 `*dest = *dest OP data[i]` 的内存读写依赖链**。函数调用开销被处理器的其他机制（流水线、分支预测）掩盖了，真正卡住的是下一节才解决的内存引用。

**⚠️ 软件工程代价**

`combine3` 直接捅进了向量的内部表示，**破坏了封装**：丢掉了边界检查（越界不再报错）、绑死了数据布局。这是性能与抽象/安全之间的真实权衡——不是所有场景都值得这么做。

---

## 5.6 消除不必要的内存引用：用寄存器累加

**🎯 问题：累加结果每次都往内存里搬**

`combine3` 内层 `*dest = *dest OP data[i]` 每次迭代都要：**从 `*dest` 读一次 + 写回一次**。看汇编（double 求和，`-O1`）：

```asm
.L17:
    movsd  (%rbx), %xmm0        # 读 *dest
    mulsd  (%rdx), %xmm0        # *dest OP data[i]
    movsd  %xmm0, (%rbx)        # 写回 *dest  ← 每次迭代都有这条
    addq   $8, %rdx
    cmpq   %rax, %rdx
    jne    .L17
```

每轮多一次 store + load，而它们完全可以攒到最后再做。

**🎯 combine4：局部变量累加，循环结束才写回**

```c
void combine4(vec_ptr v, data_t *dest) {
    long i;
    long length = vec_length(v);
    data_t *data = get_vec_start(v);
    data_t acc = IDENT;                  // 累加器，会被分配到寄存器
    for (i = 0; i < length; i++) {
        acc = acc OP data[i];           // 全程在寄存器里累加，不碰内存
    }
    *dest = acc;                        // 循环结束写回一次
}
```

`acc` 驻留在寄存器，内层循环彻底没有了对 `dest` 的读写。

**🎯 效果：CPE 大幅下降**

| 版本 | int + | int * | float + | double * | 关键改进 |
|------|-------|-------|---------|----------|----------|
| combine1 (-O1) | ~10 | ~10 | ~10 | ~11 | 基线 |
| combine2 | ~7 | ~9 | ~9 | ~11 | 外提 vec_length |
| combine3 | ~7 | ~9 | ~9 | ~11 | 去函数调用（几乎无感） |
| **combine4** | **~1.3** | **~3.0** | **~3.0** | **~5.0** | **去内存引用** |

整数加法 CPE 直接掉到约 1.3。注意 combine4 不同运算的 CPE 不同（加法 ~1、乘法/浮点 ~3~5），这是被各运算的**延迟界限（latency bound）**卡住了——这个上限和怎么突破它，是 §5.7~5.9 的内容。

**⚠️ 为什么编译器还是不替你做**

又是**内存别名**：编译器无法排除 `dest` 指向 `data` 数组内部的可能。若 `dest == &data[i]`，那么每次写 `*dest` 都会改变后面要读的 `data[i]`，combine3 和 combine4 的结果就会不同。所以编译器必须老老实实保留每次的内存读写，引入 `acc` 这一步只能程序员来做。

---

## 易错点

- 把循环不变量提出循环不是编译器一定会做的"小事"——只要不变量来自函数调用，编译器多半不敢动，得手动外提
- `for (i=0; i<strlen(s); i++)` 这类写法是隐藏的 O(n²)，短串无感、长串致命，是 code motion 的头号现实陷阱
- combine3 省了函数调用却几乎不提速——别误以为"减少函数调用"总是有效，要先确认瓶颈在不在那里
- 绕过封装（combine3）不是免费的：丢了边界检查、绑死了数据结构，是拿安全性换性能
- combine4 提速的本质是"内存引用变成寄存器引用"，不是"少写了几行代码"
- combine4 后整数加法和浮点乘法 CPE 不一样，不是代码问题，是受运算延迟界限限制（§5.7 才解释）
- 编译器不把 `*dest` 累加自动改成寄存器累加，根因仍是内存别名（dest 可能指向 data 内部），不是编译器偷懒

---

## 工程关联

- `-O2` 下 GCC 能完成部分 code motion，但跨函数调用的循环不变量（如 `vec_length`、`strlen`）通常仍需人工外提；`strlen` 在循环条件里是 code review 必抓的反模式
- combine3 破坏封装对应真实工程中"为热点路径牺牲抽象"的决策：如内核 / 高频交易里直接操作底层 buffer，而非走 getter
- combine4 的寄存器累加是所有归约（reduce / 求和 / 点积）的标准写法；OpenMP、SIMD 向量化都建立在"累加器在寄存器"这个前提上
- 内存别名障碍可用 `restrict` 解除：给 `dest` 和 `data` 加 `restrict` 后，`-O2` 的 GCC 自己就能做 combine4 那步寄存器累加
- 用 `perf stat` 看 combine3 → combine4 的变化：`mem_inst_retired`（内存指令）应显著下降，而 `instructions` 总数变化不大，直接印证"省的是内存引用"
- 汇编里内层循环是否每轮都有一条写回内存的 `mov ..., (%reg)`，是判断"有没有不必要内存引用"的最快方法

---

## 实验题

**🧪 题 1：strlen 的 O(n²) 灾难**

```c
void lower1(char *s) { for (long i=0; i<strlen(s); i++) /* to lower */ ; }
void lower2(char *s) { long n=strlen(s); for (long i=0; i<n; i++) /* to lower */ ; }
```

要求：

- 生成长度 2^10、2^12、…、2^20 的随机大写字符串，分别用 `__rdtsc()` 测 `lower1` / `lower2` 耗时
- 画出耗时-长度曲线，确认 `lower1` 是二次增长、`lower2` 是线性
- `gcc -O2 -S` 看 `lower1`，确认 `call strlen` 出现在循环内部，编译器没把它提出来

**🧪 题 2：复刻 combine1→combine4 的 CPE 阶梯**

实现 combine1~combine4 四个版本（向量 ADT + `OP`/`IDENT` 宏），对 `data_t = int, OP = +` 和 `data_t = double, OP = *` 两套配置：

要求：

- 用周期计数器测各版本 CPE，复现"~10 → ~7 → ~7 → ~1.3"的阶梯（数值因机器而异，看趋势）
- 重点验证：combine2→combine3 几乎无变化，combine3→combine4 大幅下降
- 解释这条阶梯每一级省掉了什么

**🧪 题 3：内存引用的汇编证据**

```c
// 对 combine3 和 combine4 各取内层循环
```

要求：

- `gcc -O1 -S` 分别生成 combine3、combine4 的汇编
- 在 combine3 内层循环里找出每轮都执行的写回内存指令（形如 `movsd %xmm0, (%rbx)`）
- 确认 combine4 内层循环里这条写回消失了，累加全在 `%xmm` / 通用寄存器中进行

**🧪 题 4：restrict 让编译器自己做 combine4**

```c
void combine3r(long length, data_t * restrict data, data_t * restrict dest) {
    *dest = IDENT;
    for (long i = 0; i < length; i++) *dest = *dest OP data[i];
}
```

要求：

- 不加 `restrict` 时 `-O2 -S`，确认内层每轮仍有写回 `*dest`
- 加上 `restrict` 后重新编译，观察编译器是否自动把累加放进寄存器、消除了每轮的内存写
- 结合 §5.1 解释：`restrict` 是如何替编译器排除别名、从而解锁这步优化的
