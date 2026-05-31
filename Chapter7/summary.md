# §7 链接

这一章的主线是：**多个独立编译的 `.o` 文件，是怎么被"拼接 + 填地址"合并成一个能跑的程序的**。核心只有两个动作——**符号解析**（每个引用找到唯一的定义）和**重定位**（把符号的占位地址改成最终运行地址）。理解了这两步，静态库、动态库、PIC、库打桩都是它的延伸。

整章可以浓缩成一条流水线：

```
main.c sum.c
   │  gcc -c（编译+汇编，各自独立）
   ▼
main.o sum.o          ← 可重定位目标文件（ELF），符号地址都是占位
   │  ld（链接器）：① 符号解析  ② 重定位
   ▼
prog                  ← 可执行目标文件，地址已确定
   │  execve + 动态链接器 ld-linux
   ▼
运行中的进程
```

本章实验素材就是经典的 `main.c`（定义 `array`、`main`，引用 `sum`）+ `sum.c`（定义 `sum`），下面所有真实输出都来自它。

---

## §7.1-7.2 为什么需要链接器：分离编译

**🎯 链接解决的核心问题**

把一个大程序拆成多个源文件，每个 `.c` 独立编译成 `.o`。改一个文件只需重编它一个，再重新链接即可——这就是分离编译。代价是：`main.o` 里引用了 `sum`，但编译 `main.c` 时编译器根本不知道 `sum` 在哪、地址是多少，只能先填一个占位符，把"填真地址"这件事推迟给链接器。

**🎯 链接发生的三个时机**

| 时机 | 谁来做 | 例子 |
|------|--------|------|
| 编译时（静态链接） | `ld`（被 gcc 调用） | `.o` + 静态库 `.a` 合并进可执行文件 |
| 加载时（动态链接） | 动态链接器 `ld-linux.so` | 程序启动时把 `libc.so` 映射进地址空间 |
| 运行时 | `dlopen`/`dlsym` | 插件、热更新 |

**🎯 用 `gcc` 拆开看流水线**

```bash
gcc -Og -c main.c sum.c     # 只编译+汇编，得到 main.o sum.o，不链接
gcc -Og -o prog main.c sum.c # 全程：编译 + 链接
```

---

## §7.3-7.4 三类目标文件与 ELF 结构

**🎯 三类目标文件**

- **可重定位目标文件**（`.o`）：代码和数据已编好，但地址未定，等待和其他 `.o` 合并
- **可执行目标文件**（`prog`）：地址已确定，可直接加载运行
- **共享目标文件**（`.so`）：一种特殊的可重定位文件，可在加载/运行时被动态链接

Linux 下三者都是 **ELF** 格式。

**🎯 一个 `.o` 的典型节区**

用 `readelf -S main.o` 看到的真实节区：

| 节 | 内容 |
|----|------|
| `.text` | 已编译的机器码 |
| `.data` | 已初始化的全局/静态变量（如 `int array[2]={1,2}`） |
| `.bss` | 未初始化的全局/静态变量，**不占文件空间**，只记一个大小（类型是 `NOBITS`） |
| `.rodata` | 只读数据（字符串字面量、`switch` 跳转表） |
| `.symtab` | 符号表 |
| `.rela.text` | `.text` 里需要重定位的位置清单 |

**⚠️ `.bss` 的精妙之处**：未初始化（或初始化为 0）的全局变量放 `.bss`，磁盘上只记录"需要多少字节"，加载时一次性清零。这就是为什么 `int big[1000000];` 不会让可执行文件变大 4MB。`.bss` 这个名字来自历史（Block Started by Symbol），现在记成 "Better Save Space" 即可。

---

## §7.5 符号和符号表

**🎯 三类符号**（从单个模块 m 的视角）

- **全局符号（模块内定义、可被外部引用）**：非 `static` 的函数和全局变量。如 `main.o` 里的 `main`、`array`
- **外部符号（被本模块引用、但定义在别处）**：如 `main.o` 引用的 `sum`
- **局部符号（只在本模块内可见）**：`static` 函数和 `static` 全局变量

**⚠️ 别和"局部变量"混淆**：函数里的非 static 局部变量在栈上，**根本不进符号表**，由编译器自己管理。符号表里的 "local" 指的是 `static` 修饰的、链接器层面对其他模块不可见的符号。

**🎯 读 `main.o` 的符号表**

`readelf -s main.o` 真实输出：

```
Num: Value  Size Type    Bind   Ndx Name
  3:    0     30 FUNC    GLOBAL    1 main      ← 本模块定义的全局函数，在 .text(Ndx=1)
  4:    0      8 OBJECT  GLOBAL    3 array     ← 本模块定义的全局变量，在 .data(Ndx=3)
  5:    0      0 NOTYPE  GLOBAL  UND sum       ← 引用但未定义，Ndx=UND
```

