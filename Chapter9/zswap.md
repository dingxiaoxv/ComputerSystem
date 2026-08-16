# 降低 512MB 系统内存占用的进一步优化方案

## 0. 当前 swap / zswap 参数含义

当前配置中有两个关键参数：

```sh
dd if=/dev/zero of=/data/swapfile bs=1048576 count=300
echo 25 > /sys/module/zswap/parameters/max_pool_percent
```

含义分别是：

| 配置 | 含义 | 当前数值解释 |
|---|---|---|
| `dd if=/dev/zero of=/data/swapfile bs=1048576 count=300` | 创建 swap 后端文件 | 从 `/dev/zero` 写入 300 个 1 MiB 块到 `/data/swapfile`，文件大小约 300 MiB |
| `echo 25 > /sys/module/zswap/parameters/max_pool_percent` | 设置 zswap 压缩池最大占用 | zswap 压缩后的数据最多占用系统 RAM 的 25%；在 512 MB 内存机器上约为 128 MB 上限 |

需要区分：

```text
/data/swapfile 的 300 MiB：
swap 后端容量，决定最多可以换出多少未压缩页到后端 swap。

zswap max_pool_percent=25：
zswap 压缩池 RAM 上限，决定压缩后的 swap cache 最多占用多少物理内存。
```

实际路径是：

```text
应用匿名页 -> swap out -> 先进入 zswap 压缩池
                         -> zswap 池满或页不适合压缩时，再写入 /data/swapfile
```

因此，当前配置并不是“zswap 有 300 MiB 内存池”，而是：

```text
swap 后端文件容量约 300 MiB
zswap 压缩池最多占物理内存的 25%
```

## 1. 背景与现状

基于当前 `rdmpstat*` 分析结果：

- 设备目标内存规格：512 MB。
- 当前 zswap 已生效：`zswap: enabled=true`。
- 当前日志显示 zram swap 未生效：`zram_swap=false`。
- 当前 swap 后端为 file swap：`file_swap=true`。
- 建图阶段内存压力明显高于跑机阶段。
- 建图阶段出现 direct reclaim 和 allocstall。
- 跑机阶段没有 direct reclaim / allocstall，但内存余量仍偏小。

关键数据：

| 指标 | 建图阶段 | 跑机阶段 | 全评估窗口 |
|---|---:|---:|---:|
| `MemAvailable` 最低 | 17.7 MB | 31.0 MB | 17.7 MB |
| `swap_used` 最大 | 280.4 MB | 201.1 MB | 280.4 MB |
| zswap pool 最大 | 105.7 MB | 54.1 MB | 105.7 MB |
| zswap 平均净节省 | 111.5 MB | 103.3 MB | 107.2 MB |
| direct_scan_delta 合计 | 160055 | 0 | 160055 |
| allocstall_delta 合计 | 1222 | 0 | 1222 |
| OOM | 0 | 0 | 0 |

总体判断：

```text
zswap 已经提供明显收益，但建图阶段仍然存在内存压力。
继续优化的重点不只是 swap 机制，而是建图阶段工作集、应用局部性和内核回收策略。
```

## 2. 类似 zswap 的机制对比

| 机制 | 原理 | 是否适合当前场景 | 备注 |
|---|---|---|---|
| zswap | swap 前端压缩缓存 | 已启用，收益明显 | 当前平均净节省约 107 MB |
| zram | RAM 中的压缩块设备，常作为 swap | 适合，需确认是否真正启用 | 当前日志显示 `zram_swap=false` |
| KSM | 合并内容相同的匿名页 | 可能收益有限 | 多进程相同页多时才明显 |
| MGLRU | 更先进的冷热页回收算法 | 如果内核支持，值得验证 | 可减少错误回收热页 |
| DAMON reclaim | 基于访问热度主动回收冷页 | 理论适合 | 依赖内核配置，参数复杂 |
| cgroup memory | 限制非关键进程内存 | 适合做稳定性防护 | 需要 cgroup 支持 |
| madvise | 应用主动标记冷页或可丢弃页 | 很适合 | 需要代码配合 |
| malloc_trim | 应用主动把空闲堆内存还给内核 | 很适合 | 建图结束点尤其有价值 |

## 3. zram：当前优先确认项

当前日志中一直显示：

```text
file_swap=true
zram_swap=false
zswap enabled=true
```

因此，虽然系统预期启用了 zram，但 rdmpstat 没有看到 zram swap 生效。

