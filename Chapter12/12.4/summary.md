# §12.4 多线程程序中的共享变量

这一节的主线是：**变量是否共享，不由“它写在全局区还是栈上”单独决定，而由“同一个变量实例是否可能被多个线程引用”决定；一旦共享可变状态没有明确的所有权、生命周期和 happens-before 关系，就进入同步错误的根源区。** 工程上最有效的办法往往不是给每个字段补锁，而是缩小共享面：让状态固定归属于一个线程，其他线程只通过消息投递请求操作；`one loop per thread` 正是这种思想在 Reactor 网络库中的经典落地。

> 本节与已有专题的分工：[`muduo_one_loop_per_thread.md`](../12.2/muduo_one_loop_per_thread.md) 讲完整网络调用链，本节只从“共享变量与同步边界”的角度解释该模式为什么有效；具体同步原语、同步错误分类、队列和读者—写者模式见 [§12.5](../12.5/summary.md)。

---

## 变量实例与共享判定

**🎯 先区分变量、变量实例和引用**

源代码里的一条变量声明，在运行时不一定只有一个实例：

```cpp
int global_count = 0;              // 整个进程通常只有一个实例
thread_local int local_count = 0;  // 每个线程各有一个实例

void work() {
    int stack_count = 0;           // 每次函数调用各有一个实例
}
```

判断共享性的准确问题不是“它是哪类变量”，而是：

> **某个具体变量实例，是否可能被两个或更多线程解引用访问？**

| 声明形式 | 实例数量 | 默认共享性 |
|---|---:|---|
| 全局变量、命名空间变量、`static` 数据成员 | 进程中通常一个 | 容易共享 |
| 函数内 `static` 变量 | 进程中通常一个 | 容易共享 |
| 堆对象 | 每次分配一个 | 取决于指针是否跨线程传播 |
| 自动局部变量 | 每次调用一个 | 默认由当前线程使用，但地址可逃逸到其他线程 |
| `thread_local` | 每线程一个 | 实例彼此独立，默认不共享 |

**🎯 教材的“变量映射”判断法**

可以把共享判定画成二分图：左边是线程，右边是变量实例；只要一个实例被两条线程边连接，它就是共享变量。

```text
thread A ───────> global_count instance 0 <────── thread B
thread A ───────> local variable instance A
thread B ───────> local variable instance B
```

下面两个线程访问的是同一个 `global_count`，却各自拥有一份 `local_count`：

```cpp
#include <cstdio>
#include <thread>

int global_count = 0;

void worker() {
    int local_count = 0;
    ++global_count;
    ++local_count;
    std::printf("global=%p local=%p\n",
                static_cast<void*>(&global_count),
                static_cast<void*>(&local_count));
}

int main() {
    std::thread a(worker);
    std::thread b(worker);
    a.join();
    b.join();
}
```

两个线程打印出的 `&global_count` 相同、`&local_count` 不同；但这里并发执行 `++global_count` 没有同步，已经形成 data race，不能用最终结果判断程序是否正确。

**⚠️ 栈属于线程，不等于栈对象不能共享**

线程栈只是由对应线程按调用约定管理，并不是独立地址空间。只要地址被传出去，另一个线程就能访问该栈对象：

```cpp
#include <thread>

int main() {
    int value = 42;

    std::thread reader([&value] {
        // 能访问 main 线程栈上的 value。
        // 这里只读，且 main 在 join 前不销毁 value，因此是安全的。
        (void)value;
    });

    reader.join();
}
```

如果 `main` 在 worker 完成前返回、让局部对象离开作用域，worker 就会解引用悬空地址；如果两条线程无同步地并发读写 `value`，即使对象还活着，也会产生 data race。**生命周期正确和并发访问正确是两条独立约束。**

---

## C++ 共享内存模型：data race 与 happens-before

**🎯 data race 是语言层面的未定义行为**

C++ 中，两个线程并发访问同一个内存位置，其中至少一个是写，并且这些访问既不是合适的原子操作、也没有 happens-before 顺序，就形成 data race；程序行为未定义。

