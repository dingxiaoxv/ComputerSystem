# §3.8 数组分配和访问

这一节的主线是：**数组在汇编层面就是"基地址 + 元素大小 × 下标"的地址计算，编译器把所有数组语法糖（一维、嵌套、定长、变长）都翻译成同一个公式，区别只在"元素大小"如何确定**

- 一维数组：`a[i]` → 地址 `a + i * sizeof(T)`，正是 `leaq` 比例因子的舞台
- 指针运算：加 1 不是加 1 字节，而是加 `sizeof(*p)`
- 嵌套数组 `a[i][j]`：地址 `a + (i*C + j) * sizeof(T)`，C 是列数，编译期常量
- 定长二维数组的"列数"在编译期已知，可以直接编码进指令
- 变长数组 `T a[n][m]`（C99 VLA）：行宽度 `m` 是运行时变量，需要先 `imul` 算出再寻址

---

## 数组的基本声明和分配

**🎯 声明语义**

```c
int A[10];
```

在内存里就是 `10 * sizeof(int) = 40` 字节的连续区域，`A` 这个名字本身就是首元素的地址（衰退为 `int *`）。

**🎯 类型对元素大小的影响**

| 声明 | 每元素大小 | 总大小 |
|------|----------|--------|
| `char S[12]` | 1 | 12 |
| `int T[12]` | 4 | 48 |
| `double U[12]` | 8 | 96 |
| `char *V[12]` | 8（指针！）| 96 |

注意 `char *V[12]` 是"12 个指针的数组"，不是"指向 12 个 char 的指针"——指针自己占 8 字节。

**🎯 数组访问的汇编模板**

读 `int A[]` 的第 i 个元素（`A` 在 `%rdx`，`i` 在 `%rcx`）：

```asm
movl (%rdx,%rcx,4), %eax    # eax = *(A + i*4)
```

`(base, index, scale)` 这套寻址方式正是为数组设计的，`scale` 等于 `sizeof(T)`，所以只能取 1/2/4/8——刚好覆盖 `char/short/int/long`/指针。

---

## 指针运算

**🎯 加 1 不是加 1 字节**

```c
int *p;
p + 1;   // 实际地址增加 4
p + i;   // 实际地址增加 i * 4
```

编译器把 `p + i` 翻译成 `p + i*sizeof(*p)`，所以指针的类型必须在编译期已知。

**🎯 几种等价表达式**

设 `E` 是 `T[]`，下标 `i`：

| 表达式 | 含义 | 类型 |
|--------|------|------|
| `E[i]` | 第 i 个元素的值 | `T` |
| `*(E+i)` | 同上 | `T` |
| `&E[i]` | 第 i 个元素的地址 | `T *` |
| `E+i` | 同上 | `T *` |

`E[i]` 只是 `*(E+i)` 的语法糖，所以 `i[E]` 在 C 里居然也合法（虽然没人这么写）。

**🎯 指针减法**

```c
int *p, *q;
p - q;   // 结果是元素个数，不是字节数
```

汇编上是 `subq %q, %p` 之后再 `sarq $2, %result`（除以 `sizeof(int)`）。

---

## 嵌套数组

**🎯 行优先存储（row-major）**

```c
int A[5][3];
```

内存布局：`A[0][0], A[0][1], A[0][2], A[1][0], A[1][1], ..., A[4][2]`，总共 `5*3*4 = 60` 字节。

**🎯 寻址公式**

```
&A[i][j] = A + (i * C + j) * sizeof(T)
```

其中 `C` 是列数（这里 3），是**编译期常量**。

```c
int get(int A[5][3], long i, long j) { return A[i][j]; }
```

汇编（`A` 在 `%rdi`，`i` 在 `%rsi`，`j` 在 `%rdx`）：

```asm
leaq (%rsi,%rsi,2), %rax        # rax = 3i
addq %rdx, %rax                 # rax = 3i + j
movl (%rdi,%rax,4), %eax        # eax = *(A + (3i+j)*4)
```

`leaq (%rsi,%rsi,2)` 算 `3i` 是 §3.5 学过的乘小常数惯用法。

**🎯 行的"指针"**

