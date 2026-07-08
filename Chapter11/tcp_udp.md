# 专题：TCP / UDP —— 协议数据、可靠性与 tcpdump 排障

> 挂在第 11 章的补充专题，不是书里的正式小节。
>
> **为什么放第 11 章**：CSAPP 在 §11.3 只给出一句抽象结论——IP 不可靠，UDP 是不可靠数据报，TCP 是可靠字节流。但真实 Linux 工程里，网络问题不是背概念解决的，而是靠 `tcpdump` 看包：SYN 有没有出去、ACK 有没有回来、序列号是否推进、DNS 返回了什么、是不是 MTU/重传/超时。本专题把 TCP/UDP 的协议字段、可靠性机制和抓包排障串起来。
>
> **配套数据**：`capture_tcp_udp.sh` 一键抓包，输出在 `capture_results.txt`，原始 pcap 在 `/tmp/netcap/`。本文使用的是 2026-07-08 在本机 Ubuntu 24.04 上的真实抓包：客户端 `192.168.6.226`，出口网卡 `wlp0s20f3`，当前网络把 `neverssl.com` 解析到 `198.18.0.156`，把 `example.org` 解析到 `198.18.0.157`。
>
> **重要环境备注**：`198.18.0.0/15` 是保留给网络设备基准测试的地址段，正常公网域名不应直接解析到这里；这说明当前网络环境里存在 DNS/代理/VPN/安全网关一类的改写。本文不把它当公网样本，而把它当作真实排障样本：**连接能建立、HTTP 请求被 ACK、但服务端不回 body，最终 curl 超时**。

---

## 1. 一句话：TCP 和 UDP 的根本区别

**🎯 同样跑在 IP 上，抽象完全不同**

IP 只负责「尽力把包从一台主机送到另一台主机」，不保证到达、不保证顺序、不保证不重复。

UDP 和 TCP 都是在 IP 上加一层，把通信从「主机到主机」扩展成「进程到进程」：

```
应用数据
  ↓
TCP/UDP 头：加端口，定位到进程
  ↓
IP 头：加源/目的 IP，定位到主机
  ↓
以太网/Wi-Fi 帧：加 MAC，在本地链路上传输
```

差别在于：

- **UDP**：只加端口和校验和，保留 IP 的不可靠性，给应用一个个**数据报（datagram）**。
- **TCP**：在端口之上再加序列号、确认号、重传、窗口、拥塞控制，给应用一条可靠的**字节流（byte stream）**。

**🎯 一个最小对比表**

| 维度 | UDP | TCP |
|------|-----|-----|
| 连接 | 无连接，直接发 | 有连接，先三次握手 |
| 数据形态 | 一次 `sendto` 对应一个数据报边界 | 字节流，没有消息边界 |
| 可靠性 | 不保证到达/顺序/去重 | 保证按序、可靠、不重复交付 |
| 头部长度 | 固定 8 字节 | 20 字节起，带选项可到 60 字节 |
| 常见协议 | DNS、NTP、DHCP、QUIC | HTTP/1.1、HTTP/2、SSH、MySQL、Redis |
| 工程取舍 | 延迟低、控制权交给应用 | 省心，可靠性由内核协议栈负责 |

**🔧 工程直觉**

如果你的协议天然是一问一答、能容忍丢包或自己处理重试，UDP 很合适；如果你要传文件、RPC、数据库请求，默认选 TCP。QUIC 是例外：它跑在 UDP 上，但在用户态重新实现了连接、可靠性、拥塞控制和 TLS，因此不能把「UDP」简单等同于「不可靠应用」。

---

## 2. 协议数据：一包里到底有什么

**🎯 IP 头先决定「送到哪台机器」**

以本机 HTTP 请求包为例，`tcpdump -X` 看到的开头是：

```text
0x0000:  4500 007f 41a7 4000 4006 6a99 c0a8 06e2
0x0010:  c612 009c ...
```

按 IPv4 头拆开：

| 字节 | 值 | 含义 |
|------|----|------|
| `45` | version=4, IHL=5 | IPv4，IP 头 5×4=20 字节 |
| `00` | TOS | 普通服务类型 |
| `007f` | total length=127 | IP 包总长 127 字节 |
| `41a7` | identification | 分片重组用 ID |
| `4000` | DF | Don't Fragment，不允许中间路由分片 |
| `40` | TTL=64 | 最多经过 64 跳 |
| `06` | protocol=TCP | 上层是 TCP；UDP 则是 `11` 十六进制（17） |
| `c0a8 06e2` | 192.168.6.226 | 源 IP |
| `c612 009c` | 198.18.0.156 | 目的 IP |

