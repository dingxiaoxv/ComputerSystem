# 专题：libcurl —— 在 C++ 里做 HTTP 客户端

> 挂在第 11 章的补充专题，不是书里的正式小节。
>
> **为什么放第 11 章**：第 11 章我们亲手用 `socket`→`connect`→`getaddrinfo`→RIO 循环写了 echo 客户端和 Tiny 服务器。libcurl 就是把这一整套（DNS 解析 + 建连 + read/write 收发 + 协议解析）封装成一个稳定库，再加上 HTTP/HTTPS/HTTP2、TLS、重定向、cookie、连接复用。**理解了第 10-11 章，libcurl 的每一个 `setopt` 你都能对应到底层哪一步**——这份文档的目标就是把「库的 API」和「你已经懂的 syscall」接起来。
>
> **配套代码**：`experiments/http_client.cpp`（RAII 封装的 `HttpClient`，含 GET/POST JSON/自定义 header/错误处理），`experiments/Makefile` 里 `make http_client`。
>
> **本机验证说明**：本机已安装 `libcurl4-openssl-dev`（libcurl v8.5.0，OpenSSL flavour）。Ubuntu 24.04 的头文件位于 multiarch 路径 `/usr/include/x86_64-linux-gnu/curl/curl.h`，`g++ ... -lcurl` 能自动找到。`experiments/http_client.cpp` 已在本机用 `make http_client` 实际编译链接通过，生成 x86-64 ELF 可执行文件并动态链接 `/lib/x86_64-linux-gnu/libcurl.so.4`。当前网络把 `neverssl.com` 解析到 `198.18.0.156` 且 HTTP body 超时不返回，因此运行样例会走到「传输失败：超时」分支；这正好验证了错误处理路径。

---

## 1. 一句话：libcurl 是什么

libcurl 是 curl 命令行工具背后的 C 库，做**客户端**的 URL 数据传输。支持 HTTP/HTTPS/FTP/SMTP 等一堆协议，但 90% 的 C++ 后端用它只做一件事：**发 HTTP 请求、拿响应**（调别人的 REST 接口、传文件、做健康检查）。

它是 C 库，没有 C++ 接口——所以「在 C++ 里用 libcurl」的核心工作就是：**给这套 C 的 handle + setopt + 回调，套一层 RAII，把资源管理和错误处理的样板收干净**（和 §8 里给 UDS 套 `unix_socket.hpp` 是同一种活）。

```
   你的 C++ 代码
        │  curl_easy_setopt(...) / curl_easy_perform(...)
        ▼
   ┌─────────────────────────────────────┐
   │            libcurl                   │
   │  URL 解析 → getaddrinfo(DNS)         │  ← 第 11 章 §11.4
   │  → socket()/connect()                │  ← 第 11 章 §11.4
   │  → TLS 握手(OpenSSL)                  │
   │  → write(请求) / read(响应) 循环      │  ← 第 10 章 RIO 干的活
   │  → 解析 HTTP 状态行/头/body           │
   └─────────────────┬───────────────────┘
                     ▼  回调把 body 一段段喂给你
              你的 write callback → std::string
```

---

## 2. 三层 API：先认清自己该用哪层

libcurl 有三套接口，别一上来就纠结，**99% 的场景用 easy 就够**：

| 层 | 头 | 定位 | 什么时候用 |
|----|-----|------|-----------|
| **easy** | `curl_easy_*` | **同步阻塞**，一个 handle 发一个请求，`perform` 一路阻塞到收完 | 主线，绝大多数场景 |
| **multi** | `curl_multi_*` | **单线程内并发多个传输**，非阻塞、事件驱动（配 `select`/`epoll`） | 一个线程要同时打十几个请求、又不想开十几个线程 |
| **share** | `curl_share_*` | 让多个 easy handle **共享** DNS 缓存/cookie/连接池 | 高频调同一批域名，想复用 DNS/连接，进阶优化 |