判断要点：
- `Ndx=UND`（undefined）= 这个符号本模块没有定义，留给链接器去别处找 → `sum`
- `Bind=GLOBAL` 能跨模块解析，`LOCAL` 不能
- 类型 `FUNC`/`OBJECT` 区分函数和数据对象

---

## §7.6.1 符号解析：强弱符号规则

**🎯 符号解析做什么**

把每个符号引用（如 `main.o` 里的 `UND sum`）关联到某个 `.o` 或库里**唯一**的定义。找不到 → "undefined reference" 错误；找到多个 → 触发强弱符号规则。

**🎯 强符号 vs 弱符号**

- **强符号**：函数、已初始化的全局变量（`int x = 1;`）
- **弱符号**：未初始化的全局变量（`int x;`）

**⚠️ 三条链接器规则（C 程序员的经典坑）**

1. 多个强符号定义 → **链接错误**（重复定义）

```c
// a.c
int x = 1;              // 强
int main(void){ return x; }
// b.c
int x = 2;              // 强 —— 两个强符号同名
// 链接：multiple definition of `x`，直接报错
```

2. 一个强 + 多个弱 → **选强符号**

```c
// a.c
int x = 100;            // 强
// b.c
int x;                  // 弱 —— 被忽略，最终 x 取 a.c 的 100
```

3. 多个弱 → **任选一个**（行为不可预测）

```c
// a.c
int x;                  // 弱
// b.c
int x;                  // 弱 —— 链接器随便挑一个，两边其实共用同一块内存
```

规则 2/3 极其危险：最隐蔽的是**类型还不一致**——

```c
// a.c
int x;                  // 弱，4 字节
int main(void){ x = 0x12345678; f(); printf("%d\n", x); return 0; }
// b.c
double x;               // 弱，8 字节 —— 链接器默默接受
void f(void){ x = -1.0; }  // 按 8 字节写，越界踩了 x 之后的内存
```

链接器**默默接受**，运行时 `f()` 的写操作会破坏 `x` 相邻的内存，引发极难排查的 bug。

**🔧 现代 GCC 的默认收紧**：GCC 10+ 默认 `-fno-common`，把未初始化全局变量也当强符号处理，重复定义直接报错。这正是为这条历史坑打的补丁。要复现书里的弱符号行为得手动加 `-fcommon`。

**🔧 规避之道：定义/声明分离 + 尽量 `static`**

让全局变量**只在一个 `.c` 里定义一次**，其余文件用 `extern` 声明（声明不分配内存、不产生符号定义，只是告诉编译器"它在别处"）：

```c
// config.h —— 只放声明
extern int g_count;     // extern：声明，不是定义，可被多个 .c 包含

// config.c —— 唯一的定义点
#include "config.h"
int g_count = 0;        // 定义，分配内存，全局唯一

// other.c —— 使用方
#include "config.h"     // 拿到 extern 声明即可
void inc(void){ g_count++; }
```

如果变量只在本文件内用，直接 `static`，连符号都不导出，从根上杜绝跨文件撞名：

```c
// logger.c
static int s_level = 3;  // 局部符号，其他 .o 看不见，别的文件可放心再用一个 s_level
```

---

## §7.6.2-7.6.3 静态库与解析顺序

**🎯 为什么要静态库（`.a`）**

把一堆相关 `.o`（如 `printf.o`、`scanf.o`…）打包成一个存档文件 `libc.a`。链接时**只抽取用到的那些 `.o`**，而不是把整个库塞进来，可执行文件不至于臃肿。

```bash
ar rcs libvector.a addvec.o multvec.o   # 打包
gcc main.o -L. -lvector                 # 链接时用 -l 指定
```

**🔧 用计数器探针验证"只抽取用到的 `.o`"**

本目录 `static/` 的例程就是为验证这点设计的：`addvec.c` 和 `multvec.c` 各定义了一个全局计数器 `add_cnt` / `mult_cnt`，但 `main.c` **只调用 `addvec`、既不调用 `multvec` 也不打印这两个计数器**。它们不是给程序逻辑用的，而是当"探针"——链接后查符号表，就能看出哪个 `.o` 真被抽进来了：

```bash
gcc -Og -c addvec.c multvec.c main.c
ar rcs libvector.a addvec.o multvec.o   # 库里两个计数器都在
gcc -Og main.o -L. -lvector -o prog

nm libvector.a | grep cnt   # B add_cnt   B mult_cnt   ← 库里两个都有
nm prog        | grep cnt   # B add_cnt                ← 可执行文件里只剩 add_cnt
nm prog        | grep vec   # T addvec                 ← multvec 整个函数也没进来
```

结论：`main` 只引用了 `addvec`，链接器解析时只为它从库里抽出 `addvec.o`；`multvec.o`（连同 `mult_cnt`）因为没有任何未解析符号指向它，**根本不会被链入**。这正是静态库相对"整包塞入"省空间的根本原因——按需抽取，粒度是单个 `.o`。