**🎯 TCP 头决定「连接里的哪个字节」**

紧接着是 TCP 头：

```text
0x0010:  ... bfd4 0050 6b4b 5df2 51ee 4777
0x0020:  8018 003f 8eaa 0000 0101 080a 8095 c60c
0x0030:  918f c566 4745 5420 2f20 4854 5450 2f31
```

拆成字段：

| 字节 | 值 | 含义 |
|------|----|------|
| `bfd4` | 49108 | 源端口，客户端临时端口 |
| `0050` | 80 | 目的端口，HTTP |
| `6b4b5df2` | 1800101362 | 当前段第一个数据字节的序列号 |
| `51ee4777` | 1374570359 | 确认号：期待对端下一个字节 |
| `80` 高 4 位 | data offset=8 | TCP 头 8×4=32 字节，说明有 12 字节选项 |
| `18` | PSH+ACK | 这个段带数据，并确认对端数据 |
| `003f` | window=63 | 接收窗口，需结合窗口扩大因子解释 |
| `8eaa` | checksum | 抓到 `incorrect` 常因网卡 checksum offload |
| `0101080a...` | timestamp option | TCP 时间戳选项 |

真正的 HTTP 数据从 `4745 5420` 开始：

```text
47 45 54 20 2f 20 48 54 54 50 2f 31 2e 31 0d 0a
G  E  T     /     H  T  T  P  /  1  .  1  \r \n
Host: neverssl.com\r\n
User-Agent: curl/8.5.0\r\n
Accept: */*\r\n
\r\n
```

这就是为什么 HTTP/1.1 明文时 `tcpdump -X` 能直接看到请求行和 header；换成 HTTPS 后，这部分会变成 TLS 加密记录，只能看到 IP/TCP/TLS 记录长度，看不到 HTTP header。

**🎯 UDP 头只有 8 字节**

DNS 查询包里 IP 头后面是：

```text
0x0010:  0808 0808 ace8 0035 003c d7e7 7bda 0120
```

其中 UDP 头是：

| 字节 | 值 | 含义 |
|------|----|------|
| `ace8` | 44264 | 源端口，客户端临时端口 |
| `0035` | 53 | 目的端口，DNS |
| `003c` | 60 | UDP 长度：8 字节头 + 52 字节 DNS 数据 |
| `d7e7` | checksum | 校验和；发包方向显示 bad 常因 offload |

UDP 后面立刻就是 DNS 数据：

```text
7bda 0120 0001 0000 0000 0001 0765 7861 6d70 6c65 036f 7267 00 ...
```

`7bda` 是 DNS transaction ID，本机输出中 tcpdump 解析成 `31706+`。响应包也带同一个 ID，客户端靠它把「回复」配回「请求」。注意这不是 UDP 可靠性，而是 DNS 应用协议自己加的匹配字段。

---

## 3. TCP 三次握手：建立的是双方的序列号空间

**🎯 真实抓包**

本机 `curl http://neverssl.com/` 的三次握手：

```text
1) 192.168.6.226.49108 > 198.18.0.156.80: Flags [S],
   seq 1800101361, win 64240,
   options [mss 1460,sackOK,TS val 2157299202 ecr 0,nop,wscale 10]

2) 198.18.0.156.80 > 192.168.6.226.49108: Flags [S.],
   seq 1374570358, ack 1800101362, win 65160,
   options [mss 1460,sackOK,TS val 2442118502 ecr 2157299202,nop,wscale 6]

3) 192.168.6.226.49108 > 198.18.0.156.80: Flags [.],
   seq 1, ack 1, win 63, length 0
```

**🎯 为什么 SYN 要消耗 1 个序列号**

第一次包客户端发 SYN，初始序列号（ISN）是 `1800101361`。第二个包服务端确认 `ack 1800101362`，也就是 `ISN+1`。这是 TCP 的硬规则：**SYN 和 FIN 不携带应用数据，但各占用 1 个序列号**。

`tcpdump` 默认使用相对序号，所以第三个包显示 `seq 1, ack 1`；加 `-S` 后能看到绝对序号：

```text
seq 1800101362, ack 1374570359
```

读包时建议两个视图都看：

- 相对序号适合理解数据推进：`seq 1:76` 表示发了 75 字节。
- 绝对序号适合确认真实协议行为：ISN 是随机数，不是从 0 开始。

