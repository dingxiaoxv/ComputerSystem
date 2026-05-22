# §3.7 过程

这一节的主线是：**一次函数调用，机器层面到底搬了哪些东西**——栈帧怎么随调用伸缩、`call`/`ret` 如何把控制权和返回地址一起转移、参数走寄存器还是走栈、谁负责保存寄存器、以及递归为什么"天生就对"。

---

## 运行时栈

**🎯 栈向低地址增长**

x86-64 的栈从高地址往低地址生长，`%rsp` 始终指向**栈顶（最低的已用地址）**。`pushq` 让 `%rsp` 减小，`popq` 让 `%rsp` 增大。

```
高地址  ┌──────────────┐
        │  调用者的帧   │
        ├──────────────┤  ← 调用者把第 7+ 个参数、返回地址压在这里
        │  当前函数的帧 │
低地址  └──────────────┘  ← %rsp 指向这里
```

**🎯 栈帧（stack frame）**

每个**正在执行且尚未返回**的函数对应一个栈帧。一个函数需要栈帧，通常是因为：

- 局部变量太多，寄存器放不下
- 对局部变量取了地址（`&x`），必须有内存实体
- 参数超过 6 个，多出来的要靠栈传递

**⚠️ 很多函数根本不建栈帧**

如果局部变量能全部塞进寄存器、且不调用别的函数（叶子函数），编译器**完全不碰 `%rsp`**。本目录的 `leaf` 就是例子：它只是 `y + 2`，汇编里没有任何栈操作。"每次调用都开栈帧"是错误直觉。

---

## 控制转移：`call` 与 `ret`

**🎯 `call` 干了两件事**

`call label` = 把**返回地址**（紧跟 `call` 的下一条指令地址）压栈 + 跳转到 `label`。

```asm
call leaf       # 等价于：pushq <下一条指令地址>;  jmp leaf
```

**🎯 `ret` 干一件事**

`ret` = 从栈顶弹出返回地址到 `%rip`，即 `popq %rip`。

所以 `call` 和 `ret` 是**严格配对**的：`call` 压入的那个返回地址，正好被对应的 `ret` 弹出。栈顶此刻必须正是返回地址——这条不变式是函数能正确返回的全部依据。

```
        ┌──────────────────┐
        │    caller 的帧     │
        ├──────────────────┤
        │      返回地址      │ ← call 压入：紧跟 call 的下一条指令地址
%rsp →  └──────────────────┘   ret 执行 popq %rip，正好把它弹走
```

**⚠️ 返回地址在栈上 = 攻击面**

`ret` 无条件信任栈顶的值。一旦缓冲区溢出把返回地址覆盖掉，`ret` 就跳到攻击者指定的位置——这就是 §3.10 栈溢出攻击的根。

---

## 数据传送：参数与返回值

**🎯 前 6 个整数/指针参数走寄存器**

调用约定（System V AMD64 ABI）规定参数寄存器顺序固定：

| 参数序号 | 1 | 2 | 3 | 4 | 5 | 6 |
|---------|---|---|---|---|---|---|
| 64 位 | `%rdi` | `%rsi` | `%rdx` | `%rcx` | `%r8` | `%r9` |
| 32 位 | `%edi` | `%esi` | `%edx` | `%ecx` | `%r8d` | `%r9d` |

**🎯 返回值放 `%rax`**

整数/指针返回值统一放 `%rax`（或其子寄存器 `%eax`/`%ax`/`%al`）。

**⚠️ 第 7 个及以后的参数走栈**

超过 6 个的参数由**调用者**压栈传递，且**逆序压栈**（第 7 个参数离 `%rsp` 最近）。这也是参数多的函数性能略差的原因之一——多了内存读写。

```
高地址  ┌──────────────────┐
        │   第 8 个参数  h   │
        │   第 7 个参数  g   │ ← 调用者逆序压栈，第 7 个离 %rsp 最近
        ├──────────────────┤
        │      返回地址      │ ← 紧接着 call 压入
%rsp →  └──────────────────┘
  参数 1~6 → %rdi %rsi %rdx %rcx %r8 %r9（不碰栈）；返回值 → %rax
```

**🔧 用 `process.c` 看一次完整调用**

`top(100)` → `leaf(tmp)` 的 `-Og` 汇编（已在本目录验证）：