**⚠️ 命令行顺序陷阱**

链接器**从左到右扫一遍**命令行。扫到库时，只为"当前还没解析的符号"抽取 `.o`。所以**引用方必须放在被引用的库之前**：

```bash
gcc -L. -lvector main.o   # ❌ 扫到库时还没遇到 main.o，不知道需要 addvec，啥都不抽
gcc main.o -L. -lvector   # ✅ 正确顺序
```

库之间有依赖时甚至要重复写库名，或用 `--start-group`。这是 C/C++ 工程里 "undefined reference" 报错的高频原因之一。

---

## §7.7 重定位：把占位地址改成真地址

**🎯 重定位条目**

符号解析后，链接器知道了每个符号的最终地址，接着回头修改 `.text`/`.data` 里所有引用处的占位值。哪些位置要改，记在 `.rela.text` 里。`readelf -r main.o` 真实输出：

```
偏移量  类型            符号名 + 加数
0x10   R_X86_64_PC32    array - 4
0x15   R_X86_64_PLT32   sum - 4
```

含义：在 `.text` 偏移 `0x10` 处引用了 `array`，偏移 `0x15` 处调用了 `sum`，用 PC 相对方式重定位。

**🎯 链接前后反汇编对比（最直观的一节）**

链接前 `main.o`，`call`/`lea` 的目标全是占位 0：

```asm
d:  48 8d 3d 00 00 00 00  lea 0x0(%rip),%rdi   # array 占位
14: e8 00 00 00 00        call ...             # sum 占位（e8 后 4 字节全 0）
```

链接后 `prog`，地址被填实：

```asm
1136: 48 8d 3d d3 2e 00 00 lea 0x2ed3(%rip),%rdi # 4010 <array>
113d: e8 05 00 00 00       call 1147 <sum>
```

`call` 后的 `05 00 00 00` 就是重定位算出的偏移：下一条指令地址 `0x1142 + 5 = 0x1147`，正是 `sum`。

**🎯 两种基本重定位类型**

- `R_X86_64_PC32`：**PC 相对**，填的是"目标地址 − 当前 PC"的 32 位偏移。位置无关，适合代码内部跳转/调用
- `R_X86_64_64`：**绝对寻址**，直接填符号的 64 位绝对地址

`- 4` 加数是因为 PC 相对偏移要相对于下一条指令，而引用字段距指令末尾还差 4 字节，需提前补偿。

---

## §7.8-7.9 可执行文件的加载

**🎯 加载做了什么**

`execve` 触发：内核把可执行文件的 `.text`、`.data` 等节映射进进程虚拟地址空间，`.bss` 区清零，设置好栈，把 PC 指向入口 `_start`，由它最终调到 `main`。

**🎯 节（section）vs 段（segment）：加载的粒度变了**

链接视角看的是**节**（`.text`/`.data`/`.bss`…，细粒度）；加载视角看的是**段**（segment，粗粒度）。加载时，内核不逐节映射，而是把**权限相同的相邻节聚合成一个段**，整段按统一权限映射。所以可执行文件里主要就两个加载段——一个只读可执行、一个可读写。

**🎯 只读代码段（r-x）存什么**

运行期间不修改、需要被 CPU 执行的内容，映射为可读可执行、**不可写**：

- `.init`：程序初始化代码
- `.text`：编译出的机器指令（函数体）
- `.rodata`：只读数据——字符串字面量、`switch` 跳转表、`const` 全局常量

设成不可写的意义：① 多进程共享同一份只读物理页省内存；② 防止指令被意外/恶意改写（往 `.text` 写 = 段错误）。

**🎯 读写数据段（rw-）存什么**

运行期间会被读写的全局/静态数据，映射为可读可写、**不可执行**：

- `.data`：已初始化且初值非 0 的全局/静态变量（如 `int x[2]={1,2}`）
- `.bss`：未初始化或初值为 0 的全局/静态变量（如 `int z[2]`、`int mult_cnt`），**磁盘文件中不占空间**，加载时由内核一次性清零

**🎯 典型的 x86-64 进程地址空间布局（自低到高）**

```
0x400000  ┌────────────────────┐
          │ .init .text .rodata │  只读代码段 r-x（不可写、可执行、可共享）
          ├────────────────────┤
          │ .data .bss          │  读写数据段 rw-（可写、不可执行）
          ├────────────────────┤
          │ 堆 heap →            │  malloc 向上增长
          │   ↕ 共享库映射区      │
          │ ← 栈 stack           │  向下增长
0x7fff... └────────────────────┘  内核区不可访问
```

链接器把符号最终绑定到这套虚拟地址上（如前面 `array` 落在 `0x4010`），所以不同进程看到的代码地址可以完全一致。

**🔧 自己验证段的划分**

