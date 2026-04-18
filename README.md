# SunKV

SunKV 是一个基于 C++17 实现的 RESP 兼容键值存储系统，采用多线程 Reactor 网络模型。  
当前主存储实现为 `storage2`，支持 string/list/set/hash 四类数据结构、TTL 语义，以及 WAL 与 Snapshot 组合恢复链路。

## 项目概述

SunKV 面向单机场景，提供以下核心能力：

- **网络层**：基于 `epoll`、`EventLoop` 与线程池的多线程 Reactor 架构
- **协议层**：RESP 解析与序列化，支持半包/粘包与 pipeline 场景
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

客户端 API 详见：`doc/client_api.md`  
头文件入口：

- `client/include/Client.h`
- `client/include/RespValue.h`
- `client/include/Result.h`
- `client/include/Error.h`

## 测试

建议按标签执行回归：

```bash
ctest --test-dir build -L client --output-on-failure
ctest --test-dir build -L network --output-on-failure
ctest --test-dir build -L protocol --output-on-failure
ctest --test-dir build -L server --output-on-failure
ctest --test-dir build -L storage2 --output-on-failure
```

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