**🎯 握手时还协商了能力**

SYN/SYN-ACK 里的选项很关键：

| 选项 | 本机值 | 含义 |
|------|--------|------|
| `mss 1460` | 双方都是 1460 | 单个 TCP segment 最大 payload；典型以太网 MTU 1500 - IP20 - TCP20 |
| `sackOK` | 双方支持 | 支持选择性确认，丢多个包时不用全重传 |
| `TS val/ecr` | 双方支持 | TCP timestamp，可用于 RTT 估计和 PAWS |
| `wscale 10/6` | 客户端 10，服务端 6 | 窗口扩大因子，突破 16 位窗口上限 |

**🔧 排障直觉**

- 只看到 SYN 重复发，没有 SYN-ACK：服务端不可达、防火墙丢包、路由问题、对端端口没开放但被静默丢弃。
- 看到 SYN-ACK 后本机回 RST：本机内核/安全策略/端口状态异常，或抓到了不属于当前 socket 的包。
- 三次握手完成后应用超时：连接层通了，问题在应用层、代理、服务端逻辑或后续路径。

本机这次就是第三种：握手完成，HTTP 请求也被 ACK，但没有 HTTP 响应体。

---

## 4. TCP 数据传输：可靠性靠 seq/ack 推进

**🎯 真实 HTTP 请求段**

握手后客户端立刻发 HTTP 请求：

```text
192.168.6.226.49108 > 198.18.0.156.80: Flags [P.],
seq 1:76, ack 1, win 63, length 75: HTTP
    GET / HTTP/1.1
    Host: neverssl.com
    User-Agent: curl/8.5.0
    Accept: */*
```

服务端随后确认：

```text
198.18.0.156.80 > 192.168.6.226.49108: Flags [.],
seq 1, ack 76, win 1017, length 0
```

这里的核心关系是：

```
客户端发 seq 1:76   # 发送 [1,76) 这 75 个字节
服务端回 ack 76     # 我已经收到 1..75，下次请从 76 开始发
```

TCP 的 ACK 是**累积确认（cumulative ACK）**：`ack=N` 表示 `N` 之前的所有字节都已经按序收到。

**🎯 TCP 的可靠性不是一个魔法开关**

TCP 在 IP 的不可靠投递上叠了几层机制：

1. **序列号**：每个字节都有位置，接收方能排序、去重、发现空洞。
2. **确认号**：接收方告诉发送方「我连续收到了哪里」。
3. **超时重传（RTO）**：发出去的字节迟迟没被确认，就重发。
4. **快速重传**：收到多个重复 ACK，推断中间有段丢了，不等超时。
5. **校验和**：发现损坏包，丢弃而不是交给应用。
6. **滑动窗口**：接收方用窗口控制「你最多还能发多少」，避免把接收缓冲打爆。
7. **拥塞控制**：根据丢包/延迟调整发送速率，避免把网络打爆。

**⚠️ TCP 保证的是字节流，不保证消息边界**

如果应用写：

```c
write(fd, "hello", 5);
write(fd, "world", 5);
```

对端可能一次 `read` 到 `helloworld`，也可能分两次读到 `hel` + `loworld`。TCP 只保证字节顺序，不保留「两次 write」这个边界。因此 HTTP 用 `\r\n\r\n`、`Content-Length`、chunked encoding 来做应用层 framing；CSAPP 的 RIO 也正是为了解决 short count 和按行/按字节读的问题。

**🔧 排障直觉**

- `seq` 推进但对端不 `ack`：可能回程丢包、对端没收到、对端窗口问题。
- 对端 `ack` 了请求但没有响应：网络层和 TCP 层基本通，往应用层/代理/服务端查。
- 大量重复 `seq`：重传。
- 大量相同 `ack`：接收方在提示「中间缺了某段」。

本机样本里服务端 ACK 了 75 字节请求，却 15 秒没有任何 HTTP 响应，`curl` 报：

```text
curl: (28) Operation timed out after 15002 milliseconds with 0 bytes received
```

这不是「TCP 连接不上」，而是「连接和请求都成功，应用响应没回来」。

---

## 5. TCP 挥手：FIN 也占一个序列号

**🎯 真实抓包**

`curl` 超时后主动关闭连接：