```cpp
int ready = 0;
int payload = 0;

// thread A
payload = 42;
ready = 1;

// thread B
if (ready == 1) {
    use(payload);
}
```

直觉上 A 先写 `payload` 再写 `ready`，B 看见 `ready == 1` 后似乎应该看见 `payload == 42`；但普通变量没有跨线程同步语义，编译器和 CPU 都不需要维持这个推理，两个变量上的访问还都存在 data race。

**🎯 happens-before 才是跨线程可见性的证明链**

一种正确发布方式是 release/acquire：

```cpp
#include <atomic>

int payload = 0;
std::atomic<bool> ready{false};

// thread A
payload = 42;
ready.store(true, std::memory_order_release);

// thread B
if (ready.load(std::memory_order_acquire)) {
    use(payload);  // 能看见 payload = 42
}
```

证明链是：

```text
A 写 payload
    sequenced-before
A 对 ready 做 release store
    synchronizes-with
B 对 ready 读到该值的 acquire load
    sequenced-before
B 读 payload

所以：A 写 payload happens-before B 读 payload
```

这里 `payload` 不需要本身是 atomic，因为它的写和读已经被 `ready` 建立的 happens-before 排序；如果 A 在发布后继续修改 `payload`，仍然需要新的同步。

**⚠️ 原子性、可见性和业务不变量是三个层次**

```cpp
std::atomic<int> balance{100};

void withdraw(int amount) {
    if (balance.load() >= amount) {
        balance.fetch_sub(amount);
    }
}
```

每次 atomic 操作本身都不可分，但“检查余额足够，再扣款”是两个操作组成的事务。两个线程都可能读到 100，然后各扣 80，最终得到 -60。这里没有 data race，却仍有 race condition：**atomic 保护了单个内存操作，没有自动保护跨操作业务不变量。**

正确方案可以是 mutex 包住整个 check-then-act：

```cpp
#include <mutex>

int balance = 100;
std::mutex balance_mutex;

bool withdraw(int amount) {
    std::lock_guard lock(balance_mutex);
    if (balance < amount) {
        return false;
    }
    balance -= amount;
    return true;
}
```

也可以用 CAS 循环把条件更新合并成一个原子状态转换，但复杂状态通常优先用锁，代码更容易证明。

**⚠️ `volatile` 不是线程同步工具**

```cpp
volatile bool ready = false;  // 仍然不能跨线程安全发布 payload
```

`volatile` 主要表示每次访问都具有特殊的可观察意义，常用于 memory-mapped I/O；它不提供互斥、原子 read-modify-write 或跨线程 happens-before。线程同步应使用 `std::atomic`、mutex、condition variable、semaphore、线程启动/结束等明确原语。

---

## 同步错误的统一根因：共享可变状态失去约束

**🎯 五个问题比“该加哪把锁”更接近根因**

排查并发代码时，先为每份状态回答：

1. **Identity**：多个线程操作的真的是同一个对象吗，还是 fd/指针已经被复用？
2. **Ownership**：谁有权直接修改它，是单线程拥有还是多线程共享？
3. **Lifetime**：对象在异步任务执行时是否仍然存活？
4. **Invariant**：哪些字段必须作为整体变化，临界区边界在哪里？
5. **Ordering**：哪次写必须先于哪次读，靠什么建立 happens-before？

例如网络线程把裸 fd 数字投递给 worker，随后又在原线程关闭它。错误不只是“`send` 少一把锁”：fd 只是进程描述符表的整数索引，关闭后可能立即被新连接复用；worker 延迟执行时，可能操作一条完全不同的连接。即使 fd 尚未复用，让 I/O 线程和 worker 同时对同一连接执行 read、write、close 或修改输出缓冲区，也会破坏连接状态机。根因同时涉及对象身份、生命周期和所有权。

**🎯 Reactor 中的正确做法：不转移 fd，只转移业务数据**

