# §3.11 浮点代码

浮点在机器级是**另一套并行的世界**：另一组寄存器（`%xmm0-15`）、另一套指令（SSE2/AVX）、另一套传参规则、甚至另一套比较标志的读法。这一节要回答的核心问题是——为什么浮点代码看起来和整数代码「长得完全不一样」，以及 IEEE 754 的语义（NaN、-0.0、不可精确表示）是怎么一条条落到指令上的。本节所有汇编均为本机 GCC 13.3 实测输出（x86-64，支持 AVX2），源码见 [experiments/fp_asm.c](experiments/fp_asm.c)，除特别注明外均为 `gcc -Og -S` 的结果。

## 浮点寄存器：16 个 %xmm，各自独立

**🎯 和整数寄存器完全分家**

x86-64 有 16 个 128 位的 `%xmm0`-`%xmm15`（AVX 下扩展为 256 位的 `%ymm`，AVX-512 下为 512 位的 `%zmm`）。它们和 `%rax`/`%rdi` 那套整数寄存器**没有任何重叠**，所以浮点和整数运算可以在流水线里真正并行。

**🎯 标量运算只用低位，其余位闲置**

同一个 `%xmm` 寄存器，装 `float` 用低 32 位、装 `double` 用低 64 位，靠**指令后缀**区分，不靠寄存器名：

| 后缀 | 含义 | 例子 |
|------|------|------|
| `ss` | Scalar Single（1 个 float） | `addss` |
| `sd` | Scalar Double（1 个 double） | `addsd` |
| `ps` | Packed Single（4/8 个 float） | `addps` |
| `pd` | Packed Double（2/4 个 double） | `addpd` |

本机实测，同一个加法函数换个类型就换后缀：

```asm
fp_add:                    ; double fp_add(double a, double b)
    addsd   %xmm1, %xmm0   ; sd = 标量双精度
    ret
flt_add:                   ; float flt_add(float a, float b)
    addss   %xmm1, %xmm0   ; ss = 标量单精度
    ret
```

**⚠️ 标量指令也占满整条向量流水线**

`addsd` 只算 1 个 double，但它跑在和 `addpd`（一次算 2 个）相同的功能单元上。这就是**向量化（SIMD）能提速的本质**——同样一条指令的代价，`ps`/`pd` 一次干 4-8 个元素。§5 讲的吞吐量界限在这里直接体现。

## SSE2 与 AVX：两操作数 vs 三操作数

**🎯 默认编译出来的是 SSE2，不是 AVX**

x86-64 的**基线 ABI 只保证 SSE2**，所以不加 `-march`/`-mavx` 时 GCC 生成的是老式两操作数指令。本机实测同一份源码的两种输出：

```asm
; gcc -O2（默认基线，SSE2）
    addsd   %xmm1, %xmm0            ; 机器码 f2 0f 58 c1
    ;  ↑ 破坏性：结果覆盖 %xmm0，源操作数没了

; gcc -O2 -mavx2（VEX 编码）
    vaddsd  %xmm1, %xmm0, %xmm0     ; 机器码 c5 fb 58 c1
    ;  ↑ 三操作数：dst = src2 op src1，两个源都能保留
```

**🎯 VEX 编码的三个实际好处**

- **非破坏性**：`vaddsd %xmm1, %xmm2, %xmm0` 可以把结果写到第三个寄存器，省掉大量 `movapd` 复制指令。
- **免掉 SSE/AVX 切换惩罚**：混用旧 SSE 和 AVX 指令会有寄存器上半部保存/恢复的开销，全走 VEX 就没有。
- **同样紧凑**：实测两条指令都是 4 字节，VEX 前缀（`c5`）没有让代码变胖。

**🔧 什么时候能开**

生产代码要考虑部署机器的指令集。`-march=native` 只适合自用；发布二进制常用 `-mavx2` + 运行时 CPU 特性检测（`__builtin_cpu_supports("avx2")`）做函数多版本分发（GCC 的 `target_clones` 属性）。

## 过程调用：浮点参数走独立的一套计数

**🎯 整型和浮点各排各的队**

System V AMD64 ABI 规定：前 8 个浮点参数走 `%xmm0`-`%xmm7`，前 6 个整型参数走 `%rdi,%rsi,%rdx,%rcx,%r8,%r9`。**两条队列独立计数**，互不影响。返回值在 `%xmm0`。

本机实测 `double fp_mix(int i1, double d1, int i2, double d2, int i3)`：

