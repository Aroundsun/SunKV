# SunKV 项目架构设计文档

## 1. 文档目标与范围

本文面向“完整项目设计说明”场景，覆盖 SunKV 的：

- 总体架构
- 模块分层与职责边界
- 核心调用链（启动、请求处理、写入、恢复、关闭）
- 持久化机制（WAL + Snapshot）
- 观测性、测试与 CI 工程化链路

核心代码目录：

- `server/`
- `network/`
- `protocol/`
- `storage2/`
- `client/`
- `common/`
- `test/`
- `scripts/`

---

## 2. 总体架构

### 2.1 总体分层图

```mermaid
flowchart TD
  clientLayer["Client/CLI Layer - client/ + sunkvClient"]
  transportLayer["Network Reactor Layer - network/"]
  protocolLayer["RESP Protocol Layer - protocol/"]
  serviceLayer["Service Orchestration Layer - server/"]
  storageLayer["Storage Core Layer - storage2/engine + api"]
  persistenceLayer["Persistence Layer - storage2/persistence"]
  commonLayer["Common Infra Layer - common/"]

  clientLayer --> transportLayer
  transportLayer --> protocolLayer
  protocolLayer --> serviceLayer
  serviceLayer --> storageLayer
  storageLayer --> persistenceLayer
  serviceLayer --> commonLayer
  transportLayer --> commonLayer
  storageLayer --> commonLayer
```

### 2.2 运行时组件图

```mermaid
flowchart LR
  user["redis-cli / sunkvClient"]
  tcp["TcpServer + EventLoopThreadPool"]
  server["Server - processCommand / dispatch"]
  dispatch["ArrayCmdDispatch"]
  storageApi["IStorageAPI - OrchestratedStorageAPI"]
  engine["StorageEngine"]
  orchestrator["PersistenceOrchestrator"]
  wal["WAL Files"]
  snap["Snapshot File"]

  user --> tcp
  tcp --> server
  server --> dispatch
  dispatch --> storageApi
  storageApi --> engine
  storageApi --> orchestrator
  orchestrator --> wal
  orchestrator --> snap
```

---

## 3. 模块架构与职责

## 3.1 `server/`（服务编排层）

职责：

- 生命周期管理（启动、停止、优雅关闭）
- 网络回调绑定（连接、消息、写完成）
- RESP 请求级流程控制（事务态、订阅态、命令门禁）
- 命令分发到 `ArrayCmdDispatch`
- 观测指标统计与慢命令日志

关键文件：

- `server/main.cpp`
- `server/Server.h`
- `server/Server.cpp`
- `server/ArrayCmdDispatch.h`
- `server/ArrayCmdDispatch.cpp`

### `server/` 内部结构图

```mermaid
flowchart TD
  serverMain["main.cpp"]
  serverCore["Server"]
  connState["ConnParseState - pending_input/parser/txn/pubsub"]
  cmdDispatch["ArrayCmdDispatch"]
  stats["Metrics + Slowlog"]

  serverMain --> serverCore
  serverCore --> connState
  serverCore --> cmdDispatch
  serverCore --> stats
```

## 3.2 `network/`（网络与并发底座）

职责：

- 多线程 Reactor（EventLoop + Poller + Channel）
- 连接接入（Acceptor）
- 连接对象生命周期（TcpConnection）
- 输入/输出缓冲（Buffer）
- 回调桥接到 `server`

关键文件：

- `network/EventLoop.*`
- `network/Poller.*`
- `network/Channel.*`
- `network/Acceptor.*`
- `network/TcpServer.*`
- `network/TcpConnection.*`
- `network/Buffer.*`

### 网络层类关系图

```mermaid
flowchart TD
  eventLoop["EventLoop"]
  poller["Poller(epoll)"]
  channel["Channel(fd callbacks)"]
  acceptor["Acceptor"]
  tcpServer["TcpServer"]
  tcpConn["TcpConnection"]
  buffer["Buffer"]

  eventLoop --> poller
  poller --> channel
  acceptor --> channel
  tcpConn --> channel
  tcpServer --> acceptor
  tcpServer --> tcpConn
  tcpConn --> buffer
```

## 3.3 `protocol/`（RESP 协议层）

职责：

- 增量解析 RESP（支持半包/粘包/多命令）
- 响应序列化（simple string/error/bulk/integer/array）

关键文件：

- `protocol/RESPParser.*`
- `protocol/RESPSerializer.*`
- `protocol/RESPType.*`

### 协议处理图

