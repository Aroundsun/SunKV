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
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/client/include)      # 客户端 SDK 头文件

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

# 存储模块源文件

# v2 存储模块（从零设计）源文件
set(STORAGE2_SOURCES
    storage2/Factory.cpp
    storage2/backend/InMemoryBackend.cpp
    storage2/engine/Time.cpp
    storage2/engine/StorageEngine.cpp
    storage2/persistence/PersistenceOrchestrator.cpp
    storage2/persistence/DataValueCodec.cpp
    storage2/persistence/RecordCodec.cpp
    storage2/persistence/WalWriter.cpp
    storage2/persistence/WalCodec.cpp
    storage2/persistence/WalReader.cpp
    storage2/persistence/SnapshotWriter.cpp
    storage2/persistence/SnapshotReader.cpp
    storage2/decorators/OrchestratedStorageAPI.cpp
)

# storage2 实现库（主程序与 v2 测试均链接该库）。
add_library(storage2lib STATIC ${STORAGE2_SOURCES})

# 独立只读工具：将快照 / WAL 转存为 tool/ 下 .txt
add_executable(dump_persistent tool/dump_persistent.cpp)
target_include_directories(dump_persistent PRIVATE ${CMAKE_SOURCE_DIR})
target_compile_definitions(dump_persistent PRIVATE "TOOL_OUT_DIR_STR=\"${CMAKE_SOURCE_DIR}/tool\"")
target_link_libraries(dump_persistent PRIVATE
    Threads::Threads
    spdlog::spdlog
    fmt::fmt
    storage2lib
)

# 服务器模块源文件
set(SERVER_SOURCES
    server/Server.cpp                      # 服务器主类
    server/ArrayCmdDispatch.cpp            # RESP 数组命令静态分发表
    server/main.cpp                         # 服务器主程序入口
)

# 客户端模块源文件
set(CLIENT_SOURCES
    client/src/Client.cpp
    client/src/RespCodec.cpp
)

# ============================================================================
# 可执行文件构建
# ============================================================================

# 创建 SunKV 主程序可执行文件
add_executable(sunkv
    ${SERVER_SOURCES}                      # 服务器模块
    ${NETWORK_SOURCES}                     # 网络模块
    ${PROTOCOL_SOURCES}                    # 协议模块
    ${PERSISTENCE_SOURCES}                 # 持久化模块
    common/Config.cpp                      # 配置模块
)

# 链接库文件
target_link_libraries(sunkv 
    Threads::Threads                       # 线程库
    spdlog::spdlog                         # 日志库
    fmt::fmt                               # 格式化库
    storage2lib                            # storage2 v2 存储实现
)

add_library(clientlib STATIC ${CLIENT_SOURCES})
target_link_libraries(clientlib
    Threads::Threads
)

