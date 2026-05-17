# §3.6 条件码、跳转与控制流

这一节的主线是：**C 的条件语句、循环、switch 在机器层面长什么样**——条件码怎么被隐式设置、跳转指令怎么读、cmov 为什么比 jmp 快、switch 何时变成跳转表，以及 `[[likely]]` 这类 hint 真正影响的是什么。

---

## 条件码

**🎯 四个标志位**

四个 1-bit 寄存器，由算术/逻辑指令**隐式设置**，是所有条件判断的物理基础。

| 标志 | 全称 | 含义 | 典型触发场景 |
|------|------|------|-------------|
| CF | Carry Flag | 进位（无符号溢出） | 无符号加法最高位进位 |
| ZF | Zero Flag | 零标志 | 结果为 0 |
| SF | Sign Flag | 符号标志 | 结果为负（MSB=1） |
| OF | Overflow Flag | 溢出（有符号溢出） | 正+正=负 或 负+负=正 |

**⚠️ 哪些指令对条件码"不老实"**

- `leaq` **不**设置任何条件码（它只是地址计算，不算"算术"）
- `cmp S, D` 计算 `D - S` 并丢弃结果，只更新条件码（AT&T 顺序，源在前）
- `test S, D` 计算 `D & S` 并丢弃结果，仅更新条件码
- `inc` / `dec` 设置 SF/ZF/OF 但**不**设置 CF——这是和 `add $1`/`sub $1` 唯一的区别

```asm
cmpq %rsi, %rdi    # 计算 rdi - rsi，等价于判断 "rdi 是否 > rsi"
testq %rax, %rax   # 等价于判断 rax 是否为 0（常见于 if (p) 的实现）
```

---

## 读取条件码：SET 指令

**🎯 把条件码读到寄存器**

SET 指令把条件码组合成 0/1，只写目标的低 1 字节：

```asm
sete   %al   # ZF=1            → al=1（等于）
setne  %al   # ZF=0            → al=1（不等于）
setl   %al   # SF≠OF           → al=1（有符号 <）
setle  %al   # ZF=1 或 SF≠OF   →     （有符号 ≤）
setb   %al   # CF=1            → al=1（无符号 <）
setbe  %al   # CF=1 或 ZF=1    →     （无符号 ≤）
```

**⚠️ 高位残留**

SET 只写低 1 字节，高位是上次寄存器的旧值。要拿到干净的 0/1，必须配 `movzbl`：

```asm
cmpq  %rsi, %rdi
setl  %al
movzbl %al, %eax   # 把高位清 0；写 %eax 会自动清 rax 高 32 位
```

---

## 跳转指令

**🎯 无条件跳转**

```asm
jmp  .label       # 直接跳转（PC 相对）
jmp  *%rax        # 间接：跳到 rax 里那个地址
jmp  *(%rax)      # 间接：跳到内存 [rax] 处存的地址（跳转表用法）
```

**🎯 条件跳转**

| 指令 | 条件 | 含义 |
|------|------|------|
| `je` / `jz` | ZF=1 | 等于 / 零 |
| `jne` / `jnz` | ZF=0 | 不等于 |
| `jl` / `jle` | SF≠OF / ZF=1∨SF≠OF | 有符号 < / ≤ |
| `jg` / `jge` | ZF=0∧SF=OF / SF=OF | 有符号 > / ≥ |
| `jb` / `jbe` | CF=1 / CF=1∨ZF=1 | 无符号 < / ≤ |
| `ja` / `jae` | CF=0∧ZF=0 / CF=0 | 无符号 > / ≥ |
| `js` / `jns` | SF=1 / SF=0 | 负数 / 非负 |

**🎯 跳转编码：PC 相对寻址**

编码里存的是**偏移量** = 目标地址 − 下一条指令地址，不是绝对地址：

```asm
400520: 7e 0a    jle 0x40052c     # 0a = 偏移 10；下一条 = 0x400522；目标 = 0x400522+10
```

