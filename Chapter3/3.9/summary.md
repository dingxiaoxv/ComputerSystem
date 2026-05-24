# §3.9 异质的数据结构

这一节的主线是：**结构体和联合体在内存里到底长什么样，对齐规则如何决定 padding，以及为什么对齐既是硬件约束也是性能武器**

- 结构体字段按声明顺序顺序排布，编译器在中间和末尾插入 padding 满足对齐
- 联合体所有字段共享同一块内存，大小取最大字段，对齐取最严字段
- 对齐规则：K 字节类型必须放在 K 的倍数地址上（x86-64 上 1/2/4/8 字节对应 1/2/4/8 对齐）
- 结构体整体大小必须是其最大成员对齐的倍数（为了数组里下一个元素仍满足对齐）

---

## 结构体的内存布局

**🎯 字段按声明顺序排列**

C/C++ 标准强制：结构体字段在内存中按**声明顺序**排布，编译器**不可重排**。这意味着字段顺序直接决定 padding 数量和 sizeof。

```cpp
struct S1 {
    char  a;   // offset 0
    int   b;   // offset 4（前面填 3 字节）
    char  c;   // offset 8
    // 末尾填 3 字节，sizeof = 12
};

struct S2 {
    int   b;   // offset 0
    char  a;   // offset 4
    char  c;   // offset 5
    // 末尾填 2 字节，sizeof = 8
};
```

字段排列**从大到小**（按对齐严格度降序）通常能得到最紧凑的布局。

**🎯 访问字段 = 基址 + 编译期常量偏移**

```cpp
struct rec { int i; int j; int a[2]; int *p; };
```

汇编里访问 `r->j` 就是一条 `movl 4(%rdi), %eax`——偏移 4 是编译期算出来的常量，不需要任何运行时计算。

```asm
movl  (%rdi),  %eax    # r->i
movl  4(%rdi), %eax    # r->j
movl  8(%rdi), %eax    # r->a[0]
movq  16(%rdi),%rax    # r->p
```

**🎯 指向结构体内部字段的指针**

```cpp
int *ptr = &r->a[1];
```

```asm
leaq 12(%rdi), %rax    # 基址 + 12（a 在偏移 8，a[1] 在偏移 12）
```

注意是 `leaq`——只算地址，不解引用。

---

## 联合体 union

**🎯 字段共享同一片内存**

union 的所有成员从同一个地址开始，sizeof 是最大成员的大小（再向上取整到对齐边界）。

```cpp
union U {
    char  c;       // 占 1 字节
    int   i;       // 占 4 字节
    double d;      // 占 8 字节
    // sizeof(U) == 8，对齐 == 8
};
```

**🔧 经典用途 1：节省空间的"标签联合"**

```cpp
enum NodeKind { LEAF, INTERNAL };
struct Node {
    NodeKind kind;
    union {
        struct { double value; } leaf;
        struct { Node *left, *right; } internal;
    };
};
```

leaf 和 internal 节点永远不会同时存在，让它们共享内存能省一半空间。

**🔧 经典用途 2：类型双关（type punning）**

```cpp
union { float f; uint32_t u; } pun;
pun.f = 3.14f;
uint32_t bits = pun.u;   // 拿到 float 的原始位模式
```

⚠️ **这在 C 里是合法的，但在 C++ 标准下是 UB**（只允许读最后写入的成员）。C++ 里要做位模式重解释，应该用 `std::bit_cast`（C++20）或 `memcpy`。

**⚠️ union 不能存放有自定义构造/析构的类型**（除非显式管理生命周期），因为编译器无法决定调用谁的析构。C++17 引入了 `std::variant` 作为类型安全的替代品。

---

## `std::variant`：类型安全的 union（C++17）

**🎯 为什么需要 variant**

裸 union 的三大痛点：

1. **无类型标签**——存了 `int` 还是 `double` 全靠自己记，记错就是 UB
2. **不能存非平凡类型**——`std::string`、`std::vector` 这类有构造/析构的类型放进 union 必须手动 placement new + 显式析构
3. **类型双关在 C++ 是 UB**——读非最后写入的成员是 UB

`std::variant<Ts...>` 把"标签 + 存储 + 生命周期管理"打包到一起，等价于"安全的 tagged union"。

```cpp
#include <variant>

std::variant<int, double, std::string> v;
v = 42;          // 装 int
v = 3.14;        // 自动析构 int，装 double
v = "hello";     // 自动析构 double，构造 string
```

