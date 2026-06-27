# 番外篇：`std::hardware_destructive_interference_size`

§6.4 讲 false sharing 时，给出的「规范」解法是一句注释：

```cpp
// C++17 更规范：alignas(std::hardware_destructive_interference_size)
```

这一篇把这句注释拆开。它表面只是「把魔法数字 64 换成一个标准名字」，但顺着这个名字往下挖，会牵出三件事：C++17 其实加的是**一对方向相反**的常量；这个常量在 GCC 里默认会触发一条警告**劝你别用**；以及 cache line 大小这个看似确定的数，为什么在实践中常被写成 128 而不是 64。核心矛盾只有一句话——**这是个编译期常量，而 cache line 大小本质是个运行期/微架构属性，把后者烤进 ABI 必然出事**。

---

## 从硬编码 64 说起：它想解决什么

§6.4 里 false sharing 的两种手写解法，本质都是「往结构体里塞 padding，把热点变量顶到下一条 line」：

```c
// 解法 A：显式 padding 数组
struct {
    long a;
    char pad[64 - sizeof(long)];   // 填满一条 line
    long b;
} s;

// 解法 B：alignas 硬编码
struct {
    alignas(64) long a;
    alignas(64) long b;
} s;
```

`alignas` 是 C++11 引入的对齐说明符，用于指定变量、结构体或类的内存对齐要求（以字节为单位）。

```cpp
alignas(16) int x;        // x 按 16 字节对齐
struct alignas(64) S {    // S 按 64 字节对齐（常用于避免 cache line false sharing）
    int data[16];
};
```

**🎯 64 是个魔法数字（magic number）**

两种写法都把 `64` 焊死在代码里。问题不在「不好看」，在**不可移植**：

- x86-64（主流 Intel/AMD）：cache line = 64 字节
- 多数 ARM64 服务器、**Apple Silicon（M1/M2…）**：cache line = **128 字节**
- 早期 / 嵌入式架构：可能是 32 字节

在 64 字节的机器上 `alignas(64)` 刚好；搬到 cache line 是 128 的机器上，`a` 和 `b` 各自 64 对齐，**仍可能落进同一条 128 字节 line**，false sharing 原封不动地回来了。`64` 这个值，写代码的人是在替目标 CPU 猜。

**🎯 C++17 的诉求：给这个值一个标准名字**

C++17 在 `<new>` 头里加了常量，让你写「一条 cache line 那么大」而不必写出具体数字，由标准库实现替你填对应平台的值：

```cpp
#include <new>
struct alignas(std::hardware_destructive_interference_size) Padded {
    std::atomic<long> counter;
};
```

理想情况下，同一份源码在 x86-64 编译出 64、在 Apple Silicon 编译出 128，false sharing 自动消失。**理想情况下**——下面会看到这个理想有多脆。

---

## 一对常量，方向相反

很多人只记住了 destructive 那个，其实 C++17 一次加了**两个**，名字一长串但语义正好相反：

**🎯 `hardware_destructive_interference_size`——「把它们拆开」**

> 两个对象之间至少隔这么远，就保证**不在**同一条 cache line 上。

destructive interference（破坏性干扰）指的就是 false sharing：两个对象凑在一条 line 上，互相作废对方的缓存副本。这个常量是**避免**它的最小间距，用于 `alignas` / padding，把各线程独写的热点变量隔开。**这就是 §6.4 注释里用的那个。**

```cpp
struct Counters {
    alignas(std::hardware_destructive_interference_size) std::atomic<long> a;  // 线程 0 独写
    alignas(std::hardware_destructive_interference_size) std::atomic<long> b;  // 线程 1 独写
};   // a、b 保证分属不同 line，互不弹乒乓
```

**🎯 `hardware_constructive_interference_size`——「把它们凑一起」**

> 多个对象塞进这么大的范围内，就有望落在**同一条** cache line 上，被一次性加载、一起受益。