这是 ELF 代码可重定位 / 位置无关的基础——加载到任何地址，相对偏移都不变。

---

## 条件分支：jmp vs cmov

**🎯 jmp 实现（传统、-O0 风格）**

```c
long absdiff(long x, long y) { return x > y ? x - y : y - x; }
```

```asm
  cmpq  %rsi, %rdi
  jle   .else
  movq  %rdi, %rax
  subq  %rsi, %rax
  ret
.else:
  movq  %rsi, %rax
  subq  %rdi, %rax
  ret
```

**🎯 cmov 实现（-O1+ 编译器偏好）**

```asm
  movq  %rdi, %rax
  subq  %rsi, %rax    # then_val = x - y
  movq  %rsi, %rdx
  subq  %rdi, %rdx    # else_val = y - x
  cmpq  %rsi, %rdi
  cmovle %rdx, %rax   # 若 x ≤ y，用 else_val 覆盖
  ret
```

**🎯 为什么 cmov 更快**

- 没有控制流跳转，CPU 流水线不需要预测分支方向
- 分支预测失败代价约 15–20 cycles，cmov 把它降为 0
- 代价是两个分支都要计算——当两个分支都便宜时净赚

**⚠️ 编译器不能用 cmov 的三种场景**

- **有副作用**：`x > 0 ? (*p = 1, x) : 0`，else 分支不能预先执行赋值
- **计算代价大**：`x > 0 ? a*a*a : b*b*b`，两边都算反而更慢
- **可能访问非法内存**：`p ? *p : 0`，若 p=NULL 提前 `*p` 会段错误

---

## 循环的汇编映射

**🎯 do-while：最直接**

```c
do { body; } while (cond);
```

```asm
loop:
  <body>
  <test cond>
  jnz  loop
```

**🎯 while → guarded-do（-O1+ 优化）**

`while` 比 `do-while` 多一次入口判断。编译器把它转成"先跳到尾部测试 + do-while"的形式：

```asm
  jmp  test
loop:
  <body>
test:
  <test cond>
  jnz  loop
```

更激进时（-O2）会在入口处展开一次预判断，避免零次循环的额外跳转。

**🎯 for 循环**

本质等价于 `init; while(cond) { body; step; }`，汇编形态和 while 相同。

---

## switch → 跳转表

**🎯 适用条件**

case 值**密集**（范围小、密度高）时，编译器生成 jump table，把 O(n) 比较变成 O(1) 间接跳转。

**🎯 结构**

```asm
  cmpq  $6, %rdi          # 超界检查
  ja    .default          # 用无符号比较，负数也会被一次性排除
  jmp   *.L4(,%rdi,8)     # 从跳转表取目标地址，间接跳
```

跳转表在 `.rodata` 段，每项 8 字节（64-bit 地址）：

```asm
.L4:
  .quad .case0
  .quad .case1
  .quad .case2
  ...
```

**🔧 性能特点**

- 分发代价 O(1)，case 越多优势越大
- 稀疏 case（如 1, 100, 1000）编译器会退化为 if-else 链或二分查找
- 跳转表是间接跳转，CPU 用 BTB 预测目标，模式无规律时 BTB miss 代价也很大

**🎯 多个 case 共享同一标签**

```c
case 1:
case 3:
    result = 10; break;
```

跳转表中 index 1 和 index 3 指向同一段代码。

---

## [[likely]] / [[unlikely]] 与分支预测

**🎯 CPU 分支预测的代价**

现代 CPU 流水线深、提前取指，遇到条件跳转必须**预测**方向；预测错时整条流水线冲刷掉重来，代价约 15–20 cycles。编译器在布局机器码时会把"大概率走的路径"放在 fallthrough（顺序执行）位置，因为静态预测倾向于"不跳"。

**🎯 hint 的语法（C++20）**

```cpp
if (ptr != nullptr) [[likely]] {
    use(ptr);
} else [[unlikely]] {
    handle_null();
}
```

GCC/Clang 的旧写法（Linux 内核 likely()/unlikely() 宏的本体）：