- 🎯 **easy 是阻塞的**：`curl_easy_perform` 会一直卡到请求完成或超时，语义就像你在第 11 章写的 `open_clientfd` + 收发循环。要并发，最简单的做法是**每线程一个 easy handle**（handle 不能跨线程共享，见 §9）。
- 🎯 **multi 才是「单线程高并发」正解**：`curl_multi_perform` 推进所有传输一小步就返回，你用 `curl_multi_poll` 等 fd 就绪——这就是 libcurl 版的 §12 I/O 多路复用。本文聚焦 easy，multi 知道存在即可。

---

## 3. easy handle 的生命周期：七步

一个最小 GET 请求的完整调用序列，每一步都要看懂：

```c
curl_global_init(CURL_GLOBAL_DEFAULT);   // ① 进程级：初始化全局状态（TLS 库等），非线程安全
CURL *h = curl_easy_init();              // ② 创建一个 easy handle（不透明句柄）
curl_easy_setopt(h, CURLOPT_URL, "https://example.com");  // ③ 配置：设 N 个选项
curl_easy_setopt(h, CURLOPT_WRITEFUNCTION, cb);           //    （URL、回调、超时、header…）
curl_easy_setopt(h, CURLOPT_WRITEDATA, &buf);
CURLcode rc = curl_easy_perform(h);      // ④ 执行：阻塞直到完成，返回传输层结果码
long code;
curl_easy_getinfo(h, CURLINFO_RESPONSE_CODE, &code);      // ⑤ 事后取信息：HTTP 状态码/耗时/大小
curl_easy_cleanup(h);                    // ⑥ 销毁 handle
curl_global_cleanup();                   // ⑦ 进程退出前：清理全局状态
```

- 🎯 **① `curl_global_init` 管进程、② `curl_easy_init` 管一次会话**。全局 init 初始化底层 TLS 库、Win socket 等，**整个进程只调一次**；easy handle 才是你反复创建/销毁的东西。
- ⚠️ **① 非线程安全，必须在 `main` 早期、单线程时调**。如果你不显式调，`curl_easy_init` 会替你调一次——但那是在**不保证单线程**的时机，多线程程序会踩数据竞争。所以规矩是：`main` 一开始就 `curl_global_init`，退出前 `curl_global_cleanup`。配套代码用一个 `CurlGlobal` RAII 对象兜住这对调用。
- 🎯 **③ `setopt` 是可变参数**：第三个参数的类型由第二个选项枚举决定——`long`、`char*`、函数指针、`void*` 都有。传错类型是运行期 UB，编译器**不会**帮你查，这是最常见的低级错。设 `long` 选项要写 `1L` 别写 `1`。
- 🎯 **⑤ `getinfo` 只能在 `perform` 之后调**：HTTP 状态码、DNS 耗时、下载字节数这些是传输**结果**，perform 前还不存在。
- 🎯 **handle 可复用**：`perform` 完不 cleanup，改几个 setopt 再 perform，能**复用底层 TCP 连接**（省一次 TCP 握手 + TLS 握手，这是 keep-alive）。配套 `HttpClient` 就是一个对象持有一个 handle 反复用。

---

## 4. 回调机制：libcurl 的心脏

这是 libcurl 最反直觉、也最关键的设计：**它不返回一个「响应字符串」给你，而是每收到一段数据就回调你一次**。因为响应可能有几个 GB，不可能全塞进一个返回值。三个方向三个回调：

| 选项 | 方向 | 作用 |
|------|------|------|
| `CURLOPT_WRITEFUNCTION` + `CURLOPT_WRITEDATA` | 收 | 每收到一段 **body** 调一次，你负责存起来 |
| `CURLOPT_HEADERFUNCTION` + `CURLOPT_HEADERDATA` | 收 | 每收到一行 **响应头** 调一次 |
| `CURLOPT_READFUNCTION` + `CURLOPT_READDATA` | 发 | libcurl 要发 **请求体** 时回调你要数据（上传/流式 POST 用） |

**🎯 写回调的固定签名**（记死它）：