```
i1 → %edi     （整型第 1 个）
d1 → %xmm0    （浮点第 1 个）
i2 → %esi     （整型第 2 个）
d2 → %xmm1    （浮点第 2 个）
i3 → %edx     （整型第 3 个）
返回值 → %xmm0
```

参数在源码里交错排列，但寄存器分配是各排各的，**不会因为中间夹了浮点参数就跳过整型寄存器**。

**⚠️ 没有 callee-saved 的 `%xmm` 寄存器**

这是与整数世界最大的差别：整数有 `%rbx`/`%rbp`/`%r12-15` 可以跨调用保活，而**全部 16 个 `%xmm` 都是 caller-saved**。后果是浮点值想跨过一次函数调用，只能溢出到栈：

```asm
keep:                          ; double keep(double x) { return work(x) + x; }
    subq    $24, %rsp
    movsd   %xmm0, 8(%rsp)     ; x 必须存到栈上——没有 callee-saved xmm 可用
    call    work@PLT
    movsd   8(%rsp), %xmm1     ; 调用返回后再载回来
    addq    $24, %rsp
    addsd   %xmm1, %xmm0
    ret
```

同一份文件里的整数版对照，差别一目了然：

```asm
keep_int:                      ; long keep_int(long x) { return iwork(x) + x; }
    pushq   %rbx               ; 借一个 callee-saved 寄存器
    movq    %rdi, %rbx         ; x 待在寄存器里跨过调用
    call    iwork@PLT
    addq    %rbx, %rax
    popq    %rbx
    ret
```

这解释了一个性能现象：**浮点密集的循环里插入函数调用，代价远高于整数循环**——每个活跃的浮点值都要 store/load 一轮。这也是 §5.5「减少过程调用」在浮点代码上收益更明显的原因。

**🔧 变参函数用 `%al` 传「用了几个向量寄存器」**

调用 `printf` 这类变参函数时，`%al` 必须存放**实际使用的向量寄存器个数**，好让被调方知道要不要保存 `%xmm0-7`。本机实测：

```asm
p1:  ; printf("%f\n", x)   —— 1 个浮点参数
    movl    $1, %eax        ; ← %al = 1
    jmp     __printf_chk@PLT
p2:  ; printf("%f %f\n", x, y) —— 2 个浮点参数
    movl    $2, %eax        ; ← %al = 2
    jmp     __printf_chk@PLT
```

手写汇编调用 `printf` 时忘记设 `%al`，是经典的段错误来源。

## 转换指令：方向、宽度和「截断」

**🎯 一条指令的名字就写清了三件事**

`cvt` + 源类型 + `2` + 目标类型，多出来的 `t` 表示 truncate（截断）：

| 指令 | 转换 | 本机实测出处 |
|------|------|-------------|
| `cvtsi2sdl` | int → double | `i2d` |
| `cvtsi2sdq` | long → double | — |
| `cvtss2sd` | float → double | `f2d` |
| `cvtsd2ss` | double → float | `d2f` |
| `cvttsd2sil` | double → int（**截断**） | `d2i` |
| `cvttsd2siq` | double → long（**截断**） | `d2l` |

**⚠️ C 的浮点转整数是截断，不是舍入**

`(int)3.9` 得到 3，`(int)(-3.9)` 得到 -3（向零取整）。指令里那个 `t` 就是这个语义。而**浮点之间**的转换（double→float）用的是当前舍入模式（默认向偶数舍入），两者规则不同。

**⚠️ 无符号转换要绕道 64 位**

SSE2 没有「double → unsigned int」的直接指令。本机实测 `unsigned d2u(double x)` 生成的是：

```asm
d2u:
    cvttsd2siq  %xmm0, %rax    ; 先转成 64 位有符号
    ret                         ; 再取低 32 位当 unsigned
```

借道 64 位能覆盖 `unsigned` 的全部取值。但 `double → unsigned long` 就没这个便宜可占了，编译器要生成一段带减去 $2^{63}$ 的补偿代码。

**🔧 `pxor` 先清零：打破假依赖**

注意 `i2d` 的实测输出多了一条看似无用的指令：

```asm
i2d:
    pxor    %xmm0, %xmm0       ; 先把 %xmm0 整个清零
    cvtsi2sdl   %edi, %xmm0    ; 再转换
    ret
```

`cvtsi2sd` 只写目标寄存器的低 64 位，高位保持不变，于是它对 `%xmm0` 的旧值产生了**假依赖**（false dependency），会在乱序执行里制造无谓的串行。先 `pxor` 自异或清零切断这条依赖链——这是编译器的标准惯用法，不是冗余代码。AVX 下同理，用的是 `vxorps`。

