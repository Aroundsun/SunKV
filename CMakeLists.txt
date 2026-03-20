cmake_minimum_required(VERSION 3.10)
project(SunKV VERSION 1.0.0 LANGUAGES CXX)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Set build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Compiler flags
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -Wall -Wextra -DDEBUG")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG -Wall -Wextra")

# Find required packages
find_package(Threads REQUIRED)

# Find spdlog
find_package(spdlog REQUIRED)

# Include directories
include_directories(${CMAKE_CURRENT_SOURCE_DIR})
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/network)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/protocol)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/storage)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/persistence)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/server)

# Source files
set(NETWORK_SOURCES
    network/EventLoop.cpp
    network/Channel.cpp
    network/Poller.cpp
    network/logger.cpp
    network/Timer.cpp
    network/TimerQueue.cpp
    network/Buffer.cpp
    network/Socket.cpp
    network/TcpConnection.cpp
    network/Acceptor.cpp
    network/TcpServer.cpp
    network/EventLoopThread.cpp
    network/EventLoopThreadPool.cpp
)

set(PROTOCOL_SOURCES
    protocol/RESPType.cpp
    protocol/RESPParser.cpp
)

set(STORAGE_SOURCES
    # Storage layer sources will be added here
)

set(PERSISTENCE_SOURCES
    # Persistence layer sources will be added here
)

set(SERVER_SOURCES
    server/main.cpp
)

# Create executable
add_executable(sunkv
    ${SERVER_SOURCES}
    ${NETWORK_SOURCES}
    ${PROTOCOL_SOURCES}
    ${STORAGE_SOURCES}
    ${PERSISTENCE_SOURCES}
)

# Link libraries
target_link_libraries(sunkv 
    Threads::Threads
    spdlog::spdlog
)

# Install target
install(TARGETS sunkv DESTINATION bin)

# Testing (optional)
enable_testing()
find_package(GTest QUIET)
if(GTest_FOUND)
    add_subdirectory(test)
elseif(BUILD_TEST)
    # Timer tests
    add_executable(timer_test test/timer_test.cpp
        ${NETWORK_SOURCES}
    )
    target_link_libraries(timer_test 
        Threads::Threads
        spdlog::spdlog
    )
    
    # Network layer tests
    add_executable(eventloop_test test/eventloop_test.cpp
        ${NETWORK_SOURCES}
    )
    target_link_libraries(eventloop_test 
        Threads::Threads
        spdlog::spdlog
    )
    
    add_executable(network_test test/network_test.cpp
        ${NETWORK_SOURCES}
    )
    target_link_libraries(network_test 
        Threads::Threads
        spdlog::spdlog
    )
    
    # TCP server tests
    add_executable(tcp_server_test test/tcp_server_test.cpp
        ${NETWORK_SOURCES}
    )
    target_link_libraries(tcp_server_test 
        Threads::Threads
        spdlog::spdlog
    )
    
    add_executable(multithread_tcp_test test/multithread_tcp_test.cpp
        ${NETWORK_SOURCES}
    )
    target_link_libraries(multithread_tcp_test 
        Threads::Threads
        spdlog::spdlog
    )
    
    # Performance and concurrency tests
    add_executable(concurrent_connection_test test/concurrent_connection_test.cpp
        ${NETWORK_SOURCES}
    )
    target_link_libraries(concurrent_connection_test 
        Threads::Threads
        spdlog::spdlog
    )
    
    add_executable(connection_lifecycle_test test/connection_lifecycle_test.cpp
        ${NETWORK_SOURCES}
    )
    target_link_libraries(connection_lifecycle_test 
        Threads::Threads
        spdlog::spdlog
    )
    
    add_executable(performance_benchmark_test test/performance_benchmark_test.cpp
        ${NETWORK_SOURCES}
    )
    target_link_libraries(performance_benchmark_test 
        Threads::Threads
        spdlog::spdlog
    )
    
    add_executable(resp_protocol_test test/resp_protocol_test.cpp
        ${NETWORK_SOURCES}
        ${PROTOCOL_SOURCES}
    )
    target_link_libraries(resp_protocol_test 
        Threads::Threads
        spdlog::spdlog
    )
    
    add_executable(resp_network_integration_test test/resp_network_integration_test.cpp
        ${NETWORK_SOURCES}
        ${PROTOCOL_SOURCES}
    )
    target_link_libraries(resp_network_integration_test 
        Threads::Threads
        spdlog::spdlog
    )
    
    add_executable(resp_simple_test test/resp_simple_test.cpp
        ${PROTOCOL_SOURCES}
    )
    target_link_libraries(resp_simple_test 
        Threads::Threads
        spdlog::spdlog
    )
    
    # 添加测试目标
    add_test(NAME TimerTest COMMAND timer_test)
    add_test(NAME EventLoopTest COMMAND eventloop_test)
    add_test(NAME NetworkTest COMMAND network_test)
    add_test(NAME TcpServerTest COMMAND tcp_server_test)
    add_test(NAME MultiThreadTcpTest COMMAND multithread_tcp_test)
    add_test(NAME ConcurrentConnectionTest COMMAND concurrent_connection_test)
    add_test(NAME ConnectionLifecycleTest COMMAND connection_lifecycle_test)
    add_test(NAME PerformanceBenchmarkTest COMMAND performance_benchmark_test)
    add_test(NAME RESPProtocolTest COMMAND resp_protocol_test)
    add_test(NAME RESPNetworkIntegrationTest COMMAND resp_network_integration_test)
    add_test(NAME RESPSimpleTest COMMAND resp_simple_test)
endif()
