# §12.7 其他并发问题

这一节的主线是：**同步原语只能提供构建块，真正可复用的并发软件还必须把线程安全、调用状态、返回对象所有权、调用时序和锁顺序写进函数与 API 契约。** 一个函数内部“加了锁”不代表它可重入，几个各自线程安全的方法拼在一起也不代表完整业务操作具有原子性；使用遗留库函数时，还要追问它是否隐藏了静态状态、返回的地址由谁拥有，以及后续调用会不会覆盖结果。

> 本节按教材主线依次讨论 thread safety、reentrancy、在线程程序中使用已有库函数、races 和 deadlocks。[§12.4](../12.4/summary.md) 已解释 data race、happens-before 与对象生命周期，[§12.5](../12.5/summary.md) 已系统展开同步错误、死锁、饥饿和公平队列；这里重点把这些机制提升为函数/API 级设计规则，不重复同步原语实现。

---

## 线程安全与四类线程不安全函数

**🎯 线程安全描述并发调用时的函数语义**

如果一个函数被多个并发线程反复调用时，仍始终产生正确结果，就称它是线程安全的（thread-safe）。这里的“正确”不只意味着没有崩溃，还包括：

- 共享状态不被破坏；
- 每次调用返回属于该调用的正确结果；
- 返回对象在承诺的生命周期内仍有效；
- 函数声明的不变量、顺序与错误语义都成立。

例如下面的计数函数不是线程安全的：

```cpp
int next_id = 0;

int allocate_id() {
    return next_id++;
}
```

两个线程可能都读取同一个旧值，既产生 C++ data race，也可能发出重复 id。若 id 只是独立计数，可以使用 atomic：

```cpp
#include <atomic>

std::atomic<int> next_id{0};

int allocate_id() {
    return next_id.fetch_add(1, std::memory_order_relaxed);
}
```

但如果分配 id 还要同步更新多个容器或持久化状态，单个 atomic 不足以维护复合不变量，仍要把完整事务放进同一同步边界。

**🎯 第一类：没有保护共享变量**

第一类线程不安全函数直接读写共享可变状态，却没有建立互斥或原子协议：

```cpp
#include <unordered_map>

std::unordered_map<int, int> hits;

void record_hit(int key) {
    ++hits[key];  // 并发修改 unordered_map：未定义行为
}
```

修复方式取决于业务语义：

- 独立标量计数可用合适的 atomic；
- 容器和复合不变量通常用 mutex 保护完整状态转换；
- 高频统计可先用 per-thread counter，再周期性归约；
- 能用单线程所有权和消息投递消除共享时，往往比到处补锁更容易证明。

**🎯 第二类：跨多次调用保存隐式状态**

这类函数把一次逻辑操作的进度藏在 static/global 状态里。经典例子是传统 `strtok`：第一次传字符串，后续传 `nullptr` 继续解析；“当前位置”由函数内部保存。

```c
char text[] = "alpha,beta,gamma";

char *token = strtok(text, ",");
while (token != NULL) {
    puts(token);
    token = strtok(NULL, ",");
}
```

如果两个线程交错解析两个字符串，第二个线程的第一次调用会覆盖第一个线程的隐藏进度，后续 `strtok(NULL, ...)` 可能沿着另一个字符串继续。修复方向是把状态显式交给调用者，例如 POSIX `strtok_r` 的 `saveptr`：

```c
char *saveptr = NULL;
for (char *token = strtok_r(text, ",", &saveptr);
     token != NULL;
     token = strtok_r(NULL, ",", &saveptr)) {
    puts(token);
}
```

每个并发解析任务拥有自己的 `saveptr`，调用链不再争用一份隐式游标。

**🎯 第三类：返回指向静态存储区的指针**

有些函数把结果写到内部 static buffer，然后返回它的地址：

```cpp
#include <cstdio>

const char* format_id(int id) {
    static char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "id=%d", id);
    return buffer;
}
```

两个线程并发调用会写同一个数组；即使给函数内部加锁，返回的指针在解锁后仍指向共享 buffer，下一次调用可以立即覆盖前一次结果。

更可靠的接口让结果所有权属于调用者：