建议在设备上确认：

```sh
cat /proc/swaps
swapon --show
ls -l /sys/block/zram*
```

理想情况下，`/proc/swaps` 中应该能看到类似：

```text
/dev/zram0  partition  xxx  xxx  100
/swapfile   file       xxx  xxx  -2
```

推荐策略：

```text
zram swap 高优先级
file swap 低优先级
```

这样可以优先使用内存压缩 swap，减少 file swap IO。

需要注意：

如果同时启用 zswap 和 zram，可能形成两级压缩：

```text
应用页 -> zswap 压缩 -> zram 再压缩
```

这会提升压缩收益，但也会增加 CPU 开销，需要实测实时性影响。

## 4. 更激进换页策略

### 4.1 调高 swappiness

`vm.swappiness` 控制内核更倾向于回收 page cache，还是更倾向于换出匿名页。

当前场景中：

- zswap 压缩比好，平均约 3.3x - 3.5x。
- 建图阶段出现明显 direct reclaim / allocstall。
- 提前换出冷匿名页可能有助于减少低内存尖峰。

建议实验：

```sh
sysctl vm.swappiness=100
```

更激进可以尝试：

```sh
sysctl vm.swappiness=120
sysctl vm.swappiness=150
```

更推荐分阶段调整：

```sh
# 建图前
sysctl vm.swappiness=120

# 建图结束后或跑机阶段
sysctl vm.swappiness=60
```

风险：

- 如果被换出的页很快又被访问，会增加 major fault。
- 压缩 / 解压会增加 CPU 开销。
- 可能带来实时性抖动。

### 4.2 调整 watermark，让后台回收更早启动

建图阶段出现 `allocstall`，说明部分内存分配触发了同步回收。可以尝试让 kswapd 更早启动后台回收。

查看参数：

```sh
cat /proc/sys/vm/watermark_scale_factor
cat /proc/sys/vm/min_free_kbytes
```

建议实验：

```sh
sysctl vm.watermark_scale_factor=150
```

更激进：

```sh
sysctl vm.watermark_scale_factor=200
```

作用：

```text
更早触发后台回收，减少分配路径 direct reclaim / allocstall。
```

代价：

```text
可用内存看起来会更少，可能更早 swap。
```

### 4.3 调整 vfs_cache_pressure

如果 dentry / inode cache 占用明显，可以提高 `vfs_cache_pressure`。

建议实验：

```sh
sysctl vm.vfs_cache_pressure=120
sysctl vm.vfs_cache_pressure=150
```

但从当前日志看，主要压力更像是匿名页和应用 RSS，收益可能有限。

## 5. MGLRU：如果内核支持，建议验证

MGLRU，全称 Multi-Gen LRU，是更先进的冷热页识别和回收机制。

检查是否支持：

```sh
cat /sys/kernel/mm/lru_gen/enabled
```

如果存在，可以尝试开启：

```sh
echo 7 > /sys/kernel/mm/lru_gen/enabled
```

潜在收益：

- 更准确识别冷页。
- 减少错误回收热页。
- 对小内存、多进程、工作集变化明显的场景有价值。

风险：

- 依赖内核版本和配置。
- 需要对比开启前后的 major fault、allocstall、业务耗时。

## 6. DAMON reclaim：基于程序局部性的内核回收

DAMON 可以监控内存访问热度，并对冷区域主动 reclaim。它比单纯调高 swappiness 更接近程序局部性分析。

检查是否支持：

```sh
ls /sys/kernel/mm/damon
```

适合当前场景的原因：

- 建图阶段存在大块临时数据结构。
- 部分点云、地图、历史帧、缓存可能在后续阶段变冷。
- 可以主动回收长期未访问区域，减少内存常驻。

风险：

- 嵌入式内核未必开启。
- 参数复杂。
- 需要验证实时性影响。

## 7. KSM：可低成本验证，但预期收益有限

KSM 会扫描匿名页，将内容完全相同的页合并为一份。

检查：

```sh
ls /sys/kernel/mm/ksm
```

启用：

```sh
echo 1 > /sys/kernel/mm/ksm/run
```

观察：

```sh
cat /sys/kernel/mm/ksm/pages_shared
cat /sys/kernel/mm/ksm/pages_sharing
```

适用场景：

- 多进程存在大量相同匿名页。
- 多个进程加载相似运行时或相同初始化数据。

