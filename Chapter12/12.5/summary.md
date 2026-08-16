# §12.5 用信号量同步线程

这一节的主线是：**并发错误不是“调度运气不好”，而是程序没有把共享状态的不变量和必须发生的顺序编码进同步协议。** 信号量用 `P/wait` 和 `V/post` 同时表达资源计数与等待/唤醒；mutex、condition variable、`std::atomic` 和 C++20 semaphore 则把常见同步意图表达得更直接。学习重点不是背 API，而是先区分互斥、条件同步、生命周期与公平性，再证明协议不会 data race、lost wakeup、deadlock 或 starvation。

> 本节工程拓展包括：C++ 经典有界 blocking MPMC 队列、低开销 SPSC ring buffer、带 per-slot sequence 的有界 MPMC ring，以及读者优先、写者优先和无饥饿 FIFO 分阶段读者—写者锁。生产中应先选最简单且可证明正确的设计，只有 profile 证明队列或锁是瓶颈后，再考虑无锁结构。

---

## 进度图：同步错误为何取决于执行轨迹

**🎯 并发程序的状态由多条线程指令共同推进**

假设两个线程都执行：

```cpp
counter++;
```

机器层面可抽象为：

```text
L：把 counter 读入寄存器
U：寄存器加 1
S：把寄存器写回 counter
```

每条线程内部必须保持 `L -> U -> S`，但两条线程之间可以有多种交错：

```text
安全轨迹：A.L A.U A.S B.L B.U B.S   -> counter 增加 2
错误轨迹：A.L A.U B.L B.U A.S B.S   -> counter 只增加 1
```

错误轨迹进入了“两个线程都基于同一个旧值计算”的 unsafe region。mutex 的作用就是增加约束，使任何合法轨迹都不能让两条线程同时进入该临界区：

```cpp
#include <mutex>

std::mutex mutex;
int counter = 0;

void increment() {
    std::lock_guard lock(mutex);
    ++counter;
}
```

**🎯 临界区边界由不变量决定，不由代码行数决定**

下面即使把三个字段分别改成 atomic，也不能保证快照一致：

```cpp
struct Account {
    int balance;
    int reserved;
};

// 必须始终保持 available = balance - reserved >= 0
```

一次预留需要同时检查和更新：

```cpp
bool reserve(Account& account, int amount) {
    std::lock_guard lock(account_mutex);
    if (account.balance - account.reserved < amount) {
        return false;
    }
    account.reserved += amount;
    return true;
}
```

临界区包含“读两个字段、检查条件、更新字段”整个状态转换；只锁最后一条赋值会产生 TOCTOU race。**先写出不变量，再确定哪些操作必须对其他线程表现为一个整体。**

---

## 同步原语的职责边界

**🎯 mutex、condition variable、semaphore 和 atomic 解决不同问题**

| 原语 | 最适合表达 | 状态存在哪里 | 典型例子 |
|---|---|---|---|
| `std::mutex` | 临界区互斥、复合不变量 | 受保护对象中 | 账户状态、容器修改 |
| `std::condition_variable` | 等待“谓词为真” | 业务字段中 | 队列非空/未满、状态完成 |
| `std::counting_semaphore` | 等待可用许可/资源数量 | semaphore 计数中 | buffer 空槽、连接配额 |
| `std::binary_semaphore` | 0/1 许可或事件交接 | semaphore 计数中 | 单次阶段同步 |
| `std::atomic<T>` | 单对象原子状态与内存序 | atomic 对象中 | 计数、flag、无锁算法元数据 |
| `std::latch` / `std::barrier` | 一次性/重复阶段汇合 | 同步对象内部 | 所有 worker 到齐再进入下一阶段 |

例如限制同时访问后端的任务不超过 32 个，counting semaphore 比 mutex 更贴近语义：

```cpp
#include <semaphore>

std::counting_semaphore<32> permits{32};

void call_backend() {
    permits.acquire();
    try {
        do_request();
    } catch (...) {
        permits.release();
        throw;
    }
    permits.release();
}
```

工程中应再封装 RAII permit，避免每个 return/exception 路径都手工 `release`。

**⚠️ mutex 有所有者，semaphore permit 通常没有线程所有权**

mutex 必须由成功 lock 的线程 unlock；semaphore 则允许一个线程 wait/acquire，另一个线程 post/release：

```text
consumer: items.acquire()   <- 等待有数据
producer: items.release()   <- 发布一个数据项
```

因此 semaphore 适合资源计数和线程间事件交接；不要把 binary semaphore 与 mutex 完全等同。

---

## 信号量的 P/V 语义

**🎯 semaphore 是“计数 + 原子等待/修改”**

Dijkstra 的两个操作：

```text
P(s) / wait(s) / acquire(s)：
    等到 s > 0，然后原子地执行 s--

V(s) / post(s) / release(s)：
    原子地执行 s++，必要时唤醒等待者
```

POSIX 接口示例：

```c
#include <semaphore.h>

sem_t slots;
sem_init(&slots, 0, 8);  // 线程间共享，初始有 8 个许可

sem_wait(&slots);        // P：取得一个许可
use_resource();
sem_post(&slots);        // V：归还一个许可

sem_destroy(&slots);
```

`sem_wait` 可能被信号中断并返回 `-1/EINTR`，稳健封装要循环重试；`sem_post` 可在符合 POSIX 约束的信号处理场景中使用，而 mutex unlock 通常不是 async-signal-safe。