```c
if (__builtin_expect(ptr != NULL, 1)) { ... }
```

**🔧 实际效果是改代码布局**

```cpp
if (cache.contains(key)) [[likely]] {
    return cache.get(key);
} else [[unlikely]] {
    return fetch_from_db(key);
}
```

汇编里 unlikely 分支会被外联（outline）到 cold 段，hot 路径上是顺序执行的代码：

```asm
  test  %rax, %rax
  jz    .cold_fetch
  <cache hit code>      ; likely 路径顺序执行
  ret
.cold_fetch:            ; 远处的 cold 段
  call  fetch_from_db
  ret
```

**⚠️ hint 影响的是布局，不是 CPU 预测**

CPU 有动态分支预测器（BTB/BHT），运行一段时间后会根据**实际**走向自动调整。hint 真正起作用的场景是：

- **冷启动**：BHT 没有历史数据时
- **代码布局**：把 unlikely 路径推远，提升 icache 利用率
- **寄存器分配**：编译器优先服务 hot 路径

---

## if-else vs switch：如何选择

**🎯 编译器视角的核心区别**

| 维度 | if-else | switch |
|------|---------|--------|
| 条件类型 | 任意布尔表达式 | 整型常量等值比较 |
| 汇编实现 | 顺序比较链 / cmov | 跳转表（密集）或比较链（稀疏） |
| 分支多时复杂度 | O(n) | O(1) 跳转表 |
| 优化空间 | 有限 | 编译器知道是等值比较，能更激进优化 |

**🔧 选 if-else 的场景**

- 条件不是整型等值比较（浮点、范围、复合）
- 分支很少（≤ 3）且条件各不相同
- 需要用 `[[likely]]` / `[[unlikely]]` 标注冷热路径
- 需要短路求值（`if (ptr && ptr->valid && ptr->value > 0)`）

**🔧 选 switch 的场景**

- 多路等值分发，case ≥ 4 且值密集
- 协议 opcode / 状态机 / 枚举分发——配合 `-Wswitch` 编译器能检查穷举性
- 多个 case 共享同一段逻辑（fall-through）

**🎯 速查口诀**

> case ≥ 4 且整型等值 → switch；条件是范围/浮点/复合 → if-else；要标注冷热 → if-else + likely/unlikely；状态机/协议字段 → switch + `-Wswitch`。

实际项目里常混用：外层 switch 做 O(1) 分发，内层 if-else 处理细节。

---

## 易错点

- `cmp a, b` 做的是 `b - a`，条件方向容易搞反（AT&T 顺序：目的在右，被减数在右）
- SET 指令只写低 1 字节，不配 `movzbl` 清零高位时拿到的是脏值
- `leaq` 不设置条件码，紧跟其后做条件判断会读到上一条算术指令的标志
- cmov 两个分支都会被计算，带副作用 / 可能段错误的表达式编译器只能退回 jmp
- switch 超界检查用 `ja`（无符号比较），负数转无符号后是大正数，所以一条指令就能排除越界和负数两种情况
- PC 相对跳转的偏移基准是**下一条**指令地址，不是当前指令地址，手算偏移容易错位
- `[[likely]]` 不会强制 CPU 预测方向，它只影响编译器的代码布局和寄存器分配优先级

---

## 工程关联

- **性能分析**：`perf stat -e branch-misses,branches,instructions` 看分支预测失败率；branch-misses 比率高（如 > 5%）时考虑用 cmov、查表、或加 `[[likely]]` 提示
- **反汇编对照优化等级**：`gcc -O0 -S` 几乎全是 jmp，`-O1` 开始出现 cmov，`-O2` 会做循环 guarded-do、switch 跳转表外联——读编译产物时优化等级是必须先确认的信息
- **Linux 内核**：`likely()` / `unlikely()` 宏全内核高频使用（如 syscall 入口判错误码），底层就是 `__builtin_expect`；这就是 `[[likely]]` C++20 标准化的来源
- **协议解析 / 状态机**：消息 opcode 分发优先用 switch + 密集枚举值，让编译器生成跳转表；如果 opcode 稀疏（HTTP method 这种字符串），用哈希或 if-else
- **C++ UB 与 cmov**：带副作用的三元 `cond ? (do_a(), x) : y` 不会被 cmov 化，但 `cond ? a : b`（纯值）几乎一定会——写性能敏感代码时要会预判
- **icache 与 cold 路径**：错误处理路径打 `[[unlikely]]`，让编译器把它推到远处，hot path 在同一 cache line 内顺序排布，可见的提升来自 icache miss 减少

