# §2.3 整数运算 总结

## 核心主线

所有整数运算的底层统一原理：**模运算**。加减乘都在固定位宽的环（ring）上进行，溢出 = 截断高位，结果自然是模运算。

---

## 知识点

### 1. 无符号加法（§2.3.1）

$$s = (u + v) \bmod 2^w$$

- **溢出判断**：结果 $s < u$（或 $< v$），说明发生了进位溢出
- 溢出时相当于多减了 $2^w$

```c
// 检测无符号加法溢出
int uadd_ok(unsigned x, unsigned y) {
    return (x + y) >= x;
}
```

### 2. 补码加法（§2.3.2）

$s = (u + v)$ 截断为 $w$ 位，再解释为补码

- **正溢出**：两个正数相加得负数，结果减了 $2^w$
- **负溢出**：两个负数相加得正数，结果加了 $2^w$
- 溢出检测：`x > 0 && y > 0 && s <= 0` 或 `x < 0 && y < 0 && s >= 0`

### 3. 补码取反（§2.3.3）

$$-x = \begin{cases} \text{TMin}_w & x = \text{TMin}_w \\ -x & x > \text{TMin}_w \end{cases}$$

- `INT_MIN` 的负值还是 `INT_MIN`，因为 $2^{31}$ 无法用 32 位补码表示
- 位运算技巧：`-x == ~x + 1`

### 4. 无符号乘法（§2.3.4）

$$p = (u \cdot v) \bmod 2^w$$

直接截断低 $w$ 位，高位丢弃。

### 5. 补码乘法（§2.3.5）

先做无符号乘法截断，再把结果重新解释为补码。

**重要结论**：对于相同的位模式，无符号乘法和补码乘法的**低 $w$ 位结果相同**，编译器不需要区分有符号/无符号乘法指令（x86 的 `imul` 只看低位）。

### 6. 乘以常数（§2.3.6）

编译器会把乘以常数替换为**移位+加减法**组合，因为移位比乘法快。

$$x \times 14 = x \times (8+4+2) = (x \ll 3) + (x \ll 2) + (x \ll 1)$$

或者：

$$x \times 14 = x \times (16-2) = (x \ll 4) - (x \ll 1)$$

编译器会选择移位次数少的方案。

### 7. 除以 2 的幂（§2.3.7）

| 类型 | 操作 | 结果 |
|------|------|------|
| 无符号 | `x >> k`（逻辑右移）| $\lfloor x / 2^k \rfloor$ |
| 有符号正数 | `x >> k`（算术右移）| $\lfloor x / 2^k \rfloor$ |
| 有符号负数 | 需加偏置 `(x + (1<<k) - 1) >> k` | $\lceil x / 2^k \rceil$（向零取整）|

**负数为什么需要偏置**：算术右移对负数是向负无穷取整，但 C 标准规定整数除法向零取整，两者方向相反，需要补偿。

```c
// 编译器对 x / 4 的实现（有符号）
(x + (x >> 31 & 3)) >> 2   // 等价于 (x < 0 ? x+3 : x) >> 2
```

---

## 易错点

| 易错点 | 原因 |
|--------|------|
| `INT_MIN` 取反还是 `INT_MIN` | 补码范围不对称，正数少一个 |
| 有符号溢出是**未定义行为（UB）** | C 标准未规定，编译器可随意优化 |
| 无符号溢出是**定义明确**的 | 就是模运算，合法 |
| 右移负数不等于除法 | 方向不同，需偏置修正 |

---

## C++ 溢出注意点

### 有符号溢出是 UB，编译器会消除溢出检查

```cpp
bool is_overflow(int x) {
    return x + 1 > x;  // 编译器直接返回 true，UB 假设不会溢出
}
// 正确做法
bool is_overflow(int x) {
    return x == INT_MAX;
}
```

```bash
# 验证：-O2 下编译器消除了溢出检查
echo 'int f(int x){return x+1>x;}' | gcc -O2 -x c - -S -o -
# 输出直接是 mov eax, 1 / ret
```

### 有符号/无符号混用的隐式转换

```cpp
// 有符号转无符号，负数变巨大正数
int a = -1;
unsigned int b = 1;
if (a < b) { }  // 永远不执行！a 被转为 UINT_MAX

// size_t（无符号）比较陷阱
std::vector<int> v = {1, 2, 3};
int n = -1;
if (n < v.size()) { }  // n 转为 size_t 变成超大正数，条件为假

// 正确做法
if (n < (int)v.size()) { ... }
```