```text
1) 客户端 FIN:
192.168.6.226.49108 > 198.18.0.156.80: Flags [F.],
seq 76, ack 1, length 0

2) 服务端 ACK:
198.18.0.156.80 > 192.168.6.226.49108: Flags [.],
seq 1, ack 77, length 0

3) 服务端 FIN:
198.18.0.156.80 > 192.168.6.226.49108: Flags [F.],
seq 1, ack 77, length 0

4) 客户端 ACK:
192.168.6.226.49108 > 198.18.0.156.80: Flags [.],
seq 77, ack 2, length 0
```

**🎯 为什么客户端从 `seq 76` 变成 `seq 77`**

客户端应用数据发到 `seq 1:76`，所以下一个序号是 76。FIN 本身占 1 个序列号，所以服务端 ACK 到 `77`。

服务端也一样：它没有发应用数据，当前相对序号是 1；服务端 FIN 占一个序列号，所以客户端最后 ACK 到 `2`。

**⚠️ 四次挥手不是永远四个包**

教材常画四次挥手，但真实网络里 ACK 和 FIN 可以合并；也可能应用半关闭，变成一端 `FIN_WAIT_2`、另一端继续发送数据。读包时不要死背「四个包」，要看标志位和序列号是否自洽。

---

## 6. UDP：无连接，但不是「没有协议」

**🎯 DNS 查询的一问一答**

本机 DNS 查询：

```text
192.168.6.226.44264 > 8.8.8.8.53: 31706+ [1au] A? example.org.
8.8.8.8.53 > 192.168.6.226.44264: 31706* q: A? example.org. 1/0/1 example.org. A 198.18.0.157
```

UDP 层只提供：

```text
源端口 44264 → 目的端口 53
长度 60
校验和 d7e7
```

剩下的「这是 DNS 查询」「查询 ID 是 31706」「问题是 A? example.org」「回答是 198.18.0.157」，全是 DNS 应用协议自己定义的。

**🎯 UDP 保留数据报边界**

一次 UDP `sendto` 发出的数据，要么作为一个完整数据报被对端收到，要么丢掉；不会像 TCP 那样和其他写入合并成字节流。接收方一次 `recvfrom` 面对的是一个 datagram。

这就是 UDP 适合 DNS 的原因：DNS 查询本来就是一问一答的小消息，不需要 TCP 那套连接状态。

**⚠️ UDP 不保证可靠，但应用可以补可靠性**

DNS 靠这些机制补一点可靠性：

- transaction ID：响应必须带同一个 ID，否则客户端不认。
- 超时重试：没收到响应就重新问。
- 多服务器：`/etc/resolv.conf` 可以配置多个 nameserver。
- 必要时退回 TCP：DNS 响应太大或被截断（TC bit）时可用 TCP 53。

QUIC 则更进一步：直接在 UDP payload 里自己实现连接 ID、丢包检测、重传、拥塞控制和 TLS。

**🔧 排障直觉**

- 只看到 DNS query，没有 response：DNS 服务器不可达、被防火墙丢、路由/VPN 策略问题。
- query 和 response 都有，但应用仍解析失败：可能是搜索域、IPv6/IPv4 优先级、glibc resolver、缓存或应用没用同一个 DNS 路径。
- response 的 IP 是奇怪网段：优先怀疑 DNS 劫持、代理、VPN、安全软件或公司网关。

本机样本中 `8.8.8.8` 返回 `198.18.0.157`，这不是公网 `example.org` 的常见地址，说明当前网络链路里有 DNS/代理层改写。

---

## 7. tcpdump 怎么看：从命令到结论

**🎯 三个基础命令**

```bash
# 1) 看连接过程，-nn 不反查域名/服务名，避免干扰
sudo tcpdump -i any -nnvv 'host 198.18.0.156 and tcp port 80'

# 2) 看绝对序列号，确认 ISN 和真实 seq/ack
sudo tcpdump -i any -nnvvS 'host 198.18.0.156 and tcp port 80'

# 3) 看明文 payload，HTTP/1.1 可直接读；HTTPS 只能看到 TLS 记录
sudo tcpdump -i any -nnX 'host 198.18.0.156 and tcp port 80'
```

保存 pcap 后再反复分析更稳：

```bash
sudo tcpdump -i any -nn 'host 198.18.0.156 and tcp port 80' -w /tmp/tcp.pcap
sudo tcpdump -nnvv -r /tmp/tcp.pcap
sudo tcpdump -nnvvS -r /tmp/tcp.pcap
sudo tcpdump -nnX -r /tmp/tcp.pcap 'tcp[13] & 8 != 0'
```

`tcp[13]` 是 TCP flags 字节，`8` 是 PSH bit；这个过滤器常用来挑出携带应用数据的段。