```bash
readelf -l prog   # Program Headers：R E 行=只读代码段，R W 行=读写数据段
                  # 末尾 "Section to Segment mapping" 直接列出每段含哪些节
```

划分依据一句话概括：**运行时是否要写 + 是否要执行**——代码和常量永不改写、要执行 → 只读代码段；全局变量要改、不执行 → 读写数据段。

**⚠️ 现代 PIE 的补充**：`-pie` 二进制（现代发行版默认）会多一个只读的 `GNU_RELRO` 段，把重定位后不再变动的 `.got` 等转为只读，是对古典"两段"模型的安全加固——防止 GOT 被改写劫持控制流。

---

## §7.10-7.11 动态链接与共享库

**🎯 静态链接的两个痛点 → 共享库登场**

静态链接把 `libc` 的代码**复制**进每个可执行文件：① 磁盘/内存浪费（每个程序一份 `printf` 代码）；② libc 修个 bug 要重链所有程序。共享库 `.so` 在**加载时**才链接，内存里**一份代码被多个进程共享**。

**🔧 静态 vs 动态体积对比（本例真实数据）**

```bash
gcc -static -o prog_static main.c sum.c  # 785328 字节
gcc         -o prog        main.c sum.c  #  15872 字节
ldd prog → libc.so.6, ld-linux-x86-64.so.2
```

静态版把 libc 塞进来，膨胀到 ~785 KB；动态版只有 ~16 KB，运行时再由 `ld-linux` 把 `libc.so` 映射进来。`ldd` 列出的就是动态依赖。

**🔧 `static/` vs `shared/`：同一份代码两种打包**

本章用同一套 `addvec.c`/`multvec.c`/`main.c` 分别建了静态库和共享库（`static/` 与 `shared/` 两目录）：

```bash
# static/：归档成 .a，链接时把用到的 .o 复制进 prog
gcc -Og -c addvec.c multvec.c
ar rcs libvector.a addvec.o multvec.o
gcc -Og main.o -L. -lvector -o prog

# shared/：编成位置无关的 .so，链接时只记下"依赖"，不复制代码
gcc -Og -fPIC -c addvec.c multvec.c
gcc -shared -o libvector.so addvec.o multvec.o
gcc -Og main.c -L. -lvector -o prog -Wl,-rpath,'$ORIGIN'
```

**🎯 两种方式的关键差异**

| 维度 | 静态库 `.a`（static/） | 共享库 `.so`（shared/） |
|------|----------------------|------------------------|
| 本质 | 一堆 `.o` 的归档（`ar`） | 一个完整的、位置无关的链接单元 |
| 链接时机 | 编译期，`ld` 把代码**复制进** `prog` | 编译期只**记下依赖**，不复制代码 |
| 抽取粒度 | 按需抽取单个 `.o`（`multvec.o` 未用就不进） | **整个 `.so` 作为一个单元**，不做选择性抽取 |
| `prog` 里的符号 | `addvec` 是 `T`（已定义、代码已在内） | `addvec` 是 `U`（未定义，运行时解析） |
| 真正加载 | 无额外步骤，代码已在 `prog` 内 | 运行时 `ld-linux` 把 `.so` 映射进地址空间 |

用 `nm` 实测对照（本章真实输出）：

```
# static/ prog —— addvec 已在内，multvec 根本没被抽取
00000000000011b8 T addvec
(无 multvec)

# shared/ prog —— addvec 未定义，留给运行时；.so 里两个函数都在
                 U addvec               ← prog 只留个"待解析"占位
# nm -D libvector.so：
00000000000010f9 T addvec               ← .so 是整体，addvec
000000000000112c T multvec              ← 和没人用的 multvec 都在
```

这条对照点出本质：**静态库的"选择性抽取"对共享库不成立**——`.so` 是一个不可分割的链接单元，整个被映射，里面用不到的 `multvec` 也一起在内存里（但因为是共享的，代价被多进程摊薄，见下）。

**🎯 为什么动态库内存消耗小**

核心是**一份只读代码物理页被所有进程共享**，分两个层面看：

- **磁盘**：100 个程序都用 `printf`，动态链接下 `libc.so` 在磁盘上只有**一份**；静态链接下这段 `printf` 代码被**复制进 100 个可执行文件**，磁盘上有 100 份。
- **内存**：`.so` 的 `.text` 是只读的，内核可以让所有用到它的进程的虚拟页**映射到同一份物理页**（写时复制只对可写的 `.data` 生效）。N 个进程用 `libc` → 物理内存里 libc 代码仍只占**一份**；静态链接则是每个进程私有一份，N 份。

```
静态链接：  进程A[libc副本] 进程B[libc副本] 进程C[libc副本]   → 物理内存 3 份
动态链接：  进程A ┐
            进程B ├─→ 同一份 libc.so 只读代码物理页          → 物理内存 1 份
            进程C ┘
```

