/*
 * unix_socket.hpp —— Unix Domain Socket 的现代 C++17 RAII 薄封装
 *
 * 目标：把 socket/bind/listen/accept/connect 以及传 fd 的 sendmsg+cmsg 那套
 *       裸 syscall 包成 RAII + 异常报错的类型，消灭手动 close、手动 memset、
 *       手动查返回值。cmsg 的内核机制一点没变（省不掉），变的是资源管理与错误处理。
 *
 * 依赖：仅 C++17 标准库 + POSIX syscall，无第三方库。
 *
 * 两个类：
 *   uds::Fd          —— 独占所有权、只可移动的文件描述符（rule-of-5）
 *   uds::UnixSocket  —— 对一条 AF_UNIX 连接/监听端的封装，工厂式构造
 */
#ifndef UDS_UNIX_SOCKET_HPP
#define UDS_UNIX_SOCKET_HPP

#include <cerrno>
#include <cstddef>
#include <cstring>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace uds {

// 把当前 errno 包成 std::system_error 抛出，附上出错的 syscall 名
[[noreturn]] inline void throw_errno(const char *what) {
  throw std::system_error(errno, std::generic_category(), what);
}

// ── RAII 文件描述符：独占所有权，只可移动、不可拷贝 ──
class Fd {
public:
  Fd() noexcept = default;
  explicit Fd(int fd) noexcept : fd_(fd) {}
  ~Fd() { reset(); }

  Fd(Fd &&o) noexcept : fd_(std::exchange(o.fd_, -1)) {}
  Fd &operator=(Fd &&o) noexcept {
    if (this != &o) {
      reset();
      fd_ = std::exchange(o.fd_, -1);
    }
    return *this;
  }
  Fd(const Fd &) = delete;
  Fd &operator=(const Fd &) = delete;

  int get() const noexcept { return fd_; }
  explicit operator bool() const noexcept { return fd_ >= 0; }

  // 交出所有权：调用方接管，析构不再 close
  [[nodiscard]] int release() noexcept { return std::exchange(fd_, -1); }

  // 关掉旧 fd，接管新 fd（默认置空）
  void reset(int fd = -1) noexcept {
    if (fd_ >= 0)
      ::close(fd_);
    fd_ = fd;
  }

private:
  int fd_ = -1;
};

// ── 对一条 AF_UNIX 连接（或监听端）的封装 ──
class UnixSocket {
public:
  UnixSocket() = default;
  explicit UnixSocket(Fd fd) noexcept : fd_(std::move(fd)) {}

  // 工厂：bind+listen 一个路径，返回监听 socket。会先 unlink 清残留。
  static UnixSocket listen(std::string_view path, int backlog = 8) {
    UnixSocket s(make_socket());
    sockaddr_un addr = make_addr(path);
    ::unlink(std::string(path).c_str()); // 清残留，否则 EADDRINUSE
    if (::bind(s.fd(), reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0)
      throw_errno("bind");
    if (::listen(s.fd(), backlog) < 0)
      throw_errno("listen");
    return s;
  }

  // 工厂：connect 一个路径，返回已连通的 socket
  static UnixSocket connect(std::string_view path) {
    UnixSocket s(make_socket());
    sockaddr_un addr = make_addr(path);
    if (::connect(s.fd(), reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0)
      throw_errno("connect");
    return s;
  }

  // 从监听 socket 接受一个连接，返回代表该连接的新 UnixSocket
  UnixSocket accept() const {
    int c = ::accept(fd(), nullptr, nullptr);
    if (c < 0)
      throw_errno("accept");
    return UnixSocket(Fd(c));
  }

  // 健壮写：写满 len 字节才返回（等价书里的 rio_writen）
  void write_all(const void *buf, std::size_t len) const {
    const char *p = static_cast<const char *>(buf);
    std::size_t left = len;
    while (left > 0) {
      ssize_t w = ::write(fd(), p, left);
      if (w < 0) {
        if (errno == EINTR)
          continue;
        throw_errno("write");
      }
      left -= static_cast<std::size_t>(w);
      p += w;
    }
  }

  // 单次读，返回实际读到的字节数（0 表示对端关闭 EOF）
  std::size_t read_some(void *buf, std::size_t len) const {
    ssize_t r;
    do {
      r = ::read(fd(), buf, len);
    } while (r < 0 && errno == EINTR);
    if (r < 0)
      throw_errno("read");
    return static_cast<std::size_t>(r);
  }

  // 通过本连接把一个 fd 发出去（SCM_RIGHTS）。捎带 1 字节占位数据。
  void send_fd(int fd_to_send) const {
    char dummy = '*';
    iovec iov{};
    iov.iov_base = &dummy;
    iov.iov_len = 1;

    // cmsg 有对齐要求：缓冲大小用 CMSG_SPACE 算，union 保证按 cmsghdr 对齐
    union {
      char buf[CMSG_SPACE(sizeof(int))];
      cmsghdr align;
    } u{};

    msghdr msg{};
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = u.buf;
    msg.msg_controllen = sizeof(u.buf);

    cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type = SCM_RIGHTS;
    cmsg->cmsg_len = CMSG_LEN(sizeof(int));
    std::memcpy(CMSG_DATA(cmsg), &fd_to_send, sizeof(int));

    if (::sendmsg(fd(), &msg, 0) < 0)
      throw_errno("sendmsg");
  }

  // 从本连接收一个 fd，返回自动管理生命周期的 Fd
  Fd recv_fd() const {
    char dummy;
    iovec iov{};
    iov.iov_base = &dummy;
    iov.iov_len = 1;

    union {
      char buf[CMSG_SPACE(sizeof(int))];
      cmsghdr align;
    } u{};

    msghdr msg{};
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = u.buf;
    msg.msg_controllen = sizeof(u.buf);

    if (::recvmsg(fd(), &msg, 0) < 0)
      throw_errno("recvmsg");

    cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
    if (cmsg == nullptr || cmsg->cmsg_level != SOL_SOCKET ||
        cmsg->cmsg_type != SCM_RIGHTS ||
        cmsg->cmsg_len != CMSG_LEN(sizeof(int)))
      throw std::runtime_error("recv_fd: no valid SCM_RIGHTS cmsg");

    int received;
    std::memcpy(&received, CMSG_DATA(cmsg), sizeof(int));
    return Fd(received);
  }

  int fd() const noexcept { return fd_.get(); }
  explicit operator bool() const noexcept { return static_cast<bool>(fd_); }

private:
  static Fd make_socket() {
    int s = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (s < 0)
      throw_errno("socket");
    return Fd(s);
  }

  // string_view 不保证以 '\0' 结尾：sockaddr_un 值初始化清零后，
  // 只 memcpy path.size() 字节，末尾天然留着 '\0' 作为路径终止。
  static sockaddr_un make_addr(std::string_view path) {
    sockaddr_un addr{}; // 值初始化 == 全零，取代 memset
    addr.sun_family = AF_UNIX;
    if (path.size() >= sizeof(addr.sun_path))
      throw std::length_error("unix socket path too long");
    std::memcpy(addr.sun_path, path.data(), path.size());
    return addr;
  }

  Fd fd_;
};

} // namespace uds

#endif // UDS_UNIX_SOCKET_HPP
