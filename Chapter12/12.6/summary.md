# §12.6 利用线程提高并行性

这一节的主线是：**并发只说明多个逻辑流都在推进，并行才意味着多个执行流同时占用不同硬件执行资源；把程序改成多线程并不会自动变快，只有可并行工作足够多、划分足够均衡，而且同步与内存系统开销没有吞掉收益时，墙钟时间才会下降。** 因此本节不只讨论“怎样启动线程”，还要用加速比、并行效率、强扩展、弱扩展和 Amdahl 定律回答两个工程问题：加了多少速，以及为什么曲线最终会压平。

> 本节与前后内容的分工：[§12.4](../12.4/summary.md) 解释共享变量、happens-before 和 false sharing，[§12.5](../12.5/summary.md) 解释同步协议与并发队列；本节假定程序已经正确，重点分析怎样划分计算、测量多核加速并定位扩展性瓶颈。

---

## 并发、并行与硬件执行资源

**🎯 并发不要求同一时刻真正同时执行**

并发（concurrency）描述多个逻辑控制流在一段时间内都取得进展；并行（parallelism）描述多个控制流在同一时刻由不同硬件执行资源运行。单核通过时间片轮转就能并发，却不能同时执行两个 CPU 密集型线程：

```text
单核并发：
CPU 0:  A1 A2 | B1 B2 | A3 A4 | B3 B4
时间  :  -------------------------------->

双核并行：
CPU 0:  A1 A2 A3 A4
CPU 1:  B1 B2 B3 B4
时间  :  -------------------------------->
```

这一区别会直接改变优化目标：

- I/O 并发主要用一个任务的等待时间推进其他任务，单核也能提高吞吐；
- 计算并行要让独立计算同时落到多个核上，目标是缩短 wall-clock time；
- 单线程 event loop 或单承载线程上的协程可以高效并发，但不会凭空获得多核计算加速。

**🎯 逻辑 CPU 是调度单位，不等于独享的物理核心**

Linux 调度器把可运行线程调度到逻辑 CPU（logical CPU）上。启用 SMT/超线程后，同一物理核心可能暴露多个逻辑 CPU，它们共享部分前端、执行端口和 cache；所以 16 个逻辑 CPU 不等于 16 份完全独立的计算能力。

```bash
lscpu -e=CPU,CORE,SOCKET,NODE,ONLINE
nproc
nproc --all
```

假设输出中 CPU 0 和 CPU 8 的 `CORE` 都是 0，它们就是同一物理核心上的两个硬件线程。对执行端口或内存带宽已经饱和的程序，从 8 个物理核增加到 16 个 SMT 线程，收益往往远小于 2 倍。

C++ 提供的查询也只是一条提示：

```cpp
#include <thread>

unsigned workers_hint() {
    const unsigned n = std::thread::hardware_concurrency();
    return n == 0 ? 1 : n;
}
```

`hardware_concurrency()` 允许返回 0，而且它不保证反映容器 CPU quota、cpuset 或当前进程 affinity。Linux 上判断进程当前允许在哪些 CPU 上运行，可以观察：

```bash
taskset -pc $$
grep Cpus_allowed_list /proc/self/status
```

**⚠️ worker 数量是需要测量的参数**

逻辑 CPU 数适合作为 CPU 密集任务的起始测试点，不是永远正确的答案。最佳 worker 数还取决于：

- 工作是否真能独立执行；
- 是否受计算单元、cache 容量或内存带宽限制；
- 进程是否和其他服务争用 CPU；
- 是否存在锁、原子变量、队列或串行阶段；
- 线程是否经常阻塞 I/O。

因此实际 benchmark 应至少扫描 `1, 2, 4, ...` 个 worker，而不是把 `hardware_concurrency()` 直接写死成配置。

---

## fork-join、数据划分与并行归约

**🎯 最基本的计算并行模式是“拆分—局部计算—汇合”**

数组求和可以分成互不重叠的连续区间，每个 worker 计算自己的局部和，主线程等待所有 worker 完成后再合并结果：