这就是为什么系统里几百个进程都链 `libc.so`，内存却不会被 libc 代码撑爆——代价随进程数摊薄到接近零。补充说明：本章 `shared/prog` 比 `static/prog` 没小多少（1670 vs 1567 字节 text），是因为例程库本身才几十字节、节省看不出来；真正的收益在 `libc` 这种被海量进程共享的大库上才显著（前面 785 KB vs 16 KB 的对比就是它）。

**⚠️ 动态库的代价**：① 启动时多了符号解析/重定位开销（延迟绑定缓解，见 §7.12）；② 部署时目标机必须有匹配的 `.so`，否则 "cannot open shared object file"——这正是静态链接在容器/独立分发场景仍被青睐的原因。

---

## §7.12 PIC、GOT 与 PLT

**🎯 为什么需要 PIC**

共享库要被加载到不同进程的不同地址、还要多进程共享同一份只读代码，就不能把绝对地址写死在代码里。**位置无关代码（PIC, `-fPIC`）** 让代码段不含任何绝对地址，加载到哪都能跑。

**🎯 GOT：解决全局数据/函数引用**

代码段保持不变，把"会变的地址"集中放到一张可写的表 **GOT（全局偏移量表）** 里。代码用 PC 相对方式访问 GOT 表项（PC 相对偏移在链接时就固定了），表项的具体值由动态链接器在加载时填。

**🎯 PLT + 延迟绑定**

函数调用走 **PLT（过程链接表）**，配合 GOT 实现**延迟绑定**：函数第一次被调用时才去解析真实地址并回填 GOT，之后直接命中。好处是启动时不必解析成百上千个可能根本用不到的库函数。

**🔧 上面 `main.o` 里 `sum` 的重定位类型是 `R_X86_64_PLT32`**——即使本例 `sum` 最终静态链接进来不走 PLT，编译器默认也按"可能经过 PLT"的方式生成调用，体现了现代工具链对 PIC/PIE 的默认偏好（现代发行版默认 `-pie`）。

**🎯 全局变量走的另一条路：copy relocation**

引用共享库里的**函数**和引用它的**全局变量**，机制完全不对称。用 `shared/` 例程的 `prog` 看动态重定位（`readelf -r prog`，真实输出）：

```
add_cnt  →  R_X86_64_COPY       ← 全局变量
addvec   →  R_X86_64_JUMP_SLO   ← 函数
```

- **函数 `addvec` → JUMP_SLOT**：`prog` 里只放 PLT 桩，运行时解析到 `.so` 内的真实地址。函数体**留在库里**，多进程共享只读代码。
- **全局变量 `add_cnt` → COPY**：链接器在 **`prog` 自己的 `.bss`** 里预留空间（实测落在 `0x4020`），加载时动态链接器把 `.so` 里的初值**拷贝**过来。从此 `prog` 里的副本是**唯一权威实例**，连 `.so` 内部访问 `add_cnt` 也被重定向到这份。

```
nm prog            →  ...4020 B add_cnt    ← 变量在 prog 里有定义（B）
                      U addvec             ← 函数才是未定义（U）
nm -D libvector.so →  ...400c B add_cnt    ← .so 里那份被 prog 的副本遮蔽
```

**🎯 为什么不对称**：`prog` 自身代码想用链接期就固定的地址直接访问全局变量（快，不查 GOT），但 `.so` 的加载地址链接期未知，于是把变量实例"拉进"可执行文件给它固定地址，让 `.so` 迁就。函数本来就过 PLT 一层跳转，间接成本可接受，所以留在库里。一句话：**数据往可执行文件搬，函数往库里留**。

**🔧 工程结论一：只读代码共享、可写数据不共享**

由此可以厘清"动态库省内存"到底省在哪：`.so` 的 `.text` 是只读的，多进程映射到**同一份物理页**——省的是**代码**。而全局变量这类可写数据，每个进程都有自己的副本（要么 copy relocation 搬进各自的 `prog`，要么 `.so` 数据段触发写时复制），进程间**从不共享**。所以再多进程共用一个 `.so`，省的是那份代码页，数据该占的内存一份都不少。

**🔧 工程结论二：copy relocation 是 ABI 脆弱点**

`prog` 里预留的空间大小在链接期定死。若新版 `.so` 把 `add_cnt` 换成更大的类型，老 `prog` 的空间不够，加载时拷贝会越界。这是共享库升级要保持导出变量类型/大小兼容的原因，也是工程上**偏好用 getter 函数替代直接导出全局变量**的动机之一。

---

## §7.13 库打桩

**🎯 三种打桩（interpositioning）时机**——拦截并替换对库函数的调用，常用于调试、监控、内存检测：