### 无符号下溢

```cpp
size_t len = 0;
size_t result = len - 1;   // 变成 SIZE_MAX

// 死循环：i >= 0 对无符号恒为真
for (size_t i = v.size() - 1; i >= 0; i--) { ... }

// 正确做法
for (int i = (int)v.size() - 1; i >= 0; i--) { ... }
```

### 内存分配大小计算溢出

```cpp
// 分配大小溢出，实际分配远小于预期
size_t n = 1000000;
int* p = new int[n * n];   // n*n 可能溢出

// 正确做法：用 calloc（内部会检查）或先检查
int* p = (int*)calloc(n, sizeof(int));
```

### 移位溢出

```cpp
int x = 1;
int y = x << 32;   // UB（移位位数 >= 类型宽度）
int a = -1;
int b = a << 1;    // UB（对负数左移）
int c = 1 << 31;   // UB（有符号溢出）

// 正确做法：位操作用无符号类型
uint32_t flags = 1u << 31;
```

### 安全检测手段

```cpp
// GCC/Clang 内置溢出检测（推荐）
int result;
if (__builtin_add_overflow(a, b, &result)) {
    // 溢出处理
}
// 同理：__builtin_sub_overflow, __builtin_mul_overflow

// 开发阶段：sanitizer 检测
// 编译加 -fsanitize=undefined,signed-integer-overflow
```

---

## 三句话总结

1. 整数运算 = 模运算，溢出 = 截断，一切都在固定位宽的环上进行
2. 有符号溢出是 UB，无符号溢出是合法模运算，这个区别决定了编译器的优化空间
3. 编译器把乘除法换成移位，负数右移需要偏置才能实现"向零取整"

## 工程关联

- `size_t` 做循环计数器的下溢陷阱（`i >= 0` 恒为真）
- `malloc(n * sizeof(int))` 若 `n` 很大会溢出，应用 `calloc`
- GCC 在 `-O2` 下假设有符号整数不会溢出，可能消除"溢出检查"代码

---

## 实验题：有符号溢出 UB + 负数除法偏置

**文件**：`Chapter2/2.3/exp_overflow_ub.cpp`
**编译**：
```bash
# 两次，分别观察汇编
g++ -std=c++17 -O0 -S -o exp_O0.s exp_overflow_ub.cpp
g++ -std=c++17 -O2 -S -o exp_O2.s exp_overflow_ub.cpp
# 运行用
g++ -std=c++17 -O2 -o exp_overflow_ub exp_overflow_ub.cpp
```

### 任务

写一个 C++ 程序，包含以下三个部分：

**Part 1**：`bool is_overflow_wrong(int x)` 和 `bool is_overflow_right(int x)`

- `wrong` 版本：`return x + 1 > x;`（这是有符号溢出 UB，-O2 下会被优化掉）
- `right` 版本：`return x == INT_MAX;`（正确写法）
- 在 `main` 里传入 `INT_MAX`，打印两个函数的返回值
- **然后看汇编**：对比 `exp_O0.s` 和 `exp_O2.s` 中 `is_overflow_wrong` 的指令，`-O2` 下函数体变成了什么？

**Part 2**：`void exp_neg_shift()`

- 计算 `-7 / 2` 和 `-7 >> 1`，打印两者的值
- 它们不相等——为什么？（提示：一个向零取整，一个向负无穷取整）
- 再打印 `-8 / 2` 和 `-8 >> 1`，这次相等吗？为什么？

**Part 3**：`void exp_compiler_div()`

- 写一个函数 `int div4(int x) { return x / 4; }`（单独写，方便看汇编）
- 对比 `exp_O2.s` 里 `div4` 的实现和你预期的"直接右移2位"有什么不同
- 找到汇编里的偏置修正（`sar` + `add` 或 `lea`），对应 §2.3.7 的公式

### 思考题（写在代码注释里）

1. Part 1 中，为什么说有符号溢出是 UB？如果编译器不假设"不会溢出"，它能做哪些优化？
2. Part 2 中，`-7 >> 1` 得到 `-4` 而不是 `-3`，从补码位模式角度解释。
3. Part 3 中，编译器加的偏置是 `(x + 3) >> 2` 还是别的？和 §2.3.7 公式对上了吗？