```cpp
#include <cstdio>
#include <span>

bool format_id(int id, std::span<char> output) {
    if (output.empty()) {
        return false;
    }
    const int n = std::snprintf(output.data(), output.size(), "id=%d", id);
    return n >= 0 && static_cast<std::size_t>(n) < output.size();
}
```

```cpp
char buffer[32];
if (format_id(42, buffer)) {
    use(buffer);
}
```

这里每个调用者提供独立 buffer；接口还明确暴露容量，避免隐藏截断。

**🎯 第四类：调用了其他线程不安全函数**

函数自身没有显式 global/static 变量，也可能因为依赖链而不安全：

```cpp
std::string describe_now() {
    const std::time_t now = std::time(nullptr);
    const std::tm* tm = std::localtime(&now);  // 可能返回共享静态对象
    return tm == nullptr ? std::string{} : format(*tm);
}
```

判断 thread safety 必须沿调用图向下检查。修复方法不是给外层函数贴一个注释，而是：

- 换用线程安全替代接口；
- 让依赖把状态和输出存进调用者对象；
- 或用所有调用方共同遵守的同一把锁封装，并在锁内复制结果。

如果程序其他位置仍直接调用不安全依赖，某一个局部 wrapper 的 mutex 无法提供进程级保证。

**⚠️ 四类问题的修复方式不能机械互换**

| 类别 | 根因 | 首选修复 |
|---|---|---|
| 未保护共享变量 | 多线程访问同一可变状态 | 消除共享、mutex、atomic 或单线程所有权 |
| 跨调用保存状态 | 调用进度藏在函数内部 | 显式 context，由每个调用者持有 |
| 返回静态对象地址 | 多次调用复用同一结果存储 | 调用者输出缓冲区或按值返回 |
| 调用不安全依赖 | 安全性被调用图中的函数破坏 | 替换依赖，或完整封装并复制结果 |

一个全局 mutex 可以把部分旧接口暂时串行化，却会限制并行度，也不能自动修复解锁后仍暴露内部地址的问题。

---

## 可重入函数与显式调用状态

**🎯 可重入函数不依赖跨调用共享可变状态**

可重入（reentrant）描述的是：一个函数尚未完成时，另一次调用进入同一函数，不会让两次调用因共享可变状态而互相破坏。它通常通过局部变量、按值参数和调用者拥有的状态完成工作。

下面的函数只读取输入范围并使用自动局部变量；只要调用者不在其他线程并发修改该数组，多次调用彼此独立：

```cpp
#include <cstddef>
#include <cstdint>
#include <span>

std::uint64_t sum_range(std::span<const std::uint32_t> values) {
    std::uint64_t total = 0;
    for (const auto value : values) {
        total += value;
    }
    return total;
}
```

它不需要内部 mutex，每次调用的 `total` 都是独立实例，因此天然适合并行分块。

**🎯 可重入函数是线程安全函数的重要子集**

线程安全函数可以靠内部锁保护共享状态：

```cpp
std::mutex mutex;
int value = 0;

int increment_and_get() {
    std::lock_guard lock(mutex);
    return ++value;
}
```

不同线程调用它不会并发破坏 `value`，因此它可以是线程安全的；但如果函数持有非递归 mutex 时，经 callback 或其他调用路径再次进入自身，就会等待自己已经持有的锁：

```text
increment_and_notify
  ├─ lock(mutex)
  ├─ 更新 value
  └─ callback()
        └─ increment_and_notify()
              └─ lock(mutex)  -> 自锁死锁
```

所以“线程安全”不推出“可重入”。反过来，不访问跨调用共享可变状态的可重入函数通常天然线程安全。

**🎯 显式可重入与依赖调用者约束的可重入**

可以进一步区分：

- **显式可重入**：参数按值传递，数据引用只指向本次调用的自动局部状态，不需要调用者提供额外并发约束；
- **隐式可重入**：函数接收指针或引用，只有调用者保证不同调用不共享可变对象时才可安全并发。

例如：

```cpp
void increment(int& value) {
    ++value;
}
```