```c
size_t write_cb(char *ptr, size_t size, size_t nmemb, void *userdata);
//               ▲收到的数据  ▲恒为1   ▲块数        ▲就是 WRITEDATA 传进来的指针
//   实际字节数 = size * nmemb（size 恒为 1 是历史遗留，别问）
```

**⚠️ 返回值语义是坑**：回调**必须返回「你已处理的字节数」**。libcurl 拿它和「递给你的字节数」比：

- 返回 `size*nmemb`（全收下）→ 继续传输；
- 返回**任何不等于** `size*nmemb` 的值 → libcurl 认为写失败，**立即中断传输**，`perform` 返回 `CURLE_WRITE_ERROR`。

所以「返回 0」是你主动喊停的方式（比如内存不够、或回调里出异常想终止）。

**🎯 为什么默认打印到 stdout**：不设 `WRITEFUNCTION` 时，libcurl 用内置默认回调 `fwrite` 到 `stdout`——这就是为什么你 `curl_easy_perform` 一个 URL、body 直接刷屏。设了 `WRITEFUNCTION` 就覆盖掉这个默认行为。

**用 `std::string` 收 body 的标准写法**（配套代码就是这个）：

```cpp
static size_t write_to_string(char *ptr, size_t size, size_t nmemb, void *userdata) {
    size_t total = size * nmemb;
    try {
        auto *out = static_cast<std::string *>(userdata);
        out->append(ptr, total);      // 追加这一段
        return total;                 // 全收下 → 继续
    } catch (...) {
        return 0;                     // append 抛异常(OOM) → 返回 0 中断，异常绝不外泄
    }
}
// ...
std::string body;
curl_easy_setopt(h, CURLOPT_WRITEFUNCTION, write_to_string);
curl_easy_setopt(h, CURLOPT_WRITEDATA, &body);   // 回调里的 userdata 就是它
```

⚠️ **回调是 C 函数，C++ 异常绝对不能穿出去**。libcurl 是 C 代码，异常穿过 C 栈帧是未定义行为（栈展开找不到 landing pad，直接 `terminate` 甚至更糟）。凡是回调，**整段包 `try{...}catch(...)`**，把异常转成「返回 0」这个 libcurl 看得懂的中断信号。

---

## 5. C++ 的正事：RAII 封装

libcurl 给你的是裸 `CURL*` 和一堆必须配对的 init/cleanup。C++ 该做的是用 RAII 把这些配对关系交给编译器，杜绝忘 cleanup。三个要管的资源：

**🔧 ① `CURL*` handle → `unique_ptr` + 自定义 deleter**：

```cpp
struct CurlEasyDeleter {
    void operator()(CURL *h) const noexcept { if (h) curl_easy_cleanup(h); }
};
using EasyHandle = std::unique_ptr<CURL, CurlEasyDeleter>;

EasyHandle h(curl_easy_init());   // 出作用域自动 curl_easy_cleanup，异常安全
```

**🔧 ② 全局 init/cleanup → 栈上守卫对象**：

```cpp
class CurlGlobal {
public:
    CurlGlobal()  { if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK)
                        throw std::runtime_error("curl_global_init 失败"); }
    ~CurlGlobal() { curl_global_cleanup(); }
    CurlGlobal(const CurlGlobal&) = delete;            // 不可拷贝
    CurlGlobal& operator=(const CurlGlobal&) = delete;
};
// main 里第一行： CurlGlobal g;   —— 覆盖整个进程生命周期
```

**🔧 ③ `curl_slist`（header 链表）→ 也用 RAII 守卫**（见 §6）。

这套的价值和 §8 给 UDS 套 `uds::Fd` 一模一样：**协议/传输的本质省不掉，能省的是资源管理和错误处理的样板**。配套 `HttpClient` 把上述三样打包成一个类，对外只暴露 `get()` / `post_json()`，返回一个 `{status, body}` 结构体。

---

## 6. 常用请求怎么发

**🎯 GET**：默认就是 GET，设好 URL、回调即可：