**🎯 内存布局：仍然是 union + 一个 index**

```cpp
sizeof(std::variant<char, double, int>)
// ≈ sizeof(double) + sizeof(index) + padding
// 通常是 16（8 字节存储 + 1 字节 index + 7 字节 padding）
```

存储区按最严类型对齐、按最大类型大小分配，外加一个小整数标签记当前持有哪个类型。这就是裸 union 加上手动 tag 的模式，只是编译器替你做了正确性保证。

**🎯 访问方式总览**

```cpp
std::variant<int, std::string> v = 42;

// 方式 1：按类型取，类型不对会抛 std::bad_variant_access
int x = std::get<int>(v);

// 方式 2：按索引取
int y = std::get<0>(v);

// 方式 3：试探性访问，返回指针或 nullptr
if (auto* p = std::get_if<int>(&v)) {
    std::cout << *p;
}

// 方式 4：visit——最推荐
```

---

## `std::visit` 详解

**🎯 一句话定义**

给一个 variant 和一个**能处理所有可能类型**的可调用对象，`visit` 自动按 variant 当前持有的类型派发到正确的重载。

```cpp
std::variant<int, double, std::string> v = 42;

std::visit([](auto&& x) {
    std::cout << x << '\n';
}, v);
```

这段代码做了什么：

1. 运行时 `v` 当前持有 `int`（`v.index() == 0`）
2. `visit` 查 `v.index()`，知道里面是 `int`
3. 调用 `lambda(std::get<int>(v))`，也就是 `lambda(42)`

如果之后写 `v = 3.14`，同一行 `visit` 代码不变，这次会调用 `lambda(3.14)`——派发目标在**运行时**变。

**🎯 lambda 必须能处理所有类型**

variant 里有几种可能类型，lambda 必须对**所有这些**都能编译通过。所以用 `auto&&` 让编译器为每种类型各生成一份：

```cpp
auto printer = [](auto&& x) { std::cout << x; };
//                ^^^^^^^^ 等价于 template<class T> operator()(T&& x)
```

编译器实际为 `int`/`double`/`string` 各生成一份 `operator()`，`visit` 在运行时挑一份调用。

如果写成 `[](int x) { ... }` 只接受 int——编译直接报错，因为另外两种类型没法调用。

**🎯 按类型分别处理：`if constexpr`**

```cpp
std::variant<int, std::string> v = "hello";

std::visit([](auto&& x) {
    using T = std::decay_t<decltype(x)>;
    if constexpr (std::is_same_v<T, int>) {
        std::cout << "int: " << x * 2;
    } else if constexpr (std::is_same_v<T, std::string>) {
        std::cout << "string len: " << x.size();
    }
}, v);
```

`if constexpr` 是**编译期 if**——不满足条件的分支不编译。所以 int 分支里写 `x.size()` 不会出错，因为为 `T=int` 生成的那一份代码根本不会编译 string 分支。

**🔧 overloaded 模式（C++17 标准写法，最推荐）**

写一个继承多个 lambda 的小工具：

```cpp
template<class... Ts> struct overloaded : Ts... { using Ts::operator()...; };
template<class... Ts> overloaded(Ts...) -> overloaded<Ts...>;   // 推导指引

std::variant<int, double, std::string> v = 3.14;

std::visit(overloaded {
    [](int x)                { std::cout << "int "    << x; },
    [](double x)             { std::cout << "double " << x; },
    [](const std::string& x) { std::cout << "string " << x; },
}, v);
```

`overloaded` 把三个 lambda 合并成一个对象，三个 `operator()` 都在。`visit` 按 v 当前类型选最匹配的那个。**读起来像 pattern match**。

**🎯 杀手锏：编译期穷尽性检查**

如果在 overloaded 里漏写一个类型：

```cpp
std::visit(overloaded {
    [](int x)    { ... },
    [](double x) { ... },
    // 漏了 string
}, v);
```

编译器直接报错：找不到 `operator()(std::string&)` 的重载。

对比 `switch (v.index())` + if-else：漏掉一个 case 编译器不会管，要等运行时跑到那个分支才发现。这就是 visit 相比手写派发的核心价值。

**🎯 内部实现：跳转表**

`visit` 大致等价于：

```cpp
switch (v.index()) {
    case 0: return f(std::get<0>(v));
    case 1: return f(std::get<1>(v));
    case 2: return f(std::get<2>(v));
}
```

