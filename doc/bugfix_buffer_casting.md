# Bug Fix: Buffer Type Casting Issue

## 问题描述

**时间**: 2026-04-02 01:15
**严重程度**: 高
**影响**: 服务器无法正确解析客户端命令

## 症状

1. 客户端发送正确的 RESP 格式命令（如 `*1\r\n$4\r\nPING\r\n`）
2. 服务器接收到乱码数据（如 `�Y���^�]���^`）
3. 十六进制数据完全错误（如 `f0 59 9b 97 8e 5e` 而不是 `2a 31 d a 24 34 d a 50 49 4e 47 d a`）
4. RESP 解析器无法解析，返回 "incomplete" 状态
5. 服务器始终返回版本信息而不是执行命令

## 根本原因

在 `Server::onMessage()` 方法中，错误地将 `void* data` 参数转换为 `char*`：

```cpp
// 错误的转换方式
std::string message(static_cast<char*>(data), len);
```

但实际上，`data` 参数是 `Buffer*` 类型，不是 `char*`。这种错误的类型转换导致：
1. 内存布局错误
2. 字节值被错误解释
3. 数据损坏

## 解决方案

使用正确的 Buffer API 来转换数据：

```cpp
// 正确的转换方式
Buffer* buffer = static_cast<Buffer*>(data);
std::string message = buffer->retrieveAsString(len);
```

## 修复文件

- `/home/xhy/mycode/SunKV/server/Server.cpp`
  - `onMessage()` 方法中的数据转换逻辑
  - 添加 `#include "../network/Buffer.h"`

## 测试结果

修复后：
- ✅ 服务器正确接收 RESP 命令
- ✅ PING 命令返回 `+PONG`
- ✅ 十六进制数据正确：`2a 31 d a 24 34 d a 50 49 4e 47 d a`
- ✅ 客户端-服务器通信正常

## 经验教训

1. **类型安全**: 在处理网络数据时，必须确保正确的类型转换
2. **Buffer API**: 使用专门的 Buffer 类方法而不是直接指针转换
3. **调试技巧**: 十六进制调试帮助快速定位数据传输问题
4. **网络编程**: TCP 流数据的字节级处理需要特别注意

## 相关文件

- `network/Buffer.h` - Buffer 类定义
- `network/Buffer.cpp` - Buffer 类实现
- `server/Server.cpp` - 服务器主逻辑
- `protocol/RESPParser.cpp` - RESP 协议解析器