```cpp
curl_easy_setopt(h, CURLOPT_URL, "https://api.example.com/users/1");
curl_easy_setopt(h, CURLOPT_HTTPGET, 1L);   // 显式声明（从 POST 切回来时需要）
curl_easy_perform(h);
```

**🎯 POST 表单**（`application/x-www-form-urlencoded`，libcurl 自动加这个 Content-Type）：

```cpp
curl_easy_setopt(h, CURLOPT_POSTFIELDS, "name=yanxu&age=30");
// 设了 POSTFIELDS 就自动变 POST，不用再设 CURLOPT_POST
```

**🎯 POST JSON**（现代 REST 主流，得**自己**设 Content-Type，body 是原始 JSON 串）：

```cpp
struct curl_slist *hdr = nullptr;
hdr = curl_slist_append(hdr, "Content-Type: application/json");
curl_easy_setopt(h, CURLOPT_HTTPHEADER, hdr);
curl_easy_setopt(h, CURLOPT_COPYPOSTFIELDS, R"({"name":"yanxu"})");  // 见下面 ⚠️
curl_easy_perform(h);
curl_slist_free_all(hdr);   // ⚠️ 必须 free，否则泄漏
```

- ⚠️ **`POSTFIELDS` vs `COPYPOSTFIELDS`**：`CURLOPT_POSTFIELDS` **只存指针不复制**，所以那块 body 内存必须**一直活到 `perform` 之后**——传一个局部 `std::string` 的 `.c_str()` 然后函数返回，就是悬垂指针。`CURLOPT_COPYPOSTFIELDS` 让 libcurl **复制一份**，调用方不用操心生命周期，代价是一次拷贝。**拿不准就用 COPY 版**。
- ⚠️ **`curl_slist` 是 C 链表，`curl_slist_append` 返回新头，必须 `curl_slist_free_all`**。忘了就泄漏。C++ 里用 `unique_ptr<curl_slist, decltype(&curl_slist_free_all)>` 守卫。

**🎯 自定义 header / 超时 / 重定向 / 拿状态码**：

```cpp
// 自定义/覆盖 header：加一行就是 append 一次；"Header:"（冒号后空）可删掉某个默认头
hdr = curl_slist_append(hdr, "Authorization: Bearer <token>");
hdr = curl_slist_append(hdr, "X-Request-Id: abc123");

curl_easy_setopt(h, CURLOPT_TIMEOUT, 10L);         // 整个请求最多 10 秒（含传输）
curl_easy_setopt(h, CURLOPT_CONNECTTIMEOUT, 5L);   // 只算「建立连接」阶段最多 5 秒
curl_easy_setopt(h, CURLOPT_FOLLOWLOCATION, 1L);   // 自动跟随 3xx 重定向（默认不跟）

long code;
curl_easy_getinfo(h, CURLINFO_RESPONSE_CODE, &code);   // perform 后取 HTTP 状态码
```

- ⚠️ **一定设超时**。libcurl 默认**没有**整体超时（`CURLOPT_TIMEOUT` 默认 0 = 永不超时），生产里对方 hang 住你就一起 hang。至少设 `TIMEOUT` + `CONNECTTIMEOUT`。
- 🎯 **`FOLLOWLOCATION` 默认关**。很多接口用 3xx 跳转，不开你只会拿到一个 302 和空 body。开了要注意配 `CURLOPT_MAXREDIRS` 防无限跳。

---

## 7. 错误处理：分清「传输层失败」和「HTTP 非 2xx」

这是新手最常搞混、也最影响正确性的一点：**`curl_easy_perform` 返回 `CURLE_OK` 只代表「HTTP 事务成功完成了」，不代表「业务成功」**。

```
curl_easy_perform 的 CURLcode        HTTP 状态码 (getinfo)
─────────────────────────────        ──────────────────────
CURLE_OK          传输成功  ────┬──►  200/201  真正成功
                                ├──►  404       传输成功，但资源不存在
                                └──►  500       传输成功，但服务端报错
CURLE_COULDNT_CONNECT 连不上         （没有状态码——根本没拿到响应）
CURLE_OPERATION_TIMEDOUT 超时        （没有状态码）
CURLE_COULDNT_RESOLVE_HOST DNS 失败  （没有状态码）
CURLE_SSL_CONNECT_ERROR TLS 握手失败 （没有状态码）
```