在 `one loop per thread` 模型中，一条连接从建立到关闭都固定归属于某个 I/O loop；fd、`Channel`、输入/输出缓冲区、协议解析状态和 `EPOLLOUT` 注册状态都只由该 loop 线程直接修改。worker 不接收裸 fd，也不直接操作连接内部状态：

```text
连接所属 I/O loop
    ├── read 并解析输入
    ├── 生成拥有独立生命周期的 Request
    └── 把 Request 投递到有界业务队列
                    ↓
业务 worker
    ├── 只做 CPU 计算或阻塞业务调用
    └── 生成拥有独立生命周期的 Response
                    ↓
completion queue + wakeup
                    ↓
原连接所属 I/O loop
    ├── 重新校验连接身份和状态
    ├── 按协议要求恢复响应顺序
    └── 执行实际 send 或丢弃过期结果
```

这里要同时守住五条边界：

- **连接身份**：异步任务引用稳定的连接对象和 connection id/generation，而不是只保存可能复用的 fd 数字；
- **对象生命周期**：通常使用可失效的异步引用，任务完成后重新确认连接仍存在，而不是为了一个迟到结果强行延长连接寿命；
- **逻辑有效性**：对象仍存在不等于 TCP 仍处于 connected 状态，回投后必须在所属 loop 再检查一次；
- **数据生命周期**：`Request` 必须拥有自己的数据，不能引用稍后会被 I/O loop 压缩、复用或销毁的输入缓冲区；
- **响应顺序**：worker 的完成顺序可能不同于请求到达顺序，协议要求有序时应限制每连接 in-flight 数，或用 sequence 在所属 loop 排序。

`shared_ptr<Connection>` 只能保证对象暂时不析构，不能让连接字段自动变成线程安全；真正保证连接状态一致性的是“所有可变 I/O 状态只在所属 EventLoop 串行修改”。跨线程 completion queue 仍需 mutex 或正确的 MPSC 队列同步，并且必须先发布任务、再用 `eventfd` 等机制唤醒 loop。

**🎯 Blocking worker 模型中的另一种正确做法：完整转移 fd 所有权**

在 prethreaded blocking server 中，accept 线程可以把新连接完整交给某一个 worker，但这必须是独占所有权转移，而不是共享一个 fd 数字：

```text
accept thread
    └── 移交连接所有权后，不再 read/write/close
                    ↓
有界连接队列
                    ↓
唯一 worker
    └── 独占负责 read/write/close，结束时释放连接
```

工程上通常用 move-only RAII fd 对象表达这种转移：只有成功接收所有权的一方负责关闭；提交失败时，所有权仍留在提交方并由其清理。若 fd 已经注册到某个 EventLoop，并关联 `Channel`、buffer 和状态机，就不能只移动 fd 数字；迁移必须连同全部连接状态和 epoll 注册关系一起完成，因此通常选择固定归属而不是运行中迁移。

两种模型的判断规则是：

| 模型 | fd 所有权 | worker 接收什么 | 实际 socket I/O 在哪里执行 |
|---|---|---|---|
| `one loop per thread` / Reactor | 始终归连接所属 I/O loop | 独立 `Request`，完成后回投 `Response` | 原连接所属 I/O loop |
| prethreaded blocking server | 从 accept 线程完整转移给一个 worker | 具有独占所有权的连接 | 接收所有权的唯一 worker |

最危险的是中间状态：原线程和 worker 都保存同一个裸 fd，双方都可能 read、write 或 close。它既没有单线程所有权，也没有完成独占转移，无法仅靠给某次 `send` 加锁证明正确。

**🎯 减少共享通常优于增加锁**

对共享状态有三种基本策略：

```text
不可变：构造后只读，天然适合共享
    ↓
单一所有权：只有一个线程直接修改，其他线程发消息
    ↓
受控共享：多个线程直接访问，但用 mutex/atomic 等同步
```

例如一张只读路由表可在构造完成后共享；每条连接的 parser、input/output buffer 和事件状态适合固定归一个 I/O 线程；只有无法自然归属单线程的全局配额、任务队列等，再使用同步原语。