两个线程分别传入不同 `int` 时没有共享；若都传同一个 `int`，就形成 data race。函数签名本身不能替调用者证明实参是否别名，因此 API 文档必须说明参数的共享和同步要求。

**🎯 `strtok_r` 把隐藏游标变成每次解析的 context**

```c
void parse(char *text) {
    char *saveptr = NULL;

    for (char *token = strtok_r(text, ",", &saveptr);
         token != NULL;
         token = strtok_r(NULL, ",", &saveptr)) {
        consume(token);
    }
}
```

`saveptr` 属于这次 `parse` 调用；不同线程各自拥有 `text` 和 `saveptr` 时，解析状态不会串线。这种“把隐藏 global/static 状态改成显式 context”是改造 parser、codec、随机数生成器和协议状态机的通用方法。

**⚠️ 可重入不等于 async-signal-safe**

POSIX 信号处理器可能在任意指令边界异步打断线程，除了共享状态，还涉及 allocator、stdio、内部锁等正在处于不一致中间状态的问题。`async-signal-safe` 是针对信号处理器的独立约束，必须按 POSIX 白名单选函数；不能仅凭“看起来可重入”就从 handler 调用。相关机制见 [§8.5 信号](../../Chapter8/8.5-8.6/summary.md)。

---

## 在线程程序中使用现有库函数

**🎯 先查接口契约，不按函数名猜安全性**

使用库函数前至少要确认四件事：

1. 它是否读写进程级或对象级共享状态；
2. 多线程并发调用同一对象和不同对象分别允许什么；
3. 返回值是副本、调用者对象，还是指向内部存储的指针/引用/迭代器；
4. 后续调用、容器修改或对象销毁会不会使返回结果失效。

Linux/glibc 的 man page 常在 `ATTRIBUTES` 表中标注 `MT-Safe` 等属性，可以结合 `attributes(7)` 阅读：

```bash
man 3 localtime
man 7 attributes
```

但 `MT-Safe` 只说明文档定义范围内的多线程调用安全，不代表函数可重入、async-signal-safe，也不代表返回对象可以无限期保存。

**🎯 优先选择调用者提供结果存储的接口**

传统 `localtime` 返回指向静态 `struct tm` 的指针；POSIX `localtime_r` 把结果写进调用者对象：

```cpp
#include <ctime>
#include <optional>

std::optional<std::tm> local_time(std::time_t value) {
    std::tm result{};
    if (::localtime_r(&value, &result) == nullptr) {
        return std::nullopt;
    }
    return result;
}
```

调用者拿到按值返回的 `std::tm`，其生命周期不再依赖库内静态 buffer：

```cpp
if (const auto tm = local_time(std::time(nullptr))) {
    use(*tm);
}
```

`_r` 是许多 POSIX 遗留接口的重入版本命名惯例，但不是 C/C++ 跨平台的统一规则；应以目标平台文档为准，并优先采用语义更清楚的现代替代接口。例如名称解析应优先使用本身以调用者结果链表表达状态的 `getaddrinfo`，而不是继续包装早期返回静态结果的 `gethostbyname`。

**🎯 无替代接口时，锁必须覆盖“调用并复制结果”**

兼容旧接口时可以用共同 mutex 串行化，并在锁内立即把静态结果复制到调用者拥有的对象：

```cpp
#include <ctime>
#include <mutex>
#include <optional>

std::optional<std::tm> local_time_compat(std::time_t value) {
    static std::mutex mutex;
    std::lock_guard lock(mutex);

    const std::tm* shared = std::localtime(&value);
    if (shared == nullptr) {
        return std::nullopt;
    }
    return *shared;  // 在解锁前复制，返回独立对象
}
```

下面的 wrapper 仍然错误：

```cpp
const std::tm* bad_local_time(std::time_t value) {
    static std::mutex mutex;
    std::lock_guard lock(mutex);
    return std::localtime(&value);  // 解锁后泄露共享静态对象
}
```

即使调用动作被串行化，返回后下一次调用仍能覆盖前一个线程正在读取的对象。还要注意：如果 `std::localtime`、`std::gmtime` 等函数共享底层存储，那么程序中所有相关调用都必须遵守同一封装纪律；不同 wrapper 各用一把 mutex 仍可能互相破坏。

