/*
 * passfd_cpp.cpp —— 用 unix_socket.hpp 的 RAII 封装重写「传 fd」
 *
 * 和 C 版 passfd_send.c / passfd_recv.c 对照：cmsg 的内核机制完全相同，
 * 但这里没有一处手动 close、没有 memset、没有裸 errno 判断——
 *   - fd 生命周期由 uds::Fd 托管，出作用域自动关；
 *   - 出错抛 std::system_error，不用层层判返回值；
 *   - sockaddr_un 用值初始化 {} 清零，路径用 string_view。
 *
 * 为自包含可测，这里 listen 后 fork：父进程发送、子进程接收（命名 UDS，
 * 两端仍是靠一条已连通的 AF_UNIX 连接传 fd，机制与进程关系无关）。
 *
 *   编译：见 Makefile 的 `make passfd_cpp`（g++ -std=c++17）
 */
#include "unix_socket.hpp"

#include <cstdio>
#include <string>

#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {
constexpr const char *kSockPath = "/tmp/csapp_passfd_cpp.sock";
constexpr const char *kFilePath = "/etc/hostname";
} // namespace

int main() {
  std::setvbuf(stdout, nullptr, _IONBF, 0);

  try {
    // 先 bind+listen，保证 fork 出的子进程 connect 时监听端已就绪
    uds::UnixSocket listener = uds::UnixSocket::listen(kSockPath);

    pid_t pid = ::fork();
    if (pid < 0) {
      std::perror("fork");
      return 1;
    }

    if (pid == 0) {
      // ── 子进程：接收端 ──
      uds::UnixSocket conn = uds::UnixSocket::connect(kSockPath);
      uds::Fd got = conn.recv_fd();
      std::printf("[child]  收到 fd=%d (本进程新号)\n", got.get());

      // 用收到的 fd 读文件内容
      std::string content;
      char buf[256];
      for (;;) {
        ssize_t n = ::read(got.get(), buf, sizeof(buf));
        if (n <= 0)
          break;
        content.append(buf, static_cast<std::size_t>(n));
      }
      std::printf("[child]  用该 fd 读到内容: %s", content.c_str());
      // got / conn / listener 全部在此自动 close，无需手写
      return 0;
    }

    // ── 父进程：发送端 ──
    uds::UnixSocket conn = listener.accept();
    uds::Fd file(::open(kFilePath, O_RDONLY));
    if (!file) {
      std::perror("open");
      return 1;
    }
    std::printf("[parent] open %s 得 fd=%d, 发送该 fd...\n", kFilePath, file.get());

    conn.send_fd(file.get());
    // file 立刻可以不用了——接收端照样能读，因为传的是内核对象。
    // 这里不显式 close，file 出 try 作用域时自动关。

    int status = 0;
    ::waitpid(pid, &status, 0);
    ::unlink(kSockPath);
    std::printf("[parent] done.\n");
    return 0;
  } catch (const std::exception &e) {
    std::fprintf(stderr, "error: %s\n", e.what());
    ::unlink(kSockPath);
    return 1;
  }
}