这条工程原则可以概括为：

> **share immutable data，communicate ownership，synchronize only the remaining shared mutable state。**

---

## one loop per thread：用线程所有权消除连接级竞争

**🎯 正确名称是 `one loop per thread`**

它不是 one thread per connection，而是：

```text
一个 I/O thread  <->  一个 EventLoop  <->  一个 epoll instance
一个 EventLoop   <->  多条固定归属于它的 TcpConnection
```

典型主从 Reactor：

```text
main thread / baseLoop
    └── accept 新连接
            ↓ round-robin 或负载策略
I/O thread 0 / EventLoop 0
    ├── connection A
    └── connection D
I/O thread 1 / EventLoop 1
    ├── connection B
    └── connection E
```

一条连接一旦分配给某个 loop，通常不再迁移。它的以下可变状态只由所属线程直接修改：

- socket 的 read/write 和关闭流程；
- input/output buffer；
- 协议 parser 状态；
- `Channel` 的 interest mask；
- `EPOLLOUT` 启停和连接生命周期状态。

**🎯 关键收益是“不同连接并行，同一连接串行”**

假设连接 A 的读事件、业务结果和关闭请求几乎同时到达：

```text
错误的任意线程直接操作：
thread 1: append outputBuffer
thread 2: close connection
thread 3: enable EPOLLOUT
        -> 需要多把锁、锁顺序和复杂生命周期协议

固定归属 EventLoop 0：
read callback -> completion callback -> close callback
        -> 在同一线程的任务序列中串行执行
```

这并不是让整个服务器变成单线程：EventLoop 0 和 EventLoop 1 能在不同 CPU 核上并行；只是每条连接内部保持单线程状态机，因此大幅减少连接级 mutex。

**🎯 跨线程调用要转换为消息投递**

其他线程可以发起“发送响应”或“关闭连接”的请求，但不能绕过所属 loop 直接修改连接状态：

```cpp
void TcpConnection::send(std::string message) {
    if (loop_->is_in_loop_thread()) {
        send_in_loop(std::move(message));
        return;
    }

    loop_->queue_in_loop(
        [self = shared_from_this(), message = std::move(message)]() mutable {
            self->send_in_loop(std::move(message));
        });
}
```

跨线程路径包含三步：

```text
业务线程把 callback 放入目标 loop 的任务队列
    ↓
write(eventfd) 唤醒阻塞在 epoll_wait 的 I/O 线程
    ↓
I/O 线程取出 callback，串行修改 TcpConnection
```

任务队列本身仍是共享状态，需要用 mutex 或 MPSC 队列正确同步；`eventfd` 只负责通知“有任务”，不能代替任务数据的存储和发布。

**⚠️ 先入队，再唤醒**

```cpp
void EventLoop::queue_in_loop(Functor fn) {
    {
        std::lock_guard lock(pending_mutex_);
        pending_.push_back(std::move(fn));
    }                                   // 解锁发布队列内容
    wakeup();                           // 再 write(eventfd)
}
```

如果先 wakeup、后入队，loop 可能醒来后看到空队列，再次进入 `epoll_wait`，新任务就可能长时间滞留。mutex 的 unlock/lock 已建立任务内容的 happens-before；`eventfd` 提供及时唤醒，两者职责不同。

**⚠️ 单线程所有权不等于绝对不需要同步**

one loop per thread 只消除了“连接内部由多个线程直接修改”的竞争，系统边界仍有共享对象：

- 跨线程 pending functor queue；
- 全局连接表或统计指标；
- worker pool 的任务/结果队列；
- 配置热更新和进程级资源；
- 对象引用计数与关闭协议。

应该把同步集中在这些清晰边界，而不是让锁散布到每个连接字段。

**⚠️ callback 不能阻塞 EventLoop**

```cpp
void on_message(const ConnectionPtr& conn, Request request) {
    Response response = slow_database_query(request);  // 错误：阻塞 I/O loop
    conn->send(response.serialize());
}
```