```asm
main:
    movl  $100, %edi      # 第 1 个参数 100 放进 %edi
    call  top             # 压返回地址，跳转
top:
    pushq %rbx            # 保存被调用者保存寄存器（见下节）
    leaq  -5(%rdi), %rbx  # tmp = x - 5，存进 %rbx
    movq  %rbx, %rdi      # tmp 作为参数放进 %rdi
    call  leaf            # 调用 leaf
    addq  %rbx, %rax      # ret(=%rax) + tmp(=%rbx)
    popq  %rbx            # 恢复 %rbx
    ret
```

`leaf` 的返回值在 `%rax`，`top` 直接 `addq %rbx, %rax` 把它和 `tmp` 相加——返回值寄存器约定一目了然。

---

## 栈上的局部存储（§3.7.4）

**🎯 什么时候局部变量被迫上栈**

寄存器不够、取地址、是数组或结构体——只要不能纯寄存器化，编译器就在栈帧里划一块空间，靠 `subq $N, %rsp` 一次性下移栈顶来分配。

```c
long caller() {
    long arg1 = 534;
    long arg2 = 1057;
    long sum = swap_add(&arg1, &arg2);  // 取了地址，arg1/arg2 必须在内存
    ...
}
```

```asm
caller:
    subq  $16, %rsp        # 一次分配 16 字节栈空间
    movq  $534, (%rsp)     # arg1
    movq  $1057, 8(%rsp)   # arg2
    leaq  8(%rsp), %rsi    # &arg2
    movq  %rsp, %rdi       # &arg1
    call  swap_add
    ...
    addq  $16, %rsp        # 一次性回收
    ret
```

```
        ┌──────────────────┐
        │      返回地址      │
        ├──────────────────┤ ← subq $16,%rsp 执行前的栈顶
        │   arg2    8(%rsp) │ ┐ subq 一次划出 16 字节局部区，
        │   arg1     (%rsp) │ ┘ 返回前 addq $16,%rsp 整体收回
%rsp →  └──────────────────┘
```

**⚠️ 分配和回收是对称的 `sub`/`add`**

函数入口 `subq $N,%rsp` 开空间，返回前 `addq $N,%rsp` 收回。漏掉回收会破坏调用者的栈——但正常编译产物一定配平。

**🎯 入栈的两种动作：`pushq` 与 `subq`**

栈上"放数据"有两条不同的路，看汇编时要分清——它们目的不同，不是同一件事的两种写法：

- **`pushq` 一步完成"挪栈顶 + 写值"**：`%rsp -= 8`，同时把一个 8 字节值写进去。用于*保存单个寄存器*（入口 `pushq %rbx`）和*传第 7+ 个参数*，特征是和 `popq` 严格配对。
- **`subq $N,%rsp` + `movq` 分两步**：`subq` 只挪栈顶、一次划出 N 字节空白区，再用 `movq` 按固定偏移往里写。用于*局部变量、数组、结构体*。

为什么局部变量不用 `pushq` 逐个压？因为编译器要让 `%rsp` 在函数体内**保持不动**：局部变量的偏移要固定（`8(%rsp)` 全程有效）、数组/结构体要连续空间、`&x` 要稳定地址、`%rsp` 还要维持 16 字节对齐——一次 `subq` 全部满足，逐个 `pushq` 做不到。

```
        ┌──────────────────┐
pushq → │   保存的旧 %rbx    │  单个 8 字节值，与 popq 配对
        ╞══════════════════╡
        │   局部变量 / 数组   │  ┐
subq  → │   …（N 字节空白）  │  ├ subq 一次划出，movq 按固定偏移写
        │                  │  ┘
%rsp →  └──────────────────┘
```

**🔧 看 prologue / epilogue 判断有没有栈帧**

拿到一个汇编函数，只看**开头几条**和 **`ret` 前几条**，对照三个信号：

- `pushq %rbx` / `%r12`…（压的是 callee-saved 寄存器）→ 在保存寄存器，必有配对的 `popq`
- `subq $N, %rsp` → 开了 N 字节局部变量帧，多半有配对的 `addq $N, %rsp` 或 `leave`
- `call` 前出现 `pushq` 或 `movq ...,(%rsp)` → 在给被调函数传第 7+ 个参数

三者全无 → **叶子函数、零栈帧**，整个函数不碰栈（本目录的 `leaf` 即如此）。反过来从 C 源码也能预判：有 `&局部变量`、有数组/结构体局部量、要调用参数超 6 个的函数、要跨 `call` 长期保存某值——任一为真，就会在汇编里看到对应的入栈动作。

---

## 寄存器中的局部存储：谁来保存（§3.7.5）

这是 §3.7 **最关键、最易错**的一节。寄存器全局只有一组，调用别人时怎么保证自己的值不被踩？ABI 把 16 个通用寄存器分成两类。