- 🎯 **两层错误要分别判**：
  1. **传输层**：`curl_easy_perform` 的 `CURLcode` != `CURLE_OK` → 连不上/DNS/超时/TLS 失败，**根本没有 HTTP 响应**。用 `curl_easy_strerror(rc)` 拿人话，或设 `CURLOPT_ERRORBUFFER` 拿更详细的上下文文案。
  2. **HTTP 层**：`CURLcode == OK` 但 `CURLINFO_RESPONSE_CODE` 是 4xx/5xx → 传输成功、业务失败。**libcurl 不把它当错误**（除非你设 `CURLOPT_FAILONERROR`，让 ≥400 直接令 perform 返回 `CURLE_HTTP_RETURNED_ERROR`）。
- 🔧 **配套代码的分工**：`HttpClient::perform` 把「传输层失败」抛 `std::runtime_error`（异常处理真·异常），把「HTTP 非 2xx」正常返回给调用方由 `resp.ok()` 判断（业务预期内的结果不该用异常）。这个划分很重要——**别把 404 也抛异常**。

```cpp
CURLcode rc = curl_easy_perform(h);
if (rc != CURLE_OK)                       // 传输层：真失败
    throw std::runtime_error(errbuf[0] ? errbuf : curl_easy_strerror(rc));
long status;
curl_easy_getinfo(h, CURLINFO_RESPONSE_CODE, &status);   // HTTP 层：交给调用方判 2xx
```

---

## 8. HTTPS / TLS：默认是安全的，别手贱关掉

访问 `https://` 时 libcurl 默认做**完整证书校验**，两个开关默认都开：

- `CURLOPT_SSL_VERIFYPEER = 1`：校验对端证书是否由**受信任 CA** 签发（防伪造证书）。
- `CURLOPT_SSL_VERIFYHOST = 2`：校验证书里的**域名**和你访问的主机名匹配（防拿别的合法证书冒充）。

⚠️ **网上一堆「连不上就把这俩设 0」的答案，等于关掉 HTTPS 的全部安全性**——中间人可以随便伪造证书、解密/篡改你的流量，HTTPS 退化成没加密。**正确的做法**：证书校验失败通常是**本机 CA 根证书过期/缺失**，装/更新 `ca-certificates`，或用 `CURLOPT_CAINFO` 指定 CA bundle 路径，而不是关校验。只有在**内网、自签证书、且你清楚风险**时才临时关，生产绝不关。

---

## 9. ⚠️ 易错点专区

- **`curl_global_init` 非线程安全**：必须 `main` 早期、单线程调一次。多线程程序若靠 `curl_easy_init` 隐式触发它，会数据竞争。用 `CurlGlobal` RAII 守卫。
- **一个 easy handle 不能跨线程并发**：handle 内含连接状态、缓冲，多线程同时 `perform` 同一个 handle 直接崩。**每线程各持一个 handle**（或用 `curl_share` 显式共享 DNS/连接池那部分）。
- **`curl_slist` 忘 `curl_slist_free_all`** → 内存泄漏。用 RAII 守卫兜。
- **回调里 C++ 异常穿出 C 栈是 UB**：所有回调（write/header/read）整段 `try/catch`，异常转成「返回 != 期望字节数」的中断信号。
- **`CURLOPT_POSTFIELDS` 不复制指针**：body 内存要活到 `perform` 后，否则悬垂。拿不准用 `CURLOPT_COPYPOSTFIELDS`。
- **`setopt` 第三参类型错**是运行期 UB，编译器不查：`long` 选项写 `1L`、字符串选项传 `const char*`、回调传函数指针，别混。
- **默认无超时**：不设 `CURLOPT_TIMEOUT`，对端 hang 住你就永久阻塞。
- **`FOLLOWLOCATION` 默认关**：接口用 302 跳转时不开会拿到空 body。
- **`perform` 返回 OK ≠ 业务成功**：4xx/5xx 要靠 `CURLINFO_RESPONSE_CODE` 单独判（§7）。