一个慢 callback 会拖住同一 loop 管理的全部连接。常见生产路径是：

```text
I/O loop：read + parse + 校验
    ↓ 有界任务队列
business worker pool：CPU 计算或阻塞调用
    ↓ completion queue
连接所属 I/O loop：校验 identity/lifetime + send
```

任务队列必须有界，并对满队列、慢客户端、连接已关闭和响应乱序建立明确策略；否则只是把“线程爆炸”换成“队列和内存爆炸”。

---

## 线程局部存储与所有权边界

**🎯 `thread_local` 为每个线程创建独立实例**

```cpp
#include <string>

thread_local std::string trace_buffer;

void append_trace(std::string_view text) {
    trace_buffer.append(text);  // 每个线程修改自己的实例，不需互斥
}
```

它适合线程缓存、随机数生成器状态、统计分片和 trace buffer；每线程实例可以减少锁竞争，但也有代价：线程多时内存按线程数增长，线程池中的 TLS 状态会跨任务保留，动态库卸载和析构顺序也要谨慎。

**⚠️ TLS 不能自动解决逻辑共享**

```cpp
thread_local int request_count = 0;
```

这得到的是每线程计数，不是全局总数。如果要读取进程总数，需要在安全时机汇总所有分片，或使用原子全局计数。把变量改成 `thread_local` 只是改变语义，不能作为“消除 data race”的机械修复。

**🎯 分片状态是降低竞争的经典手法**

```text
所有线程争用一个 global_counter
                ↓
每线程 local_counter，只在采样/线程退出时汇总
```

例如性能指标可以在热路径只更新 TLS 计数，监控线程周期性收集；这把每次请求上的 cache line 争用，变成低频汇总成本。

---

## 伪共享：变量逻辑独立，cache line 仍然共享

**🎯 false sharing 是缓存一致性层面的共享**

两个计数器虽然由不同线程独占，但如果落在同一个 cache line 上，两个 CPU 核的写仍会让该 line 在缓存间反复失效：

```cpp
#include <atomic>

struct Counters {
    std::atomic<long> a{0};
    std::atomic<long> b{0};  // 很可能和 a 位于同一 64-byte cache line
};
```

逻辑上没有 data race，结果也正确，但性能可能很差。可通过 cache-line padding 分离热写字段：

```cpp
struct alignas(64) PaddedCounter {
    std::atomic<long> value{0};
};

struct Counters {
    PaddedCounter a;
    PaddedCounter b;
};
```

`64` 是常见 x86-64 cache line 大小，生产代码应查询目标平台，或在可用时采用 `std::hardware_destructive_interference_size`。这个问题解释了为什么“已经按线程拆分、完全没有锁”的计数器仍可能扩展性很差。

---

## 易错点

- 把“局部变量”理解为绝不共享是错的，只要局部对象地址逃逸到其他线程，同一个实例就可能成为共享变量。
- 把“线程栈私有”理解为硬件隔离是错的，同进程线程共享页表，一个线程持有地址就能访问另一个线程的栈。
- 认为没有 data race 就没有竞态是错的，多个原子操作组成的 check-then-act 仍可能破坏业务不变量。
- 认为 `volatile` 能保证线程安全是错的，它不提供 atomicity 或 happens-before。
- 把 `one loop per thread` 理解为一个连接一个线程是错的，一个 loop 通常通过 epoll 管理大量连接。
- 认为连接固定归属一个 loop 后整个系统都不需要锁是错的，跨线程任务队列和全局状态仍需同步。
- 认为把慢业务放进 callback 只是影响当前连接是错的，它会阻塞该 loop 上所有连接。
- 认为 `eventfd` 写入就发布了 callback 内容是错的，任务数据仍须先通过锁或正确的原子队列发布，再发唤醒通知。
- 认为 `thread_local` 是 data race 的通用修复是错的，它会把一份全局语义改成多份线程局部语义。
- 认为不同字段就不会互相干扰是错的，落在同一 cache line 的独立热写字段会产生 false sharing。