```text
输入 [0, n)
  ├─ worker 0: [0, b1)   -> partial[0]
  ├─ worker 1: [b1, b2)  -> partial[1]
  ├─ worker 2: [b2, b3)  -> partial[2]
  └─ worker 3: [b3, n)   -> partial[3]
                  join
                    ↓
      partial[0] + ... + partial[3]
```

下面的 C++20 骨架处理了尾部余数、空输入和 worker 数超过元素数的边界：

```cpp
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <thread>
#include <vector>

std::uint64_t parallel_sum(std::span<const std::uint32_t> values,
                           std::size_t requested_workers) {
    if (values.empty()) {
        return 0;
    }

    const std::size_t workers =
        std::clamp<std::size_t>(requested_workers, 1, values.size());
    const std::size_t chunk = (values.size() + workers - 1) / workers;

    std::vector<std::uint64_t> partial(workers, 0);
    std::vector<std::thread> threads;
    threads.reserve(workers);

    for (std::size_t tid = 0; tid < workers; ++tid) {
        const std::size_t begin = tid * chunk;
        const std::size_t end = std::min(begin + chunk, values.size());

        threads.emplace_back([&, tid, begin, end] {
            std::uint64_t local = 0;
            for (std::size_t i = begin; i < end; ++i) {
                local += values[i];
            }
            partial[tid] = local;
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    std::uint64_t total = 0;
    for (const auto value : partial) {
        total += value;
    }
    return total;
}
```

每个线程只读自己的输入区间，并只写一个独占的 `partial[tid]`。`join()` 不只是等待生命周期结束，还让 worker 在线程结束前的写 happens-before `join()` 返回后的总归约，所以主线程读取 `partial` 不需要额外 mutex。

**🎯 归约应优先累积线程局部结果**

下面的写法虽然可以通过 atomic 做到 data-race-free，却让每个元素都争抢同一个 cache line：

```cpp
#include <atomic>
#include <cstdint>

std::atomic<std::uint64_t> total{0};

for (const auto value : my_range) {
    total.fetch_add(value, std::memory_order_relaxed);
}
```

`memory_order_relaxed` 省去了跨对象排序，却没有省掉原子 read-modify-write 和 cache coherence 所需的独占所有权转移。多个核反复写同一个 `total` 时，这条 cache line 会在核之间 bouncing，程序可能比串行循环还慢。

更好的模式是：

```cpp
std::uint64_t local = 0;
for (const auto value : my_range) {
    local += value;
}
partial[tid] = local;  // 每个 worker 最后只发布一次
```

**⚠️ 整数归约和浮点归约的正确性边界不同**

在不溢出的前提下，整数加法可按不同分组顺序得到同一结果；浮点加法不满足结合律：

```text
(a + b) + c 可能不等于 a + (b + c)
```

因此并行浮点归约即使没有 data race，也可能因分块数和合并顺序不同得到不同末位。工程上应明确允许的误差，必要时使用固定归约树、补偿求和或更高精度累加，而不是把 bitwise identical 当成默认保证。

**🎯 静态划分与动态划分解决不同负载**

如果每个元素成本接近，连续静态分块的调度开销最低；如果每个任务耗时差异很大，静态等量分块可能出现“一个慢 worker 拖住所有人”的尾部：

```text
worker 0: ████████████████████  20 ms
worker 1: ███                    3 ms
worker 2: ████                   4 ms
worker 3: ██                     2 ms
join 等待:                  ^ 总时间仍约 20 ms
```

此时可以让 worker 从共享任务队列动态取工作，或把任务切成更多适中大小的 chunk。代价是更多队列访问、同步和调度：粒度太粗会负载不均，粒度太细会让管理开销超过计算收益。

---

## 加速比与并行效率

**🎯 加速比回答“快了多少”**

对同一算法和同一输入规模，记：

- `T_1`：使用一个处理器执行的 wall-clock time；
- `T_p`：使用 `p` 个处理器执行的 wall-clock time；
- `S_p = T_1 / T_p`：加速比（speedup）；
- `E_p = S_p / p = T_1 / (pT_p)`：并行效率（parallel efficiency）。

例如：