constructive interference（建设性干扰）是反过来的需求：一组**总是一起被读**的数据（比如一个只读配置块、一对总是同时访问的字段），希望它们共享一条 line，一次 miss 把整组都拉进来。这其实就是 §6.2~6.3 **空间局部性**的主动利用——只不过这里你是在显式地「打包」。

```cpp
// 这两个字段总是被同一个循环一起读，希望它们同 line
struct HotPair {
    int key;
    int value;
};
static_assert(sizeof(HotPair) <= std::hardware_constructive_interference_size);
```

**⚠️ 两个值不一定相等**

标准把它们定义成**两个独立常量**，正是因为「避免共享的最小间距」和「促成共享的最大范围」**在概念上不是一回事**，允许实现给不同的值。实践里下一节会看到，这个「不一定相等」正是 128 vs 64 争论的根源。

| 常量 | 你的意图 | 对应 §6.4 概念 | 典型用法 |
|------|----------|----------------|----------|
| `hardware_destructive_interference_size` | 拆开，**避免** false sharing | 缓存一致性、line 作废 | `alignas` 隔离热点写变量 |
| `hardware_constructive_interference_size` | 凑拢，**利用**空间局部性 | 块内多字一起加载 | 打包总是同访问的只读数据 |

---

## 它到底等于几：64 还是 128

**🎯 直觉答案 vs 工程答案**

直觉：x86-64 的 cache line 是 64 字节，所以 destructive 就该是 64。GCC/libstdc++ 在 x86-64 上确实给的是 **64**，MSVC 也给 64。

但**很多生产级代码库宁可自己写死 128**。Facebook 的 folly 库里：

```cpp
// folly/lang/Align.h（语义复刻）
constexpr std::size_t hardware_destructive_interference_size = 128;
```

**⚠️ 为什么是 128：Intel 的相邻缓存行预取**

从 Sandy Bridge 起，Intel CPU 有个叫 **adjacent cache line prefetch（相邻行预取，又名 spatial prefetcher）** 的特性：当你访问某条 64 字节 line 时，硬件会**顺手把和它配对的相邻那条 line 也预取进来**，把相邻两条 line 当成一个 128 字节的「对（pair）」来搬。

后果：即便你按 64 字节把 `a`、`b` 分到了**相邻**的两条 line，预取器仍可能把这两条当一对处理，false sharing 的乒乓效应**部分残留**。要彻底斩断，得按 **128** 对齐，让 `a`、`b` 落进不同的「对」里。这就是 folly 选 128 的理由——用一点额外内存，换在 Intel 上确定无残留。

**🎯 所以「正确的值」依赖你优化到多偏执**

- 只想消掉教科书意义上的 false sharing：64 够了
- 想在 Intel 上连预取器造成的残留也清掉、且不在乎多花 64 字节：用 128

标准常量给的是前者；folly 选的是后者。**没有唯一正确答案，取决于目标平台和你愿意为「最后一点残留」付多少内存。**

---

## GCC 的大坑：用它会被警告

这是这一篇最该记住的一点。在 GCC（12 起）下写：

```cpp
struct alignas(std::hardware_destructive_interference_size) S { /* ... */ };
```

编译时会蹦出：

```
warning: use of 'std::hardware_destructive_interference_size' [-Winterference-size]
note: its value can vary between compiler versions or with different '-mtune'
```

**一个标准库提供的常量，标准库的实现者却默认警告你别用它。** 这不是 bug，是有意为之，背后是一个真实的 ABI 陷阱。

**⚠️ 根因：编译期常量被烤进了结构体布局（ABI）**

`hardware_destructive_interference_size` 是 `constexpr`，它的值在**编译时**就定死，并直接决定了结构体的大小和字段偏移——也就是进入了 **ABI**。而它的值**可能随这些因素变化**：

- 不同的 GCC 版本（实现可以调整这个值）
- 不同的 `-mtune` / `-march` / `-mcpu`（针对不同微架构调优）

