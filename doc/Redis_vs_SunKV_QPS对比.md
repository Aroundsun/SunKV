# 8.3 Redis vs SunKV：本机 QPS 对比（2026-04-20）
本文记录一次**可复现**的本机压测：用 `redis-benchmark` 对比 Redis 与 SunKV 在相同参数下的 QPS/延迟指标。结论用于后续优化的基线，不在本文中展开修复实现。
---
## 1. 环境
- **机器**：本机（Linux `6.17.0-20-generic`，x86_64）
- **Redis**：`redis-server 8.0.2`（临时实例）
- **redis-benchmark**：`8.0.2`
- **SunKV**：`./build/sunkv`（临时实例）
说明：
- Redis 实例为临时启动，端口独立，关闭持久化（RDB/AOF）以避免磁盘影响。
- SunKV 实例为临时启动，端口独立，并显式关闭 WAL/快照，尽量对齐“纯内存 KV”路径。
---
## 2. 启动方式（可复现）
### 2.1 Redis（临时端口 16379）
```bash
rm -rf /tmp/sunkv_redis_bench_16379 && mkdir -p /tmp/sunkv_redis_bench_16379
redis-server --port 16379 --bind 127.0.0.1 --protected-mode yes --save "" --appendonly no \
  --dir /tmp/sunkv_redis_bench_16379 --loglevel notice
redis-cli -h 127.0.0.1 -p 16379 PING
```

### 2.2 SunKV（临时端口 16380，关闭持久化）
```bash
rm -rf /tmp/sunkv_bench_16380 && mkdir -p /tmp/sunkv_bench_16380/data/{logs,wal,snapshot}
./build/sunkv --host 127.0.0.1 --port 16380 \
  --data-dir /tmp/sunkv_bench_16380/data \
  --wal-dir /tmp/sunkv_bench_16380/data/wal \
  --snapshot-dir /tmp/sunkv_bench_16380/data/snapshot \
  --log-file /tmp/sunkv_bench_16380/data/logs/sunkv.log \
  --enable-console-log 0 --log-level WARN \
  --enable-wal 0 --enable-snapshot 0
redis-cli -h 127.0.0.1 -p 16380 PING
```
---

## 3. 压测参数
统一参数：
- 总请求数：`-n 200000`
- 并发连接：`-c 50`
- 测试命令：`-t set,get`
两组对比：
- **无 pipeline**：`-P 1`（默认）
- **pipeline=16**：`-P 16`
压测命令（CSV 便于记录与对比）：

```bash
# Redis
redis-benchmark -h 127.0.0.1 -p 16379 -n 200000 -c 50 -t set,get --csv
redis-benchmark -h 127.0.0.1 -p 16379 -n 200000 -c 50 -P 16 -t set,get --csv
# SunKV
redis-benchmark -h 127.0.0.1 -p 16380 -n 200000 -c 50 -t set,get --csv
redis-benchmark -h 127.0.0.1 -p 16380 -n 200000 -c 50 -P 16 -t set,get --csv
```
备注：`redis-benchmark` 会尝试 `CONFIG` 获取服务端信息，因此可能出现\n`WARNING: Could not fetch server CONFIG`。\n该 warning 不影响 SET/GET 的实际压测输出。
---
## 4. 原始结果（CSV 摘要）
### 4.1 Redis（127.0.0.1:16379）
无 pipeline（`-P 1`）：
| test | rps | avg_latency_ms | p50 | p95 | p99 | max |
|---|---:|---:|---:|---:|---:|---:|
| SET | 26571.01 | 1.290 | 0.855 | 4.207 | 5.591 | 14.887 |
| GET | 33800.91 | 0.826 | 0.791 | 1.223 | 1.831 | 11.039 |
pipeline=16（`-P 16`）：
| test | rps | avg_latency_ms | p50 | p95 | p99 | max |
|---|---:|---:|---:|---:|---:|---:|
| SET | 179211.45 | 4.315 | 4.135 | 6.063 | 9.471 | 18.367 |
| GET | 184672.22 | 4.193 | 3.975 | 5.679 | 7.919 | 16.111 |
### 4.2 SunKV（127.0.0.1:16380，WAL/Snapshot 关闭）
无 pipeline（`-P 1`）：
| test | rps | avg_latency_ms | p50 | p95 | p99 | max |
|---|---:|---:|---:|---:|---:|---:|
| SET | 27107.62 | 1.090 | 1.015 | 1.807 | 2.975 | 22.111 |
| GET | 26322.72 | 1.041 | 0.999 | 1.439 | 2.255 | 13.815 |
pipeline=16（`-P 16`）：
| test | rps | avg_latency_ms | p50 | p95 | p99 | max |
|---|---:|---:|---:|---:|---:|---:|
| SET | 19193.86 | 0.613 | 0.407 | 1.591 | 3.335 | 20.847 |
| GET | 19277.11 | 0.448 | 0.335 | 0.983 | 2.503 | 12.607 |
---
## 5. 对比结论（本次基线）
### 5.1 pipeline 对 Redis 是正收益，对 SunKV 是负收益
- Redis：`P=16` 将吞吐从 ~2.6–3.4 万 rps 提升到 ~18 万 rps（典型 pipeline 收益）。
- SunKV：`P=16` 反而从 ~2.6–2.7 万 rps 下降到 ~1.9 万 rps。
这说明 SunKV 在当前实现下，瓶颈不在“网络往返被摊薄”，更可能在**服务端的 pipeline 消费/解析/执行/回写路径**上，或存在针对 pipeline 的额外开销。
### 5.2 pipeline 模式下的延迟分位不宜直接与 P=1 做端到端对比
`redis-benchmark` 的 pipeline 统计方式会把批处理的读写行为混入延迟估计。\n因此本文仅把它作为同一测试模式下的参考，不把 pipeline 模式的 p50/p95 当作严格 RTT 结论。
---
## 6. 后续计划（待做）
为了定位 SunKV 在 pipeline 场景掉速的原因，建议补充 3 组对齐测试：
1. **固定 value 大小**：`-d 32` / `-d 256`，观察吞吐随 payload 的变化（解析/拷贝成本）。\n2. **并发 × pipeline 矩阵**：`c=1/10/50/200` × `P=1/4/16/32`，