```text
T_1 = 10.0 s
T_4 =  3.0 s

S_4 = 10.0 / 3.0 ≈ 3.33
E_4 = 3.33 / 4 ≈ 0.833 = 83.3%
```

理想线性加速是 `S_p = p`、`E_p = 1`。现实中线程管理、串行阶段、同步和资源争用通常使 `S_p < p`，处理器越多时 `E_p` 越容易下降。

**⚠️ `T_1` 必须是公平基线**

下面的比较没有意义：

```text
串行版：朴素 O(n²) 算法，包含文件读取
并行版：优化后 O(n) 算法，数据已在内存
```

它同时改变了算法、输入状态和线程数，无法把差异归因于并行。公平基线应满足：

- 相同输入和工作量；
- 相同核心算法与结果校验；
- 相同编译选项；
- 同样决定是否计入数据准备、线程创建和结果合并；
- `T_p` 测的是整个并行区间的墙钟时间，而不是某一条 worker 的 CPU 时间。

**🎯 `task-clock` 与 elapsed time 回答不同问题**

多线程程序运行 1 秒，若四个核都持续工作，聚合 `task-clock` 可能接近 4000 ms，而 elapsed time 仍约 1 秒：

```text
wall-clock elapsed: 1.00 s
aggregate task-clock: 3990 ms
CPUs utilized: about 3.99
```

所以 `T_p` 应取 elapsed wall-clock；`task-clock / elapsed` 则可以估算平均实际占用了多少个 CPU。若启动 8 个 worker 却只得到约 1.2 CPUs utilized，应继续检查锁等待、I/O、CPU quota、affinity 或任务不足。

**⚠️ 超线性加速不一定是计算能力凭空增加**

偶尔会测到 `S_p > p`。常见原因不是违反物理规律，而是每个 worker 的局部工作集变小后进入更高层 cache，或单线程基线受分页、频率、NUMA placement 等额外影响。遇到超线性结果应先复现实验并检查工作集与测量条件，不能直接宣称并行效率超过 100% 是算法常态。

---

## 强扩展与弱扩展

**🎯 强扩展固定总问题规模**

强扩展（strong scaling）问：**同一个固定任务，增加处理器后能缩短多少时间？**

```text
总元素数始终 N = 100,000,000

p=1: 每个 worker 处理 100,000,000
p=2: 每个 worker 约处理  50,000,000
p=4: 每个 worker 约处理  25,000,000
p=8: 每个 worker 约处理  12,500,000
```

随着 `p` 增大，每个 worker 的有效计算越来越少，但线程创建、最终归约等固定成本不会按比例缩小。因此强扩展曲线通常先接近线性，随后逐渐压平，甚至因开销占主导而反向变慢。

**🎯 弱扩展固定每个处理器的工作量**

弱扩展（weak scaling）问：**系统规模和处理器数一起增长时，能否用近似不变的时间处理更大的总工作量？**

```text
每个 worker 固定处理 100,000,000 个元素

p=1: 总元素数   100,000,000
p=2: 总元素数   200,000,000
p=4: 总元素数   400,000,000
p=8: 总元素数   800,000,000
```

理想弱扩展中，完成时间近似不变，总吞吐随 `p` 线性增长。但全局归约、共享队列、内存容量、内存带宽和 NUMA 远程访问仍会使时间上升。

**⚠️ 两类实验的 speedup 不能混算**

强扩展的 `T_1/T_p` 比较的是相同总工作量；弱扩展每次总工作量不同，通常报告 scaled speedup、吞吐或相对效率，并明确问题规模随 `p` 增长。不能拿“1 个 worker 处理 1 亿元素”的时间除以“8 个 worker 处理 8 亿元素”的时间，再把结果当成固定问题的 `S_8`。

**🔧 服务端容量规划更接近弱扩展问题**

例如分片存储系统增加 4 倍节点，同时让总数据量和请求量增加 4 倍，关心的是每个节点负载是否近似不变；而把固定离线任务从 40 分钟缩短到 10 分钟，则是典型强扩展目标。先明确问题类型，才能选择正确的横轴、指标和结论。