## 符号操作：位运算，不是算术

**🎯 取负和取绝对值都只动符号位**

IEEE 754 的符号位是最高位，所以取负 = 异或 `0x8000000000000000`，取绝对值 = 与 `0x7fffffffffffffff`。本机实测：

```asm
fp_neg:                        ; double fp_neg(double x) { return -x; }
    xorpd   .LC0(%rip), %xmm0  ; .LC0 = 0x8000000000000000
    ret
fp_fabs:                       ; double fp_fabs(double x) { return __builtin_fabs(x); }
    andpd   .LC2(%rip), %xmm0  ; .LC2 = 0x7fffffffffffffff
    ret
```

验证常数：`.LC0` 在 `.rodata` 里是 `.long 0` + `.long -2147483648`，低 32 位全 0、高 32 位 `0x80000000`——拼起来正是符号位掩码。

**⚠️ 手写三目表达式，编译器不敢优化成 `andpd`**

同一份代码里的另一个函数就没这个待遇：

```asm
fp_abs:                        ; double fp_abs(double x) { return x < 0 ? -x : x; }
    pxor    %xmm1, %xmm1
    comisd  %xmm0, %xmm1       ; 老老实实比较
    ja      .L14
    ret
```

`-Og` 和 `-O2` 下都是分支（只有跳转标号编号不同），因为**两种写法语义不等价**。本机实测：

```
fp_abs(-0.0)  = -0     bits=0x8000000000000000  ← -0.0 < 0 为假，原样返回 -0.0
fp_fabs(-0.0) = +0     bits=0x0000000000000000  ← andpd 清掉符号位
fp_abs(-NaN)  = -nan   bits=0xfff8000000000000  ← 比较「无序」，两条分支都不成立
fp_fabs(-NaN) = +nan   bits=0x7ff8000000000000  ← 符号位照样被清掉
```

`-0.0 < 0` 是 false，所以三目版本把 `-0.0` 原样返回；`fabs` 的定义就是「清符号位」，必然返回 `+0.0`。NaN 上的差异同理。差别虽小，但足以让编译器不敢做这个变换。**要 `andpd` 就写 `fabs()`**，别自己写三目。

## 浮点比较：多出一个「无序」状态

**🎯 `comisd` / `ucomisd` 设置的是整数条件码**

浮点比较的结果不进 `%xmm`，而是设置 CPU 的 ZF/PF/CF 三个标志位，然后照常用 `set`/`j` 系列指令读取。关键在于它有**四种结果**而不是三种：

| 关系 | CF | ZF | PF |
|------|----|----|----|
| 大于 | 0 | 0 | 0 |
| 小于 | 1 | 0 | 0 |
| 等于 | 0 | 1 | 0 |
| **无序（有 NaN）** | 1 | 1 | **1** |

`PF`（奇偶标志）在这里被复用成「无序标志」——这是浮点比较独有的第四种状态。

**🎯 `==` 为什么要生成两条判断**

本机实测 `int fp_eq(double a, double b) { return a == b; }`：

```asm
fp_eq:
    ucomisd %xmm1, %xmm0
    setnp   %al             ; PF=0，即「有序」
    movzbl  %al, %eax
    movl    $0, %edx
    cmovne  %edx, %eax      ; ZF=0（不等）则清零
    ret
```

必须**同时**满足「有序」和「相等」才返回 1。如果只看 ZF，NaN 参与时 ZF=1 会被误判成相等——`NaN == NaN` 就会错误地返回 true。

三路比较把这个状态看得更清楚：

```asm
fp_cmp3:
    comisd  %xmm0, %xmm1
    ja      .L20            ; a < b  → -1
    comisd  %xmm1, %xmm0
    ja      .L21            ; a > b  →  1
    ucomisd %xmm1, %xmm0
    jp      .L23            ; ← PF=1，无序，跳去返回 2
    je      .L22            ; a == b →  0
.L23:
    movl    $2, %eax        ; 只有 NaN 参与才走到这里
```

**⚠️ `comisd` 和 `ucomisd` 不是同义词**

两者标志位设置完全一样，区别在**异常行为**：`comisd` 对任何 NaN（含 quiet NaN）都发出无效操作异常，`ucomisd` 只对 signaling NaN 发。这对应 IEEE 754 对「signaling 比较」和「quiet 比较」的区分——实测中 GCC 对 `<` 用 `comisd`、对 `==` 用 `ucomisd`，正是照着标准来的。

**⚠️ 比较用的是无符号跳转指令**