**🎯 binary semaphore 可以实现互斥，但 mutex 通常更合适**

```text
semaphore mutex = 1

P(mutex)
    critical section
V(mutex)
```

初始值为 1，第一条线程执行 `P` 后变 0，其他线程只能等待，直到持有者执行 `V`。但普通临界区优先选择 mutex，因为它具有所有者语义、RAII 支持，并常带错误检测和调试工具集成。

**⚠️ semaphore 的正确性取决于初值和每条路径的 P/V 配对**

```text
初值写成 0：所有线程第一次 P 就永久等待
异常路径漏 V：permit 泄漏，系统容量逐渐降到 0
多执行一次 V：凭空增加资源，突破并发上限
```

semaphore 不知道“真实资源”有几个，计数正确性完全由协议保证。

---

## 用 semaphore 实现有界生产者—消费者缓冲区

**🎯 三个量分别表达三个不变量**

容量为 `N` 的 ring buffer 需要：

```text
slots = N   // 当前空槽数量
items = 0   // 当前可消费元素数量
mutex = 1   // 保护 head/tail/buffer 的临界区

始终满足：slots + items = N
```

生产和消费协议：

```text
put(item):                       get():
    P(slots)                         P(items)
    P(mutex)                         P(mutex)
        insert(item)                     item = remove()
    V(mutex)                         V(mutex)
    V(items)                         V(slots)
```

这里 `slots/items` 负责条件同步，`mutex` 负责共享 ring 元数据的互斥。

**🎯 C++20 可以直接用 counting semaphore**

```cpp
#include <array>
#include <cstddef>
#include <mutex>
#include <semaphore>
#include <type_traits>
#include <utility>

template <typename T, std::size_t N>
class BoundedBuffer {
    static_assert(N > 0);
    static_assert(std::is_nothrow_default_constructible_v<T>);
    static_assert(std::is_nothrow_move_constructible_v<T>);
    static_assert(std::is_nothrow_move_assignable_v<T>);

public:
    void put(T value) {
        slots_.acquire();
        {
            std::lock_guard lock(mutex_);
            buffer_[tail_] = std::move(value);
            tail_ = (tail_ + 1) % N;
        }
        items_.release();
    }

    T get() {
        items_.acquire();
        T value;
        {
            std::lock_guard lock(mutex_);
            value = std::move(buffer_[head_]);
            head_ = (head_ + 1) % N;
        }
        slots_.release();
        return value;
    }

private:
    std::array<T, N> buffer_{};
    std::size_t head_ = 0;
    std::size_t tail_ = 0;
    std::mutex mutex_;
    std::counting_semaphore<N> slots_{N};
    std::counting_semaphore<N> items_{0};
};
```

这是教学版：它用静态约束排除最常见的元素默认构造、移动赋值和返回值移动构造异常，但代码重点仍只是同步协议。通用容器应使用 `std::optional<T>` 或原始存储精确管理对象生命周期，并用 RAII rollback 覆盖取得 permit 后的所有失败路径。

**⚠️ 等待资源时不要持有阻止对方推进的锁**

错误顺序：

```text
producer:
    P(mutex)
    P(slots)   // buffer 满时，producer 持 mutex 睡眠

consumer:
    P(items)
    P(mutex)   // 无法取得 mutex，不能消费并 V(slots)
```

形成死锁。应先等待 `slots/items` 这种条件许可，再进入短临界区修改 buffer。

**⚠️ 发布 permit 必须发生在元素构造完成之后**

正确顺序是：

```text
写入 buffer -> 离开 mutex 临界区 -> V(items)
```

如果先 `V(items)` 再写数据，consumer 可能立刻醒来并读取尚未构造完成的 slot。

---

## condition variable：等待状态，而不是等待通知

**🎯 condition variable 必须和“mutex 下的谓词”一起使用**

经典有界 MPMC blocking queue：

```cpp
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <utility>

template <typename T>
class BlockingQueue {
public:
    explicit BlockingQueue(std::size_t capacity)
        : capacity_(capacity) {
        if (capacity_ == 0) {
            throw std::invalid_argument("capacity 必须大于 0");
        }
    }

    bool push(T value) {
        std::unique_lock lock(mutex_);
        not_full_.wait(lock, [this] {
            return closed_ || queue_.size() < capacity_;
        });

        if (closed_) {
            return false;
        }

        queue_.push_back(std::move(value));
        not_empty_.notify_one();
        return true;
    }

    std::optional<T> pop() {
        std::unique_lock lock(mutex_);
        not_empty_.wait(lock, [this] {
            return closed_ || !queue_.empty();
        });

        if (queue_.empty()) {
            return std::nullopt;  // closed 且已排空
        }

        T value = std::move(queue_.front());
        queue_.pop_front();
        not_full_.notify_one();
        return value;
    }

    void close() {
        {
            std::lock_guard lock(mutex_);
            closed_ = true;
        }
        not_empty_.notify_all();
        not_full_.notify_all();
    }

private:
    const std::size_t capacity_;
    std::deque<T> queue_;
    bool closed_ = false;
    std::mutex mutex_;
    std::condition_variable not_empty_;
    std::condition_variable not_full_;
};
```

谓词由 `queue_` 和 `closed_` 表达，并始终在同一把 `mutex_` 下检查和修改。condition variable 的 notification 不是持久事件；真正不会丢的是受锁保护的状态。