- **编译时打桩**：用宏/头文件把 `malloc` 替换成自己的包装
- **链接时打桩**：`ld --wrap=malloc`，所有 `malloc` 调用被改成 `__wrap_malloc`，真函数变成 `__real_malloc`
- **运行时打桩**：`LD_PRELOAD` 指定一个 `.so`，里面同名函数会**覆盖**库里的版本，无需重编程序

**🔧 运行时打桩例程（书本 §7.13.3，本章 `preload/`）**

`preload/mymalloc.c` 写一对同名 `malloc`/`free`，编成 `.so`，用 `LD_PRELOAD` 抢在 libc 之前加载；包装函数用 `dlsym(RTLD_NEXT, ...)` 拿到 libc 的真实实现再转发，从而在中间插入日志：

```c
#define _GNU_SOURCE
#include <dlfcn.h>
void *malloc(size_t size) {
  void *(*mallocp)(size_t) = dlsym(RTLD_NEXT, "malloc"); /* libc 的真 malloc */
  void *ptr = mallocp(size);
  printf("malloc(%d) = %p\n", (int)size, ptr);           /* 插入的观测点 */
  return ptr;
}
```

`int.c` 是毫不知情的目标程序（一次 `malloc` + 一次 `free`）。构建并运行：

```bash
gcc -DRUNTIME -shared -fPIC -o mymalloc.so mymalloc.c -ldl
gcc -o intr int.c
./intr                          # 直接跑：无任何输出
LD_PRELOAD=./mymalloc.so ./intr # 打桩跑：
# malloc(32) = 0x...
# free(0x...)
```

**⚠️ `RTLD_NEXT` 与重入陷阱**：`dlsym(RTLD_NEXT, "malloc")` 表示"沿加载顺序找**下一个**叫 malloc 的符号"，正好跳过自己、拿到 libc 的版本。书本原版有个会段错误的坑：包装里的 `printf` 内部又会调用 `malloc`，造成 `malloc→printf→malloc` 无限递归。`preload/mymalloc.c` 用一个 `__thread` 标志位做**重入保护**修掉了它——这是真实写 malloc hook 必踩的坑。

**🔧 `LD_PRELOAD` 的真实工程用法**

运行时打桩不只是教学玩具，是 Linux 上**不改源码、不重编**就替换库实现的标准手段：

```bash
# 1) 换内存分配器：把整个程序的 malloc 换成 jemalloc/tcmalloc，对比性能
LD_PRELOAD=/usr/lib/x86_64-linux-gnu/libjemalloc.so.2 ./myserver

# 2) 内存错误检测：ASan 运行时库（用 gcc -fsanitize=address 编译更常见，
#    但对没带 ASan 编译的程序也能用 preload 方式挂上）
LD_PRELOAD=$(gcc -print-file-name=libasan.so) ./prog

# 3) 故障注入/调试：libeatmydata 让所有 fsync 变空操作（加速测试）、
#    自写 .so 拦截 open/connect 做 mock
LD_PRELOAD=./mock_net.so ./client
```

它的能力边界与注意点：
- 只能拦**动态符号**（走 PLT/GOT 的调用）；静态链接的程序、或已被内联的调用拦不到
- 多个 `.so` 用空格或冒号分隔，按顺序加载，先出现的先匹配
- setuid/setgid 程序出于安全会**忽略** `LD_PRELOAD`，所以它不是提权漏洞——但仍是常见的恶意注入手段，安全审计要检查可疑的 `LD_PRELOAD`/`/etc/ld.so.preload`

---

## §7.14 目标文件操作工具

这一节是把全章理论"工具化"——上面每个概念都对应一条能跑的命令。下面输出都来自本章 `static/` 例程（`prog` 由 `main.o` + `libvector.a` 链成）。

**🎯 工具速查表**

| 工具 | 作用 | 常用命令 |
|------|------|---------|
| `readelf` | 看 ELF 头/节区/符号/重定位（最全面） | `readelf -h/-S/-s/-r/-d` |
| `objdump` | 反汇编 + 看节内容 | `objdump -d`、`objdump -dr`（带重定位标注） |
| `nm` | 列符号表 | `nm`、`nm -D`（动态符号）、`nm -C`（解修饰） |
| `ldd` | 列动态库依赖 | `ldd prog` |
| `ar` | 创建/查看静态库 | `ar rcs lib.a *.o`、`ar -t lib.a` |
| `size` | 各段大小（text/data/bss） | `size prog` |
| `strings` | 提取可打印字符串 | `strings prog` |
| `strip` | 删除符号表，减小体积 | `strip prog` |
| `file` | 判断文件类型 | `file prog` |
| `c++filt` | C++ 修饰名 → 可读名 | `nm a.o \| c++filt` |

**🔧 `readelf`：ELF 全景**