标准库在编译期为 N 种类型生成一张大小为 N 的函数指针表，运行时一次表查找完成派发——比虚函数多一层间接，但通常被内联优化掉。

**🎯 三种写法的心智模型对比**

| 写法 | 漏 case | 适用场景 |
|------|---------|----------|
| `if (v.index() == 0) std::get<0>(v)...` | 编译不报错 | 极简临时代码 |
| `visit + auto lambda + if constexpr` | 编译不报错（漏的分支永远不进） | 类型很多、处理逻辑相似 |
| `visit + overloaded`（推荐） | **编译报错** | 默认选这个，像 match |

**🎯 完整可运行例子**

```cpp
#include <variant>
#include <string>
#include <iostream>

template<class... Ts> struct overloaded : Ts... { using Ts::operator()...; };
template<class... Ts> overloaded(Ts...) -> overloaded<Ts...>;

using Shape = std::variant<int, double, std::string>;

void describe(const Shape& s) {
    std::visit(overloaded {
        [](int n)                { std::cout << "整数 "   << n << '\n'; },
        [](double d)             { std::cout << "小数 "   << d << '\n'; },
        [](const std::string& s) { std::cout << "字符串 " << s << '\n'; },
    }, s);
}

int main() {
    describe(42);       // 整数 42
    describe(3.14);     // 小数 3.14
    describe("hello");  // 字符串 hello
}
```

**🔧 经典用法 1：表达式树（替代继承）**

```cpp
struct Add; struct Mul; struct Num;
using Expr = std::variant<Num, std::unique_ptr<Add>, std::unique_ptr<Mul>>;

struct Num { double value; };
struct Add { Expr lhs, rhs; };
struct Mul { Expr lhs, rhs; };

double eval(const Expr& e) {
    return std::visit(overloaded {
        [](const Num& n)                     { return n.value; },
        [](const std::unique_ptr<Add>& a)    { return eval(a->lhs) + eval(a->rhs); },
        [](const std::unique_ptr<Mul>& m)    { return eval(m->lhs) * eval(m->rhs); },
    }, e);
}
```

相比虚函数继承：无虚表开销、栈上分配、闭合类型集合（不会被未知子类破坏）。

**🔧 经典用法 2：错误处理（C++23 之前的 expected 替代品）**

```cpp
struct Ok    { int value; };
struct Error { std::string msg; };
using Result = std::variant<Ok, Error>;

Result parse(const std::string& s) {
    if (s.empty()) return Error{"empty input"};
    try {
        return Ok{std::stoi(s)};
    } catch (const std::exception& e) {
        return Error{e.what()};
    }
}
```

调用侧两种风格：

```cpp
// 风格 1：visit + overloaded（最推荐，穷尽性检查）
auto r = parse("42");
std::visit(overloaded {
    [](Ok ok)            { std::cout << "got " << ok.value << '\n'; },
    [](const Error& err) { std::cerr << "error: " << err.msg << '\n'; },
}, r);

// 风格 2：get_if 试探（适合只关心一种结果的场景）
if (auto* ok = std::get_if<Ok>(&r)) {
    use(ok->value);
} else {
    auto& err = std::get<Error>(r);
    log(err.msg);
}

// 链式：把 parse 结果继续传给下一步
Result doubled = std::visit(overloaded {
    [](Ok ok)            -> Result { return Ok{ok.value * 2}; },
    [](const Error& err) -> Result { return err; },          // 错误透传
}, parse("21"));
```

C++23 有了 `std::expected<T, E>`，语义更明确（专门表达"成功值 or 错误"，还支持 `.and_then()` 链式）。在此之前 variant 是首选方案。

**🔧 经典用法 3：状态机**

```cpp
struct Idle {};
struct Connecting { int retry; };
struct Connected  { int socket_fd; };
struct Failed     { std::string reason; };

using State = std::variant<Idle, Connecting, Connected, Failed>;
```

转移函数用 `std::visit` 实现，编译器保证每个状态都被处理。

**⚠️ variant 的代价和坑**

- `std::visit` 内部用函数指针表/跳转表实现，调度成本略高于直接虚函数（但通常可被内联）
- `std::variant<...>` 不能为空——必须至少持有一个类型。如果初始化失败会进入 `valueless_by_exception()` 状态
- 想表达"可能没值"用 `std::variant<std::monostate, ...>` 或 `std::optional`
- 所有类型必须可拷贝/可移动，否则相应操作被禁用
- 不能递归：`variant<int, vector<self>>` 写不出来，必须借助 `unique_ptr` 或专用库（如 boost::recursive_variant）