**⚠️ `wait` 必须循环检查谓词**

下面写法错误：

```cpp
if (queue_.empty()) {
    not_empty_.wait(lock);
}
return pop_front();
```

原因有两个：

- condition variable 允许 spurious wakeup；
- 多个 consumer 被唤醒后，另一个 consumer 可能先取走元素。

应使用 `wait(lock, predicate)`，等价于 `while (!predicate()) wait(lock)`。

**🎯 shutdown 是队列协议的一部分**

没有 `close` 状态时，程序退出阶段可能出现：producer 已停止，consumer 永久睡在空队列上，main 又在 `join` consumer。`closed_ + notify_all` 让等待者能区分：

```text
暂时为空：继续等待
closed 且为空：不会再有元素，退出
closed 但非空：先排空，再退出
```

这说明同步协议不仅要覆盖 steady state，还必须覆盖启动、错误和关闭路径。

---

## 同步错误的根因分类

**🎯 data race：缺少互斥或 happens-before**

```cpp
int counter = 0;

void worker() {
    ++counter;  // 多线程并发执行时是未定义行为
}
```

根因是同一内存位置存在无序的冲突访问。修复方式是让变量单线程拥有、用 mutex 包住完整不变量，或在适合单对象状态时使用 atomic。

**🎯 atomicity violation：临界区切得太小**

```cpp
if (!cache.contains(key)) {  // 锁 1
    cache.insert(key, load(key));  // 锁 2
}
```

即使 `contains` 和 `insert` 各自线程安全，两步组合也不是原子事务，两个线程可能重复加载和插入。根因是同步边界没有覆盖 check-then-act。

**🎯 order violation：阶段先后未被编码**

```cpp
Task* task;

// consumer 可能先运行
use(*task);

// producer 后运行
task = new Task(...);
```

根因不是共享写冲突本身，而是“初始化必须先于使用”的顺序没有通过线程启动、future、semaphore、condition variable 或 release/acquire 建立。

**🎯 lost wakeup：把通知误当成状态**

```cpp
// 错误思路：先无锁检查，再去等待
if (!ready) {
    cv.wait(lock);
}
```

检查后、真正 wait 前，producer 可能设置 `ready=true` 并 notify；notification 没有排队，consumer 随后永久等待。根因是谓词没有和 wait 在同一 mutex 协议内原子衔接。

**🎯 deadlock：等待关系形成环**

```cpp
// thread A                 // thread B
lock(a);                    lock(b);
lock(b);                    lock(a);
```

等待图为 `A -> b -> B -> a -> A`。常见根因是锁顺序不一致、持锁调用未知代码、持锁做阻塞 I/O，或等待 condition/semaphore 时仍持有对方需要的资源。

使用统一锁顺序或 `std::scoped_lock`：

```cpp
void transfer(Account& from, Account& to, int amount) {
    std::scoped_lock lock(from.mutex, to.mutex);
    from.balance -= amount;
    to.balance += amount;
}
```

**🎯 starvation：系统在推进，但某条线程长期得不到机会**

```text
reader-preference RW lock：新 reader 不断插队，writer 永远等不到 readers=0
writer-preference RW lock：writer 持续到达，reader 永远过不了入口门
```

这不同于 deadlock：其他线程仍然持续完成工作。修复需要公平排队、限制连续批次、phase-fair 协议，或改变调度/分片设计。

**🎯 livelock：线程都在运行，却不断互相退让**

```cpp
while (!try_lock_both()) {
    release_everything();
    // 两条线程同步重试、同步失败
}
```

可引入随机/指数退避、统一锁顺序或由一个协调者串行化冲突操作。仅看 CPU 利用率时，livelock 可能表现为“CPU 很忙但吞吐为零”。

**🎯 lifetime/identity race：同步了内存，却操作了错误对象**

```cpp
workers.submit([fd] { write(fd, "x", 1); });
close(fd);  // fd 数字以后可被复用
```

给 fd 数字本身加 atomic 也没用；异步任务需要稳定的对象身份和生命周期协议。这类 bug 常表现为 use-after-free、ABA 或向错误连接发送响应。

---

## 死锁的四个必要条件与工程规避

**🎯 四个条件同时成立才可能死锁**

1. **互斥**：资源一次只能由一个线程持有；
2. **占有并等待**：持有资源时继续等待其他资源；
3. **不可抢占**：资源只能由持有者主动释放；
4. **循环等待**：等待图中存在环。

经典反例：

```cpp
std::mutex left;
std::mutex right;

void task_a() {
    std::lock_guard l(left);
    std::lock_guard r(right);
}

void task_b() {
    std::lock_guard r(right);
    std::lock_guard l(left);
}
```

**🎯 工程上最有效的是破坏循环等待**

```cpp
void task_a() {
    std::scoped_lock lock(left, right);
    // 同时取得两把锁
}

void task_b() {
    std::scoped_lock lock(left, right);
    // 相同协议
}
```

或者为锁定义全局层级：

```text
process registry lock
    -> connection lock
        -> output buffer lock
```

所有代码只能按从高到低顺序获取，禁止反向加锁。

**⚠️ 持锁调用 callback 是隐藏锁顺序的来源**

```cpp
{
    std::lock_guard lock(mutex_);
    user_callback(state_);  // callback 可能重入对象或获取另一把锁
}
```

更安全的做法是在锁内复制必要快照，释放锁后再调用外部代码：

