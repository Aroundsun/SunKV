cmake_minimum_required(VERSION 3.10)
project(SunKV VERSION 1.0.0 LANGUAGES CXX)

# ============================================================================
# 项目基本配置
# ============================================================================

# 设置 C++ 标准为 C++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)  # 必须支持 C++17
set(CMAKE_CXX_EXTENSIONS OFF)        # 禁用编译器特定扩展

# 设置默认构建类型
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)     # 默认使用 Release 构建
endif()

# 设置编译器标志
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -Wall -Wextra -DDEBUG")     # Debug 模式：包含调试信息，无优化
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG -Wall -Wextra")    # Release 模式：最高优化，无调试信息

# ============================================================================
# 依赖查找
# ============================================================================

# 查找必需的线程库
find_package(Threads REQUIRED)

# 查找 spdlog 日志库
find_package(spdlog REQUIRED)

# ============================================================================
# 包含目录设置
# ============================================================================
# 添加头文件搜索路径，使各模块可以相互包含头文件

include_directories(${CMAKE_CURRENT_SOURCE_DIR})                    # 项目根目录
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/network)             # 网络模块
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/protocol)            # 协议模块
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/storage)            # 存储模块
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/persistence)        # 持久化模块
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/server)              # 服务器模块

# ============================================================================
# 源文件定义
# ============================================================================
# 按模块组织源文件，便于管理和维护

# 网络模块源文件
set(NETWORK_SOURCES
    common/MemoryPool.cpp                 # 线程本地内存池
    network/Buffer.cpp                    # 缓冲区实现
    network/Channel.cpp                   # 事件通道
    network/EventLoop.cpp                 # 事件循环核心
    network/Poller.cpp                    # I/O 多路复用封装
    network/Socket.cpp                     # Socket 封装
    network/Acceptor.cpp                  # 连接接收器
    network/TcpConnection.cpp            # TCP 连接管理
    network/TcpServer.cpp                 # TCP 服务器
    network/Timer.cpp                     # 定时器
    network/TimerQueue.cpp                # 定时器队列
    network/EventLoopThread.cpp           # 事件循环线程
    network/EventLoopThreadPool.cpp       # 事件循环线程池
    network/logger.cpp                    # 日志模块
)

# 协议模块源文件
set(PROTOCOL_SOURCES
    protocol/RESPParser.cpp                 # RESP 协议解析器
    protocol/RESPType.cpp                   # RESP 数据类型
    protocol/RESPSerializer.cpp              # RESP 协议序列化器
)

# 命令模块源文件
set(COMMAND_SOURCES
    command/Command.cpp                   # 命令基类
    command/CommandRegistry.cpp           # 命令注册表
    command/CommandDispatcher.cpp         # 命令分发器
)

# 存储模块源文件
set(STORAGE_SOURCES
    storage/StorageEngine.cpp             # 存储引擎核心
    storage/ShardedKVStore.cpp            # 分片 KV 存储
    storage/WAL.cpp                        # 预写日志
    storage/Snapshot.cpp                  # 快照管理
)

# 持久化模块源文件
set(PERSISTENCE_SOURCES
    # 持久化层源文件将在这里添加
)

# 服务器模块源文件
set(SERVER_SOURCES
    server/Server.cpp                      # 服务器主类
    server/main.cpp                         # 服务器主程序入口
)

# ============================================================================
# 可执行文件构建
# ============================================================================

# 创建 SunKV 主程序可执行文件
add_executable(sunkv
    ${SERVER_SOURCES}                      # 服务器模块
    ${NETWORK_SOURCES}                     # 网络模块
    ${PROTOCOL_SOURCES}                    # 协议模块
    ${COMMAND_SOURCES}                     # 命令模块
    ${STORAGE_SOURCES}                     # 存储模块
    ${PERSISTENCE_SOURCES}                 # 持久化模块
    common/Config.cpp                      # 配置模块
)

# 链接库文件
target_link_libraries(sunkv 
    Threads::Threads                       # 线程库
    spdlog::spdlog                         # 日志库
    fmt::fmt                               # 格式化库
)

# ============================================================================
# 安装配置
# ============================================================================