**🎯 与 union 的对比**

| 维度 | `union` | `std::variant` |
|------|---------|----------------|
| 类型标签 | 自己维护 | 自动管理（`index()`） |
| 非平凡类型 | 需 placement new + 手动析构 | 自动构造/析构 |
| 类型安全 | 全无（UB 容易触发） | 编译期 + 运行期双保险 |
| 类型双关 | C 合法、C++ UB | 设计上不支持，要双关用 `std::bit_cast` |
| 额外开销 | 0 | 一个 index 字段（通常 1-4 字节） |
| C 兼容 | ✅ | ❌（仅 C++17+） |

一句话：**新代码用 `std::variant`，只有跟 C 交互或对内存极度敏感才用裸 union**。

---

## 数据对齐规则

**🎯 x86-64 的对齐要求**

| 类型大小 | 对齐边界 |
|---------|---------|
| 1 字节（`char`, `bool`） | 1 |
| 2 字节（`short`） | 2 |
| 4 字节（`int`, `float`） | 4 |
| 8 字节（`long`, `double`, 指针） | 8 |
| 16 字节（`long double`, `__m128`） | 16 |

规则一句话：**K 字节类型的地址必须是 K 的倍数**。

**🎯 为什么硬件需要对齐**

- 早期 x86 不对齐访问会抛 `SIGBUS`（现代 x86 容忍但会拆成多次内存访问，性能受损）
- ARM、MIPS 等 RISC 架构对未对齐访问**直接拒绝**
- 缓存行（通常 64 字节）按对齐边界切分，跨缓存行的访问要读两次缓存

**🎯 结构体整体对齐**

结构体的对齐等于其**最严字段的对齐**，sizeof 必须是该对齐的倍数。

```cpp
struct S { int i; char c; };
// alignof(S) = 4（最严字段是 int）
// sizeof(S) = 8（末尾补 3 字节，让 S 数组中每个元素的 i 仍然 4 字节对齐）
```

末尾 padding 的作用：保证 `S arr[10]` 里 `arr[1].i` 仍然是 4 字节对齐。

---

## C++ 中的字节对齐控制（重点补充）

**🎯 `alignof` 和 `alignas`：查询和指定对齐**

```cpp
static_assert(alignof(int) == 4);
static_assert(alignof(double) == 8);

alignas(16) int x;            // 强制 x 按 16 字节对齐
alignas(64) struct CacheLine {
    int counter;
};
```

`alignas(N)` 只能**加严**，不能放松（不能让 int 变成 1 字节对齐）。要放松必须用 `#pragma pack` 或 `__attribute__((packed))`。

**🎯 `alignas` 何时使用**

日常代码很少需要它——编译器默认对齐已经够用。只有这几个场景才主动加严：

1. **SIMD 指令要求严格对齐**

```cpp
alignas(32) float buf[8];                // AVX 256-bit 要 32 字节对齐
__m256 v = _mm256_load_ps(buf);          // vmovaps 不对齐会触发 #GP
```

不加 `alignas`，`buf` 只按 `float`（4 字节）对齐，7/8 概率崩。

2. **避免 false sharing**（见下一节）

```cpp
struct alignas(64) Counter { std::atomic<long> v; };
```

3. **和硬件 / DMA 交互**

```cpp
alignas(4096) char dma_buffer[4096];     // DMA 引擎要求页对齐
```

4. **自己实现内存池 / 小对象优化**

```cpp
template<typename T, size_t N>
class SmallVector {
    alignas(T) char inline_storage[sizeof(T) * N];   // 起始地址必须满足 T 的对齐
};
```

**什么时候不需要 `alignas`**：

- 普通业务结构体——编译器按字段类型自动算对齐
- `new T` / `make_shared<T>`——C++17 起 `operator new` 满足 `alignof(T)`
- 栈上局部变量——编译器自动按类型对齐

判断准则：**除非能指出一个具体的硬件 / 性能原因要求更严的对齐，否则不要加 `alignas`**。乱加只会浪费内存。

**🎯 `#pragma pack` / `__attribute__((packed))`：取消 padding**

