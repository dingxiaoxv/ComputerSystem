# §3.2-3.3 汇编入门与数据格式

这一节的主线是：**学会用工具链把 C 代码变成可读的汇编，并建立"x86-64 数据宽度后缀"这一最基本的阅读直觉**。`gcc -S` 看到的是带大量伪指令的中间产物，`objdump -d` 才是 CPU 实际执行的指令；指令后缀 b/w/l/q 直接对应 1/2/4/8 字节，几乎决定了你怎么读懂一段汇编。

---

## 编译工具链

**🎯 四个命令对应的产物**

| 命令 | 产物 | 作用 |
|------|------|------|
| `gcc -S mstore.c` | `mstore.s` | 停在汇编阶段，看 C → 汇编映射 |
| `gcc -c mstore.c` | `mstore.o` | 目标文件（机器码 + 重定位信息，未链接） |
| `gcc mstore.c main.c -o prog` | `prog` | 完整链接，产生可执行文件 |
| `objdump -d mstore.o` | 终端反汇编 | 机器码 → 助记符 |

**🎯 `.s` 与 `objdump` 的差异**

- `gcc -S` 输出大量伪指令（`.globl` / `.cfi_*` / `.type` / `.size`），它们是给汇编器、链接器、调试器用的元信息，不是 CPU 指令
- `objdump -d` 输出的是真正会被 CPU 执行的指令序列

同一个 `multstore` 函数，`.s` 有 20+ 行，`objdump` 反汇编后只剩 6 条真实指令——其余全是元信息。

**🎯 优化等级：`-O0` / `-Og` / `-O2`**

`gcc` 用 `-O` 系列选项控制优化强度，直接决定汇编的可读性：

| 选项 | 含义 | 何时用 |
|------|------|--------|
| `-O0` | 完全不优化（默认） | 想看变量逐条 load/store 的最朴素汇编 |
| `-Og` | 为调试优化，删冗余但保留结构 | 读汇编学原理的首选，与书中风格最接近 |
| `-O2` | 重度优化 | 观察循环展开、尾调用、等差求和等编译器变换 |

```bash
gcc -Og -S process.c          # 干净又不失结构，日常读汇编用这个
gcc -O0 -S process.c          # 最啰嗦，能看到每个变量进出栈
gcc -O2 -S process.c          # 观察激进优化的产物
```

同一段循环，`-O0` 可能十几条指令逐次累加，`-O2` 可能被换成一条 `imul` 的等差公式——优化等级一换，汇编面貌完全不同。

**🎯 `-g`（调试信息）与 `-S`（出汇编）是两回事**

- `-S`：让编译**停在汇编阶段**，产出 `.s` 文本文件
- `-g`：往**可执行文件/目标文件**里塞 DWARF 调试信息（行号、变量名、类型）
- `-o name`：指定输出文件名，不写时可执行文件默认叫 `a.out`

```bash
gcc -Og -S process.c              # 出 process.s，看 C→汇编映射
gcc -Og -g process.c -o a.out     # 出带调试符号的可执行文件，供 gdb/objdump 用
```

要用 `gdb` 下断点、看源码行、让 `b 函数名` 定位准确，**编译时必须带 `-g`**；只带 `-S` 得到的是汇编文本，不能拿去调试。两者各管一段，常常一起出现但用途不同。

**🔧 读汇编 / 调试常配的两个选项**

- `-fno-stack-protector`：关掉栈保护金丝雀，汇编里不再出现 `%fs:0x28` 相关的几条指令，函数体更纯净——§3.10 之前读汇编都可加
- `objdump -d a.out`：反汇编可执行文件；配 `grep -A1 'call.*leaf'` 可只看某条 `call` 及其下一条指令（即被压栈的返回地址）

---

## x86-64 数据宽度后缀

**🎯 b / w / l / q 对应 1/2/4/8 字节**

| 后缀 | 大小 | 对应 C 类型 |
|------|------|------------|
| `b` | 1 字节 | `char` |
| `w` | 2 字节 | `short` |
| `l` | 4 字节 | `int` |
| `q` | 8 字节 | `long`、指针 |

读汇编时先扫一眼后缀，就知道这条指令在搬几字节——这是判断变量类型的最快线索。

**🔧 x86-64 上 `long` 和指针都是 `q`**

`multstore` 反汇编里全是 `movq` / `pushq` / `popq`，因为参数和返回值都是 `long*` 和 `long`，宽度都是 8 字节。如果看到 `movl`，说明源代码里多半是 `int`。

---

## `multstore` 反汇编逐行解读

**🎯 源码**

```c
long mult2(long, long);

void multstore(long x, long y, long* dest) {
    long t = mult2(x, y);
    *dest = t;
}
```

**🎯 反汇编**

```asm
multstore:
    endbr64               ; CET 安全指令（现代 GCC 默认插入，可忽略）
    pushq  %rbx           ; 保存 callee-saved 寄存器
    movq   %rdx, %rbx     ; 把第 3 参数 dest 暂存到 %rbx
    call   mult2@PLT      ; 调用 mult2(x, y)，返回值进 %rax
    movq   %rax, (%rbx)   ; *dest = t
    popq   %rbx           ; 恢复 %rbx
    ret
```