```cpp
State snapshot;
{
    std::lock_guard lock(mutex_);
    snapshot = state_;
}
user_callback(snapshot);
```

---

## 读者—写者问题：两类偏好模式

**🎯 共同目标是多读并行、写操作独占**

约束为：

```text
reader + reader：允许并发
reader + writer：禁止并发
writer + writer：禁止并发
```

读多写少的配置、路由表和索引适合这种模型；如果临界区很短或写很多，普通 mutex 反而可能更快、更公平、更容易验证。

**🎯 读者优先：第一类读者—写者问题**

**🎯 只要没有 writer 正在写，新 reader 就可以进入**

C++ 经典实现：

```cpp
#include <condition_variable>
#include <cstddef>
#include <mutex>

class ReaderPreferredRWLock {
public:
    void lock_shared() {
        std::unique_lock lock(mutex_);
        cv_.wait(lock, [this] { return !writer_active_; });
        ++active_readers_;
    }

    void unlock_shared() {
        std::lock_guard lock(mutex_);
        if (--active_readers_ == 0) {
            cv_.notify_all();
        }
    }

    void lock() {
        std::unique_lock lock(mutex_);
        cv_.wait(lock, [this] {
            return !writer_active_ && active_readers_ == 0;
        });
        writer_active_ = true;
    }

    void unlock() {
        {
            std::lock_guard lock(mutex_);
            writer_active_ = false;
        }
        cv_.notify_all();
    }

private:
    std::mutex mutex_;
    std::condition_variable cv_;
    std::size_t active_readers_ = 0;
    bool writer_active_ = false;
};
```

当 writer 等待时，`writer_active_` 仍为 false，新 reader 可以不断进入。因此在持续读流量下，`active_readers_` 可能永远不降到 0，writer starvation。

使用方式可交给标准 RAII wrapper：

```cpp
#include <shared_mutex>  // std::shared_lock

ReaderPreferredRWLock rwlock;

{
    std::shared_lock read_lock(rwlock);
    read_state();
}

{
    std::unique_lock write_lock(rwlock);
    update_state();
}
```

**🎯 写者优先：第二类读者—写者问题**

**🎯 只要有 writer 等待，就阻止新 reader 插队**

```cpp
class WriterPreferredRWLock {
public:
    void lock_shared() {
        std::unique_lock lock(mutex_);
        readers_cv_.wait(lock, [this] {
            return !writer_active_ && waiting_writers_ == 0;
        });
        ++active_readers_;
    }

    void unlock_shared() {
        std::lock_guard lock(mutex_);
        if (--active_readers_ == 0) {
            writers_cv_.notify_one();
        }
    }

    void lock() {
        std::unique_lock lock(mutex_);
        ++waiting_writers_;
        writers_cv_.wait(lock, [this] {
            return !writer_active_ && active_readers_ == 0;
        });
        --waiting_writers_;
        writer_active_ = true;
    }

    void unlock() {
        std::lock_guard lock(mutex_);
        writer_active_ = false;
        if (waiting_writers_ != 0) {
            writers_cv_.notify_one();
        } else {
            readers_cv_.notify_all();
        }
    }

private:
    std::mutex mutex_;
    std::condition_variable readers_cv_;
    std::condition_variable writers_cv_;
    std::size_t active_readers_ = 0;
    std::size_t waiting_writers_ = 0;
    bool writer_active_ = false;
};
```

一旦第一名 writer 登记为 waiting，后来 reader 都等待；现有 readers 退出后，调度转向 writer 群体。它阻止持续 reader 流量无限插队，但 `condition_variable::notify_one` 不承诺 writer 之间 FIFO，因此不能由这段代码推出“每个 writer 都有严格等待上界”；持续 writer 流量还可能造成 reader starvation。

**⚠️ `std::shared_mutex` 不承诺统一的公平策略**

```cpp
#include <shared_mutex>

std::shared_mutex mutex;
```

标准规定互斥语义，但不要求实现严格 reader preference、writer preference 或 FIFO。是否饥饿以及调度细节依赖标准库和平台实现；有延迟上界要求时不能仅凭接口名称假定公平。

---

## 无饥饿读者—写者锁：FIFO 到达顺序 + reader phase

**🎯 避免两边饿死，要让已经排队的请求不能被后来者无限超越**

一个容易证明的方案是显式 FIFO 队列：

- 队首是 writer：资源空闲后只授予这一个 writer；
- 队首是 reader：把队首连续 readers 作为一个 reader phase 批量授予；
- 新请求只能排到队尾，不能越过已经等待的另一类请求。