# 安装可执行文件到系统 bin 目录
install(TARGETS sunkv DESTINATION bin)

# ============================================================================
# 测试配置
# ============================================================================

# 启用测试功能
enable_testing()

# 查找 Google Test 框架
find_package(GTest QUIET)
if(GTest_FOUND)
    # 如果找到 GTest，使用 test 子目录的配置
    add_subdirectory(test)
elseif(BUILD_TEST)
    # 如果没有 GTest 但启用了 BUILD_TEST，使用简单测试框架
    
    # ========================================================================
    # 网络层测试
    # ========================================================================
    
    # 定时器测试 - 验证定时器功能和精度
    add_executable(timer_test test/timer_test.cpp
        ${NETWORK_SOURCES}
    )
    target_link_libraries(timer_test 
        Threads::Threads
        spdlog::spdlog
    )
    
    # 事件循环测试 - 验证事件循环核心功能
    add_executable(eventloop_test test/eventloop_test.cpp
        ${NETWORK_SOURCES}
    )
    target_link_libraries(eventloop_test 
        Threads::Threads
        spdlog::spdlog
    )
    
    # 网络基础测试 - 验证基础网络功能
    add_executable(network_test test/network_test.cpp
        ${NETWORK_SOURCES}
    )
    target_link_libraries(network_test 
        Threads::Threads
        spdlog::spdlog
    )

    # Buffer 优化测试 - 验证缓冲区复用与扩容行为
    add_executable(buffer_optimization_test test/buffer_optimization_test.cpp
        common/MemoryPool.cpp
        network/Buffer.cpp
        network/logger.cpp
    )
    target_link_libraries(buffer_optimization_test
        Threads::Threads
        spdlog::spdlog
    )

    # 内存池测试 - 验证线程本地内存池分配与复用
    add_executable(memory_pool_test test/memory_pool_test.cpp
        common/MemoryPool.cpp
    )
    target_link_libraries(memory_pool_test
        Threads::Threads
        spdlog::spdlog
    )
    
    # ========================================================================
    # TCP 服务器测试
    # ========================================================================
    
    # TCP 服务器基础测试
    add_executable(tcp_server_test test/tcp_server_test.cpp
        ${NETWORK_SOURCES}
    )
    target_link_libraries(tcp_server_test 
        Threads::Threads
        spdlog::spdlog
    )
    
    # 多线程 TCP 测试 - 验证多线程环境下的 TCP 功能
    add_executable(multithread_tcp_test test/multithread_tcp_test.cpp
        ${NETWORK_SOURCES}
    )
    target_link_libraries(multithread_tcp_test 
        Threads::Threads
        spdlog::spdlog
    )
    
    # ========================================================================
    # 性能和并发测试
    # ========================================================================
    
    # 并发连接测试 - 验证高并发连接处理能力
    add_executable(concurrent_connection_test test/concurrent_connection_test.cpp
        ${NETWORK_SOURCES}
    )
    target_link_libraries(concurrent_connection_test 
        Threads::Threads
        spdlog::spdlog
    )
    
    # 连接生命周期测试 - 验证连接建立、维护和关闭
    add_executable(connection_lifecycle_test test/connection_lifecycle_test.cpp
        ${NETWORK_SOURCES}
    )
    target_link_libraries(connection_lifecycle_test 
        Threads::Threads
        spdlog::spdlog
    )
    
    # 性能基准测试 - 测试系统性能指标
    add_executable(performance_benchmark_test test/performance_benchmark_test.cpp
        ${NETWORK_SOURCES}
    )
    target_link_libraries(performance_benchmark_test 
        Threads::Threads
        spdlog::spdlog
    )
    
    # ========================================================================
    # RESP 协议测试
    # ========================================================================
    
    # RESP 协议测试 - 验证 RESP 协议解析和生成
    add_executable(resp_protocol_test test/resp_protocol_test.cpp
        ${NETWORK_SOURCES}
        ${PROTOCOL_SOURCES}
    )
    target_link_libraries(resp_protocol_test 
        Threads::Threads
        spdlog::spdlog
    )
    
    # RESP 网络集成测试 - 验证 RESP 协议在网络传输中的表现
    add_executable(resp_network_integration_test test/resp_network_integration_test.cpp
        ${NETWORK_SOURCES}
        ${PROTOCOL_SOURCES}
    )
    target_link_libraries(resp_network_integration_test 
        Threads::Threads
        spdlog::spdlog
    )
    
    # RESP 简单测试 - 基础 RESP 功能测试（文件不存在，暂时注释）
    # add_executable(resp_simple_test test/resp_simple_test.cpp
    #     ${PROTOCOL_SOURCES}
    # )
    # target_link_libraries(resp_simple_test 
    #     Threads::Threads
    #     spdlog::spdlog
    # )
    
    # RESP 管道测试 - 验证 RESP 协议管道功能
    add_executable(resp_pipeline_test test/resp_pipeline_test.cpp
        ${PROTOCOL_SOURCES}
    )
    target_link_libraries(resp_pipeline_test 
        Threads::Threads
        spdlog::spdlog
    )
    
    # ========================================================================
    # 命令模块测试
    # ========================================================================
    
    # 命令框架测试 - 验证命令框架基础功能
    add_executable(command_framework_test test/command_framework_test.cpp
        ${PROTOCOL_SOURCES}
        ${COMMAND_SOURCES}
        ${STORAGE_SOURCES}
        network/logger.cpp
    )
    target_link_libraries(command_framework_test 
        Threads::Threads
        spdlog::spdlog
    )
    
    # ========================================================================
    # 测试目标注册
    # ========================================================================
    # 将所有测试注册到 CTest 框架中，可以通过 'make test' 或 'ctest' 运行
    
    # 网络层测试目标
    add_test(NAME TimerTest COMMAND timer_test)
    add_test(NAME EventLoopTest COMMAND eventloop_test)
    add_test(NAME NetworkTest COMMAND network_test)
    add_test(NAME BufferOptimizationTest COMMAND buffer_optimization_test)
    add_test(NAME MemoryPoolTest COMMAND memory_pool_test)
    
    # TCP 服务器测试目标
    add_test(NAME TcpServerTest COMMAND tcp_server_test)
    add_test(NAME MultiThreadTcpTest COMMAND multithread_tcp_test)
    
    # 性能和并发测试目标
    add_test(NAME ConcurrentConnectionTest COMMAND concurrent_connection_test)
    add_test(NAME ConnectionLifecycleTest COMMAND connection_lifecycle_test)
    add_test(NAME PerformanceBenchmarkTest COMMAND performance_benchmark_test)
    
    # RESP 协议测试目标
    add_test(NAME RESPProtocolTest COMMAND resp_protocol_test)
    add_test(NAME RESPNetworkIntegrationTest COMMAND resp_network_integration_test)
    # add_test(NAME RESPSimpleTest COMMAND resp_simple_test)  # 文件不存在，暂时注释
    add_test(NAME RESPPipelineTest COMMAND resp_pipeline_test)
    
    # 基础命令测试 - 验证基本 Redis 命令功能
    add_executable(basic_commands_test test/basic_commands_test.cpp
        ${PROTOCOL_SOURCES}
        ${COMMAND_SOURCES}
        ${STORAGE_SOURCES}
        network/logger.cpp
    )
    target_link_libraries(basic_commands_test 
        Threads::Threads
        spdlog::spdlog
    )
    
    # 命令集成测试 - 验证命令系统的完整集成
    add_executable(command_integration_test test/command_integration_test.cpp
        ${PROTOCOL_SOURCES}
        ${COMMAND_SOURCES}
        ${STORAGE_SOURCES}
        network/logger.cpp
    )
    target_link_libraries(command_integration_test 
        Threads::Threads
        spdlog::spdlog
    )
    
    # 命令单元测试 - 验证各个命令的单元功能
    add_executable(command_unit_test test/command_unit_test.cpp
        ${PROTOCOL_SOURCES}
        ${COMMAND_SOURCES}
        ${STORAGE_SOURCES}
        network/logger.cpp
    )
    target_link_libraries(command_unit_test 
        Threads::Threads
        spdlog::spdlog
    )
    
    # 命令错误测试 - 验证命令错误处理机制
    add_executable(command_error_test test/command_error_test.cpp
        ${PROTOCOL_SOURCES}
        ${COMMAND_SOURCES}
        ${STORAGE_SOURCES}
        network/logger.cpp
    )
    target_link_libraries(command_error_test 
        Threads::Threads
        spdlog::spdlog
    )
    
    # ========================================================================
    # 存储模块测试
    # ========================================================================
    
    # 分片 KV 存储测试 - 验证分片存储功能
    add_executable(sharded_kvstore_test test/sharded_kvstore_test.cpp
        ${STORAGE_SOURCES}
        network/logger.cpp
    )
    target_link_libraries(sharded_kvstore_test 
        Threads::Threads
        spdlog::spdlog
    )
    
    # 缓存存储测试 - 验证缓存层功能
    add_executable(cached_storage_test test/cached_storage_test.cpp
        ${STORAGE_SOURCES}
        network/logger.cpp
    )
    target_link_libraries(cached_storage_test 
        Threads::Threads
        spdlog::spdlog
    )
    
    # 存储引擎测试 - 验证存储引擎核心功能
    add_executable(storage_engine_test test/storage_engine_test.cpp
        ${STORAGE_SOURCES}
        network/logger.cpp
    )
    target_link_libraries(storage_engine_test 
        Threads::Threads
        spdlog::spdlog
    )
    
    # ========================================================================
    # 持久化模块测试
    # ========================================================================
    
    # WAL 测试 - 验证预写日志功能
    add_executable(wal_test test/wal_test.cpp
        storage/WAL.cpp
        ${STORAGE_SOURCES}
        network/logger.cpp
    )
    target_link_libraries(wal_test 
        Threads::Threads
        spdlog::spdlog
    )
    
    # 快照测试 - 验证数据快照功能（文件不存在，暂时注释）
    # add_executable(snapshot_test test/snapshot_test.cpp
    #     storage/Snapshot.cpp
    #     storage/StorageEngine.cpp
    #     storage/ShardedKVStore.cpp
    #     network/logger.cpp
    # )
    # target_link_libraries(snapshot_test 
    #     Threads::Threads
    #     spdlog::spdlog
    # )
    
    # 数据恢复测试 - 验证数据恢复功能
    add_executable(recovery_test test/recovery_test.cpp
        storage/Recovery.cpp
        storage/Snapshot.cpp
        storage/WAL.cpp
        storage/StorageEngine.cpp
        storage/ShardedKVStore.cpp
        network/logger.cpp
    )
    target_link_libraries(recovery_test 
        Threads::Threads
        spdlog::spdlog
    )
    
    # 持久化集成测试 - 验证持久化系统完整功能
    add_executable(persistence_test test/persistence_test.cpp
        storage/Recovery.cpp
        storage/Snapshot.cpp
        storage/WAL.cpp
        storage/StorageEngine.cpp
        storage/ShardedKVStore.cpp
        network/logger.cpp
    )
    target_link_libraries(persistence_test 
        Threads::Threads
        spdlog::spdlog
    )
    
    # ========================================================================
    # 完整测试目标注册
    # ========================================================================
    
    # 命令模块测试目标
    add_test(NAME BasicCommandsTest COMMAND basic_commands_test)
    add_test(NAME CommandIntegrationTest COMMAND command_integration_test)
    add_test(NAME CommandUnitTest COMMAND command_unit_test)
    add_test(NAME CommandErrorTest COMMAND command_error_test)
    
    # 存储模块测试目标
    add_test(NAME ShardedKVStoreTest COMMAND sharded_kvstore_test)
    add_test(NAME CachedStorageTest COMMAND cached_storage_test)
    add_test(NAME StorageEngineTest COMMAND storage_engine_test)
    
    # 持久化模块测试目标
    add_test(NAME WALTest COMMAND wal_test)
    # add_test(NAME SnapshotTest COMMAND snapshot_test)  # 文件不存在，暂时注释
    add_test(NAME RecoveryTest COMMAND recovery_test)
    add_test(NAME PersistenceTest COMMAND persistence_test)
    
endif()
