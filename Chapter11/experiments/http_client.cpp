// http_client.cpp —— libcurl easy 接口的 C++17 RAII 封装示例
//
// 覆盖：GET 拿状态码+body、POST JSON、自定义 header、超时、错误处理、
//       用 std::string 收 body 的回调、区分「传输层失败」vs「HTTP 非 2xx」。
//
// 编译： g++ -std=c++17 -Wall http_client.cpp -lcurl -o http_client
// 运行： ./http_client [url]          # 不给 url 用默认
//
// 说明：libcurl 底层就是第 11 章的 socket+connect+DNS + 第 10 章的 read/write 循环，
//       用 strace -e trace=network ./http_client 能看到 socket()/connect()。

#include <curl/curl.h>

#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

// ─────────────────────────────────────────────────────────────
// 1) 全局初始化的 RAII 守卫
//    curl_global_init 非线程安全，必须在 main 早期、单线程时调一次；
//    curl_global_cleanup 进程退出前调一次。用一个栈对象兜住这对调用。
// ─────────────────────────────────────────────────────────────
class CurlGlobal {
public:
  CurlGlobal() {
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK)
      throw std::runtime_error("curl_global_init 失败");
  }
  ~CurlGlobal() { curl_global_cleanup(); }
  CurlGlobal(const CurlGlobal &) = delete;
  CurlGlobal &operator=(const CurlGlobal &) = delete;
};

// ─────────────────────────────────────────────────────────────
// 2) 用 unique_ptr + 自定义 deleter 管理 CURL*（easy handle）
//    出作用域自动 curl_easy_cleanup，杜绝忘 cleanup 的泄漏。
// ─────────────────────────────────────────────────────────────
struct CurlEasyDeleter {
  void operator()(CURL *h) const noexcept {
    if (h)
      curl_easy_cleanup(h);
  }
};
using EasyHandle = std::unique_ptr<CURL, CurlEasyDeleter>;

// ─────────────────────────────────────────────────────────────
// 3) 写回调：libcurl 每收到一段 body 就回调一次。
//    签名固定：size_t cb(char *ptr, size_t size, size_t nmemb, void *userdata)
//    - 实际数据长度 = size * nmemb（size 恒为 1，历史遗留）
//    - 返回值必须 == 收到的字节数，否则 libcurl 认为写失败、中断传输
//    - userdata 就是 CURLOPT_WRITEDATA 传进来的指针
//    ⚠️ 这是 C 回调，C++ 异常绝不能穿出去（穿过 C 栈是 UB），故整段包在 try 里。
// ─────────────────────────────────────────────────────────────
static size_t write_to_string(char *ptr, size_t size, size_t nmemb, void *userdata) {
  size_t total = size * nmemb;
  try {
    auto *out = static_cast<std::string *>(userdata);
    out->append(ptr, total);
    return total; // 返回已处理字节数 = total → 继续
  } catch (...) {
    return 0; // 返回 != total → 主动中断传输
  }
}

// ─────────────────────────────────────────────────────────────
// 4) 一次 HTTP 响应的结果
// ─────────────────────────────────────────────────────────────
struct HttpResponse {
  long status = 0;  // HTTP 状态码，如 200 / 404 / 500
  std::string body; // 响应体

  bool ok() const { return status >= 200 && status < 300; }
};

// ─────────────────────────────────────────────────────────────
// 5) 薄封装：一个 HttpClient 复用一个 easy handle（连接可被 libcurl 复用）
//    注意：一个 handle 不能跨线程并发使用，每线程各自持有一个 HttpClient。
// ─────────────────────────────────────────────────────────────
class HttpClient {
public:
  HttpClient() : handle_(curl_easy_init()) {
    if (!handle_)
      throw std::runtime_error("curl_easy_init 失败");
    // 公共默认项
    curl_easy_setopt(handle_.get(), CURLOPT_WRITEFUNCTION, write_to_string);
    curl_easy_setopt(handle_.get(), CURLOPT_FOLLOWLOCATION, 1L);   // 跟随 3xx 重定向
    curl_easy_setopt(handle_.get(), CURLOPT_TIMEOUT, 10L);         // 整体超时 10s
    curl_easy_setopt(handle_.get(), CURLOPT_CONNECTTIMEOUT, 5L);   // 连接超时 5s
    curl_easy_setopt(handle_.get(), CURLOPT_ERRORBUFFER, errbuf_); // 详细错误文案
    // HTTPS 证书校验默认就是开的（VERIFYPEER=1/VERIFYHOST=2），不要关。
  }