```cpp
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>

class FairRWLock {
public:
    void lock_shared() {
        Waiter waiter(false);
        std::unique_lock lock(mutex_);
        waiters_.push_back(&waiter);
        grant_waiters();
        waiter.cv.wait(lock, [&waiter] { return waiter.granted; });
    }

    void unlock_shared() {
        std::lock_guard lock(mutex_);
        if (--active_readers_ == 0) {
            grant_waiters();
        }
    }

    void lock() {
        Waiter waiter(true);
        std::unique_lock lock(mutex_);
        waiters_.push_back(&waiter);
        grant_waiters();
        waiter.cv.wait(lock, [&waiter] { return waiter.granted; });
    }

    void unlock() {
        std::lock_guard lock(mutex_);
        writer_active_ = false;
        grant_waiters();
    }

private:
    struct Waiter {
        explicit Waiter(bool is_writer) : writer(is_writer) {}

        bool writer;
        bool granted = false;
        std::condition_variable cv;
    };

    void grant_waiters() {
        // 调用者必须持有 mutex_。
        if (writer_active_ || active_readers_ != 0 || waiters_.empty()) {
            return;
        }

        if (waiters_.front()->writer) {
            Waiter* waiter = waiters_.front();
            waiters_.pop_front();
            writer_active_ = true;
            waiter->granted = true;
            waiter->cv.notify_one();
            return;
        }

        // 把队首连续 reader 合并为一个 phase。
        while (!waiters_.empty() && !waiters_.front()->writer) {
            Waiter* waiter = waiters_.front();
            waiters_.pop_front();
            ++active_readers_;
            waiter->granted = true;
            waiter->cv.notify_one();
        }
    }

    std::mutex mutex_;
    std::deque<Waiter*> waiters_;
    std::size_t active_readers_ = 0;
    bool writer_active_ = false;
};
```

`Waiter` 位于等待线程自己的栈上，但在该线程返回 `lock/lock_shared` 前一直存活；对象先从队列移除并设置 `granted`，再被等待线程继续使用。`wait(lock, predicate)` 同时处理“grant 发生在 wait 之前”和 spurious wakeup。

**🎯 为什么它不会让已入队请求饿死**

请求入队后，前方请求数量是有限的；后来请求只能排在后面。每次 writer 或 reader phase 结束都会调用 `grant_waiters`，队首最终会被授予。这里的公平起点是“成功取得内部 mutex 并入队之后”；OS 调度器仍可能让某线程长期得不到 CPU，任何用户态锁都无法消除这种调度层不公平。

**⚠️ 严格公平会牺牲部分吞吐**

上面的实现不会在一个 reader phase 已运行时继续吸收后来的 reader，即使队列里暂时没有 writer；这样证明简单、延迟有界，但 reader 批次切换更多。生产级 phase-fair RW lock 常会更精细地控制 reader batching，在吞吐与尾延迟之间取舍。

---

## C++ 经典 SPSC 队列

**🎯 SPSC 的关键优势来自固定角色，而不只是 atomic 更少**

SPSC（single producer, single consumer）约束：

```text
只有 producer 写 head 和对应的新 slot
只有 consumer 写 tail 和对应的旧 slot
producer 只读取 tail
consumer 只读取 head
```

因此不需要 CAS 抢位置；release/acquire 只用于发布元素和回收 slot。

教学版有界 ring buffer：

```cpp
#include <array>
#include <atomic>
#include <cstddef>
#include <utility>

template <typename T, std::size_t N>
class SpscRing {
    static_assert(N >= 2);

public:
    bool try_push(T value) {
        const std::size_t head = head_.load(std::memory_order_relaxed);
        const std::size_t next = increment(head);

        if (next == tail_.load(std::memory_order_acquire)) {
            return false;  // 满：保留一个空槽区分 empty/full
        }

        buffer_[head] = std::move(value);
        head_.store(next, std::memory_order_release);
        return true;
    }

    bool try_pop(T& value) {
        const std::size_t tail = tail_.load(std::memory_order_relaxed);

        if (tail == head_.load(std::memory_order_acquire)) {
            return false;
        }

        value = std::move(buffer_[tail]);
        tail_.store(increment(tail), std::memory_order_release);
        return true;
    }

    static constexpr std::size_t capacity() noexcept {
        return N - 1;
    }

private:
    static constexpr std::size_t increment(std::size_t index) noexcept {
        return (index + 1) % N;
    }

    std::array<T, N> buffer_{};

    alignas(64) std::atomic<std::size_t> head_{0};
    alignas(64) std::atomic<std::size_t> tail_{0};
};
```

这同样是教学版，要求 `T` 可默认构造和赋值；成熟实现会用 raw storage/placement construction 支持非默认构造类型，并处理析构。

**🎯 release/acquire 发布的是 slot 内容**

producer 路径：

```text
buffer[head] = value
    sequenced-before
head.store(next, release)
    synchronizes-with
consumer head.load(acquire) 读到 next
    sequenced-before
consumer 读取 buffer[tail]
```

consumer 对 `tail` 的 release 和 producer 对 `tail` 的 acquire，则保证 producer 只有在 consumer 完成旧 slot 读取后才复用它。

**⚠️ SPSC 接口约束是正确性条件**

如果第二个 producer 也调用 `try_push`，两者都可能读到同一个 `head` 并写同一 slot；代码没有 CAS，也没有锁，立即失效。类型本身很难确认调用线程身份，工程上应通过架构所有权、debug 断言和清晰接口维持 SPSC 约束。

**⚠️ 空队列等待策略要显式选择**

```cpp
while (!queue.try_pop(value)) {
    // busy spin
}
```

busy spin 延迟低但持续占 CPU；可选择：

- 短时间 spin，再 `std::this_thread::yield`；
- C++20 `atomic::wait/notify_one`；
- semaphore/eventfd 做阻塞唤醒；
- 在 event loop 中把队列与 wakeup fd 组合。

不能只比较单次入队纳秒数，还要结合空闲占比、CPU 预算和尾延迟。

---

## C++ 经典 MPMC 队列：先从 mutex + condition variable 开始

**🎯 MPMC 的并发拓扑比 SPSC 多了两类位置竞争**

```text
多个 producers 争用“下一个可写位置”
多个 consumers 争用“下一个可读位置”
```