注意上面用的是 `ja`（above）而不是 `jg`（greater）。因为浮点比较把结果编码进了 CF/ZF，读法和**无符号整数比较**一致，用 `jg`/`jl` 这类有符号跳转会读错标志。

## 浮点常数：没有立即数，只能从内存加载

**🎯 所有浮点字面量都躺在 `.rodata` 里**

x86-64 的浮点指令**没有立即数操作数形式**，常数必须先放进只读数据段，再用 RIP 相对寻址加载：

```asm
fp_scale:                      ; double fp_scale(double x) { return x * 3.14 + 1.0; }
    mulsd   .LC3(%rip), %xmm0  ; .LC3 = 3.14
    addsd   .LC4(%rip), %xmm0  ; .LC4 = 1.0
    ret
```

本机实测常数池内容（小端，两个 `.long` 拼成一个 `double`）：

| 标号 | 字节内容 | 拼起来 | 用途 |
|------|---------|--------|------|
| `.LC0` | `0`, `-2147483648` | `0x8000000000000000` | 取负掩码（`fp_neg`） |
| `.LC2` | `-1`, `2147483647` | `0x7FFFFFFFFFFFFFFF` | 绝对值掩码（`fp_fabs`） |
| `.LC3` | `1374389535`, `1074339512` | `0x40091EB851EB851F` | 3.14 |
| `.LC4` | `0`, `1072693248` | `0x3FF0000000000000` | 1.0 |

有个小彩蛋：这两个掩码若当成 `double` 解释，`.LC0` 恰好是 `-0.0`、`.LC2` 恰好是一个 NaN——它们在这里的身份是**位掩码**而不是数值，再次印证「位模式的含义由使用它的指令决定」。

**🎯 只有 0.0 有捷径**

```asm
fp_zero:   pxor    %xmm0, %xmm0      ; 自异或直接得 +0.0，不访存
fp_one:    movsd   .LC4(%rip), %xmm0 ; 1.0 也得老老实实加载
```

**🔧 这是「浮点常量比整数常量贵」的根源**

整数 `x + 1` 是 `addl $1, %eax`（立即数，零访存）；浮点 `x + 1.0` 必须访问一次内存。虽然常数池几乎总在 L1 里，但它占用一个 load 端口——§5.12 讲的 load 单元竞争在浮点循环里更容易成为瓶颈。

## 易错点

- 浮点转整数是**截断（向零取整）**不是四舍五入，指令名里的 `t` 就是这个意思，`(int)(-3.9)` 得到 -3 而不是 -4。
- 默认编译出来的是 SSE2 两操作数指令（`addsd`），不是 AVX 三操作数（`vaddsd`）——想看 VEX 编码必须显式 `-mavx2` 或 `-march=native`。
- 全部 16 个 `%xmm` 都是 caller-saved，**没有一个是 callee-saved**，浮点值跨函数调用只能溢出到栈。
- 手写 `x < 0 ? -x : x` 不等价于 `fabs(x)`：前者对 `-0.0` 返回 `-0.0`，后者返回 `+0.0`，所以编译器不会替你优化成 `andpd`。
- 浮点比较后要用 `ja`/`jb` 这类**无符号**跳转，不是 `jg`/`jl`——标志位的编码方式和无符号整数比较一致。
- 判断浮点相等必须同时检查 PF（有序）和 ZF（相等），只看 ZF 会让 `NaN == NaN` 错误地为真。
- `cvtsi2sd` 前面那条 `pxor` 不是冗余，是为了切断对目标寄存器高位的假依赖，删掉会拖慢乱序执行。
- 浮点常数没有立即数形式，一律从 `.rodata` 加载；只有 `0.0` 能用 `pxor` 自异或省掉这次访存。
- `%xmm` 寄存器名不区分 float 和 double，**只有指令后缀（ss/sd/ps/pd）能看出宽度**，读汇编时不能靠寄存器名推断类型。
- 手写汇编调用 `printf` 忘记设 `%al` 为向量寄存器个数，会导致段错误。

## 工程关联