  // GET
  HttpResponse get(const std::string &url) {
    reset_per_request();
    curl_easy_setopt(handle_.get(), CURLOPT_URL, url.c_str());
    curl_easy_setopt(handle_.get(), CURLOPT_HTTPGET, 1L);
    return perform();
  }

  // POST JSON：手动设 Content-Type: application/json，body 是原始 JSON 串
  HttpResponse post_json(const std::string &url, const std::string &json) {
    reset_per_request();
    curl_easy_setopt(handle_.get(), CURLOPT_URL, url.c_str());
    // COPYPOSTFIELDS 让 libcurl 复制一份 body，免去调用者维护其生命周期的负担；
    // 若用 CURLOPT_POSTFIELDS 则只存指针，perform 前 json 必须一直存活。
    curl_easy_setopt(handle_.get(), CURLOPT_COPYPOSTFIELDS, json.c_str());

    struct curl_slist *headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    // slist 是 C 链表，perform 完必须 free，这里用 RAII 守卫兜住
    std::unique_ptr<curl_slist, decltype(&curl_slist_free_all)> hdr_guard(headers,
                                                                          curl_slist_free_all);
    curl_easy_setopt(handle_.get(), CURLOPT_HTTPHEADER, headers);

    return perform();
  }

private:
  EasyHandle handle_;
  char errbuf_[CURL_ERROR_SIZE] = {0};
  std::string body_;

  void reset_per_request() {
    body_.clear();
    errbuf_[0] = '\0';
    curl_easy_setopt(handle_.get(), CURLOPT_WRITEDATA, &body_);
    // 清掉上一次可能设过的 header，避免串味
    curl_easy_setopt(handle_.get(), CURLOPT_HTTPHEADER, nullptr);
  }

  HttpResponse perform() {
    CURLcode rc = curl_easy_perform(handle_.get());
    if (rc != CURLE_OK) {
      // 传输层失败：连不上 / DNS 解析失败 / 超时 / TLS 握手失败 …
      // 这类根本没拿到 HTTP 响应，和「HTTP 返回 500」是两码事。
      std::string msg = errbuf_[0] ? errbuf_ : curl_easy_strerror(rc);
      throw std::runtime_error("传输失败: " + msg);
    }
    HttpResponse resp;
    curl_easy_getinfo(handle_.get(), CURLINFO_RESPONSE_CODE, &resp.status);
    resp.body = body_;
    // 注意：status 是 4xx/5xx 时 rc 仍是 CURLE_OK —— 传输成功、业务失败，
    // 由调用方看 resp.ok() 自行判断，不是异常。
    return resp;
  }
};

int main(int argc, char *argv[]) {
  CurlGlobal g; // 全局 init/cleanup 守卫，覆盖整个 main 生命周期

  const std::string url = (argc > 1) ? argv[1] : "https://httpbin.org/get";

  try {
    HttpClient client;

    // ── GET ──
    std::cout << "== GET " << url << " ==\n";
    HttpResponse r = client.get(url);
    std::cout << "HTTP 状态码: " << r.status << (r.ok() ? "  (2xx OK)" : "  (非 2xx，业务失败)")
              << "\n";
    std::cout << "body 长度: " << r.body.size() << " 字节\n";
    std::cout << "body 前 200 字节:\n" << r.body.substr(0, 200) << "\n\n";

    // ── POST JSON ──
    const std::string post_url = "https://httpbin.org/post";
    std::cout << "== POST JSON " << post_url << " ==\n";
    HttpResponse r2 = client.post_json(post_url, R"({"name":"yanxu","chapter":11})");
    std::cout << "HTTP 状态码: " << r2.status << "\n";
    std::cout << "body 前 300 字节:\n" << r2.body.substr(0, 300) << "\n";

  } catch (const std::exception &e) {
    // 传输层失败落到这里；HTTP 非 2xx 不会抛异常
    std::cerr << "错误: " << e.what() << "\n";
    return 1;
  }
  return 0;
}
