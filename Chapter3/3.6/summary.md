# §3.6 条件码、跳转与控制流

## 1. 条件码（Condition Codes）

四个 1-bit 寄存器，由算术/逻辑指令**隐式设置**：

| 标志 | 全称 | 含义 | 典型触发场景 |
|------|------|------|-------------|
| CF | Carry Flag | 进位标志（无符号溢出） | 无符号加法最高位进位 |
| ZF | Zero Flag | 零标志 | 结果为 0 |
| SF | Sign Flag | 符号标志 | 结果为负（MSB=1） |
| OF | Overflow Flag | 溢出标志（有符号溢出） | 有符号加法正+正=负或负+负=正 |

**易错点**：
- `leaq` **不**设置条件码（它只做地址计算）
- `cmp a, b` 计算 `b - a`（注意操作数顺序是 AT&T 风格，源在前）
- `test a, b` 计算 `a & b`，仅设置条件码不写结果
- `inc`/`dec` 设置 SF/ZF/OF 但**不**设置 CF

---

## 2. 读取条件码：SET 指令

SET 指令只写目标的低 1 字节：

```asm
sete   %al   # ZF=1  → al=1（等于）
setne  %al   # ZF=0  → al=1（不等于）
sets   %al   # SF=1  → al=1（负数）
setl   %al   # SF≠OF → al=1（有符号 <）
setle  %al   # ZF=1 或 SF≠OF（有符号 ≤）
setb   %al   # CF=1  → al=1（无符号 <）
setbe  %al   # CF=1 或 ZF=1（无符号 ≤）
```

使用后通常需要 `movzbl %al, %eax` 把高位清零，否则高位残留旧值。

---

## 3. 跳转指令

### 无条件跳转
```asm
jmp  .label       # 直接跳转（PC 相对）
jmp  *%rax        # 间接跳转（寄存器中的地址）
jmp  *(%rax)      # 间接跳转（内存中的地址）
```

### 条件跳转（常用）
| 指令 | 条件 | 含义 |
|------|------|------|
| `je` / `jz` | ZF=1 | 等于 / 零 |
| `jne` / `jnz` | ZF=0 | 不等于 |
| `jl` | SF≠OF | 有符号 < |
| `jle` | ZF=1 或 SF≠OF | 有符号 ≤ |
| `jg` | ZF=0 且 SF=OF | 有符号 > |
| `jb` | CF=1 | 无符号 < |
| `ja` | CF=0 且 ZF=0 | 无符号 > |
| `js` | SF=1 | 负数 |

### 跳转编码（PC 相对寻址）
编码存储的是**偏移量** = 目标地址 − 下一条指令地址。
- 反汇编中看到负偏移是正常的（向上跳）
- 重定位时修改偏移量，而非目标地址，代码位置无关

---

## 4. 条件分支：jmp vs cmov

### jmp 实现（传统）
```c
if (x > y) return x - y;
else return y - x;
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

### cmov 实现（编译器 -O1+ 偏好）
```asm
  movq  %rdi, %rax
  subq  %rsi, %rax    # then_val = x - y
  movq  %rsi, %rdx
  subq  %rdi, %rdx    # else_val = y - x
  cmpq  %rsi, %rdi
  cmovle %rdx, %rax   # 若 x≤y，用 else_val 覆盖
  ret
```

### 为什么 cmov 更快
- 消除分支：流水线不需要预测跳转方向
- 预测失败代价约 15-20 个时钟周期
- cmov 是条件数据移动，无控制流跳转

### 编译器**不能**用 cmov 的场景
1. **有副作用**：`x > 0 ? (*p = 1, x) : 0`，else 分支的赋值不能预先执行
2. **计算代价大**：`x > 0 ? a*a*a : b*b*b`，两个分支都算代价反而更高
3. **访问可能非法的内存**：`p ? *p : 0`，若 p=NULL 提前访问会崩溃

---

## 5. 循环的汇编映射

### do-while（最直接）
```c
do { body; } while (cond);
```
```asm
loop:
  <body>
  <test cond>
  jnz  loop
```

### while → guarded-do（-O1+ 优化）
```c
while (cond) { body; }
```
编译器转化为：
```asm
  jmp  test        # 先跳到测试
loop:
  <body>
test:
  <test cond>
  jnz  loop
```
或更激进地（-O2）先做一次判断再进循环，避免零次循环的额外跳转。

### for 循环
本质与 while 相同：`init; while(cond) { body; step; }`

---

## 6. switch → 跳转表

### 适用条件
case 值**密集**（范围小、密度高）时，编译器生成 jump table。

### 结构
```asm
  cmpq  $6, %rdi         # 超界检查：case 最大值
  ja    .default         # 超出则跳 default（无符号比较，同时处理负数）
  jmp   *.L4(,%rdi,8)    # 间接跳转：从 jump table 取目标地址
```

jump table 存在 `.rodata` 段，每项 8 字节（64 位地址）：
```asm
.L4:
  .quad  .case0
  .quad  .case1
  ...
```

### 性能特点
- 分发代价 O(1)，与 case 数量无关
- 稀疏 case（如 1, 100, 1000）编译器退化为 if-else 链或二分查找

### 多个 case 共享同一标签
```c
case 1:
case 3:
  result = 10; break;