```cpp
#pragma pack(push, 1)
struct Packed {
    char a;
    int  b;
    char c;
};                         // sizeof == 6，无 padding
#pragma pack(pop)

// 或 GCC 扩展
struct __attribute__((packed)) Packed2 {
    char a;
    int  b;
};                         // sizeof == 5
```

**🔧 何时用 packed**：
- 网络协议、文件格式解析（要求字段紧贴）
- 与硬件寄存器、嵌入式设备交互

⚠️ **代价**：取 packed 结构里未对齐字段的指针是 UB（在 ARM 上可能直接崩），并且编译器会生成多条 `movb` 拼装的低效代码。

**🔧 缓存行对齐：避免 false sharing**

**背景：缓存的最小单位是缓存行**

CPU 不按字节读内存，而是按**缓存行**——x86-64 上一行通常 **64 字节**。访问任一字节，整条 64 字节会被加载到 L1 cache。

```
内存地址：  0   8   16  24  32  40  48  56  64  72 ...
            |←─────── 缓存行 0 ───────→|←──── 缓存行 1 ──...
```

**缓存一致性协议（MESI）**

多核 CPU 每个核都有自己的 L1/L2 cache。如果两个核都缓存了同一行：

- **核 A 写这一行** → 广播 invalidate 给所有缓存了这一行的核
- **核 B 收到后** → 自己 cache 里的这一行被标记失效
- **核 B 下次读这一行** → 缓存未命中，要重新从核 A 的 cache（或内存）拉一份

**关键点：invalidate 是按整条缓存行为单位的，硬件不知道你是不是在改这行里的不同字节**。

**False sharing 是什么**

"伪共享"——两个变量**逻辑上完全独立**，但**物理上落在同一缓存行**，导致两个核互相 invalidate 对方的缓存：

```cpp
struct BadCounter {
    std::atomic<long> a;   // offset 0
    std::atomic<long> b;   // offset 8
};
BadCounter c;

// 线程 1 在核 0：  for (...) c.a.fetch_add(1);
// 线程 2 在核 1：  for (...) c.b.fetch_add(1);
```

两个 atomic 各 8 字节，连在一起 16 字节，**100% 在同一条 64 字节缓存行里**。发生的事：

1. 线程 1 改 `a` → 核 0 把缓存行变 Modified，发 invalidate 给核 1
2. 线程 2 改 `b` → 核 1 缓存行已失效，要先从核 0 拉数据（走 L3 或内存，几十~上百 cycle），改完再 invalidate 核 0
3. 线程 1 改 `a` → 又要把缓存行从核 1 拉回来
4. ...循环往复

**虽然 a 和 b 互不相关，但因为共享同一缓存行，两个线程实际上在串行执行**。实测一般慢 5-10 倍，cache-misses 暴增。

**解决：让两个变量分开住不同缓存行**

```cpp
// 方式 1：手动 padding
struct GoodCounter {
    std::atomic<long> a;
    char pad[64 - sizeof(std::atomic<long>)];
    std::atomic<long> b;
};

// 方式 2：alignas（推荐）
struct alignas(64) PaddedAtomic { std::atomic<long> v; };
PaddedAtomic a, b;               // a 和 b 各占一条独立缓存行

// 方式 3：C++17 标准常量
struct alignas(std::hardware_destructive_interference_size) PaddedAtomic2 {
    std::atomic<long> v;
};
```

**🔧 数组 / vector 的 false sharing（最常见形式）**

比显式 atomic 字段更贴近真实代码的场景：**多个线程各自更新数组的不同下标**。

```cpp
constexpr int N = 8;
long counters[N];                       // 8 个 long = 64 字节 = 一条缓存行

void worker(int tid) {
    for (int i = 0; i < 100'000'000; ++i) {
        counters[tid]++;                // 看似互不干扰
    }
}
```

看起来"每个线程只改自己的下标"——但 `counters[0..7]` 全挤在同一条缓存行里。8 个核在同一条缓存行上 ping-pong，实测**比单线程还慢**。

⚠️ **普通 `int` 也一样会 false sharing，不需要 atomic**——MESI 是硬件级协议，对任何写操作都生效。atomic 只是加了内存序约束，缓存行竞争与 atomic 无关。

**`std::vector` 同理**：底层就是连续数组，前 8 个 `long` 元素共用一条缓存行。更隐蔽的是 **vector of struct**：

```cpp
struct Stat { long count; double avg; };   // 16 字节
std::vector<Stat> stats(num_threads);
// 前 4 个线程的 stats 共用一条缓存行
```

