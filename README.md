# SunKV

一个高性能的 KV 存储系统，基于 C++17 实现。

## 项目特性

- **高性能**: 基于多线程 Reactor 架构，支持高并发
- **RESP 协议**: 兼容 Redis 协议
- **持久化**: 支持 WAL + Snapshot 数据恢复
- **Pipeline**: 支持命令管道处理
- **LRU 淘汰**: 内存管理策略

## 性能目标

- QPS ≥ 50,000（单机）
- 支持 ≥ 1,000,000 keys
- 平均延迟 < 1ms（本地测试）
- 支持 ≥ 1000 并发连接

## 构建要求

- CMake ≥ 3.10
- GCC ≥ 7.0 或 Clang ≥ 5.0 (支持 C++17)
- Linux 系统

## 构建步骤

```bash
# 克隆项目
git clone <repository-url>
cd SunKV

# 创建构建目录
mkdir build && cd build

# 配置项目
cmake ..

# 编译
make -j$(nproc)

# 运行
./sunkv
```

## 项目结构

```
SunKV/
├── CMakeLists.txt          # 主构建文件
├── README.md               # 项目说明
├── doc/                    # 文档目录
│   ├── 设计文档.md
│   └── 开发计划.md
├── network/                # 网络层
│   ├── EventLoop.h/cpp     # 事件循环
│   ├── Channel.h/cpp       # 事件通道
│   ├── Poller.h/cpp        # epoll 封装
│   └── logger.h/cpp        # 日志系统
├── protocol/               # 协议层
├── storage/                # 存储引擎
├── persistence/            # 持久化层
├── server/                 # 服务器主程序
│   └── main.cpp
└── test/                   # 测试代码
```

## 开发进度

当前处于第一阶段：基础框架搭建

- [x] 项目结构初始化
- [x] 配置 CMake 构建系统
- [ ] 网络层基础组件
- [x] 日志系统（基于 spdlog）

## 联系方式

如有问题，请提交 Issue 或联系开发者。