于是出现经典的 **ODR / ABI 不一致**场景：

```
编译单元 A：-march=skylake   →  size 算成 64  →  struct S 占 64 字节
编译单元 B：-march=znver3    →  size 算成 X   →  struct S 占 X 字节
两个 .o 链接到一起 / 跨 .so 边界传 S → 两边对 S 的布局认知不一致 → UB
```

只要这个结构体会**跨编译单元、跨动态库、被序列化、或持久化到磁盘/共享内存**，两端用不同编译选项编译，布局就对不上，是非常隐蔽的内存损坏。

**🔧 GCC 官方建议的两种安全姿势**

1. **跨 ABI 边界用的结构**：别用这个常量，自己定一个**固定**值，把它焊死，谁也别想偷偷改：

   ```cpp
   // 显式选定，写进注释说明依据，跨 TU 永远一致
   constexpr std::size_t kCacheLine = 64;   // x86-64 line size
   struct alignas(kCacheLine) S { /* ... */ };
   ```

2. **确实想用标准常量、且确认不跨 ABI**：用 `#pragma` 局部关掉警告，表示「我清楚风险，这个类型不出本二进制」：

   ```cpp
   #pragma GCC diagnostic push
   #pragma GCC diagnostic ignored "-Winterference-size"
   struct alignas(std::hardware_destructive_interference_size) LocalOnly { /* ... */ };
   #pragma GCC diagnostic pop
   ```

**⚠️ Clang/libc++ 长期干脆不提供这两个常量**

正因为 ABI 争议，libc++ 很长时间**根本没实现**这俩常量，用了直接编译不过。这进一步说明：它远没有名字看上去那么「标准、可放心用」。**跨平台代码里，自己定常量往往比依赖它更省心。**

---

## 还有一个坑：over-aligned 类型的分配

就算你决定用 `alignas(64)`（无论数字还是常量），还有个容易忽略的问题：**堆分配是否真的尊重了这个对齐**。

**⚠️ C++17 之前 `new` 不保证过对齐**

```cpp
struct alignas(64) Padded { std::atomic<long> x; };
Padded* p = new Padded[100];   // p 真的 64 对齐吗？
```

- **C++14 及之前**：`operator new` 只保证 `alignof(std::max_align_t)`（x86-64 上通常是 **16** 字节）的对齐。对 64 对齐的类型，`new` 返回的指针**不保证** 64 对齐，你的 padding 可能白做。
- **C++17 起**：引入了**对齐版本的 `operator new`**，编译器对 over-aligned 类型会自动调用 `operator new(size, std::align_val_t)`，`new`/`delete` 才正确尊重 `alignas(64)`。

**⚠️ 容器和 `malloc` 同理**

- 裸 `malloc` 只保证 `max_align_t`（16），要过对齐得用 `posix_memalign` / `aligned_alloc`（C11）/ `std::aligned_alloc`。
- `std::vector<Padded>` 在 C++17 起配合默认 `std::allocator` 能正确处理过对齐类型；老标准下需要自定义分配器，否则 `vector` 里每个元素的对齐可能不对。

**🔧 一句话**：`alignas` 只约束**类型布局**，分配器得**配合**才能让运行期地址也对齐——C++17 把这条链补齐了，这也是「C++17 更规范」这句注释里隐含的另一层意思。

---

## 易错点

- `hardware_destructive_interference_size`（拆开避免 false sharing）和 `hardware_constructive_interference_size`（凑拢利用局部性）是**方向相反**的一对，别只记一个，更别混用
- 它是**编译期常量**，而 cache line 大小本质是**运行期/微架构属性**——把它写进跨 ABI 的结构体布局，不同编译选项会算出不同 size，导致隐蔽的 ABI 不一致
- GCC 默认对它发 `-Winterference-size` 警告**不是噪音**，是在提醒上面这个 ABI 风险；跨边界的结构应改用自己焊死的固定常量
- x86-64 上标准常量给 64，但 Intel 的相邻行预取器会把相邻两条 line 当一对，**想连残留都清掉得按 128 对齐**（folly 的选择）——64 和 128 都「对」，看你优化到多偏执
- `alignas(64)` 只管类型布局；**C++17 之前 `new`/`malloc` 不保证过对齐**，padding 可能白做，C++17 的对齐 `operator new` 才补上这一环
- Clang/libc++ 长期不提供这两个常量，跨平台项目里它没有名字看上去那么「随手可用」

