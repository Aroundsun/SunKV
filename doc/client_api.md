# SunKV C++ Client API 文档

本文档对应当前客户端头文件：`client/include/Client.h`。

## 1. 命名空间与核心类型

- 命名空间：`sunkv::client`
- 核心头文件：
  - `client/include/Client.h`
  - `client/include/Result.h`
  - `client/include/Error.h`
  - `client/include/RespValue.h`

## 2. 错误模型

客户端统一使用 `Result<T>` 返回值：

- `ok == true`：调用成功，结果在 `value`
- `ok == false`：调用失败，错误在 `error`

`ErrorCode` 定义于 `client/include/Error.h`：

- `Ok`
- `NotConnected`
- `ConnectFailed`
- `Timeout`
- `IoError`
- `ProtocolError`
- `ServerError`
- `InvalidResponse`
- `InvalidArgument`

## 3. 连接与生命周期

### 3.1 连接参数

`Client::Options`：

- `host`：默认 `127.0.0.1`
- `port`：默认 `6380`
- `connect_timeout_ms`：连接超时
- `read_timeout_ms`：读超时
- `write_timeout_ms`：写超时

### 3.2 生命周期 API

- `Result<void> connect()`
- `void close()`
- `bool isConnected() const`

说明：

- `connect()` 可重复调用；已连接时返回成功。
- 发生 I/O 级别断连时，客户端会在内部关闭 fd，后续调用需要重新 `connect()`。

## 4. 通用命令 API

### 4.1 单命令

- `Result<RespValue> command(const std::vector<std::string>& args)`

用途：

- 发送任意 RESP Array 命令（如 `{ "SET", "k", "v" }`）
- 适合 typed API 未覆盖的命令形态

行为：

- 服务端返回 RESP Error 时，返回 `ok=false` 且 `error.code=ServerError`

### 4.2 pipeline

- `Result<std::vector<RespValue>> pipeline(const std::vector<std::vector<std::string>>& commands)`
- `Result<std::vector<RespValue>> pipeline(const std::vector<std::vector<std::string>>& commands, const PipelineOptions& options)`

`PipelineOptions`：

- `fail_on_server_error`：是否在遇到服务端错误时立即失败
- `close_on_server_error`：严格失败时是否关闭连接，避免剩余响应污染会话

## 5. Typed API 一览

## 5.1 基础/管理命令

- `ping() -> Result<std::string>`
- `get(key) -> Result<std::optional<std::string>>`
- `set(key, value) -> Result<void>`
- `del(key) -> Result<int64_t>`
- `exists(keys) -> Result<int64_t>`
- `keys() -> Result<std::vector<std::string>>`
- `dbsize() -> Result<int64_t>`
- `flushall() -> Result<void>`
- `stats() -> Result<std::string>`
- `monitor() -> Result<std::string>`
- `snapshot() -> Result<void>`
- `health() -> Result<void>`
- `debugInfo() -> Result<std::string>`
- `debugResetStats() -> Result<void>`

## 5.2 TTL

- `expire(key, seconds) -> Result<int64_t>`
- `ttl(key) -> Result<int64_t>`
- `pttl(key) -> Result<int64_t>`
- `persist(key) -> Result<int64_t>`

## 5.3 List

- `lpush(key, values) -> Result<int64_t>`
- `rpush(key, values) -> Result<int64_t>`
- `lpop(key) -> Result<std::optional<std::string>>`
- `rpop(key) -> Result<std::optional<std::string>>`
- `llen(key) -> Result<int64_t>`
- `lindex(key, index) -> Result<std::optional<std::string>>`

## 5.4 Set

- `sadd(key, members) -> Result<int64_t>`
- `srem(key, members) -> Result<int64_t>`
- `scard(key) -> Result<int64_t>`
- `sismember(key, member) -> Result<int64_t>`
- `smembers(key) -> Result<std::vector<std::string>>`

## 5.5 Hash

- `hset(key, field_values) -> Result<int64_t>`
- `hget(key, field) -> Result<std::optional<std::string>>`
- `hdel(key, fields) -> Result<int64_t>`
- `hlen(key) -> Result<int64_t>`
- `hexists(key, field) -> Result<int64_t>`
- `hgetall(key) -> Result<std::vector<std::pair<std::string, std::string>>>`

## 5.6 批量封装

- `mget(keys) -> Result<std::vector<std::optional<std::string>>>`
- `mset(kvs) -> Result<void>`

说明：

- `mget/mset` 基于 `pipeline` 组合实现。

## 5.7 事务

- `multi() -> Result<void>`
- `exec() -> Result<std::vector<RespValue>>`
- `discard() -> Result<void>`

返回语义：

- `multi()`：期望 `+OK`
- `exec()`：期望 RESP Array（每个元素是事务中一条命令的执行结果）
- `discard()`：期望 `+OK`

注意：

- 在事务中发送普通命令（如 `SET/GET`）会收到 `QUEUED`（SimpleString），可通过 `command({...})` 继续入队。
- 若不在事务态调用 `exec()/discard()`，会返回 `ServerError`（与服务端错误一致）。

## 5.8 发布订阅

- `publish(channel, payload) -> Result<int64_t>`
- `subscribe(channels) -> Result<RespValue>`
- `unsubscribe(channels = {}) -> Result<RespValue>`

返回语义：

- `publish()`：返回投递到的订阅者数量（Integer）
- `subscribe()/unsubscribe()`：返回服务端的 RESP Array 回包（兼容 Redis 形态）

注意：

- `subscribe()` 要求至少一个 channel，否则返回 `InvalidArgument`。
- 进入订阅态后，服务端仅允许 `SUBSCRIBE/UNSUBSCRIBE/PING/QUIT`，其他命令会报错；建议用独立连接做订阅消费。

## 6. RespValue 说明

`RespValue`（`client/include/RespValue.h`）支持：

- `SimpleString`
- `Error`
- `Integer`
- `BulkString`
- `Array`
- `NullBulkString`

显示辅助：

- `std::string toDisplayString(const RespValue&)`

## 7. 最小示例

```cpp
#include "client/include/Client.h"
#include <iostream>

int main() {
    sunkv::client::Client::Options opts;
    opts.host = "127.0.0.1";
    opts.port = 6379;

    sunkv::client::Client c(opts);
    auto conn = c.connect();
    if (!conn.ok) {
        std::cerr << "connect failed: " << conn.error.message << "\n";
        return 1;
    }

    auto set = c.set("k", "v");
    if (!set.ok) {
        std::cerr << "set failed: " << set.error.message << "\n";
        return 1;
    }

    auto get = c.get("k");
    if (!get.ok) {
        std::cerr << "get failed: " << get.error.message << "\n";
        return 1;
    }

    if (get.value.has_value()) {
        std::cout << "k=" << *get.value << "\n";
    } else {
        std::cout << "k not found\n";
    }

    c.close();
    return 0;
}
```

## 8. 兼容性与建议

- 客户端为同步模型，适合工具链与业务侧轻量接入。
- 对于未封装或新扩展命令，优先使用 `command(args)`。
- 建议业务侧统一检查 `Result<T>::ok`，并记录 `error.code + error.message`。
- 对事务和 Pub/Sub，建议按“一个连接一个职责”使用：普通请求连接、事务连接、订阅连接分离，避免状态互相影响。