---

## Amdahl 上限与现实并行开销

**🎯 串行部分决定强扩展天花板**

设程序中可并行部分占原执行时间比例 `α`，其余 `1-α` 必须串行；若可并行部分理想地由 `p` 个处理器均分，则：

```text
T_p / T_1 >= (1 - α) + α / p

S_p <= 1 / ((1 - α) + α / p)
```

若 `α = 0.95`、`p = 8`：

```text
S_8 <= 1 / (0.05 + 0.95 / 8)
    = 1 / 0.16875
    ≈ 5.93
```

即使 95% 的工作可并行，8 个处理器也不可能仅靠理想分摊达到 8 倍。若让 `p -> ∞`：

```text
S_∞ <= 1 / (1 - 0.95) = 20
```

剩余 5% 串行部分把无限处理器的理论加速封顶在 20 倍。这与 [§5.12–§5.14](../../Chapter5/5.12-5.14/summary.md) 的优化结论完全一致：**未被加速的部分最终成为新瓶颈。**

**🎯 程序结构中的“串行部分”不只是一段串行循环**

一次并行任务可能包含：

```text
读取/解析输入（串行）
        ↓
创建或唤醒 worker
        ↓
并行计算
        ↓
barrier / join
        ↓
归约与输出（串行）
```

输入解析、任务切分、最后一个慢任务、结果合并和输出都可能进入不可并行时间。若用短任务反复创建线程，线程创建本身也会扩大串行或管理开销；工程中通常通过长期存活的有界线程池摊销这部分成本。

**⚠️ Amdahl 公式是理想上界，不包含所有现实损失**

公式里的 `α/p` 假定可并行部分能被完美均分且没有新增开销，真实程序还会受到：

| 瓶颈 | 典型现象 | 常见观测 |
|---|---|---|
| 负载不均 | 多数线程已结束，少数尾任务仍运行 | 各 worker 耗时差距大 |
| 锁/atomic 竞争 | CPU 利用率上不去或大量自旋 | futex、CAS 重试、热点 cache line |
| false sharing | 逻辑独立的计数器仍互相拖慢 | cache-to-cache transfer 增多 |
| 内存带宽饱和 | worker 增加后 instructions 增长，吞吐不再增长 | LLC miss、内存带宽到顶 |
| NUMA 远程访问 | 跨 socket 扩展突然变差 | remote memory access、错误 first touch |
| 调度与切换 | runnable 线程远多于 CPU | context-switches、migrations 增加 |
| 任务粒度过小 | 管理任务比执行任务更贵 | 短任务吞吐随线程数下降 |

