# SunKV

SunKV 是一个基于 C++17 实现的 RESP 兼容键值存储系统，采用多线程 Reactor 网络模型。  
当前主存储实现为 `storage2`，支持 string/list/set/hash 四类数据结构、TTL 语义，以及 WAL 与 Snapshot 组合恢复链路。

## 项目概述

SunKV 面向单机场景，提供以下核心能力：

- **网络层**：基于 `epoll`、`EventLoop` 与线程池的多线程 Reactor 架构
- **协议层**：RESP 解析与序列化，支持半包/粘包与 pipeline 场景
- **事务子集**：支持连接级 `MULTI/EXEC/DISCARD` 事务队列执行
- **发布订阅**：支持最小闭环 `SUBSCRIBE/UNSUBSCRIBE/PUBLISH`
- **存储层**：`storage2` 统一实现，覆盖多数据类型与惰性过期语义
- **持久化层**：WAL + Snapshot，并通过 `PersistenceOrchestrator` 进行恢复编排
- **测试体系**：按模块分层组织（`client` / `network` / `protocol` / `server` / `storage2`）
- **客户端能力**：提供同步 C++ SDK 及 CLI 工具 `sunkvClient`

## 构建与运行

### 环境依赖

- Linux
- CMake >= 3.10
- GCC 或 Clang（支持 C++17）
- `spdlog`、`fmt`、`Threads`

### 编译

```bash
cd SunKV
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j$(nproc)
```

### 启动服务端

```bash
./build/sunkv --port 6379
```

可选参数示例：

```bash
./build/sunkv --port 6379 --thread-pool-size 4 --max-connections 2000
```

## 客户端

### CLI 用法

```bash
# 单次命令模式
./build/sunkvClient 127.0.0.1 6379 PING

# 交互模式
./build/sunkvClient 127.0.0.1 6379
```

### C++ SDK 能力（同步）

- 连接管理：`connect()` / `close()` / `isConnected()`
- 通用命令：`command(args)`（完整 RESP 响应解析）
- 批量命令：`pipeline(commands)`（一次写入、顺序读取多响应）
  - 支持严格模式（遇到服务端错误立即失败，并可选择关闭连接）
- typed string 与管理命令：  
  `ping/get/set/del/exists/keys/dbsize/flushall/stats/monitor/snapshot/health/debugInfo/debugResetStats`
- typed TTL 命令：`expire/ttl/pttl/persist`
- typed list 命令：`lpush/rpush/lpop/rpop/llen/lindex`
- typed set 命令：`sadd/srem/scard/sismember/smembers`
- typed hash 命令：`hset/hget/hdel/hlen/hexists/hgetall`
- typed 批量封装：`mget/mset`
- typed 事务命令：`multi/exec/discard`
- typed 发布订阅：`publish/subscribe/unsubscribe`

事务与 Pub/Sub 使用建议：

- 事务中可继续通过 `command({...})` 发送普通命令入队，收到 `QUEUED` 后由 `exec()` 一次性返回结果数组。
- 订阅连接进入 subscribe 模式后有命令白名单限制，建议使用独立连接做订阅消费，业务读写使用另一条连接。

客户端 API 详见：`doc/client_api.md`  
头文件入口：

- `client/include/Client.h`
- `client/include/RespValue.h`
- `client/include/Result.h`
- `client/include/Error.h`

## 事务子集（MULTI / EXEC / DISCARD）

当前版本已支持最小闭环事务语义（连接级）：

- `MULTI`：进入事务态，后续普通数组命令入队并返回 `+QUEUED`
- `EXEC`：按入队顺序逐条执行，并一次性返回 RESP 数组结果
- `DISCARD`：清空队列并退出事务态

### 当前限制

- `WATCH/UNWATCH` 暂不支持，返回：`-ERR WATCH/UNWATCH is not supported`
- 不支持嵌套事务（重复 `MULTI` 返回错误）
- `EXEC` 中单条命令失败仅影响对应结果项，不回滚已执行命令

### 快速示例

```bash
redis-cli -p 6379 MULTI
redis-cli -p 6379 SET a 1
redis-cli -p 6379 GET a
redis-cli -p 6379 EXEC
```

## 发布订阅（SUBSCRIBE / UNSUBSCRIBE / PUBLISH）

当前版本已支持最小闭环 Pub/Sub 语义：

- `SUBSCRIBE channel [channel ...]`：订阅频道，回包为 Redis 风格数组
- `UNSUBSCRIBE [channel ...]`：取消订阅，未传频道时取消全部订阅
- `PUBLISH channel message`：广播消息并返回投递数量