```mermaid
flowchart LR
  bytesIn["Byte Stream"]
  parser["RESPParser - incremental parse"]
  value["RESPValue Tree"]
  serializer["RESPSerializer"]
  bytesOut["RESP Bytes"]

  bytesIn --> parser
  parser --> value
  value --> serializer
  serializer --> bytesOut
```

## 3.4 `storage2/`（存储与持久化核心）

职责：

- 数据模型与命令语义（string/list/set/hash + TTL）
- 变更抽象（MutationBatch）
- 持久化编排（WAL、Snapshot、恢复）
- 工厂组装（Factory）

关键子模块：

- `storage2/engine/`
- `storage2/model/`
- `storage2/persistence/`
- `storage2/decorators/`
- `storage2/Factory.*`

### `storage2` 分层图

```mermaid
flowchart TD
  api["IStorageAPI"]
  orchApi["OrchestratedStorageAPI"]
  engine["StorageEngine"]
  backend["InMemoryBackend"]
  mutation["MutationBatch"]
  orchestrator["PersistenceOrchestrator"]
  walPart["WalWriter/WalReader/WalCodec"]
  snapPart["SnapshotWriter/SnapshotReader"]

  api --> orchApi
  orchApi --> engine
  engine --> backend
  engine --> mutation
  orchApi --> orchestrator
  orchestrator --> walPart
  orchestrator --> snapPart
```

## 3.5 `client/`（客户端层）

职责：

- 连接服务端
- 命令封装（typed API + pipeline）
- RESP 客户端编解码

关键文件：

- `client/include/Client.h`
- `client/src/Client.cpp`
- `client/sunkvClient.cpp`

---

## 4. 关键调用链设计

## 4.1 服务启动调用链

```mermaid
sequenceDiagram
  participant Main as main.cpp
  participant S as Server
  participant F as storage2::Factory
  participant T as TcpServer
  participant L as EventLoop

  Main->>S: Server::start()
  S->>S: initializeStorage()
  S->>F: createStorage2(options)
  F-->>S: Storage2Components
  S->>S: initializeNetwork()
  S->>T: setupConnectionCallbacks()
  S->>T: TcpServer::start()
  S->>L: main_loop->loop()
```

## 4.2 单请求处理调用链（收包到回包）

```mermaid
sequenceDiagram
  participant C as Client
  participant EL as EventLoop/Poller
  participant TC as TcpConnection
  participant SV as Server::onMessage
  participant RP as RESPParser
  participant PC as Server::processCommand
  participant AD as ArrayCmdDispatch
  participant SA as storage2 api

  C->>EL: send RESP bytes
  EL->>TC: readable event
  TC->>SV: messageCallback(conn, buffer, len)
  SV->>RP: parse(string_view)
  RP-->>SV: ParseResult(complete)
  SV->>PC: processCommand(conn, ctx, value)
  PC->>AD: dispatchArrayCommandsLookup(...)
  AD->>SA: set/get/... command
  SA-->>AD: result
  AD-->>PC: RESP response
  PC-->>TC: conn->send(...)
  TC-->>C: RESP bytes
```

## 4.3 事务调用链（MULTI / EXEC）

```mermaid
sequenceDiagram
  participant C as Client
  participant S as Server::processCommand
  participant Q as ConnParseState::queued_commands
  participant D as executeArrayCommandToResp

  C->>S: MULTI
  S->>Q: in_multi=true, clear queue
  S-->>C: +OK

  C->>S: SET/GET...
  S->>Q: enqueue command IR
  S-->>C: +QUEUED

  C->>S: EXEC
  S->>Q: iterate queued commands
  S->>D: executeArrayCommandToResp(each)
  D-->>S: RESP item
  S-->>C: RESP array(all results)
```

## 4.4 Pub/Sub 调用链

```mermaid
sequenceDiagram
  participant Sub as SubscriberConn
  participant Pub as PublisherConn
  participant S as Server
  participant M as channel_subscribers_

  Sub->>S: SUBSCRIBE ch
  S->>M: add subscriber
  S-->>Sub: subscribe ack

  Pub->>S: PUBLISH ch msg
  S->>M: lookup subscribers
  S-->>Sub: ["message", ch, msg]
  S-->>Pub: :delivered_count
```

## 4.5 写入持久化调用链（含异步队列）

```mermaid
sequenceDiagram
  participant Cmd as Command Handler
  participant API as OrchestratedStorageAPI
  participant Eng as StorageEngine
  participant Orc as PersistenceOrchestrator
  participant Wal as WalWriter

  Cmd->>API: set/get-expire-path
  API->>Eng: mutate data
  Eng-->>API: StorageResult + MutationBatch
  API->>Orc: submit(mutations)
  Orc->>Wal: appendBytes(encoded mutations)
  Wal-->>Orc: written
```