---

## 工程关联

- Muduo、Netty event loop、libuv loop 等框架都利用线程亲和性，把连接状态收敛为单线程状态机，再通过任务队列处理跨线程请求。
- Linux `eventfd` 常与 epoll 配合做 loop wakeup，mutex/MPSC queue 负责安全发布数据，`eventfd` 负责把睡眠线程及时叫醒。
- ThreadSanitizer 适合定位 data race，但无法证明业务级 check-then-act、死锁、饥饿和对象 identity 错误都不存在。
- `perf c2c`、`perf stat` 的 cache miss 指标可用于定位 false sharing；正确性没问题但核数越多越慢时，应检查 cache line 争用。
- 线上连接对象不能只靠 fd 数字标识，因为 `close` 后 fd 会复用；通常还要受控对象生命周期、connection id 或 generation。
- 分片计数器、per-CPU 数据和 thread-local allocator cache 都是在用“复制状态、低频汇总”换取低竞争。

---

## 实验题

**🧪 题 1：画出变量—线程引用图**

分析下面程序中的每个变量实例：

```cpp
#include <thread>

int global_value;
thread_local int tls_value;

void worker(int* escaped) {
    static int static_value;
    int local_value = 0;
    ++*escaped;
    ++static_value;
    ++tls_value;
    ++local_value;
}

int main() {
    int main_stack_value = 0;
    std::thread a(worker, &main_stack_value);
    std::thread b(worker, &main_stack_value);
    a.join();
    b.join();
}
```

要求：

- 为 `global_value`、`tls_value`、`static_value`、`local_value` 和 `main_stack_value` 标出实例数量；
- 画出线程到实例的引用边；
- 找出实际发生 data race 的变量；
- 解释为什么 `main_stack_value` 位于 main 栈上仍然是共享变量。

**🧪 题 2：用 TSan 观察发布错误**

源码片段：

```cpp
#include <thread>

int payload;
bool ready;

int main() {
    std::thread producer([] {
        payload = 42;
        ready = true;
    });
    std::thread consumer([] {
        while (!ready) {}
        (void)payload;
    });
    producer.join();
    consumer.join();
}
```

要求：

- 用 `g++ -std=c++20 -O1 -g -fsanitize=thread -pthread` 编译运行；
- 找出 TSan 报告中的冲突读写和线程创建栈；
- 把 `ready` 改为 `std::atomic<bool>`，分别使用 relaxed 和 release/acquire；
- 解释为什么只有“原子 flag + 正确内存序”才能发布普通 `payload`。

**🧪 题 3：验证 one loop per thread 的线程亲和性**

在已有 Reactor/Muduo 风格服务器的连接回调中记录 connection id 和 thread id：

```cpp
void on_message(const ConnectionPtr& conn, Buffer* input) {
    std::cout << conn->id() << ' '
              << std::this_thread::get_id() << '\n';
}
```

要求：

- 配置至少三个 I/O EventLoop；
- 建立九条连接，观察不同连接被分配到不同线程；
- 在每条连接上连续发送十次消息，验证同一连接始终归同一线程；
- 临时加入一个两秒阻塞 callback，验证它只拖慢同一 loop 上的连接；
- 把慢任务移到有界 worker pool，再将结果投递回原 loop。

**🧪 题 4：测量 false sharing**

分别实现紧邻计数器和 `alignas(64)` 分离计数器：

```cpp
struct Packed {
    std::atomic<long> a{0};
    std::atomic<long> b{0};
};

struct alignas(64) Counter {
    std::atomic<long> value{0};
};
```

要求：

- 两个线程各自对一个计数器执行至少一亿次 relaxed `fetch_add`；
- 用 `g++ -O2 -pthread` 编译，固定到两个物理核后对比耗时；
- 使用 `perf stat -e cycles,instructions,cache-misses` 记录指标；
- 条件允许时用 `perf c2c` 观察发生 cache line bouncing 的地址；
- 解释为什么两个版本都无 data race，性能却可能相差数倍。