**🎯 调用者保存（caller-saved）**

`%rax`、`%rdi`~`%r9`（参数寄存器）、`%r10`、`%r11`。

含义：**被调用者可以随便改这些寄存器**。如果调用者希望 `call` 之后某个 caller-saved 寄存器的值还在，必须**自己**在 `call` 前保存、`call` 后恢复。

例：`top` 调用 `leaf` 后，返回值落在 caller-saved 的 `%rax`，`top` 紧接着 `addq %rbx, %rax` 立刻用掉它——之后不再 `call` 任何函数，`%rax` 没有"被踩"的风险，谁都不用保存。caller-saved 寄存器适合放**算完就用掉、不跨调用**的短命值。

**🎯 被调用者保存（callee-saved）**

`%rbx`、`%rbp`、`%r12`~`%r15`。

含义：**被调用者如果想用这些寄存器，必须先保存、返回前恢复**，让调用者觉得它们"从没被动过"。

例——CSAPP 的经典函数，`P` 连续两次调用 `Q`：

```c
long P(long x, long y) {
    long u = Q(y);   // 第一次调用
    long v = Q(x);   // 第二次调用
    return u + v;
}
```

```asm
P:
    pushq %rbp           # 入口：保存调用者的 %rbp
    pushq %rbx           # 入口：保存调用者的 %rbx
    subq  $8, %rsp       # 对齐栈帧到 16 字节
    movq  %rdi, %rbp     # x 要跨两次 call 存活 → 放 callee-saved %rbp
    movq  %rsi, %rdi
    call  Q              # Q(y)
    movq  %rax, %rbx     # Q(y) 的结果要跨第二次 call → 放 callee-saved %rbx
    movq  %rbp, %rdi
    call  Q              # Q(x)
    addq  %rbx, %rax     # u + v
    addq  $8, %rsp
    popq  %rbx           # 出口：原样恢复
    popq  %rbp
    ret
```

`x` 必须活过第一次 `call Q`、`u`（即 `Q(y)` 的结果）必须活过第二次 `call Q`——两个值都要"跨调用"，所以编译器都把它们放进 callee-saved 寄存器，并在入口一次性 `pushq` 存下这两个寄存器的旧值。这正是「值要跨调用存活 → 放 callee-saved」最完整的演示。

**🔧 `top` 为什么 `push %rbx`**

`top` 里 `tmp` 的值要**跨过 `call leaf` 继续用**（`leaf` 返回后还要 `addq %rbx,%rax`）。

- 若把 `tmp` 放在 caller-saved 寄存器，`leaf` 可能把它覆盖掉
- 编译器选择把 `tmp` 放进 **callee-saved 的 `%rbx`**——这样 `leaf` 哪怕用了 `%rbx` 也必须自己恢复
- 代价是 `top` 自己用了 `%rbx`，所以入口 `pushq %rbx`、出口 `popq %rbx`

一句话：**值需要"跨调用存活" → 编译器倾向放 callee-saved 寄存器。**

```
        ┌──────────────────┐
        │      返回地址      │
        ├──────────────────┤ ← 入口 pushq %rbx 执行前的栈顶
        │   保存的旧 %rbx    │ ← top 要用 %rbx，先把调用者的值存这
%rsp →  └──────────────────┘   出口 popq %rbx 原样恢复
```

**⚠️ 两类寄存器的命名极易反过来记**

"caller-saved"不是"由 caller 用"，而是"**想保命就得 caller 自己存**"。记法：名字里那个角色，就是**有保存义务**的那个角色。

---

## 递归过程（§3.7.6）

**🎯 递归不需要任何特殊机制**

每次递归调用就是一次普通 `call`：新建一个独立栈帧，拥有独立的栈空间和保存的 callee-saved 寄存器。栈天生的"后进先出"正好匹配递归的展开/回卷。

```c
long rfact(long n) {
    if (n <= 1) return 1;
    return n * rfact(n - 1);
}
```

```asm
rfact:
    pushq %rbx           # 保存 %rbx（要跨递归调用存活）
    movl  $1, %eax
    cmpq  $1, %rdi
    jle   .done
    movq  %rdi, %rbx     # 把 n 存进 callee-saved 的 %rbx
    leaq  -1(%rdi), %rdi # n-1
    call  rfact          # 递归
    imulq %rbx, %rax     # n * rfact(n-1)
.done:
    popq  %rbx
    ret
```

**🎯 每层栈帧各存各的 `n`**