`A[i]` 本身的类型是 `int (*)[3]`（指向 3 个 int 的指针），加 1 跨越 3*4=12 字节，所以 `A[i+1] - A[i]` 是 1（行步长）。

---

## 定长数组

**🎯 列数固定，编译器可以激进优化**

```c
#define N 16
typedef int fix_matrix[N][N];

int fix_prod(fix_matrix A, fix_matrix B, long i, long k) {
    int result = 0;
    for (long j = 0; j < N; j++)
        result += A[i][j] * B[j][k];
    return result;
}
```

GCC `-O1` 会做这些优化：
- 把 `A[i]` 算成一个基址 `Aptr = A + i * 64`，循环内直接 `*Aptr++`
- 把 `B[0][k]` 算成 `Bptr = B + 4*k`，循环内 `Bptr += 64`（跨一行）
- 用 `Bend = Bptr + 64*N` 作终止判断，省掉 `j` 计数器

整个循环退化成两个指针推进 + 一个终止比较，看不到原本的 `j`。

---

## 变长数组（VLA）

**🎯 C99 引入的运行时长度数组**

```c
int var_ele(long n, int A[n][n], long i, long j) {
    return A[i][j];
}
```

行宽 `n` 是参数，编译期不知道，所以不能用立即数编码。

汇编大致：

```asm
imulq %rdx, %rdi        # rdi = n * i  （行宽 * 行号）
leaq (%rdi,%rcx), %rax  # rax = n*i + j
movl (%rsi,%rax,4), %eax
```

和定长版本对比：`leaq (%rsi,%rsi,2)` 变成了真正的 `imulq`，因为乘数是变量。

**⚠️ VLA 的性能代价**

- 每次寻址都要一次乘法（定长是 `leaq` 的"免费乘法"）
- 在循环里编译器有机会把乘法提到循环外（强度削减），但前提是它能证明 `n` 没变
- VLA 还会改变栈帧大小（需要运行时 `sub %rax, %rsp`），增加复杂性
- 实际工程里常被批评，C11 已经把 VLA 改成可选特性

---

## 易错点

- `char *V[12]` 是 12 个指针的数组，不是指向数组的指针——读声明要从内到外
- 指针 `p+1` 加的是 `sizeof(*p)` 字节，不是 1 字节，类型决定步长
- `A[i]` 的类型在嵌套数组里是"一行的指针"而不是元素，`sizeof(A[i])` 是一行的字节数
- 行优先存储下 `A[i][j]` 和 `A[j][i]` 的访问模式对 cache 影响差距巨大（§6 重点）
- 定长二维数组的列数 `C` 必须是编译期常量，否则会变成 VLA 走 `imul` 路径
- VLA 的乘法不一定每次都执行，编译器会做强度削减，但函数边界、别名等会阻止优化

---

## 工程关联

- `(base, index, scale)` 寻址方式硬件支持的 scale 只有 1/2/4/8，正好对应 C 的基础整数类型，超出范围（如 struct 元素）必须显式 `imul`
- 矩阵乘法的循环顺序（ijk vs ikj）在汇编层面影响内层循环是按行扫还是按列跳，直接决定 cache miss 率
- `perf stat -e L1-dcache-load-misses` 对比行优先 vs 列优先遍历，能直观看到嵌套数组寻址带来的局部性差异
- VLA 在内核代码里被 Linus 明确禁止（Linux 4.20 起移除所有 VLA），原因之一是栈大小不可预测、`imul` 性能不可控
- 看反汇编时如果一段循环内出现 `leaq (%r, %r, 2)` 之类的常数乘法，多半是定长数组寻址；如果是真正的 `imulq` 进入循环热路径，多半是 VLA 或步长来自参数

---

## 实验题

**🧪 题 1：一维数组的寻址指令**

```cpp
int read_int(int *A, long i)     { return A[i]; }
short read_short(short *A, long i) { return A[i]; }
long read_long(long *A, long i)   { return A[i]; }
char read_char(char *A, long i)   { return A[i]; }
```

要求：

- `gcc -O1 -S` 生成汇编
- 找出每个函数 `mov` 指令的 scale 因子，验证它等于 `sizeof(T)`
- 思考：如果元素类型是 `struct { int a, b, c; }`（12 字节），汇编会变成什么样？