---

## 实验题

**🧪 题 1：观察 jmp → cmov 的优化跃迁**

```cpp
long absdiff(long x, long y) {
    return x > y ? x - y : y - x;
}
```

要求：

- 分别用 `gcc -O0 -S`、`-O1 -S`、`-O2 -S` 生成汇编
- 找出从 `jle + jmp` 变成 `cmovle` 的优化等级临界点
- 解释为什么这种"两个分支都是简单减法"的情形 cmov 最划算

**🧪 题 2：cmov 不能用的场景**

```cpp
long pick(long *p, long x) {
    return p ? *p : x;     // 可能解引用空指针
}
long with_side_effect(long *p, long x) {
    return x > 0 ? (*p = 1, x) : 0;  // else 分支不该执行 *p = 1
}
```

要求：

- `-O2 -S` 看汇编，确认这两个函数都退回 jmp，没有 cmov
- 解释每个函数 cmov 不能用的具体理由（参考"编译器不能用 cmov 的三种场景"）

**🧪 题 3：switch 密集 vs 稀疏**

```cpp
int dense(int x) {
    switch (x) {
        case 0: return 10; case 1: return 20; case 2: return 30;
        case 3: return 40; case 4: return 50; case 5: return 60;
        default: return -1;
    }
}
int sparse(int x) {
    switch (x) {
        case 1:    return 10;
        case 100:  return 20;
        case 1000: return 30;
        default:   return -1;
    }
}
```

要求：

- `-O2 -S` 对比两个函数的汇编
- 在 `dense` 的汇编里找到跳转表（`.quad .Lxxx` 序列）和 `jmp *.Lxxx(,%rdi,8)` 指令
- 在 `sparse` 的汇编里确认编译器退化为 if-else 链或二分
- 用 `objdump -s -j .rodata` 验证跳转表确实在 `.rodata` 段

**🧪 题 4：用 perf 实测 likely/unlikely 的实际收益**

```cpp
#include <cstdint>
__attribute__((noinline))
uint64_t hot_loop(const int* a, size_t n) {
    uint64_t s = 0;
    for (size_t i = 0; i < n; ++i) {
        if (a[i] >= 0) [[likely]] s += a[i];
        else           [[unlikely]] s -= a[i];
    }
    return s;
}
```

要求：

- 生成两份数据：一份 99% 为正数（hint 准），一份 50/50 随机（hint 错）
- 用 `perf stat -e branches,branch-misses,cycles,instructions ./a.out` 测量两种数据下的 branch-miss 比率和 cycles
- 去掉 `[[likely]]/[[unlikely]]` 再测一次，看汇编布局变化和性能差异
- 结论：hint 在"偏置极强"时才有显著收益，且主要来自代码布局而非预测器

**🧪 题 5：手算 PC 相对跳转偏移**

给出汇编片段（来自 `objdump -d`）：

```
4004f0: 48 39 f7              cmp    %rsi,%rdi
4004f3: 7e 05                 jle    4004fa
4004f5: ...
4004fa: ...
```

要求：

- 手算 `7e 05` 这条 `jle` 的目标地址，验证它确实是 `0x4004fa`
- 写出"目标地址 = 下一条指令地址 + 偏移"的具体计算
- 改变源码中 if 内部代码量，观察偏移字节数何时从 1 字节扩展到 4 字节（短跳 vs 长跳）