---
## 7. 问题定位与修复记录（2026-04-20）
在上述基线压测后，进一步代码排查确认 `GET P=16 < P=1` 的核心是逻辑路径问题，而不是“参数没调好”。

### 7.1 发现的问题
1. **输入缓冲前删导致 O(N^2)**  
   `Server::onMessage` 在循环里频繁执行 `pending_input.erase(0, consumed)`；pipeline 批次越大，内存搬移越多。
2. **pipeline 响应逐条发送**  
   每条命令处理完就 `conn->send(...)`，导致 pipeline 场景下产生大量小写入，系统调用/缓冲开销放大。

### 7.2 修复方式
1. **改为偏移推进 + 阈值压缩**  
   在连接解析状态中新增 `pending_offset`，解析时基于 `string_view` 读取未消费区间；仅在满足阈值时做一次 `erase` 压缩。
2. **增加连接内写聚合**  
   在 `TcpConnection` 增加 `beginWriteCoalescing()/endWriteCoalescing()`；`onMessage` 在单轮命令消费期间开启聚合，把多条响应先聚到 `outputBuffer_` 再统一发送。

### 7.3 修复后回归结果（同机复测）
使用脚本：`scripts/redis_benchmark_stable.sh`（`PORT=6382`，`n=30000`，`c=50`，`P=16`）。

| test | 模式 | rps | p50 |
|---|---|---:|---:|
| SET | P=1 | 27881.04 | 0.999 ms |
| GET | P=1 | 26690.39 | 1.015 ms |
| SET | P=16 | 337078.66 | 1.711 ms |
| GET | P=16 | 384615.38 | 1.127 ms |

结论：修复后 `GET` 在 `P=16` 已显著高于 `P=1`，pipeline 吞吐趋势恢复正常。

### 7.4 涉及文件清单（代码落点）
- `server/Server.h`：连接解析状态新增 `pending_offset`，用于未消费区间推进。
- `server/Server.cpp`：`onMessage` 改为偏移推进解析，并在单轮处理中启用写聚合作用域。
- `network/TcpConnection.h`：新增 `beginWriteCoalescing()/endWriteCoalescing()` 接口与聚合状态字段。
- `network/TcpConnection.cpp`：在 `sendInLoop` 中支持聚合模式（先缓冲后统一发送）。

---
## 8. 修复后复测（第二次，2026-04-20）
为验证修复稳定性，追加进行一次同机同参数复测（Redis 与 SunKV 同轮对比）：

- 请求数：`n=200000`
- 并发：`c=50`
- 命令：`SET/GET`
- 模式：`P=1` 与 `P=16`
- 原始输出：`/tmp/redis_sunkv_compare_20260420_165537.txt`

| 系统 | 模式 | SET rps | GET rps |
|---|---|---:|---:|
| Redis | P=1 | 34264.18 | 34322.98 |
| Redis | P=16 | 433839.47 | 497512.44 |
| SunKV | P=1 | 27746.95 | 27329.87 |
| SunKV | P=16 | 312989.03 | 406504.06 |

结论：
- Redis、SunKV 均表现为 `P=16 >> P=1`，pipeline 收益稳定。
- SunKV 在第二次复测中继续保持 `GET P=16` 显著高于 `GET P=1`，与修复目标一致。