**⚠️ 标准容器的线程安全保证不是“容器自动加锁”**

不同线程操作不同容器对象通常彼此独立；多个线程只读同一稳定对象通常也可以。但如果一个线程修改 `std::vector`，另一个线程同时读取它，即使读取的是先前保存的指针或迭代器，也可能同时踩中 data race 与重新分配失效：

```cpp
std::vector<int> values{1, 2, 3};
const int* first = values.data();

// thread A
values.push_back(4);  // 可能重分配并修改容器状态

// thread B
use(*first);          // 可能并发访问且 first 已悬空
```

线程安全不仅是库函数内部有没有锁，还包括调用者是否遵守同一对象的并发访问规则与返回地址生命周期。

**🔧 wrapper 应把所有权和锁边界写进类型**

比起返回裸内部指针，更好的并发接口通常选择：

- 按值返回不可变快照；
- 让调用者提供输出对象；
- 提供一次完成完整状态转换的方法；
- 用 RAII guard 表达“引用只在持锁期间有效”；
- 明确 callback 是锁内还是锁外执行。

接口若只返回 `T*`/`T&`，却不说明谁拥有对象、何时失效以及访问需要哪把锁，就把最关键的并发协议留给调用者猜。

---

## 竞争：调用时序也是 API 契约

**🎯 race 是结果错误地依赖线程推进顺序**

教材中的经典错误是把循环变量 `i` 的同一地址传给所有新线程：

```c
#include <pthread.h>
#include <stdio.h>

void *worker(void *arg) {
    int id = *(int *)arg;
    printf("worker %d\n", id);
    return NULL;
}

int main(void) {
    enum { N = 4 };
    pthread_t threads[N];
    int i;

    for (i = 0; i < N; ++i) {
        pthread_create(&threads[i], NULL, worker, &i);  // 错：同一个 i
    }
    for (i = 0; i < N; ++i) {
        pthread_join(threads[i], NULL);
    }
}
```

主线程继续修改 `i`，worker 在不确定时刻解引用 `&i`；它们可能打印重复 id、越界值，而且并发读写同一个非 atomic `int` 本身就是 data race。错误不在 `pthread_create`，而在参数对象没有独立 identity、稳定 lifetime 和同步协议。

一种修复是给每个 worker 独立且稳定的参数槽：

```c
pthread_t threads[N];
int ids[N];

for (int i = 0; i < N; ++i) {
    ids[i] = i;
    pthread_create(&threads[i], NULL, worker, &ids[i]);
}
for (int i = 0; i < N; ++i) {
    pthread_join(threads[i], NULL);
}
```

`ids[i]` 在对应 worker 结束前一直存活，且主线程创建后不再修改该元素。C++ 中同样应优先捕获值：

```cpp
for (int id = 0; id < 4; ++id) {
    threads.emplace_back([id] { process(id); });
}
```

**🎯 单个方法线程安全，不代表调用序列原子**

假设 `empty()` 和 `pop()` 各自在内部加锁：

```cpp
if (!queue.empty()) {  // 调用结束后锁已释放
    auto item = queue.pop();
}
```

可能发生：

```text
thread A: empty() -> false
thread B: pop()   -> 取走最后一个元素
thread A: pop()   -> 队列已空
```

每个成员函数都没有 data race，完整的 check-then-act 却仍有竞态。更好的接口把检查和状态转换合并：

```cpp
std::optional<T> try_pop() {
    std::lock_guard lock(mutex_);
    if (queue_.empty()) {
        return std::nullopt;
    }
    T value = std::move(queue_.front());
    queue_.pop();
    return value;
}
```

API 设计应尽量让调用者直接表达“尝试取一个元素”，而不是要求调用者用多个瞬时查询拼装协议。具体 race condition、atomicity violation 与临界区证明见 [§12.4](../12.4/summary.md) 和 [§12.5](../12.5/summary.md)。

**⚠️ TSan 能发现 data race，不能证明没有业务竞态**