```
jump table 中 index 1 和 index 3 指向同一地址。

---

## 7. `[[likely]]` / `[[unlikely]]` 与分支预测（C++20）

### CPU 分支预测的代价

现代 CPU 流水线会**预测**条件跳转的方向，提前取指执行。预测错误时需要冲刷流水线，代价约 15-20 个时钟周期。

编译器在布局机器码时会把**"大概率走的路径"放在连续内存**，利用 CPU 的静态预测倾向（fallthrough = 不跳转，预测为 not-taken 更快）。

### `[[likely]]` / `[[unlikely]]` 的作用

```cpp
// C++20
if (ptr != nullptr) [[likely]] {
    // 编译器把这个分支放在 fallthrough 路径（顺序执行，无跳转）
    use(ptr);
} else [[unlikely]] {
    handle_null();
}
```

本质是给编译器一个**提示**，让它在两件事上做优化：
1. **代码布局**：likely 分支放 fallthrough，unlikely 分支放跳转目标（可能是远处的 cold 代码段）
2. **寄存器分配和指令调度**：likely 路径上的指令优先级更高

在 GCC/Clang 中等价的旧写法（C 语言常用）：
```c
if (__builtin_expect(ptr != NULL, 1)) { ... }  // 1=likely, 0=unlikely
```

### 实际效果

```cpp
// 热路径示例：缓存命中（大概率）vs 缓存未命中（小概率）
if (cache.contains(key)) [[likely]] {
    return cache.get(key);          // fallthrough，顺序执行
} else [[unlikely]] {
    return fetch_from_db(key);      // 跳转，在 cold section
}
```

汇编布局变化：
```asm
; [[likely]] 之后
  test  %rax, %rax
  jz    .cold_fetch      ; unlikely 路径：向前跳到远处
  <cache hit code>       ; likely 路径：紧接着顺序执行
  ret
.cold_fetch:
  call  fetch_from_db
  ret
```

**注意**：`[[likely]]`/`[[unlikely]]` 影响的是**代码布局**，不会强制 CPU 预测行为。CPU 有自己的动态分支预测器（BTB/BHT），运行时观察到真实规律后会自动调整。这个 hint 主要在**冷启动**或**预测器没有历史数据**时有效，以及对编译器做更激进的 unlikely 路径外联（outline）优化。

---

## 8. if-else vs switch：如何选择

### 编译器视角的核心区别

| 维度 | if-else | switch |
|------|---------|--------|
| 条件类型 | 任意布尔表达式 | 整型常量等值比较 |
| 汇编实现 | 顺序比较链 / cmov | 跳转表（密集）或比较链（稀疏） |
| 分支数量多时 | O(n) 比较 | O(1) 分发（跳转表） |
| 编译器优化空间 | 有限 | 更大（知道是等值比较） |

### 选 if-else 的场景

**1. 条件不是整型等值比较**
```cpp
if (x > 0.5 && y < threshold) { ... }  // 浮点、范围、组合条件
```

**2. 分支很少（≤ 3 个）且条件各不相同**
```cpp
if (err == EAGAIN) retry();
else if (err == ENOENT) return NOT_FOUND;
else return UNKNOWN_ERROR;
```

**3. 需要用 `[[likely]]`/`[[unlikely]]` 控制布局**
```cpp
if (fast_path_condition) [[likely]] { ... }
else { ... }
```
switch 目前不支持在 case 上标注 likely。

**4. 条件之间有优先级或短路求值语义**
```cpp
if (ptr && ptr->valid && ptr->value > 0) { ... }
```

### 选 switch 的场景

**1. 多路等值分发（case ≥ 4，且值密集）**
```cpp
switch (opcode) {
    case ADD: ...; case SUB: ...; case MUL: ...; /* 10+ cases */
}
```
跳转表把 O(n) 比较变成 O(1) 分发，case 越多优势越大。

**2. 协议/状态机/枚举分发**
```cpp
switch (state) {
    case State::IDLE:    ...; break;
    case State::RUNNING: ...; break;
    case State::ERROR:   ...; break;
}
```
语义更清晰，编译器能对枚举做穷举检查（加 `-Wswitch`）。

**3. 多个 case 共享同一逻辑（fall-through）**
```cpp
switch (c) {
    case 'a': case 'e': case 'i': case 'o': case 'u':
        return VOWEL;
}
```

### 关键结论

> **case 数量 ≥ 4 且是整型等值比较** → 优先 switch（编译器更容易生成跳转表）  
> **条件是范围/浮点/复合** → 只能 if-else  
> **需要标注冷热路径** → if-else + `[[likely]]`/`[[unlikely]]`  
> **状态机/协议字段分发** → switch，配合 `-Wswitch` 保证穷举

实际项目中两者常混用：外层 switch 做 O(1) 分发，内层 if-else 处理细节条件。

---

## 易错点汇总

1. `cmp a, b` 做的是 `b-a`，不是 `a-b`——条件判断方向容易搞反
2. SET 指令只写低 1 字节，必须配合 `movzbl` 清零高位
3. `leaq` 不设置条件码
4. cmov 两个分支都会被计算，有副作用时不能用
5. switch 超界检查用 `ja`（无符号比较），这样 x<0 也能被一次检查排除（负数转无符号后是大正数）
6. PC 相对跳转偏移 = 目标地址 - **下一条**指令地址（不是当前指令）