```bash
readelf -h prog   # ELF 头：类型(EXEC/DYN/REL)、入口地址、架构
readelf -S prog   # 节区表：.text/.data/.bss/.got/.plt 的地址和大小
readelf -s prog   # 符号表（.symtab，strip 后消失）
readelf -r main.o # 重定位条目（见 §7.7）
readelf -d prog   # .dynamic 段：NEEDED 列出依赖的 .so、动态链接信息
```

`readelf -d` 里的 `NEEDED libc.so.6` 就是 `ldd` 信息的源头。

**🔧 `size` + `file`：一眼看清布局和性质**

```
$ size prog
   text    data     bss     dec     hex
   1567     616      24    2207     89f       ← 对应 .text/.data/.bss 字节数

$ file prog
prog: ELF 64-bit LSB pie executable, x86-64, dynamically linked,
      interpreter /lib64/ld-linux-x86-64.so.2, ... not stripped
```

`file` 一行就交代了三件事：`pie`（位置无关可执行，对应 §7.12）、`dynamically linked`（动态链接，§7.10）、`not stripped`（符号表还在）。

**🔧 `nm -D`：看程序运行时要从库里解析哪些符号**

```
$ nm -D prog
   U __printf_chk@GLIBC_2.3.4      ← U=未定义，运行时由 libc 提供
   U __libc_start_main@GLIBC_2.34
   w __gmon_start__                ← w=弱符号
```

`U` 标记的就是延迟绑定要解析的外部函数；`@GLIBC_x.y` 是符号版本，跨 glibc 版本部署时这里是"运行不起来"的常见根因。

**🔧 `ar -t` + `ldd`：库的两端**

```
$ ar -t libvector.a        # 静态库里有哪些成员 .o
addvec.o
multvec.o

$ ldd prog                 # 运行时依赖哪些动态库
   libc.so.6 => /lib/x86_64-linux-gnu/libc.so.6
   /lib64/ld-linux-x86-64.so.2
```

**⚠️ `ldd` 的安全提醒**：`ldd` 在某些实现下会实际"加载并运行"目标的动态链接逻辑，**不要对不可信的可执行文件直接 `ldd`**，用 `objdump -p prog | grep NEEDED` 或 `readelf -d` 更安全。

**🔧 `strip`：体积与符号的权衡**

```bash
strip prog          # 删掉 .symtab/.strtab，体积变小
readelf -s prog     # strip 后只剩 .dynsym，看不到 main/局部符号了
```

发布版常 `strip` 减小体积、隐藏内部符号，代价是 core dump、`gdb`、`perf` 拿不到函数名（需保留带符号的版本或单独的 `.debug` 文件）。

---

## 易错点

- `.bss` 不占磁盘空间，只记大小，加载时清零——大数组放全局不会撑大可执行文件
- 符号表里的 "LOCAL" 指 `static` 符号，不是函数里的局部变量；普通局部变量在栈上、根本不进符号表
- 弱符号规则会让两个 `int x;` 静默共用内存，是隐蔽 bug 之源；现代 GCC 默认 `-fno-common` 才把它变成报错
- 链接命令行顺序敏感：引用方在前、库在后，否则"undefined reference"；与编译时的文件顺序无关，是链接阶段的规则
- 重定位的 PC 相对偏移要 `-4`（相对下一条指令补偿），不要误以为偏移就是符号地址本身
- `R_X86_64_PC32` 是相对偏移、`R_X86_64_64` 才是绝对地址，二者用途不同
- 静态库 `.a` 只抽取用到的 `.o`，不是整包塞入；共享库 `.so` 是加载/运行时才链接
- "选择性抽取"只适用于静态库：`.so` 是不可分割的整体，里面没人用的函数也会被一起映射；省内存靠的是只读代码页多进程共享，不是少装代码
- 动态链接省的是"物理内存/磁盘的总份数"，不是单个可执行文件一定更小；小库甚至可能因 PLT/GOT 略大，收益在 libc 这类被海量进程共享的大库上才明显
- 引用 `.so` 的全局变量在 `prog` 符号表里是已定义（`B`/`D`，因 copy relocation 搬进了 `prog` 的 `.bss`），而引用 `.so` 的函数才是 `U`——别看到 `B` 就以为它没用动态库
- `LD_PRELOAD` 能在不改、不重编程序的前提下替换库函数，这是它既强大又有安全风险的原因
- `nm` 默认看 `.symtab`、`nm -D` 看动态符号表 `.dynsym`；`strip` 后 `.symtab` 没了，`nm` 会报 "no symbols"，但 `nm -D` 仍有输出
- 别对不可信文件直接 `ldd`（某些实现会触发加载逻辑），改用 `readelf -d` 或 `objdump -p` 看 `NEEDED` 更安全

---

## 工程关联