前面的 `BlockingQueue<T>` 是经典且实用的 MPMC 基线：一把 mutex 保护 deque 和关闭状态，`not_empty/not_full` 分别管理消费者和生产者等待。它具备：

- 清晰的有界背压；
- 无 busy spin；
- 容易支持 `close`、timeout 和批量操作；
- 容易用 TSan 和普通调试器验证；
- 对中低竞争负载通常足够快。

**⚠️ “用了 mutex”不等于一定慢**

如果临界区只是 push/pop 一个指针，锁没有竞争时通常只走用户态 fast path；无锁结构反而会增加 CAS 重试、cache line bouncing、复杂内存序和测试成本。应以吞吐、p99/p999 延迟、CPU 利用率和真实拓扑压测，而不是按“lock-free”标签选型。

---

## 高并发有界 MPMC ring：per-slot sequence 设计

**🎯 每个 slot 的 sequence 同时编码代际和状态**

经典 bounded MPMC ring 为每个 cell 保存递增 sequence：

```text
producer 期望 cell.sequence == enqueue_position
consumer 期望 cell.sequence == dequeue_position + 1
consumer 释放后把 sequence 推进一个完整 capacity
```

这样 position 发生环绕时，sequence 能区分“这一圈的 slot”和“上一圈的旧状态”，避免仅靠 empty/full bit 引发 ABA 式混淆。

核心实现：

```cpp
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <utility>

template <typename T, std::size_t Capacity>
class BoundedMpmcRing {
    static_assert(Capacity >= 2);
    static_assert((Capacity & (Capacity - 1)) == 0,
                  "Capacity 必须是 2 的幂");

    struct Cell {
        std::atomic<std::size_t> sequence{0};
        T data{};
    };

public:
    BoundedMpmcRing() {
        for (std::size_t i = 0; i < Capacity; ++i) {
            buffer_[i].sequence.store(i, std::memory_order_relaxed);
        }
    }

    bool try_push(T value) {
        std::size_t pos = enqueue_pos_.load(std::memory_order_relaxed);
        Cell* cell;

        for (;;) {
            cell = &buffer_[pos & mask_];
            const std::size_t seq =
                cell->sequence.load(std::memory_order_acquire);
            const auto diff = static_cast<std::intptr_t>(seq) -
                              static_cast<std::intptr_t>(pos);

            if (diff == 0) {
                if (enqueue_pos_.compare_exchange_weak(
                        pos, pos + 1, std::memory_order_relaxed)) {
                    break;  // 当前 producer 独占这个 slot
                }
            } else if (diff < 0) {
                return false;  // 当前 enqueue position 尚不可复用
            } else {
                pos = enqueue_pos_.load(std::memory_order_relaxed);
            }
        }

        cell->data = std::move(value);
        cell->sequence.store(pos + 1, std::memory_order_release);
        return true;
    }

    bool try_pop(T& value) {
        std::size_t pos = dequeue_pos_.load(std::memory_order_relaxed);
        Cell* cell;

        for (;;) {
            cell = &buffer_[pos & mask_];
            const std::size_t seq =
                cell->sequence.load(std::memory_order_acquire);
            const auto diff = static_cast<std::intptr_t>(seq) -
                              static_cast<std::intptr_t>(pos + 1);

            if (diff == 0) {
                if (dequeue_pos_.compare_exchange_weak(
                        pos, pos + 1, std::memory_order_relaxed)) {
                    break;  // 当前 consumer 独占这个 slot
                }
            } else if (diff < 0) {
                return false;  // 当前 dequeue position 尚不可消费
            } else {
                pos = dequeue_pos_.load(std::memory_order_relaxed);
            }
        }

        value = std::move(cell->data);
        cell->sequence.store(pos + Capacity, std::memory_order_release);
        return true;
    }

private:
    static constexpr std::size_t mask_ = Capacity - 1;

    std::array<Cell, Capacity> buffer_{};
    alignas(64) std::atomic<std::size_t> enqueue_pos_{0};
    alignas(64) std::atomic<std::size_t> dequeue_pos_{0};
};
```

**🎯 position CAS 只负责认领，sequence 才负责发布**

producer 先用 CAS 认领 position，再写 `data`，最后用 release store 更新 cell sequence；consumer 只有 acquire load 看到期望 sequence 后才能读 `data`。position CAS 负责“谁拥有这个 slot”，sequence release/acquire 负责“slot 内容何时可见”。

**⚠️ 这个教学骨架的 `false` 只描述目标 position 此刻不可用**

如果 producer A 已认领 position 0 后被抢占，而 producer B 已发布 position 1 并返回成功，consumer 仍会在 position 0 返回 `false`；对称地，迟到的 consumer 也可能暂时阻塞 slot 的代际复用，使 producer 返回 `false`。因此它不是可直接承诺常规线性化 empty/full 语义的通用队列；调用方若采用这一骨架，必须允许暂时失败并重试，不能把单次 `false` 解释为“调用期间整个队列绝对为空/满”。position CAS 是 reservation point，不是整个 `try_push`/`try_pop` 的线性化点。

**⚠️ 这段代码是算法骨架，不是可直接替代成熟库的通用容器**

生产化还要处理：