---

## 10. 🔧 工程关联：把 libcurl 拆回你懂的 syscall

- **libcurl 底层就是第 10-11 章的组合**：URL 解析 → `getaddrinfo`（§11.4 DNS）→ `socket`/`connect`（§11.4 建连）→ `write`/`read` 循环收发（§10 RIO 干的活）→ 解析 HTTP 状态行/头/body。你手写过一遍 echo 客户端，libcurl 只是把这套做到工业级 + 加 TLS/重定向/连接池。
- **`strace -e trace=network ./http_client <url>` 能看穿它**：会打出 `socket(AF_INET, SOCK_STREAM, ...)`、`connect(...)`、以及 DNS 查询的 UDP `sendto`/`recvfrom`——和你在 §11 echo 客户端里看到的一模一样。`strace -e trace=%network,read,write` 还能看到 body 的 `read` 短读（§10 short count 在这里也成立）。
- **`curl --libcurl code.c <url>`**：curl 命令行有个神仙选项，把「等价的 C 代码」直接生成出来。想不起某个功能对应哪个 `setopt`？用 curl 命令拼出来，加 `--libcurl` 看它生成什么。
- **`CURLOPT_VERBOSE = 1L`（或命令行 `curl -v`）**：把 DNS、连接、TLS 握手、请求头、响应头全打到 stderr，排查「为什么连不上/证书报错/header 没生效」的第一工具。
- **和 §8 UDS 传 fd 的对称性**：那边是给 socket syscall 套 `uds::Fd` RAII；这边是给 libcurl 的 C handle 套 `unique_ptr` RAII。**贴着 C 接口写现代 C++，套路是通用的**：不透明句柄 → `unique_ptr` + deleter，配对的 init/cleanup → 栈守卫对象，C 回调 → 挡住异常。

---

## 11. 🧪 实验题

**🧪 题 1：编译跑通配套代码 + 看穿底层 syscall**

```
cd Chapter11/experiments
sudo apt install libcurl4-openssl-dev     # 本机缺 -dev，先装
make http_client
./http_client https://httpbin.org/get
strace -e trace=network ./http_client https://httpbin.org/get 2>&1 | grep -E 'socket|connect'
```

要求：① 观察 GET 打出的 HTTP 状态码和 body；② 从 strace 输出里找出 `socket()` 和 `connect()`，确认目标端口是 443（HTTPS）；③ 换 `http://httpbin.org/get`（明文）再抓一次，对比 `connect` 端口变成 80。**结论**：libcurl 的一次 GET 就是你 §11 手写的 socket+connect。

**🧪 题 2：亲手制造并区分两类错误**

要求：分别构造并观察三种情况，确认代码走的是哪个分支——
1. `./http_client http://127.0.0.1:9/x`（没人监听的端口）→ 应打「传输失败: Couldn't connect」（传输层，走异常分支）；*（本机已实测通过这一条）*
2. 找一个返回 404 的 URL，如 `./http_client https://httpbin.org/status/404` → 应打「HTTP 状态码: 404 (非 2xx，业务失败)」（HTTP 层，**不**走异常分支）；
3. `./http_client https://expired.badssl.com/` → 应打「传输失败: SSL certificate problem」（TLS 校验失败，传输层）。

**结论**：把 §7 的两层错误模型对上号——只有 1、3 抛异常，2 是正常返回。

**🧪 题 3：验证证书校验的作用（理解 §8，勿用于生产）**

要求：对题 2 的第 3 条（`expired.badssl.com`），临时在代码里加 `curl_easy_setopt(h, CURLOPT_SSL_VERIFYPEER, 0L)` 后重新编译运行，观察请求**竟然成功了**。**结论**：这一个 `0L` 就让你对一个证书已过期（潜在被劫持）的站点建立了「加密但不可信」的连接——直观体会为什么生产绝不能关校验。看完把这行删掉。