**三种解决方案**：

```cpp
// 方案 1：每个元素独占一条缓存行（内存膨胀 8 倍）
struct alignas(64) PaddedLong { long value; };
std::vector<PaddedLong> counters(num_threads);

// 方案 2（推荐）：线程局部累加 + 最后聚合
void worker(int tid, long& result) {
    long local = 0;                     // 栈上局部，每个线程独立
    for (int i = 0; i < N; ++i) local++;
    result = local;                     // 只在结束写一次
}

// 方案 3：thread_local
thread_local long local_counter = 0;
```

**方案 2 是最通用的思路**——把"高频写共享数据"变成"高频写本地数据 + 一次性聚合"。OpenMP 的 `reduction`、并行 framework 的 thread-local accumulator 本质都是这个套路。

**其他常见隐藏坑**：

- **直方图统计**：`int bins[256]`，多个线程累加不同 bin——bins[0..15] 共用一条缓存行
- **shard 计数器**：`Counter shards[NUM_CORES]` 本意是分片避免争抢，但数组连续分配反而触发 false sharing
- **MPMC 队列的 head/tail 指针**：producer 改 tail、consumer 改 head，两个指针在同一缓存行就 ping-pong（无锁队列设计的经典坑）
- **链表节点紧贴分配**：每个线程操作自己的节点，但 malloc 可能把它们分到相邻地址
- **per-CPU 数据结构**：Linux 内核用 `____cacheline_aligned` 宏强制每个 CPU 的数据独占缓存行

**直觉判断准则**：遇到 `T arr[N]` 或 `vector<T>`，问自己：

1. 多个线程是否会**同时写**不同元素？
2. `sizeof(T) × 同时活跃的线程数` 是否小于 64 字节？

两个都"是"，就有 false sharing 风险。

**检测工具**：

```bash
perf c2c record ./your_program           # 专门检测 cache-line 竞争
perf c2c report                          # 报告哪条缓存行在哪些函数间反复 invalidate
perf stat -e cache-misses,cycles ./prog  # 简单看 cache-miss 数量
```

**为什么叫"伪"共享**：真共享是两个线程改同一变量，竞争真实存在；伪共享是改不同变量，但硬件按缓存行追踪一致性，"假装"它们在共享——竞争是硬件造成的假象。

**🔧 SIMD 对齐**

`__m128`/`__m256`/`__m512` 需要 16/32/64 字节对齐才能用 `movaps` 等对齐 load 指令（不对齐就只能用 `movups`，速度略慢）。

```cpp
alignas(32) float buf[8];        // 用于 AVX 256-bit 操作
__m256 v = _mm256_load_ps(buf);  // 要求 32 字节对齐
```

**🔧 字段重排序优化 sizeof（C++ 标准不允许编译器自动做）**

```cpp
// 12 字节
struct Bad  { char a; int b; char c; };
// 8 字节
struct Good { int b; char a; char c; };
```

对于热点结构体（如百万级实例的对象），手动重排字段（大对齐字段在前）能显著降低内存占用，提高 cache 利用率。

**🔧 `std::aligned_storage`（C++11，C++23 弃用）和 `std::align`**

```cpp
// 在 buffer 中找一段满足对齐的区域
char buffer[1024];
void* p = buffer;
std::size_t space = sizeof(buffer);
if (std::align(16, sizeof(double), p, space)) {
    auto* d = new (p) double(3.14);  // placement new
}
```

主要用于实现内存池、small-object optimization、对象存储等底层设施。C++17 之后更推荐用 `alignas` + 数组直接表达。

---

## 易错点

- 字段顺序直接决定 sizeof——编译器不会帮你重排，先大后小通常最紧凑
- `sizeof(struct)` 不等于字段大小之和，要算上 padding（中间 padding + 末尾 padding）
- 末尾 padding 不是为了"好看"，是为了让该结构体的数组中每个元素都保持对齐
- `alignas(N)` 只能加严不能放松，要放松必须 `packed`
- 取 packed 结构未对齐字段的指针是 UB（在 ARM 上直接段错误）
- union 在 C++ 里做类型双关是 UB，要用 `std::bit_cast` 或 `memcpy`
- false sharing 是"看起来无关的变量互相拖累"——两个独立的 `atomic<long>` 也可能因为落在同一缓存行而相互失效

---

## 工程关联