### 订阅态限制

- 进入订阅态后仅允许：`SUBSCRIBE/UNSUBSCRIBE/PING/QUIT`
- 其他命令返回：`-ERR only SUBSCRIBE/UNSUBSCRIBE/PING/QUIT allowed in this context`
- 连接断开后自动清理该连接的订阅关系

### 快速示例

```bash
# 终端 A
redis-cli -p 6379 SUBSCRIBE ch1

# 终端 B
redis-cli -p 6379 PUBLISH ch1 hello
redis-cli -p 6379 UNSUBSCRIBE ch1
```

## 可观测性（Week3 第一版）

当前版本已接入命令级埋点与慢日志最小闭环，`STATS/MONITOR/DEBUG INFO` 输出沿用 `key=value` 文本格式。

- 新增命令指标：`total_command_errors`、`total_slow_commands`
- 新增时延指标：`avg_command_latency_us`、`max_command_latency_us`
- 新增缓冲指标：`max_conn_input_buffer_bytes`、`output_buffer_peak_bytes`
- 新增 Pub/Sub 指标：`pubsub_channel_count`、`pubsub_subscription_count`、`pubsub_publish_total`、`pubsub_delivered_total`

### 慢日志配置

- `enable_slowlog`：是否打印慢命令结构化日志（默认 `false`）
- `slowlog_threshold_ms`：慢命令阈值（毫秒，默认 `20`）
- 即使关闭打印，慢命令计数仍会累计到 `total_slow_commands`

示例：

```bash
./build/sunkv --port 6379 --enable-slowlog true --slowlog-threshold-ms 10
redis-cli -p 6379 STATS
```

## 测试

建议按标签执行回归：

```bash
ctest --test-dir build -L client --output-on-failure
ctest --test-dir build -L network --output-on-failure
ctest --test-dir build -L protocol --output-on-failure
ctest --test-dir build -L server --output-on-failure
ctest --test-dir build -L storage2 --output-on-failure
```

事务路径可单独执行：

```bash
ctest --test-dir build -R server_transaction_multi_exec --output-on-failure
```

Pub/Sub 路径可单独执行：

```bash
ctest --test-dir build -R server_pubsub_integration --output-on-failure
```

可观测性路径可单独执行：

```bash
ctest --test-dir build -R server_observability_info --output-on-failure
```

第4周新增稳定性回归与异常专项可单独执行：

```bash
ctest --test-dir build -R "server_regression_matrix|server_resp_error_recovery|server_input_buffer_limit|server_half_close_inflight_request|client_disconnect_recovery" --output-on-failure
```

## 最终演示入口

- 一键演示脚本：`scripts/demo_all_in_one.sh`
- 运行手册：`doc/demo_runbook.md`
- 最终交付总结：`doc/final_delivery_2026Q2.md`
- 5 分钟讲解提纲：`doc/interview_pitch_5min.md`

## 开发与质量门禁

- 格式化：`.clang-format`
- 静态检查：`.clang-tidy`
- 本地运行与 CI 门禁说明见：`doc/质量门禁与贡献指南.md`

## 版本与兼容性说明

- **项目版本阶段**：当前仓库处于持续演进阶段，建议以主分支最新代码与对应测试结果作为行为基准。
- **协议兼容性**：SunKV 采用 RESP 协议并兼容 Redis 风格命令交互，但并非 Redis 全量语义实现。
- **命令兼容范围**：当前已覆盖 string/list/set/hash、TTL 及部分管理命令；未实现命令会返回标准错误响应。
- **平台与工具链**：主要面向 Linux 环境，使用 C++17、CMake 构建；建议 GCC/Clang 的现代版本。
- **稳定性建议**：生产或压测场景建议固定构建参数（如 `Release`）并配套回归测试标签执行结果使用。

## 目录结构

```text
SunKV/
├── server/                 # 服务端主流程（命令分发、生命周期）
├── network/                # 网络与事件循环
├── protocol/               # RESP 解析与序列化
├── storage2/               # 存储引擎、持久化与模型定义
├── client/                 # 客户端 SDK 与 CLI
├── common/                 # 配置、内存池等公共组件
├── test/
│   ├── client/
│   ├── network/
│   ├── protocol/
│   ├── server/
│   └── storage2/
└── doc/                    # 设计说明与阶段记录
```

## 说明

- 项目当前以 `storage2` 为唯一主存储路径。
- 性能压测建议使用 `Release` 构建，并适当降低日志级别以减少额外开销。
- 稳定压测可使用脚本：`scripts/redis_benchmark_stable.sh`（覆盖 SET/GET 与 pipeline 对照）。