- `T` 的构造、移动异常和析构；
- position 极长期回绕时的整数比较假设；
- cache line padding 是否符合目标平台；
- empty/full 时 spin、drop、timeout 还是阻塞；
- 线程取消和进程关闭；
- 一个线程认领 slot 后被长期抢占造成的队头停顿；
- sanitizer、弱内存序架构和高并发压力验证。

该算法使用 atomic RMW、没有 mutex，但不应草率承诺 wait-free；一个线程认领位置后停顿，可能延迟后续对应位置的可见性。工程文档应准确说明具体 progress guarantee，而不是统称“无锁所以永不阻塞”。

---

## SPSC、MPMC 与事件循环队列怎么选

**🎯 先根据生产者/消费者基数和等待语义选结构**

| 场景 | 推荐起点 | 主要理由 |
|---|---|---|
| 一个采集线程 -> 一个处理线程 | bounded SPSC ring | 无 CAS，角色固定，低延迟 |
| 多 worker -> 一个 EventLoop completion queue | MPSC queue + `eventfd` | 单 consumer 与 loop 所有权天然匹配 |
| 多 producer -> 多 consumer 通用任务池 | mutex + CV bounded MPMC | 易关闭、易背压、易验证 |
| 极高竞争且 profile 已确认队列瓶颈 | 成熟 bounded MPMC 实现 | 避免自行承担内存序和生命周期风险 |
| 网络 socket I/O 状态 | one loop per thread + 消息投递 | 不要把连接本身做成任意线程共享的 MPMC 对象 |

例如业务 worker 结果回到某个 EventLoop 时，每个 loop 是唯一 consumer，但有多个 worker producer，因此严格说是 MPSC，而不是 MPMC：

```text
worker 0 ─┐
worker 1 ─┼─> completion queue ─> EventLoop 0
worker 2 ─┘          MPSC             single consumer
```

识别更窄的并发拓扑，通常能得到更简单、更快、更可证明的算法。

**⚠️ 有界容量和过载策略比队列算法名称更重要**

满队列时必须选择：

```text
block producer
return false / EAGAIN
丢弃最新或最旧任务
按优先级淘汰
对上游暂停读取或返回 overload
```

无界队列在基准中看似“永不阻塞”，实际只是把背压延迟到 OOM。在线服务必须把队列容量、每连接 in-flight 数和 output buffer 高水位纳入同一个容量规划。

---

## 预线程化服务器与 one loop per thread 的区别

**🎯 教材 prethreaded server 是固定 worker pool + 共享连接队列**

```text
accept thread
    ↓ put(connfd)
bounded shared buffer
    ↓ get(connfd)
worker threads：每个 worker 阻塞式服务一条连接
```

相比 thread-per-connection，它限制线程数并复用 worker，避免每条连接都创建/销毁线程；但一个慢连接会长期占住一个 worker，高并发长连接下仍可能耗尽 worker。

**🎯 one loop per thread 是固定 I/O loops + 每 loop 管很多连接**

```text
baseLoop：accept
    ↓ 分配连接
EventLoop 0：epoll 管很多 connfd
EventLoop 1：epoll 管很多 connfd
```

两者都把线程数控制在固定范围，但调度单位不同：

| 模型 | 一个线程同一时刻管理 | 等待 I/O 的方式 | 共享队列用途 |
|---|---|---|---|
| prethreaded blocking server | 通常一条正在服务的连接 | worker 阻塞在 read/write | 分发新 `connfd` |
| one loop per thread | 大量非阻塞连接 | epoll 等 readiness | 跨线程 callback/completion |

现代高并发服务常组合使用：少量 I/O loops 管连接，独立 bounded worker pool 做 CPU/阻塞业务，再把结果投回连接所属 loop。

---

## 易错点

- 把并发 bug 归因于“线程切换太巧”是错的，根因是程序允许错误轨迹存在，没有建立必要的互斥或 happens-before。
- 只给最终写操作加锁是错的，临界区必须覆盖维护业务不变量的完整 read-check-modify 序列。
- 把 binary semaphore 完全等同于 mutex 是错的，mutex 有所有者语义，而 semaphore 允许跨线程归还 permit。
- 先持 buffer mutex 再等待 slots/items 是危险的，等待者可能占住对方推进所必需的锁并形成死锁。
- 把 condition variable 的 notify 当成可积累消息是错的，持久状态必须放在 mutex 保护的谓词中。
- 用 `if` 而不是 `while`/predicate wait 检查条件是错的，spurious wakeup 和其他 consumer 都会使条件再次为假。
- 只设计队列 push/pop、不设计 close 是不完整的，退出阶段的永久等待同样属于同步错误。
- 读者优先不会让 reader 饿死，但可能让 writer 饿死；写者优先解决 writer 饥饿后又可能让 reader 饿死。
- 假定 `std::shared_mutex` 严格公平是错的，C++ 标准不规定统一的读写者调度策略。
- 把 SPSC 队列交给两个 producer 使用是错的，单生产者/单消费者是算法成立的前提而非性能提示。
- 把 atomic position 的 CAS 当成 slot 内容已经发布是错的，data 还要通过 per-slot release/acquire sequence 发布。
- 认为 lock-free 必然低延迟是错的，CAS 重试、cache line bouncing、抢占和队头停顿都可能放大尾延迟。
- 使用无界 MPMC 队列逃避满队列处理是错的，它只会把背压变成延迟膨胀和 OOM。
- 把 prethreaded server 与 one loop per thread 混为一谈是错的，前者通常让一个 worker 阻塞服务一条连接，后者让一个 epoll loop 管理很多非阻塞连接。