## 4.6 恢复调用链（Snapshot -> WAL）

```mermaid
sequenceDiagram
  participant S as Server::initializeStorage
  participant Orc as PersistenceOrchestrator
  participant Snap as SnapshotReader
  participant WalR as WalReader
  participant Eng as StorageEngine

  S->>Orc: recoverInto(engine)
  Orc->>Snap: read snapshot file
  Snap-->>Orc: records
  Orc->>Eng: loadSnapshot(records)
  Orc->>WalR: read WAL chain
  WalR-->>Orc: mutations
  loop replay
    Orc->>Eng: applyMutation(m)
  end
```

## 4.7 关闭调用链（优雅关闭）

```mermaid
flowchart TD
  stopReq["stop requested"]
  stopThreads["stop signal/ttl/stats threads"]
  stopTcp["TcpServer::stop"]
  stopLoop["main loop quit"]
  waitConn["wait connections close"]
  finalSnap["take final snapshot"]
  walFlush["orchestrator flush WAL"]
  done["shutdown complete"]

  stopReq --> stopThreads
  stopThreads --> stopTcp
  stopTcp --> stopLoop
  stopLoop --> waitConn
  waitConn --> finalSnap
  finalSnap --> walFlush
  walFlush --> done
```

---

## 5. 观测性架构

核心内容：

- 命令耗时统计（累计与最大）
- 错误命令计数
- 慢命令计数与结构化日志
- Pub/Sub 指标（频道数、订阅关系数、发布数、投递数）
- `STATS/MONITOR/DEBUG INFO` 统一输出 `key=value`

### 观测数据采集图

```mermaid
flowchart LR
  cmd["processCommand"]
  scope["CommandMetricsScope"]
  rec["recordCommandMetrics_"]
  stats["atomic counters"]
  report["buildStatsReport()"]
  out["STATS/MONITOR/DEBUG INFO"]
  slow["slow_command log"]

  cmd --> scope
  scope --> rec
  rec --> stats
  stats --> report
  report --> out
  rec --> slow
```

---

## 6. 工程化交付架构

## 6.1 测试架构图

```mermaid
flowchart TD
  testRoot["test/"]
  clientTests["client tests"]
  networkTests["network tests"]
  protocolTests["protocol tests"]
  serverTests["server tests"]
  storageTests["storage2 tests"]

  testRoot --> clientTests
  testRoot --> networkTests
  testRoot --> protocolTests
  testRoot --> serverTests
  testRoot --> storageTests
```

## 6.2 CI 分层图（fast/full）

```mermaid
flowchart LR
  pr["PR / Push"]
  fast["CI Fast - format + tidy + build + core smoke"]
  full["CI Full - build + full ctest + artifact upload"]
  logs["Artifacts - ctest logs + data/logs"]

  pr --> fast
  pr --> full
  fast --> logs
  full --> logs
```

## 6.3 演示与交付链路图

```mermaid
flowchart LR
  demo["scripts/demo_all_in_one.sh"]
  func["functional_suite.sh"]
  bench["redis_benchmark_stable.sh"]
  sum["summary.md"]
  runbook["doc/demo_runbook.md"]
  delivery["doc/final_delivery_2026Q2.md"]
  pitch["doc/interview_pitch_5min.md"]

  demo --> func
  demo --> bench
  demo --> sum
  sum --> runbook
  runbook --> delivery
  delivery --> pitch
```

---

## 7. 架构关键设计原则

- 分层清晰：网络/协议/业务/存储/持久化职责分离
- 增量解析：面向真实网络半包粘包与 pipeline 场景
- 语义优先：事务、Pub/Sub、TTL 等行为与错误路径可验证
- 可恢复性：Snapshot + WAL 双层恢复链路
- 可观测性：命令级埋点和结构化慢日志
- 可交付性：测试分层、CI 分层、演示脚本和文档闭环

---

## 8. 建议的后续演进图（Roadmap）

```mermaid
flowchart TD
  current["Current - single-node + WAL/Snapshot + txn/pubsub/obs"]
  cluster["Cluster/Replication"]
  auth["ACL/Auth"]
  memory["Memory governance - LRU/LFU/eviction"]
  observability["Metrics export - Prometheus/OpenTelemetry"]
  ci["Performance regression gate"]

  current --> cluster
  current --> auth
  current --> memory
  current --> observability
  current --> ci
```

本文档可作为项目评审、面试讲解、二次开发设计输入的统一基线文档。