add_executable(sunkvClient
    client/sunkvClient.cpp
)
target_link_libraries(sunkvClient
    clientlib
    Threads::Threads
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

# 测试入口统一：仓库内测试由 BUILD_TEST 控制，不依赖系统是否安装 GTest。
# 说明：避免出现“某些环境恰好装了 GTest 反而走到不存在的 test/ 子目录”。
if(BUILD_TEST)
    # 如果启用了 BUILD_TEST，使用内置测试可执行文件 + ctest 注册
    
    # ========================================================================
    # client/
    # ========================================================================

    add_executable(client_resp_codec_test
        test/client/client_resp_codec_test.cpp
        client/src/RespCodec.cpp
    )
    target_link_libraries(client_resp_codec_test
        Threads::Threads
    )

    add_executable(client_basic_integration_test
        test/client/client_basic_integration_test.cpp
        server/Server.cpp
        server/ArrayCmdDispatch.cpp
        common/Config.cpp
        ${NETWORK_SOURCES}
        ${PROTOCOL_SOURCES}
    )
    target_link_libraries(client_basic_integration_test
        Threads::Threads
        spdlog::spdlog
        storage2lib
        clientlib
    )

    add_executable(client_pipeline_integration_test
        test/client/client_pipeline_integration_test.cpp
        server/Server.cpp
        server/ArrayCmdDispatch.cpp
        common/Config.cpp
        ${NETWORK_SOURCES}
        ${PROTOCOL_SOURCES}
    )
    target_link_libraries(client_pipeline_integration_test
        Threads::Threads
        spdlog::spdlog
        storage2lib
        clientlib
    )

    # ========================================================================
    # network/
    # ========================================================================
    
    # 定时器测试 - 验证定时器功能和精度
    add_executable(network_timer_test test/network/network_timer_test.cpp
        ${NETWORK_SOURCES}
    )
    target_link_libraries(network_timer_test 
        Threads::Threads
        spdlog::spdlog
    )
    
    # 事件循环测试 - 验证事件循环核心功能
    add_executable(network_event_loop_test test/network/network_event_loop_test.cpp
        ${NETWORK_SOURCES}
    )
    target_link_libraries(network_event_loop_test 
        Threads::Threads
        spdlog::spdlog
    )

    # EventLoop queueInLoop 跨线程唤醒路径 - 验证队列能在 poll 阻塞时被及时执行
    add_executable(network_event_loop_queue_in_loop_test
        test/network/network_event_loop_queue_in_loop_test.cpp
        ${NETWORK_SOURCES}
    )
    target_link_libraries(network_event_loop_queue_in_loop_test
        Threads::Threads
        spdlog::spdlog
    )

    # EventLoop reentrant 队列行为 - 验证 doPendingTasks 期间 queueInLoop 的次序
    add_executable(network_event_loop_reentrant_queue_test
        test/network/network_event_loop_reentrant_queue_test.cpp
        ${NETWORK_SOURCES}
    )
    target_link_libraries(network_event_loop_reentrant_queue_test
        Threads::Threads
        spdlog::spdlog
    )
    
    # 网络基础测试 - 验证基础网络功能
    add_executable(network_socket_basics_test test/network/network_socket_basics_test.cpp
        ${NETWORK_SOURCES}
    )
    target_link_libraries(network_socket_basics_test 
        Threads::Threads
        spdlog::spdlog
    )

    # Buffer 优化测试 - 验证缓冲区复用与扩容行为
    add_executable(network_buffer_growth_test test/network/network_buffer_growth_test.cpp
        common/MemoryPool.cpp
        network/Buffer.cpp
        network/logger.cpp
    )
    target_link_libraries(network_buffer_growth_test
        Threads::Threads
        spdlog::spdlog
    )

    # Buffer fd 错误路径 - 验证非法 fd 的错误码回传
    add_executable(network_buffer_fd_error_test
        test/network/network_buffer_fd_error_test.cpp
        common/MemoryPool.cpp
        network/Buffer.cpp
        network/logger.cpp
    )
    target_link_libraries(network_buffer_fd_error_test
        Threads::Threads
        spdlog::spdlog
    )

    # 内存池测试 - 验证线程本地内存池分配与复用
    add_executable(network_memory_pool_test test/network/network_memory_pool_test.cpp
        common/MemoryPool.cpp
    )
    target_link_libraries(network_memory_pool_test
        Threads::Threads
        spdlog::spdlog
    )
    
    # network/（TCP 子集）
    
    # TCP 服务器基础测试
    add_executable(network_tcp_server_test test/network/network_tcp_server_test.cpp
        ${NETWORK_SOURCES}
    )
    target_link_libraries(network_tcp_server_test 
        Threads::Threads
        spdlog::spdlog
    )

    # epoll 边界事件回归测试 - 验证 FIN/半关闭/RST 等边界路径
    add_executable(network_epoll_edge_cases_test test/network/network_epoll_edge_cases_test.cpp
        ${NETWORK_SOURCES}
    )
    target_link_libraries(network_epoll_edge_cases_test
        Threads::Threads
        spdlog::spdlog
    )
    
    # 多线程 TCP 测试 - 验证多线程环境下的 TCP 功能
    add_executable(network_tcp_multithread_test test/network/network_tcp_multithread_test.cpp
        ${NETWORK_SOURCES}
    )
    target_link_libraries(network_tcp_multithread_test 
        Threads::Threads
        spdlog::spdlog
    )

    add_executable(network_tcp_backpressure_test test/network/network_tcp_backpressure_test.cpp
        ${NETWORK_SOURCES}
    )
    target_link_libraries(network_tcp_backpressure_test
        Threads::Threads
        spdlog::spdlog
    )

    add_executable(network_tcp_buffered_send_test test/network/network_tcp_buffered_send_test.cpp
        ${NETWORK_SOURCES}
    )
    target_link_libraries(network_tcp_buffered_send_test
        Threads::Threads
        spdlog::spdlog
    )
    
    # network/（并发与性能）
    
    # 并发连接测试 - 验证高并发连接处理能力
    add_executable(network_concurrent_connections_test test/network/network_concurrent_connections_test.cpp
        ${NETWORK_SOURCES}
    )
    target_link_libraries(network_concurrent_connections_test 
        Threads::Threads
        spdlog::spdlog
    )
    
    # 连接生命周期测试 - 验证连接建立、维护和关闭
    add_executable(network_connection_lifecycle_test test/network/network_connection_lifecycle_test.cpp
        ${NETWORK_SOURCES}
    )
    target_link_libraries(network_connection_lifecycle_test 
        Threads::Threads
        spdlog::spdlog
    )
    
    # 性能基准测试 - 测试系统性能指标
    add_executable(network_performance_benchmark_test test/network/network_performance_benchmark_test.cpp
        ${NETWORK_SOURCES}
    )
    target_link_libraries(network_performance_benchmark_test 
        Threads::Threads
        spdlog::spdlog
    )
    
    # ========================================================================
    # protocol/
    # ========================================================================
    
    # RESP 协议测试 - 验证 RESP 协议解析和生成
    add_executable(protocol_resp_parser_serializer_test test/protocol/protocol_resp_parser_serializer_test.cpp
        ${NETWORK_SOURCES}
        ${PROTOCOL_SOURCES}
    )
    target_link_libraries(protocol_resp_parser_serializer_test 
        Threads::Threads
        spdlog::spdlog
    )
    
    # ========================================================================
    # server/
    # ========================================================================

    # ========================================================================
    # server/（真实 Server 行为回归）
    # ========================================================================

    add_executable(server_message_framing_integration_test
        test/server/server_message_framing_integration_test.cpp
        server/Server.cpp
        server/ArrayCmdDispatch.cpp
        common/Config.cpp
        ${NETWORK_SOURCES}
        ${PROTOCOL_SOURCES}
    )
    target_link_libraries(server_message_framing_integration_test
        Threads::Threads
        spdlog::spdlog
        storage2lib
    )

    add_executable(server_error_paths_integration_test
        test/server/server_error_paths_integration_test.cpp
        server/Server.cpp
        server/ArrayCmdDispatch.cpp
        common/Config.cpp
        ${NETWORK_SOURCES}
        ${PROTOCOL_SOURCES}
    )
    target_link_libraries(server_error_paths_integration_test
        Threads::Threads
        spdlog::spdlog
        storage2lib
    )

    add_executable(server_connection_lifecycle_test
        test/server/server_connection_lifecycle_test.cpp
        server/Server.cpp
        server/ArrayCmdDispatch.cpp
        common/Config.cpp
        ${NETWORK_SOURCES}
        ${PROTOCOL_SOURCES}
    )
    target_link_libraries(server_connection_lifecycle_test
        Threads::Threads
        spdlog::spdlog
        storage2lib
    )

    add_executable(server_string_commands_test
        test/server/server_string_commands_test.cpp
        server/Server.cpp
        server/ArrayCmdDispatch.cpp
        common/Config.cpp
        ${NETWORK_SOURCES}
        ${PROTOCOL_SOURCES}
    )
    target_link_libraries(server_string_commands_test
        Threads::Threads
        spdlog::spdlog
        storage2lib
    )

    add_executable(server_collection_commands_test
        test/server/server_collection_commands_test.cpp
        server/Server.cpp
        server/ArrayCmdDispatch.cpp
        common/Config.cpp
        ${NETWORK_SOURCES}
        ${PROTOCOL_SOURCES}
    )
    target_link_libraries(server_collection_commands_test
        Threads::Threads
        spdlog::spdlog
        storage2lib
    )

    add_executable(server_ttl_and_debug_test
        test/server/server_ttl_and_debug_test.cpp
        server/Server.cpp
        server/ArrayCmdDispatch.cpp
        common/Config.cpp
        ${NETWORK_SOURCES}
        ${PROTOCOL_SOURCES}
    )
    target_link_libraries(server_ttl_and_debug_test
        Threads::Threads
        spdlog::spdlog
        storage2lib
    )

    add_executable(server_shutdown_integration_test
        test/server/server_shutdown_integration_test.cpp
        server/Server.cpp
        server/ArrayCmdDispatch.cpp
        common/Config.cpp
        ${NETWORK_SOURCES}
        ${PROTOCOL_SOURCES}
    )
    target_link_libraries(server_shutdown_integration_test
        Threads::Threads
        spdlog::spdlog
        storage2lib
    )

    add_executable(server_transaction_multi_exec_test
        test/server/server_transaction_multi_exec_test.cpp
        server/Server.cpp
        server/ArrayCmdDispatch.cpp
        common/Config.cpp
        ${NETWORK_SOURCES}
        ${PROTOCOL_SOURCES}
    )
    target_link_libraries(server_transaction_multi_exec_test
        Threads::Threads
        spdlog::spdlog
        storage2lib
    )

    add_executable(server_pubsub_integration_test
        test/server/server_pubsub_integration_test.cpp
        server/Server.cpp
        server/ArrayCmdDispatch.cpp
        common/Config.cpp
        ${NETWORK_SOURCES}
        ${PROTOCOL_SOURCES}
    )
    target_link_libraries(server_pubsub_integration_test
        Threads::Threads
        spdlog::spdlog
        storage2lib
    )

    add_executable(server_observability_info_test
        test/server/server_observability_info_test.cpp
        server/Server.cpp
        server/ArrayCmdDispatch.cpp
        common/Config.cpp
        ${NETWORK_SOURCES}
        ${PROTOCOL_SOURCES}
    )
    target_link_libraries(server_observability_info_test
        Threads::Threads
        spdlog::spdlog
        storage2lib
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
    add_executable(protocol_resp_pipeline_test test/protocol/protocol_resp_pipeline_test.cpp
        ${PROTOCOL_SOURCES}
    )
    target_link_libraries(protocol_resp_pipeline_test 
        Threads::Threads
        spdlog::spdlog
    )
    
    # ========================================================================
    # 测试目标注册（按目录分层）
    # ========================================================================
    # 将所有测试注册到 CTest 框架中，可以通过 'make test' 或 'ctest' 运行
    
    # network/
    add_test(NAME client_resp_codec COMMAND client_resp_codec_test)
    add_test(NAME client_basic_integration COMMAND client_basic_integration_test)
    add_test(NAME client_pipeline_integration COMMAND client_pipeline_integration_test)
    set_tests_properties(
        client_resp_codec
        client_basic_integration
        client_pipeline_integration
        PROPERTIES LABELS "client"
    )

    # network/
    add_test(NAME network_timer COMMAND network_timer_test)
    add_test(NAME network_event_loop COMMAND network_event_loop_test)
    add_test(NAME network_event_loop_queue_in_loop COMMAND network_event_loop_queue_in_loop_test)
    add_test(NAME network_event_loop_reentrant_queue COMMAND network_event_loop_reentrant_queue_test)
    add_test(NAME network_socket_basics COMMAND network_socket_basics_test)
    add_test(NAME network_buffer_growth COMMAND network_buffer_growth_test)
    add_test(NAME network_buffer_fd_error COMMAND network_buffer_fd_error_test)
    add_test(NAME network_memory_pool COMMAND network_memory_pool_test)
    
    # network/（TCP 子集）
    add_test(NAME network_tcp_server COMMAND network_tcp_server_test)
    add_test(NAME network_epoll_edge_cases COMMAND network_epoll_edge_cases_test)
    add_test(NAME network_tcp_multithread COMMAND network_tcp_multithread_test)
    add_test(NAME network_tcp_backpressure COMMAND network_tcp_backpressure_test)
    add_test(NAME network_tcp_buffered_send COMMAND network_tcp_buffered_send_test)
    
    # network/（并发与性能）
    add_test(NAME network_concurrent_connections COMMAND network_concurrent_connections_test)
    add_test(NAME network_connection_lifecycle COMMAND network_connection_lifecycle_test)
    add_test(NAME network_performance_benchmark COMMAND network_performance_benchmark_test)
    set_tests_properties(
        network_timer
        network_event_loop
        network_event_loop_queue_in_loop
        network_event_loop_reentrant_queue
        network_socket_basics
        network_buffer_growth
        network_buffer_fd_error
        network_memory_pool
        network_tcp_server
        network_epoll_edge_cases
        network_tcp_multithread
        network_tcp_backpressure
        network_tcp_buffered_send
        network_concurrent_connections
        network_connection_lifecycle
        network_performance_benchmark
        PROPERTIES LABELS "network"
    )
    
    # protocol/
    add_test(NAME protocol_resp_parser_serializer COMMAND protocol_resp_parser_serializer_test)
    add_test(NAME protocol_resp_pipeline COMMAND protocol_resp_pipeline_test)
    set_tests_properties(
        protocol_resp_parser_serializer
        protocol_resp_pipeline
        PROPERTIES LABELS "protocol"
    )

    # server/
    add_test(NAME server_message_framing_integration COMMAND server_message_framing_integration_test)
    add_test(NAME server_error_paths_integration COMMAND server_error_paths_integration_test)
    add_test(NAME server_connection_lifecycle COMMAND server_connection_lifecycle_test)
    add_test(NAME server_string_commands COMMAND server_string_commands_test)
    add_test(NAME server_collection_commands COMMAND server_collection_commands_test)
    add_test(NAME server_ttl_and_debug COMMAND server_ttl_and_debug_test)
    add_test(NAME server_shutdown_integration COMMAND server_shutdown_integration_test)
    add_test(NAME server_transaction_multi_exec COMMAND server_transaction_multi_exec_test)
    add_test(NAME server_pubsub_integration COMMAND server_pubsub_integration_test)
    add_test(NAME server_observability_info COMMAND server_observability_info_test)
    set_tests_properties(
        server_message_framing_integration
        server_error_paths_integration
        server_connection_lifecycle
        server_string_commands
        server_collection_commands
        server_ttl_and_debug
        server_shutdown_integration
        server_transaction_multi_exec
        server_pubsub_integration
        PROPERTIES LABELS "server"
    )
    # ========================================================================
    # storage2/
    # ========================================================================

    # storage2 smoke test：验证 v2 骨架可编译/可运行
    add_executable(storage2_smoke_test
        test/storage2/storage2_smoke_test.cpp
        network/logger.cpp
    )
    target_link_libraries(storage2_smoke_test
        Threads::Threads
        spdlog::spdlog
        storage2lib
    )
    add_test(NAME storage2_smoke COMMAND storage2_smoke_test)

    # storage2 multitype ops test：验证 list/set/hash 的命令语义骨架
    add_executable(storage2_multitype_commands_test
        test/storage2/storage2_multitype_commands_test.cpp
        network/logger.cpp
    )
    target_link_libraries(storage2_multitype_commands_test
        Threads::Threads
        spdlog::spdlog
        storage2lib
    )
    add_test(NAME storage2_multitype_commands COMMAND storage2_multitype_commands_test)

    add_executable(storage2_persistence_cycle_test
        test/storage2/storage2_persistence_cycle_test.cpp
        network/logger.cpp
    )
    target_link_libraries(storage2_persistence_cycle_test
        Threads::Threads
        spdlog::spdlog
        storage2lib
    )
    add_test(NAME storage2_persistence_cycle COMMAND storage2_persistence_cycle_test)

    add_executable(storage2_recovery_orchestrator_test
        test/storage2/storage2_recovery_orchestrator_test.cpp
        network/logger.cpp
    )
    target_link_libraries(storage2_recovery_orchestrator_test
        Threads::Threads
        spdlog::spdlog
        storage2lib
    )
    add_test(NAME storage2_recovery_orchestrator COMMAND storage2_recovery_orchestrator_test)

    add_executable(storage2_wal_roundtrip_test
        test/storage2/storage2_wal_roundtrip_test.cpp
        network/logger.cpp
    )
    target_link_libraries(storage2_wal_roundtrip_test
        Threads::Threads
        spdlog::spdlog
        storage2lib
    )
    add_test(NAME storage2_wal_roundtrip COMMAND storage2_wal_roundtrip_test)

    add_executable(storage2_wal_truncation_test
        test/storage2/storage2_wal_truncation_test.cpp
        network/logger.cpp
    )
    target_link_libraries(storage2_wal_truncation_test
        Threads::Threads
        spdlog::spdlog
        storage2lib
    )
    add_test(NAME storage2_wal_truncation COMMAND storage2_wal_truncation_test)

    add_executable(storage2_wal_max_file_enforced_test
        test/storage2/storage2_wal_max_file_enforced_test.cpp
        network/logger.cpp
    )
    target_link_libraries(storage2_wal_max_file_enforced_test
        Threads::Threads
        spdlog::spdlog
        storage2lib
    )
    add_test(NAME storage2_wal_max_file_enforced COMMAND storage2_wal_max_file_enforced_test)

    add_executable(storage2_factory_wiring_test
        test/storage2/storage2_factory_wiring_test.cpp
        network/logger.cpp
    )
    target_link_libraries(storage2_factory_wiring_test
        Threads::Threads
        spdlog::spdlog
        storage2lib
    )
    add_test(NAME storage2_factory_wiring COMMAND storage2_factory_wiring_test)

    add_executable(storage2_ttl_lazy_expiry_semantics_test
        test/storage2/storage2_ttl_lazy_expiry_semantics_test.cpp
        network/logger.cpp
    )
    target_link_libraries(storage2_ttl_lazy_expiry_semantics_test
        Threads::Threads
        spdlog::spdlog
        storage2lib
    )
    add_test(NAME storage2_ttl_lazy_expiry_semantics COMMAND storage2_ttl_lazy_expiry_semantics_test)

    add_executable(storage2_pttl_boundary_test
        test/storage2/storage2_pttl_boundary_test.cpp
        network/logger.cpp
    )
    target_link_libraries(storage2_pttl_boundary_test
        Threads::Threads
        spdlog::spdlog
        storage2lib
    )
    add_test(NAME storage2_pttl_boundary COMMAND storage2_pttl_boundary_test)

    add_executable(storage2_dump_live_records_test
        test/storage2/storage2_dump_live_records_test.cpp
        network/logger.cpp
    )
    target_link_libraries(storage2_dump_live_records_test
        Threads::Threads
        spdlog::spdlog
        storage2lib
    )
    add_test(NAME storage2_dump_live_records COMMAND storage2_dump_live_records_test)

    add_executable(storage2_recover_precedence_idempotence_test
        test/storage2/storage2_recover_precedence_idempotence_test.cpp
        network/logger.cpp
    )
    target_link_libraries(storage2_recover_precedence_idempotence_test
        Threads::Threads
        spdlog::spdlog
        storage2lib
    )
    add_test(NAME storage2_recover_precedence_idempotence COMMAND storage2_recover_precedence_idempotence_test)
    set_tests_properties(
        storage2_smoke
        storage2_multitype_commands
        storage2_persistence_cycle
        storage2_recovery_orchestrator
        storage2_wal_roundtrip
        storage2_wal_truncation
        storage2_wal_max_file_enforced
        storage2_factory_wiring
        storage2_ttl_lazy_expiry_semantics
        storage2_pttl_boundary
        storage2_dump_live_records
        storage2_recover_precedence_idempotence
        PROPERTIES LABELS "storage2"
    )
    
    # 防止运行期代码出现 stdout/stderr 双轨日志
    add_test(NAME NoStdioRuntimeTest COMMAND bash "${CMAKE_SOURCE_DIR}/scripts/no_stdio_runtime_check.sh")
    set_tests_properties(NoStdioRuntimeTest PROPERTIES LABELS "server")
    
endif()