`n` 放在 `%rbx`，每层 `rfact` 进入时 `pushq %rbx` 把上一层的 `n` 压栈、退出时 `popq %rbx` 恢复。于是 N 层递归 = 栈上 N 份保存的 `%rbx`，互不干扰。

```
高地址  ┌──────────────────┐
        │ rfact(3) 的栈帧    │ 返回地址 + 保存的 %rbx（这层 n=3）
        ├──────────────────┤
        │ rfact(2) 的栈帧    │ 返回地址 + 保存的 %rbx（这层 n=2）
        ├──────────────────┤
        │ rfact(1) 的栈帧    │ 触底：n<=1，直接返回 1
低地址  └──────────────────┘ ← %rsp；每深一层就多叠一帧
```

**⚠️ 递归深度受栈大小限制**

每层栈帧占用真实栈空间（Linux 默认 8 MB，`ulimit -s` 可查）。递归过深 → 栈溢出 → `SIGSEGV`。这和"逻辑上无限递归"是两码事。

---

## 易错点

- 不是每次调用都建栈帧——叶子函数、局部变量能寄存器化的函数完全不碰 `%rsp`
- `call` 不只是跳转，它**先把返回地址压栈**；`ret` 不只是返回，它**从栈顶弹返回地址**，两者严格配对
- 参数寄存器顺序是 `%rdi,%rsi,%rdx,%rcx,%r8,%r9`，不是按 `%rax` 递推，必须记死
- 第 7 个及以后的参数走栈，而且是**逆序压栈**，不是顺序
- "caller-saved / callee-saved"指的是**谁有保存义务**，不是"谁去使用"——极易记反
- 一个值要跨越 `call` 继续用，编译器才会动用 callee-saved 寄存器并在入口 `push`
- 递归没有专用机制，靠的就是每次 `call` 自动开新栈帧；递归深度被物理栈大小卡死
- `pushq` 是"挪栈顶 + 写一个值"、用于保存寄存器或传参；`subq $N,%rsp` 只"挪栈顶"、用于划出固定大小的局部变量区——两者目的不同，不能混为一谈

---

## 工程关联

- `gdb` 里 `bt`（backtrace）能打印调用链，正是因为每个栈帧都保存了返回地址，沿 `%rbp`/返回地址链就能回溯
- 编译 `-fomit-frame-pointer`（`-O` 默认开启）会省掉 `%rbp` 做帧指针，栈回溯改为靠 `.eh_frame`/CFI 信息——`perf` 采样时若拿不到 CFI 就需要 `--call-graph dwarf`
- `perf` 的火焰图本质就是周期性采样调用栈，函数调用约定决定了它能否正确解出每一层
- 栈溢出（`SIGSEGV` 且地址接近栈底）几乎都来自无限递归或超大栈数组，`ulimit -s` 决定上限
- `ret` 无条件信任栈顶 → ROP（返回导向编程）攻击的基础，现代缓解措施有栈保护金丝雀、`-fstack-protector`、shadow stack
- 函数参数从 7 个开始走栈，热点路径上参数过多会带来额外 load/store，是接口设计时值得注意的微优化点

---

## 实验题

**🧪 题 1：`call`/`ret` 与返回地址**

用本目录的 `process.c`：

```c
long leaf(long y) { return y + 2; }
long top(long x) { long tmp = x - 5; long ret = leaf(tmp); return tmp + ret; }
int main() { return top(100); }
```

要求：

- `gcc -Og -S process.c` 生成汇编，确认 `main` 用 `call top`、`top` 用 `call leaf`
- 用 `objdump -d a.out` 看 `call` 指令的下一条指令地址，说明它就是被压栈的返回地址
- 用 `gdb` 在 `leaf` 入口下断点，执行 `x/gx $rsp` 看栈顶，验证它正是 `top` 中 `call leaf` 的下一条地址

gdb 操作步骤（先 `gcc -Og -g process.c -o a.out` 编译，注意带 `-g`，再 `gdb a.out` 进入）：

```text
(gdb) b leaf                    # 在 leaf 入口下断点
(gdb) run                       # 跑到断点停下
(gdb) x/gx $rsp                 # 看栈顶 8 字节（即返回地址）
(gdb) x/i *(void**)$rsp         # 把栈顶值反汇编成指令，应落在 top 内
(gdb) bt                        # 看调用栈，#1 的地址应与上面一致
(gdb) info registers rsp rip    # 顺便看寄存器
(gdb) c                         # 继续运行到结束
(gdb) quit                      # 退出
```