把所有字段改成 atomic 或给每个方法独立加锁后，ThreadSanitizer 可能保持安静，但“余额检查后扣款”“empty 后 pop”“对象状态检查后使用”等多步不变量仍可能被其他线程插入。动态工具只能覆盖被执行到的轨迹，API 的原子语义仍需要设计和证明。

---

## 死锁：从锁顺序提升到接口边界

**🎯 两条正确的局部加锁路径可能组成全局等待环**

下面的转账函数总是先锁 `from`、再锁 `to`：

```cpp
#include <mutex>

struct Account {
    std::mutex mutex;
    int balance{};
};

void transfer_bad(Account& from, Account& to, int amount) {
    std::lock_guard first(from.mutex);
    std::lock_guard second(to.mutex);
    from.balance -= amount;
    to.balance += amount;
}
```

若线程 A 执行 `transfer_bad(x, y, 10)`，线程 B 同时执行 `transfer_bad(y, x, 20)`：

```text
thread A 持有 x.mutex，等待 y.mutex
thread B 持有 y.mutex，等待 x.mutex
```

两条线程都不会继续推进。局部看每次访问都受锁保护，全局协议却没有统一锁顺序。

C++ 可以用 `std::scoped_lock` 对多把 mutex 做无死锁加锁：

```cpp
void transfer(Account& from, Account& to, int amount) {
    if (&from == &to) {
        return;
    }

    std::scoped_lock lock(from.mutex, to.mutex);
    from.balance -= amount;
    to.balance += amount;
}
```

另一种通用策略是定义全局锁层级，例如总按对象稳定 id 或地址顺序加锁；关键不是选哪一种语法，而是所有调用路径遵守同一个协议。

**🎯 未知 callback 不应在内部锁下执行**

下面的通知函数持锁遍历 callback：

```cpp
void Registry::notify(const Event& event) {
    std::lock_guard lock(mutex_);
    for (const auto& callback : callbacks_) {
        callback(event);  // 未知代码可能阻塞、加其他锁或重入 Registry
    }
}
```

如果 callback 再次调用 `Registry`，可能自锁；如果 callback 先获取另一把锁，而其他线程以相反顺序进入 `Registry`，就可能形成跨模块死锁。更稳妥的模式是在锁内复制稳定快照，锁外执行未知代码：

```cpp
void Registry::notify(const Event& event) {
    std::vector<Callback> callbacks;
    {
        std::lock_guard lock(mutex_);
        callbacks = callbacks_;
    }

    for (const auto& callback : callbacks) {
        callback(event);
    }
}
```

这还需要明确快照语义和 callback 捕获对象的生命周期：解锁后 callback 列表稳定，不代表它引用的所有外部对象自动存活。

**⚠️ 锁是模块间协议，不只是实现细节**

并发 API 至少应文档化：

- 方法会不会阻塞；
- 调用时是否要求调用者已持有某把锁；
- 内部会按什么顺序获取多把锁；
- 是否会在锁内调用外部代码；
- 返回的引用、guard 或 iterator 在何时失效；
- 对象销毁能否与进行中的调用并发。