- `pahole` 工具（来自 dwarves）能直接打印结构体的字段布局和 padding 大小，是优化 sizeof 的利器
- Linux 内核大量使用 `____cacheline_aligned` 宏避免 false sharing（典型例子：per-CPU 变量）
- 网络协议头（如 IP/TCP 头）的 C 表达通常配合 `__attribute__((packed))`，因为线上字节是紧贴的
- 自定义内存分配器（如 jemalloc、tcmalloc）按对齐边界组织 size class，分配请求会向上取整到最近的对齐
- C++ 标准库 `std::vector<T>` 的元素天然按 `alignof(T)` 对齐，因为底层 `operator new` 至少满足 `__STDCPP_DEFAULT_NEW_ALIGNMENT__`（一般 16）
- `perf c2c` 工具专门用来检测 false sharing 引发的缓存行竞争

---

## 实验题

**🧪 题 1：字段顺序影响 sizeof**

```cpp
struct A { char a; int b; char c; short d; };
struct B { int b; short d; char a; char c; };
struct C { char a; char c; short d; int b; };
```

要求：

- 用 `sizeof` 打印三者大小，预测后再验证
- 用 `offsetof(struct A, b)` 之类打印每个字段的偏移
- 安装 `pahole`，对编译后的 `.o` 文件运行 `pahole a.o`，对照其输出
- 给出排列规则：为什么 C 最紧凑

**🧪 题 2：packed 结构体的代价**

```cpp
struct Normal { char a; int b; };
struct __attribute__((packed)) Packed { char a; int b; };

int read_normal(Normal* s) { return s->b; }
int read_packed(Packed* s) { return s->b; }
```

要求：

- `gcc -O2 -S` 看两个函数的汇编
- 对比 `read_normal` 的单条 `movl 4(%rdi), %eax` 和 `read_packed` 拼装字节的过程
- 在 ARM 上（或用 `-fsanitize=alignment`）运行取 `&packed->b` 的指针解引用，观察 UB 触发

**🧪 题 3：false sharing 实测**

```cpp
struct NoPadding   { std::atomic<long> a; std::atomic<long> b; };
struct WithPadding { std::atomic<long> a; char pad[64]; std::atomic<long> b; };
```

要求：

- 起两个线程，分别在循环里递增 `a` 和 `b` 各 1 亿次
- 用 `perf stat -e cache-misses,cycles` 测两种结构的性能
- 验证 WithPadding 版本快 5-10 倍
- 用 `perf c2c record` 抓 false sharing 事件

**🧪 题 4：union 内存复用观察**

```cpp
union U { int i; double d; char buf[16]; };

int main() {
    U u;
    u.i = 0x12345678;
    printf("addr i=%p d=%p buf=%p\n", &u.i, &u.d, u.buf);
    printf("sizeof=%zu alignof=%zu\n", sizeof(U), alignof(U));
}
```

要求：

- 确认三个字段地址相同
- 确认 sizeof 是 16（最大字段）、alignof 是 8（最严字段 double）
- 把 `buf[16]` 改成 `buf[3]`，观察 sizeof 是否变成 8

**🧪 题 5：variant 内存布局观察**

```cpp
#include <variant>
#include <string>

using V1 = std::variant<int, double>;
using V2 = std::variant<int, double, std::string>;
using V3 = std::variant<char, char, char>;   // 重复类型也合法
```

要求：

- 打印 `sizeof` 和 `alignof`，预测后再验证
- 用 `std::visit` + `if constexpr` 实现一个 `print(v)` 函数处理所有类型
- 故意删掉某个类型的分支，确认编译器报错（证明穷尽性检查）
- 用 GDB 查看 variant 实例的二进制布局：找到那个隐藏的 `index` 字段在哪里

**🧪 题 6：cache line 对齐写法对比**

```cpp
// 方式 A：手动 padding
struct CounterA { std::atomic<long> v; char pad[64 - sizeof(std::atomic<long>)]; };

// 方式 B：alignas
struct alignas(64) CounterB { std::atomic<long> v; };

// 方式 C：C++17 标准常量
struct alignas(std::hardware_destructive_interference_size) CounterC {
    std::atomic<long> v;
};
```

要求：

- 打印三者 sizeof 和 alignof
- 用 `CounterB arr[4]` 验证数组里每个元素的地址都是 64 的倍数
- 思考：方式 A 和方式 B 在结构体单独使用 vs 作为数组元素时有什么差异（提示：A 显式占满 64 字节，B 让编译器决定末尾 padding）