---

## 工程关联

- **folly / 其它高性能库的做法**：`folly::hardware_destructive_interference_size` 自定义为 128 并写死，注释直言是为对抗 Intel 相邻行预取——生产代码普遍「不信任」标准常量、宁可显式控制
- **跨进程共享内存 / mmap 持久结构**：结构体布局会落到磁盘或被多进程共享，此时**绝不能**用会随编译选项漂移的常量，必须焊死数字，否则一端写、另一端读会错位
- **`alignas` + 对齐 `new`**：C++17 起 `new over-aligned-type` 自动走 `operator new(size, align_val_t)`，可用 `gdb`/`p &obj` 验证返回地址确实 64/128 对齐（低 6/7 位为 0）
- **验证 line 大小**：`getconf LEVEL1_DCACHE_LINESIZE` 或 `cat /sys/devices/system/cpu/cpu0/cache/index0/coherency_line_size` 读出本机真实 line size，和你 `alignas` 的数字对一对，确认没猜错平台
- **`perf c2c`**：第 12 章会用它做 padding/对齐前后的 false sharing 对比——能直接看到「拆到不同 line 后跨核 HITM（cache-to-cache 命中）事件骤降」

---

## 实验题

**🧪 题 1：看 GCC 的警告和 line size**

```cpp
#include <new>
#include <atomic>
struct alignas(std::hardware_destructive_interference_size) S {
    std::atomic<long> x;
};
int main() { S s; return (int)s.x.load(); }
```

要求：

- 用 `g++ -std=c++17 -Wall a.cpp` 编译，观察是否出现 `-Winterference-size` 警告
- 打印 `std::hardware_destructive_interference_size` 和 `std::hardware_constructive_interference_size` 两个值，看本机是否相等、各是多少
- 和 `getconf LEVEL1_DCACHE_LINESIZE` 的输出对比，确认标准常量和硬件真实 line size 是否一致
- 加 `-march=native` 再编一次，看值有没有变化（体会「随调优参数漂移」）

**🧪 题 2：验证 over-aligned 的 `new` 真的对齐了**

```cpp
#include <new>
#include <cstdio>
struct alignas(64) P { long v; };
int main() {
    for (int i = 0; i < 4; i++) {
        P* p = new P;
        printf("%p  low6=%ld\n", (void*)p, (long)((uintptr_t)p & 63));
        delete p;
    }
}
```

要求：

- 用 `-std=c++17` 编译，确认每次输出 `low6=0`（地址 64 对齐）
- 改用 `-std=c++14` 编译（若编译器仍接受对齐 new 则用 `malloc(sizeof(P))` 代替 `new`），观察 `malloc` 版本是否还能保证 `low6=0`
- 解释：为什么 C++17 的对齐 `operator new` 是 `alignas` padding 真正生效的前提

**🧪 题 3：64 对齐为何在 Intel 上可能仍有残留**

要求：

- 写一个两线程各自疯狂自增的 benchmark：版本 A 两个计数器仅按 **64** 对齐相邻放置，版本 B 按 **128** 对齐
- 在一台 Intel（Sandy Bridge 及以后）机器上用 `perf c2c record` / `perf stat -e ...` 跑两个版本，对比跨核一致性事件和耗时
- 验证：64 版本可能比完全不对齐好、但仍慢于 128 版本——把残留归因到相邻行预取器
- 在 AMD 或关闭了相邻行预取的机器上重跑，看 64 和 128 的差距是否缩小