完整的死锁四条件、进度图、饥饿和公平策略见 [§12.5](../12.5/summary.md#死锁的四个必要条件与工程规避)。本节的关键结论是：**死锁通常跨越函数和模块边界，只有统一的锁层级与调用契约才能全局规避。**

---

## 易错点

- 认为函数内部没有 global 变量就一定线程安全是错的，它还可能访问堆上共享对象、返回静态结果或调用其他线程不安全函数。
- 认为没有 data race 就证明 API 正确是错的，多步 check-then-act 可以只使用 atomic 或独立加锁方法却仍违反业务不变量。
- 把线程安全等同于可重入是错的，内部加锁能串行化多线程调用，但函数在持锁期间再次进入可能自锁。
- 认为换成 `recursive_mutex` 就获得完整可重入性是错的，递归加锁不保证共享状态在 callback 重入时处于可观察的一致状态。
- 看到 `_r` 后缀就假定所有平台都提供同样保证是错的，它是常见 POSIX 命名惯例，仍需查具体接口与目标平台文档。
- 给返回静态指针的函数加锁、随后原样返回该指针是错的，解锁后的下一次调用仍可覆盖旧结果。
- 认为每个容器成员函数都可调用就能并发修改同一个容器是错的，同一对象的修改、迭代器失效和元素生命周期仍需外层协议。
- 把循环变量 `&i` 传给所有线程是错的，worker 共享同一参数对象且读取时机不确定，应使用独立参数槽或按值捕获。
- 认为多个各自线程安全的方法可以任意组合是错的，跨调用不变量必须由一个原子业务 API 或调用者持有的同一锁覆盖。
- 认为不发生死锁就保证每个请求都能完成是错的，livelock 和 starvation 仍可能让系统忙碌却没有个体进展。

---

## 工程关联

- 封装遗留 C API 时优先让调用者拥有 context 和输出缓冲区，并按值返回快照，避免把 static buffer、裸引用和隐式游标泄露到并发边界外。
- 时间格式化、用户/组查询、名称解析、分词和随机数生成是审查隐式状态的高频区域；Linux 上可结合 man page 的 `ATTRIBUTES` 与 `attributes(7)` 核对。
- C++ 容器本身不会替业务协议加锁；返回 iterator、`data()`、`string_view` 或内部引用后，任何并发修改和对象销毁都必须重新审查生命周期。
- “锁内调用 callback”是代码审查中的高风险模式；通常应在锁内复制状态与回调快照，在锁外调用未知代码，并明确取消订阅与对象销毁语义。
- 线程池会复用工作线程，因此 `thread_local` 状态会跨任务保留；请求 id、认证上下文和临时缓存若不显式清理，可能污染下一项任务。
- ThreadSanitizer 擅长发现实际执行路径中的 data race，却不能证明线程安全、可重入、无死锁或无逻辑竞态；还要配合不变量审查、压力测试和锁依赖分析。
- 死锁排查可用 `gdb -p <pid>` 后执行 `thread apply all bt` 查看线程分别卡在哪把锁；线上还应建立全局锁顺序并缩小锁内未知调用。
- POSIX `async-signal-safe` 与 thread-safe/reentrant 不能互相替代；signal handler 只能使用明确允许的接口，并把复杂工作交回正常线程上下文。
- 一把进程级大锁可以快速兼容旧库，却会把所有调用串行化；应先保证正确，再由 profile 判断是否值得改成显式 context 或分片状态。

---

## 实验题

**🧪 题 1：对比 `strtok` 与 `strtok_r` 的调用状态**

不安全版本核心：

```c
#define _POSIX_C_SOURCE 200809L
#include <pthread.h>
#include <stdio.h>
#include <string.h>

pthread_barrier_t barrier;

void *parse_unsafe(void *arg) {
    char *text = arg;
    char *first = strtok(text, ",");

    pthread_barrier_wait(&barrier);

    char *second = strtok(NULL, ",");
    printf("%s -> %s\n", first, second != NULL ? second : "NULL");
    return NULL;
}
```

安全版本核心：

```c
void *parse_safe(void *arg) {
    char *text = arg;
    char *saveptr = NULL;
    char *first = strtok_r(text, ",", &saveptr);

    pthread_barrier_wait(&barrier);

    char *second = strtok_r(NULL, ",", &saveptr);
    printf("%s -> %s\n", first, second != NULL ? second : "NULL");
    return NULL;
}
```

要求：

- 初始化两个**可修改数组** `char text1[] = "A1,A2,A3";`、`char text2[] = "B1,B2,B3";`（`strtok`/`strtok_r` 会原地写入 `\0`，不能传字符串字面量），再执行 `pthread_barrier_init(&barrier, NULL, 2)` 并让两个线程分别解析；
- 分别运行 unsafe 与 safe 版本，编译选项为 `cc -std=c17 -O1 -Wall -Wextra -Wpedantic -pthread`；
- 重复运行并记录 unsafe 版本是否出现 token 串线、重复、`NULL` 或其他不稳定结果，不能因一次“碰巧正确”就判定安全；
- 画出两次首调用怎样覆盖同一个隐式游标，并说明 safe 版本为何由两个独立 `saveptr` 隔离状态；
- 不要求 TSan 必然报告 libc 内部实现，实验结论以 API 契约和可观察结果为准。

**🧪 题 2：消除静态结果缓冲区**

源码：

```c
#include <stddef.h>
#include <stdio.h>

const char *format_id_unsafe(int id) {
    static char buffer[32];
    snprintf(buffer, sizeof(buffer), "id=%d", id);
    return buffer;
}

int format_id_safe(int id, char *output, size_t capacity) {
    if (capacity == 0) {
        return 0;
    }
    int n = snprintf(output, capacity, "id=%d", id);
    return n >= 0 && (size_t)n < capacity;
}
```

要求：

- 启动两个线程高频调用 `format_id_unsafe`，保存返回地址和值，并验证两个线程得到的地址相同；
- 在“函数返回”和“复制字符串”之间加入 barrier 或 `sched_yield()`，放大结果被另一线程覆盖的窗口；
- 改用每线程独立 `char output[32]` 调用 safe 版本，校验地址、内容和生命周期均独立；
- 再尝试只给 unsafe 函数内部加 mutex，解释为什么原样返回指针后保护已经结束；
- 使用 `-fsanitize=thread` 辅助观察 data race，同时说明 sanitizer 安静也不能改变静态地址的所有权缺陷。

**🧪 题 3：复现循环变量参数竞态**

错误版本：

```c
#include <pthread.h>
#include <stdio.h>

void *worker(void *arg) {
    printf("%d\n", *(int *)arg);
    return NULL;
}

int main(void) {
    enum { N = 8 };
    pthread_t threads[N];
    int i;

    for (i = 0; i < N; ++i) {
        pthread_create(&threads[i], NULL, worker, &i);
    }
    for (i = 0; i < N; ++i) {
        pthread_join(threads[i], NULL);
    }
}
```

要求：

- 用 `cc -std=c17 -O1 -g -Wall -Wextra -Wpedantic -pthread` 编译并重复运行，记录重复、遗漏或越界 id；
- 用 `-fsanitize=thread` 重新编译，定位主线程写 `i` 与 worker 读 `i` 的 data race；
- 修复为 `int ids[N]`，每个线程接收 `&ids[i]`，并保证数组活到所有 `join` 完成；
- 再写一个 C++20 版本，对比 lambda 的 `[&id]` 与 `[id]` 捕获；
- 用 identity、ownership、lifetime、ordering 四个词解释修复为何成立，而不只说“加一点 sleep”。

**🧪 题 4：制造并消除双锁死锁**

错误版本用 barrier 保证两条线程都先拿到第一把锁：

```cpp
#include <barrier>
#include <mutex>
#include <thread>

struct Account {
    std::mutex mutex;
    int balance{100};
};

std::barrier both_locked{2};

void transfer_bad(Account& from, Account& to, int amount) {
    std::lock_guard first(from.mutex);
    both_locked.arrive_and_wait();
    std::lock_guard second(to.mutex);
    from.balance -= amount;
    to.balance += amount;
}
```

正确版本：

```cpp
void transfer(Account& from, Account& to, int amount) {
    if (&from == &to) {
        return;
    }
    std::scoped_lock lock(from.mutex, to.mutex);
    from.balance -= amount;
    to.balance += amount;
}
```

要求：

- 分别启动 `transfer_bad(a, b, 10)` 与 `transfer_bad(b, a, 20)`，用 `g++ -std=c++20 -O1 -g -Wall -Wextra -Wpedantic -pthread` 编译；
- 先用 `timeout 3s ./deadlock_bad` 验证错误版本不会自行结束，并画出 `a.mutex -> b.mutex -> a.mutex` 等待环；
- 另一次调试时执行 `./deadlock_bad & pid=$!` 保存 PID，再用另一终端运行 `gdb -p "$pid"` 和 `thread apply all bt` 观察两条线程的阻塞栈，退出 GDB 后用 `kill "$pid"` 清理进程；
- 改用 `std::scoped_lock`，去掉只为制造错误轨迹而设的 barrier，重复运行并校验总余额保持 200；
- 额外设计统一 lock hierarchy 版本，并说明为什么“每个函数都记得先锁自己的对象”不是全局锁顺序。