[§12.4 的 false sharing](../12.4/summary.md#伪共享变量逻辑独立cache-line-仍然共享)就是典型例子：程序不存在 data race，业务变量也彼此独立，但 cache coherence 仍会让跨核写入串行化。

**🔧 内存受限程序不会按核心数线性加速**

数组求和每个元素只做一次简单加法，却必须从内存读取数据。当少量核心已经打满内存通道后，增加 worker 只会让更多核心等待数据。此时优化方向不是继续加线程，而是减少内存流量、改善数据布局和局部性，或把计算与数据放到合适 NUMA node。

---

## 可复现的 Linux 性能测量

**🎯 先固定实验协议，再看曲线**

一个最低限度的强扩展测量流程是：

```bash
  # 记录平台与可用 CPU
lscpu
cat /proc/self/status | grep Cpus_allowed_list

  # 统一 release 编译选项
g++ -std=c++20 -O3 -march=native -pthread benchmark.cpp -o benchmark

  # 每个 worker 数重复测量
for p in 1 2 4 8; do
    perf stat -r 7 \
      -e task-clock,cycles,instructions,context-switches,cpu-migrations,cache-misses \
      ./benchmark --workers "$p" --elements 100000000
done
```

实验至少应记录：CPU 型号与拓扑、进程允许的 CPU、编译器和选项、输入规模、worker 数、是否复用线程池、是否绑核、重复次数及汇总方法。首次运行可能包含缺页、动态链接和冷 cache 影响，可以先预热，再报告多轮中位数或 `perf stat -r` 的均值与波动。

**🎯 指标必须和瓶颈假设对应**

- `elapsed time`：计算 `T_p` 和强扩展 speedup；
- `task-clock`：所有线程消耗的 CPU 时间，可结合 elapsed 估算平均 CPU 利用数；
- `cycles`、`instructions`：观察总工作量、额外同步/调度代码及 IPC；
- `context-switches`、`cpu-migrations`：线程过多、阻塞或未固定调度位置的线索；
- `cache-misses`：cache/内存压力线索，但不能单凭一个总数证明具体根因；
- `perf c2c`：在权限与平台支持时定位跨核 cache line 争用和 false sharing。

例如 worker 从 4 增至 8 后 elapsed 几乎不变，而 `task-clock` 和 cycles 接近翻倍，说明程序消耗了更多 CPU 却没有提高完成速度；应优先检查内存带宽、同步热点和负载均衡，而不是继续增加线程。

**⚠️ 绑核能减少噪声，却也可能改变问题**

```bash
taskset -c 0-3 ./benchmark --workers 4
```

CPU affinity 可以减少迁移并让实验更容易复现，但必须先看拓扑：`0-3` 可能是四个物理核，也可能包含同一核心的 SMT siblings。生产环境通常还有共享机器负载、容器 quota、睿频和温控影响；benchmark 应报告这些条件，而不是隐藏不利数据。

**⚠️ 正确性校验是性能实验的一部分**

所有 worker 数都必须得到和基准相同的结果。一个因漏算元素、整数溢出或提前返回而“更快”的程序没有性能意义。建议在计时区外计算可靠参考值，并在每轮结束后比较：

```cpp
if (parallel_result != reference_result) {
    std::abort();
}
```

---

## 易错点

- 把并发等同于并行是错的，单核时间片交错也能并发，计算并行必须同时占用多个硬件执行资源。
- 认为线程数越多程序越快是错的，超过有效并行度后，调度、同步、cache coherence 和带宽竞争可能让程序更慢。
- 把逻辑 CPU 数当成独立物理核数是错的，SMT siblings 会共享核心资源，容器 quota 和 affinity 还可能进一步限制可用 CPU。
- 只比较某一条 worker 的耗时是错的，加速比的 `T_p` 应使用相同工作量的端到端 wall-clock time。
- 把不同算法、不同输入或不同数据准备条件拿来算 speedup 是错的，并行基线必须保持除处理器数之外的关键条件一致。
- 把每次 atomic 操作设为 relaxed 就认为没有竞争成本是错的，relaxed 不建立额外顺序，但原子 RMW 仍会争抢 cache line 所有权。
- 把浮点并行归约末位不同直接判为 data race 是错的，分组顺序变化本身就会因浮点不满足结合律而改变舍入结果。
- 用弱扩展的不同总工作量直接计算强扩展 `T_1/T_p` 是错的，两种实验回答的问题和报告指标不同。
- 认为 Amdahl 公式能准确预测实测时间是错的，它给出理想分摊上界，未自动计入线程管理、负载不均、锁和内存系统开销。
- 只运行一次 benchmark 就下结论是错的，缺页、频率、迁移、系统噪声和温控都可能影响单次结果。

---

## 工程关联

- 线程池让 worker 长期存活，以任务队列提交工作，能摊销短任务反复创建和回收线程的成本；线程数与队列容量都必须有上界。
- Reactor + worker pool 常把网络 I/O 留在所属 event loop，把可独立的 CPU/阻塞业务交给 worker，再通过 completion queue 投回原 loop；这同时利用 I/O 并发和多核并行。
- 图像处理、压缩、日志批处理、数据库扫描常采用连续分块加线程局部归约，既减少共享写，又保留良好的空间局部性。
- 内存数据库、向量扫描和数组归约常先撞内存带宽而不是核心数上限；`perf` 指标要结合 Memory Mountain 和硬件带宽测量解释。
- NUMA 机器上“谁 first touch 页面”会影响物理页归属；worker 跨 node 访问远程内存时，加线程可能增加延迟而不是提升吞吐。
- 服务端扩展性不能只看平均吞吐，还要观察 p95/p99 延迟、排队长度和背压；高吞吐但尾请求长期等待并不代表容量设计健康。
- 容器中的 `nproc --all` 或 `/proc/cpuinfo` 可能显示宿主机拓扑，而 cgroup quota 才是实际预算；线程池默认值应允许部署层覆盖。
- 生产优化应先 profile：若锁等待或内存带宽已经占主导，继续调大 worker 数只会增加 CPU 成本和尾延迟。

---

## 实验题

**🧪 题 1：测量并行归约的强扩展曲线**

源码骨架：

```cpp
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <numeric>
#include <span>
#include <thread>
#include <vector>

std::uint64_t parallel_sum(std::span<const std::uint32_t> values,
                           std::size_t workers);

int main(int argc, char** argv) {
    const std::size_t workers =
        argc == 2 ? std::stoull(argv[1]) : 1;
    std::vector<std::uint32_t> values(100'000'000, 1);
    const std::uint64_t expected = values.size();

    const auto begin = std::chrono::steady_clock::now();
    const auto actual = parallel_sum(values, workers);
    const auto end = std::chrono::steady_clock::now();

    if (actual != expected) {
        return 1;
    }
    std::cout << std::chrono::duration<double>(end - begin).count()
              << '\n';
}
```

要求：

- 补全本节的 `parallel_sum`，用 `g++ -std=c++20 -O3 -march=native -Wall -Wextra -Wpedantic -pthread` 编译；
- 固定总输入为一亿个元素，分别用 1、2、4、8 个 worker，每种配置预热一次并至少测量 7 次；
- 以中位数计算 `T_p`、`S_p` 和 `E_p`，画出 worker 数—speedup 表；
- 每轮校验总和，确认分块无遗漏、无重叠且没有整数溢出；
- 说明曲线从哪个 worker 数开始偏离线性，并结合 CPU 拓扑、固定开销或内存带宽提出解释。

**🧪 题 2：区分强扩展与弱扩展**

源码片段：

```cpp
constexpr std::size_t elements_per_worker = 25'000'000;

const bool weak_scaling = /* 从命令行读取 */;
const std::size_t elements = weak_scaling
    ? elements_per_worker * workers
    : 200'000'000;

std::vector<std::uint32_t> values(elements, 1);
```

要求：

- 强扩展组固定 `elements == 200'000'000`，弱扩展组固定每 worker 处理 `25'000'000` 个元素；
- 两组都测试 1、2、4、8 个 worker，并记录 elapsed time、总吞吐和每 worker 吞吐；
- 强扩展报告 `S_p`、`E_p`，弱扩展报告时间相对 `T_1` 的变化和总吞吐增长；
- 解释为什么弱扩展中总输入不同，不能直接把 `T_1/T_p` 写成固定问题 speedup；
- 若弱扩展在跨 NUMA node 后明显退化，检查 first touch、affinity 与远程内存访问假设。

**🧪 题 3：对比共享 atomic 热点与局部归约**

实现两个 worker 核心：

```cpp
// 版本 A：每个元素都争抢同一个 cache line
for (const auto value : my_range) {
    shared_total.fetch_add(value, std::memory_order_relaxed);
}

// 版本 B：在线程内累积，最后只写一次独占结果
std::uint64_t local = 0;
for (const auto value : my_range) {
    local += value;
}
partial[tid] = local;
```

要求：

- 两个版本使用相同输入、相同分块和相同 worker 数，并验证最终总和一致；
- 分别测试 1、2、4、8 个 worker，使用 `perf stat -r 7 -e task-clock,cycles,instructions,context-switches,cache-misses`；
- 记录 elapsed time 和 CPUs utilized，观察版本 A 是否消耗更多 cycles 却没有获得同比吞吐；
- 在平台支持且有权限时使用 `perf c2c` 定位 `shared_total` 所在 cache line；
- 解释 `memory_order_relaxed` 为什么能满足纯计数的顺序需求，却不能消除原子 RMW 的 coherence 成本。