---

## 工程关联

- ThreadSanitizer 能发现 data race；死锁可结合线程栈、锁依赖图和 `gdb thread apply all bt`；livelock 则常表现为 CPU 高、吞吐低和大量 CAS/try-lock 重试。
- `futex(2)` 是 Linux 上许多 mutex、condition variable 和 semaphore 慢路径的基础：无竞争时用户态 atomic fast path，竞争时再进入内核睡眠/唤醒。
- bounded queue 是背压边界，不只是线程安全容器；容量应和内存预算、处理速率、允许排队延迟共同确定。
- one loop per thread 的 completion queue 通常是 MPSC，结合 `eventfd` 唤醒；任务先安全入队，再通知 loop。
- `std::shared_mutex` 适合读多、临界区有一定长度的负载，必须用实际 read/write 比例压测；短临界区下普通 mutex 可能更优。
- 高性能消息系统常用 SPSC ring 连接固定 pipeline stage，用多个 SPSC channel 代替一个全局 MPMC 热点。
- 真正通用的 lock-free 链式队列还涉及 hazard pointer、epoch-based reclamation 或 RCU；有界 ring 通过固定存储避开了一部分动态内存回收难题。
- 公平锁降低 starvation 和尾延迟，但可能减少 reader batching、增加 context switch；公平性是 SLA 与吞吐之间的设计选择。

---

## 实验题

**🧪 题 1：从错误轨迹定位临界区**

源码：

```cpp
#include <thread>

int balance = 100;

bool withdraw(int amount) {
    if (balance < amount) {
        return false;
    }
    balance -= amount;
    return true;
}
```

要求：

- 启动两个线程并发执行 `withdraw(80)`；
- 把函数拆成 load/check/store，画出得到 `balance == -60` 的轨迹；
- 用同一把 mutex 保护完整 check-then-act；
- 再尝试只把 `balance` 改成 atomic，解释为什么业务竞态仍可能存在；
- 用 `-fsanitize=thread` 区分 data race 与 race condition。

**🧪 题 2：实现可关闭的 bounded MPMC queue**

以本节 `BlockingQueue<T>` 为基础：

```cpp
BlockingQueue<int> queue(64);
```

要求：

- 启动四个 producers 和四个 consumers；
- 每个 producer 写入带 producer id 与 sequence 的消息；
- 验证所有消息恰好消费一次；
- 实现 `close`，保证所有等待线程都能退出并被 `join`；
- 增加 `push_for/pop_for` timeout；
- 用极小容量 1/2 压测 `not_empty/not_full` 的边界和关闭竞态。

**🧪 题 3：对比 SPSC 与 MPMC 基线**

准备两种实现：

```cpp
SpscRing<std::uint64_t, 1024> spsc;
BlockingQueue<std::uint64_t> mpmc(1023);
```

要求：

- 固定一个 producer 和一个 consumer 到两个物理核；
- 传输至少一亿个递增序号并校验无丢失、无重复、顺序正确；
- 对比 throughput、p50/p99 延迟、cycles/instructions/cache-misses；
- 分别测试 busy spin、yield 和阻塞唤醒；
- 再错误地启动第二个 SPSC producer，仅用于 TSan/正确性观察，说明为何接口约束被破坏。

**🧪 题 4：验证 per-slot sequence MPMC ring**

以本节 `BoundedMpmcRing` 为算法骨架：

```cpp
BoundedMpmcRing<std::uint64_t, 1024> queue;
```

要求：

- 四个 producers 各生成独立编号空间，四个 consumers 并发读取；
- 用 bitmap 或最终排序验证恰好一次消费；
- 将容量缩到 2/4，增加 position 高频环绕；
- 在“CAS 认领 slot”和“sequence release 发布”之间主动 `yield`，观察队头停顿；
- 在 x86-64 与可用的 ARM64 环境分别运行压力测试；
- 不把教学骨架用于生产，列出选用成熟库前要核对的 progress guarantee 与回收策略。

**🧪 题 5：制造并消除读写者饥饿**

测试三种锁：

```cpp
ReaderPreferredRWLock reader_first;
WriterPreferredRWLock writer_first;
FairRWLock fair;
```

要求：

- 场景 A：持续启动/循环 reader，周期性加入 writer，测 writer 最大等待时间；
- 场景 B：持续 writer 流量，周期性加入 reader，测 reader 最大等待时间；
- 对比三种锁的 throughput、p99/p999 wait time；
- 验证 reader-first 的 writer starvation 倾向和 writer-first 的 reader starvation 倾向；
- 验证 fair lock 入队后不会被后来请求无限超越；
- 和目标平台的 `std::shared_mutex` 对比，但不要预设其公平策略。

**🧪 题 6：观察 prethreaded server 与 one loop per thread**

实现或复用两个 echo server：

```text
A：固定 4 个 blocking workers + bounded connfd queue
B：固定 4 个 EventLoop + nonblocking socket + epoll
```

要求：

- 建立 1000 条保持空闲的长连接，对比线程数和内存；
- 让四条连接发送半行后停住，观察 A 的 worker 是否被占满；
- 对 B 验证空闲/半包连接不会占住独立线程；
- 给 B 的 callback 加两秒 sleep，验证同一 loop 上的其他连接仍会被拖慢；
- 将慢任务交给 bounded worker pool，再通过 MPSC completion queue + `eventfd` 投回原 loop。