**🧪 题 2：指针运算 vs 数组下标**

```cpp
int sum_index(int *a, long n) {
    int s = 0;
    for (long i = 0; i < n; i++) s += a[i];
    return s;
}

int sum_ptr(int *a, long n) {
    int s = 0;
    int *end = a + n;
    while (a < end) s += *a++;
    return s;
}
```

要求：

- `-O0` 和 `-O2` 分别看两个函数的汇编
- 在 `-O0` 下对比循环体，看下标版本是否多了一次 `i*4` 计算
- 在 `-O2` 下对比，确认两者是否被优化到几乎相同（强度削减）

**考查点**

- 下标 `a[i]` 在汇编层面的本质是 `*(a + i*sizeof(T))`，"乘元素大小"这一步藏在 `lea` 的比例因子里（`lea rdx,[rax*4+0]`），不会写成 `imul`
- 指针自增 `*p++` 维护的是"字节地址本身"，每次循环只需 `add rdi, 4` 这种常数偏移
- `-O0` 不做任何优化，C 源码里的每个变量都老实地 spill 到栈，循环体指令数差异直接反映源码写法
- `-O2` 会做**强度削减**（strength reduction）：把"每次迭代的乘法 `i*sizeof(T)`"递推改写成"每次迭代的加法 `+sizeof(T)`"，下标变量 `i` 整个被消除

**结论**

- `-O0` 下下标版本每轮循环多一条 `lea rdx,[rax*4+0]`（i*4 换算），指针版本只有 `lea rdx,[rax+4]`（常数偏移），下标版本循环体约 9 条指令、指针版本约 7 条
- `-O2` 下两个版本汇编几乎完全一样：都用"指针 + end 哨兵"的形式，循环体均为 4 条指令，下标 `i` 在指针版本里本就不存在，在下标版本里被编译器优化掉
- 工程结论：**优先写可读性更好的下标版本**，把"为性能改写成指针递增"这种工作交给编译器；现代 `-O2` 下抽象层次（下标 vs 指针）对性能影响几乎为零
- 这是 §5「优化程序性能」里**强度削减**的活样本，提前见到一次，后面学起来更顺

**🧪 题 3：嵌套数组的 `leaq` 模式**

```cpp
int get_3x5(int A[3][5], long i, long j) { return A[i][j]; }
int get_4x7(int A[4][7], long i, long j) { return A[i][j]; }
int get_3x6(int A[3][6], long i, long j) { return A[i][j]; }
```

要求：

- `-O1 -S` 看汇编
- 找出每个函数里"乘列数"的实现：5、7、6 分别怎么用 `leaq` 或 `imul` 表达
- 重点观察 `5 = 4+1`、`7 = 8-1`、`6 = 2*3` 这些常数的编译器分解技巧

**🧪 题 4：行优先 vs 列优先的性能差异**

```cpp
constexpr int N = 2048;
int A[N][N];

long sum_row_major() {
    long s = 0;
    for (int i = 0; i < N; i++)
        for (int j = 0; j < N; j++) s += A[i][j];
    return s;
}

long sum_col_major() {
    long s = 0;
    for (int j = 0; j < N; j++)
        for (int i = 0; i < N; i++) s += A[i][j];
    return s;
}
```

要求：

- 编译 `-O2`，跑 `perf stat -e cycles,instructions,L1-dcache-load-misses ./a.out`
- 对比两个函数的耗时和 L1 miss 数
- 用 `objdump -d` 看两个内层循环的地址递增模式：行优先递增 4，列优先递增 `N*4 = 8192`
- 这是 §6 的预热实验，记住直觉

**🧪 题 5：定长 vs VLA 寻址成本**

```cpp
int get_fix(int A[16][16], long i, long j) { return A[i][j]; }
int get_vla(long n, int A[n][n], long i, long j) { return A[i][j]; }
```

要求：

- `-O1 -S` 对比两段汇编
- 找出定长版的 `leaq` 常数乘法 vs VLA 版的 `imulq`
- 写一个小循环重复调用两个函数 1e8 次，`time` 测耗时差距
- 思考：如果在循环里反复访问 `A[i][j]`，编译器能否把 `imulq` 提到循环外？什么条件下不能？