当前场景中，业务进程差异较大，KSM 预期收益可能有限，同时会增加 CPU 扫描开销。

## 8. 应用侧局部性优化

当前内存峰值最主要来自：

```text
/application/lib/fast_lio/fastlio_online
```

关键 RSS 数据：

| 阶段 | 平均 RSS | P99 RSS | 最大 RSS |
|---|---:|---:|---:|
| 建图 | 74.8 MB | 135.7 MB | 150.6 MB |
| 跑机 | 64.9 MB | 102.7 MB | 145.9 MB |
| 全窗口 | 68.3 MB | 129.3 MB | 150.6 MB |

重点排查方向：

```text
点云 buffer
地图 voxel / grid / kd-tree
历史 scan 队列
特征点缓存
轨迹缓存
中间矩阵 / Jacobian / residual buffer
ROS message queue / DDS queue
日志缓存
图优化窗口
```

### 8.1 限制历史窗口

可以考虑：

```text
只保留最近 N 帧
只保留局部地图半径 R 内点云
建图结束后释放临时结构
保存完成后释放构图中间状态
```

目标：

```text
减少建图阶段和跑机阶段的长期常驻工作集。
```

### 8.2 降低数据精度和结构体大小

可能的优化：

```text
double -> float
int64 -> int32
Eigen::MatrixXd -> 固定大小 Matrix<float, ...>
减少 PointXYZI 或自定义 point 结构中的冗余字段
```

如果点云数量大，这类优化收益会很明显。

### 8.3 分块处理，避免全量常驻

避免同时持有多份大对象：

```text
全局 map
当前 scan
滤波后 scan
特征点集合
配准输入
地图插入临时数据
发布消息 copy
```

建议采用：

```text
streaming / chunk 化处理
复用 workspace buffer
减少中间 copy
```

### 8.4 减少点云拷贝

点云处理常见峰值来源：

```text
原始点云一份
滤波后一份
特征点一份
配准输入一份
地图插入临时一份
发布消息又一份
```

如果每份都是数 MB 到数十 MB，会快速造成内存峰值。

建议：

```text
优先使用 move 语义
避免不必要的深拷贝
复用 vector capacity
明确释放阶段性临时 buffer
```

### 8.5 容器释放内存

C++ 中 `std::vector::clear()` 不会释放 capacity。

如需释放 capacity，可以使用：

```cpp
std::vector<T>().swap(vec);
```

或：

```cpp
vec.shrink_to_fit();
```

注意：`shrink_to_fit()` 不保证一定释放。

建图结束后尤其应该释放：

```text
临时点云
历史帧缓存
候选匹配缓存
debug buffer
中间 map 构建结构
```

## 9. madvise：应用主动告诉内核哪些页可以回收

这是非常适合程序局部性优化的手段。

### 9.1 MADV_PAGEOUT

如果某些大 buffer 后续短时间不用，但未来可能还要用，可以使用：

```cpp
madvise(ptr, len, MADV_PAGEOUT);
```

含义：

```text
主动请求内核将这段内存 page out 到 swap / zswap。
```

优点：

```text
比单纯提高 swappiness 更可控。
```

风险：

```text
后续再次访问会触发 page fault。
```

### 9.2 MADV_DONTNEED

如果某些 buffer 内容可以丢弃，后续会重新生成，可以使用：

```cpp
madvise(ptr, len, MADV_DONTNEED);
```

含义：

```text
直接释放物理页，后续访问变成零页或重新 fault。
```

适合对象：

```text
大块临时点云 buffer
建图中间结果
可重建 cache
历史特征缓存
一次性计算 workspace
```

## 10. malloc_trim 与 allocator 调优

如果程序使用 glibc malloc，释放后的内存不一定马上还给内核，RSS 可能居高不下。

建图结束后可以尝试：

```cpp
#include <malloc.h>

malloc_trim(0);
```

适合调用点：

```text
建图结束
地图保存后
跑机开始前
大批临时对象释放后
```

如果是多线程 C++ 程序，glibc arena 可能导致 RSS 偏高。可以在启动脚本中设置：

```sh
export MALLOC_ARENA_MAX=2
export MALLOC_TRIM_THRESHOLD_=131072
export MALLOC_MMAP_THRESHOLD_=131072
```

预期收益：

```text
降低释放后仍常驻的堆内存，减少阶段切换后的 RSS 残留。
```

