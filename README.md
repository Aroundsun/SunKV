# SunKV

基于 C++17 的高性能 KV 存储系统，兼容 RESP 协议，采用多线程 Reactor 架构，支持 WAL + Snapshot 持久化恢复。

## 当前状态

- 阶段进度：已完成 `8.1 功能测试`、`8.2 性能优化阶段`、`8.3 稳定性测试`
- 8.2 结果：性能较基线显著提升，当前主要区间约 `SET 24k~25k` / `GET 30k~31k`
- 8.3 结果：1 小时长跑、内存趋势、崩溃恢复（snapshot 模式）、压力测试均通过

说明：性能目标 `QPS >= 50,000` 尚未完全达成，后续将继续微优化高频路径。

## 主要特性

- 高并发网络模型：多线程 Reactor（`epoll`）
- RESP 协议支持：含 pipeline 场景
- 存储与持久化：多类型内存存储 + WAL + Snapshot
- 可观测能力：统计/调试命令与阶段性性能记录
- 测试脚本：功能、性能、稳定性测试脚本持续完善

## 构建与运行

### 依赖要求

- Linux
- CMake >= 3.10
- GCC/Clang（支持 C++17）

### 编译

```bash
cd SunKV
mkdir -p build
cd build
cmake ..
cmake --build . -j$(nproc)
```

### 构建类型与压测建议

- **Debug vs Release**
  - **Release**（推荐用于压测/对比性能）：默认启用优化，性能数据更接近真实表现。
  - **Debug**（推荐用于排障）：通常包含更多断言与更详细的日志，便于定位问题，但会显著影响性能。
- **断言（assert）与 `-DNDEBUG`**
  - 常见情况下 **Release 会启用 `-DNDEBUG`**，这会让 `assert(...)` 在编译期被移除。
  - 因此某些仅靠断言暴露的生命周期/线程不变式问题，在 Release 可能不会“立即崩溃”，而会以更隐蔽的方式表现；排查此类问题建议优先用 Debug 构建复现。
- **日志级别对性能的影响**
  - Debug 级别下会产生大量日志（即使做了采样，仍可能影响延迟/QPS），压测建议使用 `--log-level INFO`（或更高）。

### 启动

```bash
./build/sunkv --port 6379
```

可选参数（示例）：

```bash
./build/sunkv --port 6379 --thread-pool-size 4 --max-connections 2000
```

说明：
- 日志默认写入 `./data/logs/sunkv.log`，并通过 rotating 机制自动滚动。

## 测试入口

- 功能测试（8.1）：`scripts/functional_suite.sh`
- 性能与 profiling（8.2）：`scripts/perf_profile.sh`
- 稳定性测试（8.3）：`scripts/stability_suite.sh`

## 文档索引

- 总体计划：`doc/开发计划.md`
- 架构设计：`doc/设计文档.md`
- 接口说明：`doc/API文档.md`
- 7.2 优化详记：`doc/7.2性能优化详细记录.md`
- 8.2 过程记录：`doc/8.2性能测试记录.md`
- 8.2 总览版：`doc/8.2性能优化总览.md`
- 8.3 稳定性记录：`doc/8.3稳定性测试记录.md`

## 项目目录（简版）

```text
SunKV/
├── server/      # 服务端主流程
├── network/     # 网络与事件循环
├── protocol/    # RESP 解析/序列化
├── storage/     # 存储引擎与数据结构
├── common/      # 通用模块（配置、数据结构、内存池等）
├── test/        # 测试与阶段脚本
└── doc/         # 阶段记录与设计文档
```