- **排查 "undefined reference"**：先用 `nm`/`readelf -s` 确认符号是 `UND` 还是 `LOCAL`，再检查链接命令行里库的顺序，是 C/C++ 构建报错的头号场景
- **可执行文件体积优化**：`-static` 把依赖打包便于部署但体积大；动态链接省空间但要保证目标机有对应 `.so`；用 `ldd` 排查运行时 "cannot open shared object file"
- **ELF 工具链**：`readelf -S/-s/-r` 看节区/符号/重定位、`objdump -d` 反汇编、`nm` 列符号、`strip` 去符号表减小体积——本章是这套工具的理论基础
- **`LD_PRELOAD` / `LD_LIBRARY_PATH`**：运维和调试常用，前者打桩拦截、后者改库搜索路径。真实用途如把 `malloc` 换成 `libjemalloc.so`/`libtcmalloc.so` 对比性能、挂 `libasan.so` 查内存错误、用自写 `.so` mock 网络/文件调用做故障注入；也是常见的提权/注入攻击面（`/etc/ld.so.preload`），安全审计必查
- **链接时优化（LTO, `-flto`）**：把"链接器只是拼接"扩展为跨模块优化，链接阶段还能内联跨文件函数
- **符号可见性控制**：大型库用 `-fvisibility=hidden` + `__attribute__((visibility("default")))` 收敛导出符号，减小 `.so` 的动态符号表、加快加载、避免符号冲突

---

## 实验题

**🧪 题 1：观察链接前后的地址填充**

用本目录的 `main.c` + `sum.c`：

```bash
gcc -Og -c main.c sum.c
gcc -Og -o prog main.c sum.c
objdump -d main.o | grep -A8 '<main>:'
objdump -d prog   | grep -A8 '<main>:'
```

要求：
- 找出链接前 `call`/`lea` 后面的占位字节（应全是 `00`）
- 找出链接后 `call` 的偏移字节，手算"下一条指令地址 + 偏移"应等于 `sum` 的地址
- 解释为什么 `lea` 用的是 PC 相对寻址而不是绝对地址

**🧪 题 2：读符号表，分类每个符号**

```bash
readelf -s main.o
readelf -s sum.o
```

要求：
- 标出每个符号是全局/外部(UND)/局部，分别对应源码里的哪个标识符
- 解释为什么 `main.o` 里 `sum` 的 `Ndx` 是 `UND`，而 `sum.o` 里 `sum` 有具体节号
- 给 `sum` 加上 `static` 重新编译，观察 `main.o` 还能不能链接，错误信息是什么

**🧪 题 3：复现弱符号陷阱**

写两个文件：

```c
// a.c
#include <stdio.h>
int x;            // 弱符号
void f(void);
int main(void){ x = 0x12345678; f(); printf("%d\n", x); return 0; }

// b.c
double x;         // 同名，弱符号，但类型不同（8 字节）
void f(void){ x = -1.0; }
```

要求：
- 用 `gcc -fcommon a.c b.c -o bug` 编译（现代 GCC 需显式 `-fcommon` 才能复现），运行看 `x` 被怎样破坏
- 去掉 `-fcommon`（默认 `-fno-common`）重编，观察链接器是否报重复定义
- 总结：默认行为为什么更安全

**🧪 题 4：静态库与命令行顺序**

把 `sum.c` 打包成静态库再链接：

```bash
gcc -Og -c sum.c
ar rcs libsum.a sum.o
gcc -Og -c main.c
gcc main.o -L. -lsum -o prog_lib      # 正确顺序
gcc -L. -lsum main.o -o prog_bad      # 错误顺序
```

要求：
- 观察哪条命令成功、哪条报 "undefined reference to sum"
- 用 `nm libsum.a` 确认库里确实有 `sum`
- 解释链接器从左到右扫描、按需抽取的机制

**🧪 题 5：静态 vs 动态链接对比**

```bash
gcc -static -o prog_static main.c sum.c
gcc         -o prog        main.c sum.c
ls -l prog prog_static
ldd prog
ldd prog_static
file prog prog_static
```

要求：
- 记录两者体积差异，解释差距来自哪里
- 解释为什么 `ldd prog_static` 会显示 "not a dynamic executable"
- 说明动态版运行时由谁（`ld-linux`）在何时把 `libc.so` 映射进来

**🧪 题 6：用 LD_PRELOAD 做运行时打桩**

写一个拦截 `malloc` 的 `.so`：

```c
// myhook.c
#define _GNU_SOURCE
#include <stdio.h>
#include <dlfcn.h>
void *malloc(size_t size){
    static void *(*real)(size_t) = NULL;
    if(!real) real = dlsym(RTLD_NEXT, "malloc");
    void *p = real(size);
    fprintf(stderr, "malloc(%zu) = %p\n", size, p);
    return p;
}
```

要求：
- `gcc -shared -fPIC -o myhook.so myhook.c -ldl` 编译
- 用 `LD_PRELOAD=./myhook.so ls` 运行任意程序，观察每次 malloc 被打印
- 解释 `RTLD_NEXT` 的作用，以及为什么这能在不重编目标程序的情况下生效
