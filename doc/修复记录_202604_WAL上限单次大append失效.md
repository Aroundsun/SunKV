# 修复记录：WAL 上限在单次大 append 下失效

## 背景

在开启 `max_wal_file_size_mb`（例如 100MB）时，线上观察到 `wal2.bin` 仍可能超过上限且未及时滚动。  
该问题在异步 group commit 场景更容易触发：一次 `appendBytes` 传入的是合并后的大 buffer，而不是单条 mutation。

## 现象

- 配置了 `max_wal_file_size_mb > 0`
- 仍出现单个活跃 `wal2.bin` 明显超过上限
- 目录中未按预期生成足够的 `wal2.bin.N` 滚动文件

## 根因

旧逻辑按“单次写入前判断”处理滚动，存在场景会放过超大单次写入：

- 当一次 `appendBytes(data, len)` 的 `len` 很大（例如来自 group commit 合并）
- 若当前文件接近空或判断分支未覆盖“单次超大 buffer”
- 则会一次性写入，导致活跃 WAL 超过上限

本质上，旧实现将“上限约束”理解为“写前判断一次”，而不是“写入过程持续约束”。

## 修复方案

在 `WalWriter::appendBytes` 中改为**分块写入 + 过程内滚动**：

1. `max_file_bytes_ == 0`：保持不限制逻辑，直接写入。
2. `max_file_bytes_ > 0`：
   - 循环直到 `offset == len`
   - 每轮 `fstat(fd_)` 获取当前文件大小 `cur`
   - 若 `cur >= max`，先 `rotate_()` 再继续
   - 计算剩余空间 `room = max - cur`
   - 本轮只写 `chunk = min(remaining, room)`
   - 写满后下一轮再判断是否滚动

同时，`fstat` 失败改为直接返回 `false`，避免静默跳过大小控制。

## 关键代码位置

- 修复实现：`storage2/persistence/WalWriter.cpp` 的 `appendBytes(...)`
- 滚动实现：`storage2/persistence/WalWriter.cpp` 的 `rotate_()`

## 回归测试

新增测试：`test/storage2/storage2_wal_max_file_enforced_test.cpp`

测试构造：

- `kMax = 64KB`
- 单次 `appendBytes` 传入 `kPayload = 200KB`

断言：

1. 活跃 WAL 文件大小 `<= kMax`
2. 存在滚动文件（如 `.1`）
3. WAL 链总字节数等于原始 payload（无丢失）

并在 `CMakeLists.txt` 中注册为：
- 可执行：`storage2_wal_max_file_enforced_test`
- 测试项：`storage2_wal_max_file_enforced`

## 影响与收益

- 修复后，`max_wal_file_size_mb` 对“单次大 append”也生效
- 避免活跃 WAL 异常膨胀
- 保持数据完整性（链总字节不变）
- 与异步 group commit 更一致

## 备注

该修复不改变 WAL 语义（仍为追加日志），只修正“上限约束在写入过程中的执行方式”。