**🎯 三个关键点**

1. **调用约定**：Linux x86-64 ABI 前 6 个整数参数依次走 `%rdi %rsi %rdx %rcx %r8 %r9`，返回值进 `%rax`
2. **为什么把 `dest` 搬到 `%rbx`**：`call mult2` 可能破坏所有 caller-saved 寄存器（包含 `%rdx`），`%rbx` 是 callee-saved，存进去才安全；函数末尾 `popq` 恢复保证调用者看不到改动
3. **`(%rbx)` 是间接寻址**：把 `%rax` 写到 `%rbx` 所指地址处，对应 C 里的 `*dest = t`

**⚠️ `call` 地址全零是因为未链接**

`mstore.o` 是单独编译的目标文件，`mult2` 的实际地址要等链接器解析符号后才能填上——这就是 §7 要讲的重定位（relocation）。

---

## 易错点

- `gcc -S` 产物里大量 `.cfi_*` / `.type` / `.size` 是伪指令，不是真实 CPU 指令，读汇编时要会忽略
- `mstore.o` 反汇编里 `call` 的目标地址全是 0 不是 bug，是因为符号还没被链接器解析
- 指令后缀 b/w/l/q 看的是宽度而不是类型，`movl` 和 `movq` 都可能搬运 `int` 或 `unsigned`，靠后缀分不出有符号性
- `endbr64` 是 CET 安全功能插入的指令，与函数逻辑无关，分析时直接略过
- `%rbx` 用于暂存 `dest` 不是随意决定，是 caller-saved / callee-saved 约定带来的必然结果
- AT&T 语法源在左、目的在右；后续章节读 `subq %rax, %rdx` 是 `rdx -= rax`，不是反过来

---

## 工程关联

- ABI 调用约定（`%rdi/%rsi/...` 顺序、`%rax` 返回、callee-saved 集合）是跨模块编译能正确链接的基础；改 ABI 等于全系统 ABI break
- 看 core dump / GDB `bt` 时，理解栈帧和寄存器保存约定才能从 `%rax` / `%rdi` 反推出"哪个函数返回值是什么、第几个参数是什么"
- `objdump -d` 是分析 crash、看 panic 调用栈、定位优化级别差异（`-O0` vs `-O2`）的第一工具
- 链接器报 `undefined reference` 时，反汇编里看到 `call <symbol>@PLT` 加上全零的偏移就是直接证据
- `endbr64` / Shadow Stack 等 CET 指令在新 Intel CPU 上对应 ROP 攻击防护，是现代发行版默认启用的硬件级安全机制

---

## 实验题

**🧪 题 1：`gcc -S` vs `objdump -d` 输出对比**

```c
// mstore.c
long mult2(long, long);
void multstore(long x, long y, long* dest) {
    long t = mult2(x, y);
    *dest = t;
}
```

要求：

- `gcc -O1 -S mstore.c -o mstore.s` 看 `.s`
- `gcc -O1 -c mstore.c && objdump -d mstore.o` 看反汇编
- 数一下：`.s` 里有多少行，反汇编里真实指令多少行
- 找出 `.s` 里 `objdump` 不显示的 3 种伪指令

**🧪 题 2：`-O0` vs `-O2` 编译差异**

```c
long sum(long n) {
    long s = 0;
    for (long i = 0; i < n; ++i) s += i;
    return s;
}
```

要求：

- 分别 `-O0 -S` 和 `-O2 -S`，对比函数体
- 找出 `-O2` 下编译器把循环替换成等差求和公式的证据（应该只剩 `imul` / `lea` 等几条算术指令）
- 数指令数量差异

**🧪 题 3：数据宽度后缀与 C 类型的对应**

```c
int   f_int (int   x) { return x + 1; }
long  f_long(long  x) { return x + 1; }
short f_short(short x) { return x + 1; }
char  f_char (char  x) { return x + 1; }
```

要求：

- `gcc -O1 -S` 编译，找出每个函数里 `add` 指令的后缀
- 列出后缀和类型的对应表，验证 b/w/l/q 直觉
- 注意：`f_short` 和 `f_char` 的返回值可能用 `movzbl` / `movsbl` 扩展，理解为什么

**🧪 题 4：未链接目标文件的 `call` 地址**

要求：

- `gcc -c mstore.c -o mstore.o`，`objdump -d mstore.o` 看 `call` 后面的地址
- 链接：`gcc mstore.c main.c -o prog`，`objdump -d prog` 再看同一个 `call`
- 解释两次结果差异的来源（重定位）
- 用 `readelf -r mstore.o` 看重定位表里关于 `mult2` 的条目

**🧪 题 5：手动从汇编反推 C**

给定汇编：

```asm
endbr64
movq   %rsi, %rax
addq   (%rdi), %rax
movq   %rax, (%rdi)
ret
```

要求：

- 推测函数签名（参数类型、返回类型、参数数量）
- 写出最可能对应的 C 代码
- 解释为什么参数是 `%rdi`（指针）和 `%rsi`（值）而不是反过来