**⚠️ 抓包里常见的两个假警报**

第一，发包方向常看到：

```text
cksum 0x8eaa (incorrect -> 0xcc20)
[bad udp cksum 0xd7e7 -> 0x1f61!]
```

这通常是 **checksum offload**：内核把包交给网卡时 checksum 还没填最终值，网卡发出前才计算；tcpdump 在内核侧截获，所以误以为错误。只要是本机发出的包，先别急着判断 checksum 坏。

第二，`-i any` 抓的是 Linux cooked capture，输出会说：

```text
link-type LINUX_SLL2
Warning: interface names might be incorrect
```

这不影响 IP/TCP/UDP 解析。要看二层以太网 MAC 细节时再抓具体网卡：`-i wlp0s20f3`。

**🔧 常见故障模式速查**

| 现象 | tcpdump 形态 | 结论方向 |
|------|--------------|----------|
| 连接超时 | SYN 重复发，无 SYN-ACK | 路由、防火墙、服务端端口、云安全组 |
| 连接拒绝 | SYN 后收到 RST | 对端主机可达，但端口没人监听或主动拒绝 |
| TLS 卡住 | 三次握手完成，ClientHello 后无响应 | TLS/SNI/证书/中间设备/服务端应用 |
| HTTP 超时 | 请求 payload 被 ACK，但无响应 payload | 应用层、代理、后端处理、网关改写 |
| 慢 | 大量 Retransmission/Dup ACK | 丢包、拥塞、Wi-Fi 质量、路径问题 |
| 上传卡住 | 对端 `win 0` | 接收方窗口耗尽，应用读得慢或缓冲满 |
| DNS 异常 | query 到目标 DNS，但 response IP 不符合预期 | DNS 污染、代理、VPN、split DNS |

本机这次 HTTP 属于「请求被 ACK，但无响应 payload」：

```text
客户端发 GET length 75
服务端 ack 76
15 秒后 curl timeout
```

所以排障第一步不是怀疑 `curl` 或 TCP 三次握手，而是看当前 DNS/代理路径为什么把目标域名导向 `198.18.0.156` 且不返回 HTTP body。

---

## 8. TCP 重传：这次实验没命中，但该怎么看

**🎯 这次 netem 实验结果**

脚本给 `wlp0s20f3` 注入了 30% 丢包：

```text
# netem 已注入到 wlp0s20f3
curl: (52) Empty reply from server
8 packets captured
```

抓包里没有出现明显重复 `seq` 或 tcpdump 自动标注的 retransmission。原因很简单：样本太小，只有握手、一个 75 字节请求和挥手，30% 随机丢包未必刚好丢到关键 TCP 段；而且当前网络里的 `198.18.0.156` 可能是本地/网关代理，响应行为也不是标准公网 HTTP 服务。

这本身是一个好教训：**丢包实验要有足够长的数据流，短连接不一定能稳定复现重传**。

**🎯 真正看到重传时应关注什么**

典型重传形态：

```text
10.0.0.1.50000 > 10.0.0.2.80: Flags [P.], seq 1001:2461, ack 1, length 1460
... 一段时间没有 ack 推进 ...
10.0.0.1.50000 > 10.0.0.2.80: Flags [P.], seq 1001:2461, ack 1, length 1460
```

同一个 `seq` 区间再次出现，就是重传。Wireshark 会直接标 `TCP Retransmission`，tcpdump 需要你自己看 `seq`。

快速重传常伴随多个重复 ACK：

```text
对端连续回 ack 1001
对端连续回 ack 1001
对端连续回 ack 1001
发送方重发 seq 1001:2461
```

这表示接收方收到了后面的字节，但中间从 1001 开始有洞，所以 ACK 一直卡在 1001。

**🧪 更稳定的重传实验建议**

用一个大一点的下载目标，或者本机起服务端再用 `tc` 对 veth/loopback 之外的路径加丢包。例如公网可达时：

```bash
sudo tc qdisc add dev wlp0s20f3 root netem loss 5%
sudo tcpdump -i any -nnvv 'tcp and host <目标IP>' -w /tmp/retx-big.pcap
curl -o /dev/null http://speedtest.tele2.net/1MB.zip
sudo tc qdisc del dev wlp0s20f3 root
sudo tcpdump -nnvv -r /tmp/retx-big.pcap | grep -E 'seq|ack'
```

如果网络环境会劫持 HTTP，建议改用你自己控制的局域网机器，或者用 `iperf3` 做长流量。