## 11. cgroup memory：保护关键进程

如果系统支持 cgroup v2，可以给非关键进程设置 `memory.high` 或 `memory.max`。

目标：

```text
保护 fast_lio / navigation / ai_platform 等关键进程。
限制 ota / factory / debug / 非关键 daemon 占用。
```

优点：

- 防止非关键进程挤占建图阶段内存。
- 比全局回收更可控。
- 可作为稳定性防护手段。

风险：

- 依赖 cgroup 支持。
- 设置过紧可能导致非关键服务异常。

## 12. 不建议或需要谨慎的操作

### 12.1 频繁 drop_caches

不建议常规使用：

```sh
echo 3 > /proc/sys/vm/drop_caches
```

原因：

- 主要丢 page cache，对匿名 RSS 帮助有限。
- 可能造成后续 IO 抖动。
- 不能解决 fast_lio 等进程自身工作集过大的问题。

### 12.2 过高 swappiness

全程设置过高 swappiness，例如 150 以上，需要谨慎。

风险：

- 热页误换出。
- major fault 增加。
- 压缩 / 解压 CPU 开销增加。
- 实时链路抖动。

### 12.3 zswap + zram 双重压缩

双重压缩可能有效，也可能增加 CPU 压力。

建议必须通过实测判断：

```text
观察 CPU 使用率
观察 major fault
观察任务耗时
观察 direct_scan / allocstall 是否下降
```

## 13. 推荐实验组合

### 13.1 第一组：确认 zram + 保守内核参数

```sh
cat /proc/swaps
sysctl vm.swappiness=100
sysctl vm.watermark_scale_factor=150
sysctl vm.vfs_cache_pressure=120
```

观察指标：

```text
MemAvailable 最低值是否抬高
swap_used 是否更平滑
direct_scan_delta 是否下降
allocstall_delta 是否下降
major fault 是否明显上升
业务耗时是否变差
```

### 13.2 第二组：建图阶段临时激进

```sh
# 建图前
sysctl vm.swappiness=120
sysctl vm.watermark_scale_factor=200

# 建图结束后
sysctl vm.swappiness=60
sysctl vm.watermark_scale_factor=100
```

目标：

```text
让冷页更早进入 zswap / zram，减少建图阶段低内存尖峰。
```

### 13.3 第三组：应用主动释放

建图结束后执行应用级释放：

```text
释放临时容器
释放历史构图缓存
std::vector<T>().swap(vec)
malloc_trim(0)
必要时 madvise(MADV_DONTNEED / MADV_PAGEOUT)
```

目标：

```text
降低跑机开始时的 RSS 和 swap_used，减少后续长期内存压力。
```

### 13.4 第四组：fast_lio 局部性专项

重点排查：

```text
点云是否存在多份拷贝
历史帧是否可限长
局部地图是否可裁剪
debug buffer 是否常驻
double 是否可改 float
vector clear 后 capacity 是否未释放
```

目标：

```text
将 fast_lio_online P99 RSS 从 135 MB 降到 90-100 MB。
```

## 14. 推荐优先级

建议按以下顺序推进：

1. 确认 zram 是否真的在 `/proc/swaps` 中生效。
2. 建图阶段试验 `swappiness=100/120`。
3. 建图阶段试验 `watermark_scale_factor=150/200`。
4. 建图结束点增加应用主动释放和 `malloc_trim(0)`。
5. 针对 `fast_lio_online` 点云、地图、历史帧结构做局部性优化。
6. 如果内核支持，验证 MGLRU。
7. 如果内核支持，进一步验证 DAMON reclaim。
8. 如系统支持 cgroup，限制非关键进程内存。

## 15. 最终建议

当前 zswap 已经提供明显正收益：

```text
平均占用约 46 MB RAM，平均净节省约 107 MB。
```

继续依赖 swap 压缩机制还能获得一定收益，但主要优化空间已经转向应用侧：

```text
建图阶段工作集
fast_lio_online 内存峰值
点云和地图数据结构局部性
阶段切换后的主动释放
```

如果必须维持 512 MB 内存规格，建议优先组合使用：

```text
zram 确认生效
zswap 保持开启
建图阶段提高 swappiness
适度提高 watermark_scale_factor
建图结束后 malloc_trim / madvise
fast_lio 数据结构和局部性优化
```

如果硬件规格可调整，768 MB 会明显降低建图阶段内存风险。