验证标准：`x/i *(void**)$rsp` 反汇编出的指令应显示为 `<top+偏移>`，且与 `objdump` 里 `call leaf` 的下一条指令吻合——这就证明 `call` 压入的返回地址此刻正躺在栈顶。

**🧪 题 2：叶子函数不建栈帧**

```c
long leaf(long y) { return y + 2; }
```

要求：

- `-Og -S` 编译，确认 `leaf` 的汇编里**没有** `push`/`subq %rsp`/`pop`
- 再写一个取局部变量地址的函数 `long g(long y){ long a=y+2; long*p=&a; return *p+1; }`
- 对比两者汇编，说明为什么 `g` 出现了 `subq $N, %rsp` 而 `leaf` 没有

**🧪 题 3：callee-saved 寄存器为何被 `push`**

仍用 `process.c` 的 `top`：

要求：

- 在 `top` 的汇编里找到 `pushq %rbx` / `popq %rbx`，并指出 `tmp` 被放进了 `%rbx`
- 解释：为什么编译器不把 `tmp` 放进 caller-saved 寄存器（如 `%rdi`）
- 把 `top` 改成 `return tmp;`（不再跨调用使用 `tmp`），重新编译，观察 `pushq %rbx` 是否消失，并解释原因

⚠️ 直接改 `return tmp;` 会被「死代码消除」带偏：`leaf` 是无副作用的纯函数、返回值又没人用，编译器会把 `call leaf` 整个删掉，`top` 只剩 `leaq -5(%rdi),%rax; ret`。`pushq` 确实消失了，但直接原因是连 `call` 都没了，没隔离出「`tmp` 不跨调用」这一个变量。

要干净地验证，用下面这个版本 B——**保留 `call leaf`，但让 `tmp` 只在 `call` 之前用完**：

```c
long top(long x) {
    long tmp = x - 5;
    long ret = leaf(tmp);   // tmp 在 call 之前就用完
    return ret;             // 只返回 leaf 的结果，tmp 不跨调用
}
```

```asm
top:
    endbr64
    subq   $5, %rdi    # tmp 直接算在 %rdi（参数寄存器，caller-saved）
    call   leaf        # call leaf 仍然在
    ret                # leaf 结果已在 %rax，正好是返回值，无需搬运
```

`call leaf` 还在，`pushq %rbx` 却消失了。结论：`pushq %rbx` 出现与否，唯一取决于「有没有值需要跨 `call` 存活」——原版 `tmp` 要活过 `call leaf` 才进 `%rbx`；版本 B 里 `tmp` 用完即弃，callee-saved 寄存器没了用武之地。

**🧪 题 4：第 7 个参数走栈**

```c
long sum8(long a,long b,long c,long d,long e,long f,long g,long h) {
    return a+b+c+d+e+f+g+h;
}
long call_it() { return sum8(1,2,3,4,5,6,7,8); }
```

要求：

- `-Og -S` 编译，在 `call_it` 里找出参数 1~6 分别进了哪个寄存器
- 找出第 7、8 个参数（`7`、`8`）是怎么被压栈的，确认压栈顺序
- 在 `sum8` 里找出它如何用正偏移（如 `8(%rsp)`、`16(%rsp)`）读取栈上的第 7、8 个参数

**🧪 题 5：递归阶乘的栈帧**

```c
long rfact(long n) { return n <= 1 ? 1 : n * rfact(n - 1); }
int main() { return (int)rfact(5); }
```

要求：

- `-Og -S` 编译，确认 `rfact` 入口 `pushq %rbx`、出口 `popq %rbx`，并说明 `n` 存在哪
- 用 `gdb` 在 `rfact` 下断点，`run` 后连续 `continue`，每次用 `bt` 观察调用栈层数增长
- 在最深一层用 `info frame` 看栈帧地址，估算单层栈帧大小
- 思考题：把 `rfact(5)` 改成 `rfact(100000000)` 会发生什么信号？为什么？用 `ulimit -s` 解释

**🧪 题 6：尾递归是否被优化**

```c
// 普通递归
long rsum(long n) { return n == 0 ? 0 : n + rsum(n - 1); }
// 尾递归形式
long tsum(long n, long acc) { return n == 0 ? acc : tsum(n - 1, acc + n); }
```

要求：

- 分别用 `-O0`、`-O2` 编译两个函数，对比汇编
- 观察 `-O2` 下 `tsum` 是否还有 `call tsum`（尾调用优化会把它变成 `jmp`/循环）
- 说明：尾递归被优化成循环后，递归深度还会不会受栈大小限制