---

## 9. 易错点

- 误以为 TCP 是「消息流」→ TCP 是字节流，应用必须自己设计边界。
- 误以为 ACK 确认的是「包」→ ACK 确认的是字节序号，`ack=N` 表示 N 之前的字节已连续收到。
- 误以为 UDP 一定不可靠到不能用 → UDP 只是传输层不保证可靠，DNS/QUIC/应用协议可以自己补重试和确认。
- 误以为三次握手只是「打招呼」→ 三次握手的核心是双方交换并确认各自 ISN，同时协商 MSS/SACK/window scale/timestamp 等能力。
- 误以为 `tcpdump` 的 bad checksum 一定是坏包 → 本机发包方向常因 checksum offload 显示 incorrect。
- 误以为 `curl timeout` 就是 TCP 连不上 → 要看 SYN/SYN-ACK/ACK 和请求 payload 是否被 ACK；本机样本是应用响应没回来。
- 误以为 `neverssl.com` 一定代表公网真实服务 → 当前网络解析到 `198.18.0.156`，说明 DNS/代理路径已经改写，必须先解释环境。

---

## 10. 工程关联

- **Linux socket API**：UDP 常用 `sendto`/`recvfrom`，TCP 常用 `connect`/`accept` 后 `read`/`write`；但二者在内核里对应完全不同的传输语义。
- **RIO 的必要性**：TCP 没有消息边界且可能 short count，CSAPP 的 `rio_readn`/`rio_readlineb` 正是把字节流重新组织成应用可处理的单位。
- **HTTP 排障**：明文 HTTP 可用 `tcpdump -X` 直接看请求行和 header；HTTPS 需要结合 SNI、证书、TLS alert、服务端日志，不能指望抓包看到 URL path。
- **性能和可靠性**：TCP 重传会同时拉低吞吐和增加尾延迟；线上看 `ss -ti`、`netstat -s`、`nstat`、`tcpdump` 可以定位 retrans、rto、cwnd、rtt。
- **DNS 是独立路径**：`curl http://host` 的失败可能发生在 DNS、TCP、TLS、HTTP 任一层；先 `dig`，再 `tcpdump`，不要把所有错误都归给应用代码。
- **代理/VPN/公司网关**：抓包看到保留地址段、奇怪 DNS 结果或请求被 ACK 但无响应时，要把中间设备当成排障对象，而不是只盯客户端代码。

---

## 11. 实验题

**🧪 题 1：复现本文抓包并手工解释 seq/ack**

源码/脚本：

```bash
cd ~/studyspace/ComputerSystem
sudo bash Chapter11/capture_tcp_udp.sh
```

要求：

- 在 `Chapter11/capture_results.txt` 里找到三次握手的三行。
- 写出客户端 ISN、服务端 ISN，以及为什么第二个包的 `ack` 是客户端 ISN+1。
- 找到 HTTP GET 的 `seq 1:76` 和服务端 `ack 76`，解释 75 字节是怎么来的。
- 找到 FIN/ACK，解释为什么 FIN 后 ACK 会加 1。

**🧪 题 2：用 tcpdump 看 DNS 的 UDP 头和 DNS ID**

命令：

```bash
sudo tcpdump -i any -nnvvX 'udp port 53' -w /tmp/dns.pcap &
dig +short example.org @8.8.8.8
sudo pkill tcpdump
sudo tcpdump -nnvvX -r /tmp/dns.pcap
```

要求：

- 找到 UDP 源端口、目的端口、UDP length。
- 找到 DNS transaction ID，确认 query 和 response ID 相同。
- 找到 response 里的 A 记录地址，判断是否符合你对公网 DNS 的预期。

**🧪 题 3：对比 HTTP 和 HTTPS 的 payload 可见性**

命令：

```bash
sudo tcpdump -i any -nnX 'tcp port 80 or tcp port 443' -w /tmp/http_vs_https.pcap &
curl -s http://neverssl.com/ -o /dev/null || true
curl -s https://example.org/ -o /dev/null || true
sudo pkill tcpdump
sudo tcpdump -nnX -r /tmp/http_vs_https.pcap | less
```

要求：

- 在 HTTP 流里找到 `GET / HTTP/1.1` 或其他明文 header。
- 在 HTTPS 流里找到 TLS record，但确认看不到 HTTP path/header。
- 解释为什么生产排障 HTTPS 时需要结合服务端日志、TLS 信息和应用指标，而不能只靠抓包读明文。
