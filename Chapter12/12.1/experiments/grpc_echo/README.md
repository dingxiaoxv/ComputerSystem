# gRPC echo IPC 示例

这个目录演示第 12 章 §12.1 里更高层的 IPC/RPC：**gRPC = Protobuf IDL + HTTP/2 传输 + 生成的 client/server stub**。

它和 UDS / `mmap` / `eventfd` 的定位不同：

- UDS：本机进程间服务 API，轻量、可传 fd。
- `mmap`：共享同一批物理页，适合大块数据和低延迟共享状态。
- `eventfd`：只做事件通知/计数唤醒，常配 `epoll`。
- gRPC：面向服务接口，天然跨语言、跨机器；如果只在本机跑，也只是把 loopback TCP 当 IPC 通道。

## 文件

- `echo.proto`：接口定义，声明 `EchoService.Echo` RPC。
- `server.cpp.in`：服务端源码模板，CMake 配置到 build 目录后编译。
- `client.cpp.in`：客户端源码模板，CMake 配置到 build 目录后编译。
- `CMakeLists.txt`：用 `protoc` 和 `grpc_cpp_plugin` 生成 C++ stub。

## 依赖

Ubuntu 上常见安装方式：

```bash
sudo apt install protobuf-compiler libprotobuf-dev libgrpc++-dev protobuf-compiler-grpc cmake g++
```

不同发行版包名可能不同。若 CMake 找不到 `gRPC CONFIG`，说明系统包没有提供对应 CMake config，建议用 vcpkg/源码安装 gRPC，或者先把这个例程当作阅读材料。

## 编译

```bash
cd Chapter12/12.1/experiments/grpc_echo
cmake -S . -B build
cmake --build build -j
```

## 运行

终端 1：

```bash
./build/grpc_echo_server 0.0.0.0:50051
```

终端 2：

```bash
./build/grpc_echo_client localhost:50051 'hello via gRPC'
```

预期输出：

```text
client received: echo: hello via gRPC
```

## 观察点

```bash
# gRPC 默认走 TCP；本机调用时就是 loopback TCP IPC
ss -tnp | grep 50051

# 观察连接系统调用
strace -f -e trace=network ./build/grpc_echo_client localhost:50051 'hello'
```

工程直觉：gRPC 解决的是“接口契约 + 序列化 + 跨语言调用”，不是最轻量的本机 IPC。若只是同机高频小消息，UDS 通常更直接；若是大块共享数据，`mmap` 更合适；若只是唤醒事件循环，`eventfd` 更合适。