- **性能分析**：看到热点循环里全是 `addsd`/`mulsd`（标量后缀）而不是 `addpd`/`vfmadd*`（打包后缀），说明**向量化失败**了，用 `gcc -fopt-info-vec-missed` 查原因（通常是别名、循环依赖或不定长度）。
- **`-ffast-math` 的机器级后果**：允许把 `a/b` 换成 `a * (1/b)`（乘法比除法快得多）、允许重结合改变求和顺序、允许忽略 NaN 和 -0.0 的边界语义——正是本节 `fp_abs` 那个例子里编译器不敢做的变换。精度敏感代码别开。
- **FMA 指令**：`-mfma` 下 `a*b+c` 会合并成一条 `vfmadd213sd`，延迟更低且**只舍入一次**（中间结果不截断），所以开 FMA 后计算结果可能与不开时末位不同——这不是 bug，是精度提高了。
- **ABI 调试**：跨语言调用（C 调 Fortran/Rust）时浮点参数错位，用 `gdb` 的 `info registers xmm0` 直接看寄存器内容验证；`p $xmm0.v2_double` 能按 double 视图打印。
- **`long double` 是另一套**：x86-64 上 `long double` 是 80 位扩展精度，**不走 `%xmm`**，而是走老的 x87 栈（`%st(0)-%st(7)`）和 `fldt`/`fstpt` 指令。混用会看到两套完全不同的汇编。
- **软浮点目标**：嵌入式 ARM 无 FPU 时（`-msoft-float`），所有浮点运算都变成 libgcc 的函数调用（`__adddf3` 等），性能差两个数量级——这时候把浮点改成定点是标准做法。

## 实验题

**🧪 题 1：确认浮点参数的寄存器分配规则**

```c
double mix(int i1, double d1, int i2, double d2, int i3);
```

要求：
1. `gcc -Og -S` 编译，逐行标注每个参数落在哪个寄存器。
2. 验证「整型和浮点各排各的队」——`i2` 是否用了 `%esi`（整型第 2 个），而不是因为前面夹了 `d1` 就跳到 `%edx`。
3. 把参数顺序改成 `(double, double, int, int, int)`，确认寄存器分配不变。

**🧪 题 2：证明没有 callee-saved 的 `%xmm`**

```c
extern double work(double);
double keep(double x) { return work(x) + x; }
double keep_int(long x) { extern long iwork(long); return iwork(x) + x; }
```

要求：
1. 编译两个函数，对比 `x` 是怎么跨过 `call` 保活的。
2. 确认浮点版把 `x` 存到了栈上（`movsd %xmm0, N(%rsp)`），整数版则用了 callee-saved 寄存器（`%rbx` 之类）并只 push 一次。
3. 解释为什么这让「浮点循环里插函数调用」的代价更高。

**🧪 题 3：-0.0 陷阱**

```c
double my_abs(double x)  { return x < 0 ? -x : x; }
double lib_abs(double x) { return __builtin_fabs(x); }
```

要求：
1. `gcc -O2 -S` 对比两者汇编，确认只有后者是 `andpd`。
2. 写驱动程序，用 `memcpy` 取出 `my_abs(-0.0)` 和 `lib_abs(-0.0)` 的位模式并打印。
3. 再测 `my_abs(NAN)` 和 `lib_abs(NAN)` 的位模式，看两者对 NaN 符号位的处理是否也不同。

**🧪 题 4：NaN 与 PF 标志**

```c
int cmp3(double a, double b)
{
    if (a < b)  return -1;
    if (a > b)  return 1;
    if (a == b) return 0;
    return 2;
}
```

要求：
1. 编译后找到 `jp`（jump if parity）指令，说明它在过滤什么。
2. 分别用 `(1.0, 2.0)`、`(2.0, 2.0)`、`(NAN, 1.0)` 调用，验证返回值。
3. 把 `if (a == b)` 删掉直接 `return 0`，看编译器是否还生成 `jp`——理解「NaN 分支是代码要求的，不是编译器多此一举」。

**🧪 题 5：SSE2 与 AVX 的编码差异**

要求：
1. 同一份源码分别用 `gcc -O2` 和 `gcc -O2 -mavx2` 编译成 `.o`。
2. `objdump -d` 对比同一个函数的机器码字节，找出 VEX 前缀 `c5`。
3. 用 `perf stat` 跑一个浮点密集循环，对比两个版本的 `instructions` 和 `cycles`，看三操作数编码省掉的 `movapd` 有没有体现在指令数上。

**🧪 题 6：常数池与向量化**

```c
void scale(double *a, double *b, int n)
{
    for (int i = 0; i < n; i++)
        b[i] = a[i] * 3.14 + 1.0;
}
```

要求：
1. `gcc -O2 -S` 看是否向量化（找 `addpd`/`mulpd` 或 `vfmadd*`）。
2. `objdump -s -j .rodata` 看常数 3.14 在向量化后是以什么形式存放的（标量一份还是打包多份）。
3. 加 `restrict` 修饰两个指针，看向量化结果有无变化——把 §5.1 的别名障碍和本节的向量化连起来。
