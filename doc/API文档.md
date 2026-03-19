# SunKV API 文档

## 📚 目录
- [网络层 API](#网络层-api)
- [事件循环 API](#事件循环-api)
- [定时器 API](#定时器-api)
- [日志 API](#日志-api)
- [TCP 服务器 API](#tcp-服务器-api)
- [多线程 API](#多线程-api)

---

## 🌐 网络层 API

### EventLoop 类
事件循环核心类，基于 epoll 实现高性能 I/O 多路复用。

#### 构造函数
```cpp
EventLoop();
```

#### 主要方法
```cpp
// 启动事件循环
void loop();

// 停止事件循环
void quit();

// 在事件循环线程中执行任务
void runInLoop(Functor cb);

// 在事件循环线程中排队执行任务
void queueInLoop(Functor cb);

// 运行定时器
int64_t runAt(const TimePoint& time, const TimerCallback& cb);
int64_t runAfter(Duration delay, const TimerCallback& cb);
int64_t runEvery(Duration interval, const TimerCallback& cb);

// 取消定时器
void cancel(int64_t timerId);

// 检查是否在事件循环线程中
bool isInLoopThread() const;
void assertInLoopThread() const;

// 检查是否正在析构
bool isDestructing() const;
```

#### 使用示例
```cpp
#include "network/EventLoop.h"

EventLoop loop;

// 运行定时器
loop.runAfter(std::chrono::seconds(5), []() {
    std::cout << "5 seconds later" << std::endl;
});

// 启动事件循环
loop.loop();
```

---

### Channel 类
事件通道，封装文件描述符的事件监听。

#### 构造函数
```cpp
Channel(EventLoop* loop, int fd);
```

#### 主要方法
```cpp
// 设置事件回调
void setReadCallback(const EventCallback& cb);
void setWriteCallback(const EventCallback& cb);
void setCloseCallback(const EventCallback& cb);
void setErrorCallback(const EventCallback& cb);

// 启用/禁用事件
void enableReading();
void disableReading();
void enableWriting();
void disableWriting();
void disableAll();

// 事件状态查询
bool isReading() const;
bool isWriting() const;
bool isNoneEvent() const;

// 获取文件描述符
int fd() const;

// 获取所属事件循环
EventLoop* ownerLoop() const;

// 移除通道
void remove();
```

#### 使用示例
```cpp
#include "network/Channel.h"

int sockfd = socket(AF_INET, SOCK_STREAM, 0);
Channel channel(&loop, sockfd);

// 设置读事件回调
channel.setReadCallback([sockfd]() {
    char buffer[1024];
    int n = read(sockfd, buffer, sizeof(buffer));
    if (n > 0) {
        std::cout << "Received: " << std::string(buffer, n) << std::endl;
    }
});

// 启用读事件
channel.enableReading();
```

---

### Poller 类
epoll 封装，提供高效的 I/O 多路复用。

#### 构造函数
```cpp
Poller(EventLoop* loop);
```

#### 主要方法
```cpp
// 等待事件发生
Timestamp poll(int timeoutMs, std::vector<Channel*>* activeChannels);

// 更新通道事件
void updateChannel(Channel* channel);

// 移除通道
void removeChannel(Channel* channel);

// 检查通道是否存在
bool hasChannel(Channel* channel) const;
```

---

## ⏰ 定时器 API

### Timer 类
定时器类，支持一次性定时器和周期性定时器。

#### 构造函数
```cpp
Timer(const TimerCallback& cb, TimePoint when, Duration interval);
```

#### 主要方法
```cpp
// 执行定时器回调
void run() const;

// 重启定时器
void restart(TimePoint now);

// 获取过期时间
TimePoint expiration() const;

// 检查是否重复
bool repeat() const;

// 获取序号
int64_t sequence() const;
```

### TimerQueue 类
定时器队列，管理多个定时器。

#### 构造函数
```cpp
TimerQueue(EventLoop* loop);
```

#### 主要方法
```cpp
// 添加定时器
int64_t addTimer(const TimerCallback& cb, TimePoint when, Duration interval);

// 取消定时器
void cancel(int64_t timerId);
```

---

## 📝 日志 API

### Logger 类
异步日志系统，基于 spdlog 实现。

#### 主要方法
```cpp
// 获取单例实例
static Logger& instance();

// 设置日志级别
void setLevel(spdlog::level::level_enum level);

// 设置日志文件
void setFile(const std::string& filename);

// 设置日志格式
void setPattern(const std::string& pattern);

// 刷新日志
void flush();

// 日志宏
LOG_TRACE("message");
LOG_DEBUG("message");
LOG_INFO("message");
LOG_WARN("message");
LOG_ERROR("message");
```

#### 使用示例
```cpp
#include "network/logger.h"

// 设置日志级别
Logger::instance().setLevel(spdlog::level::info);

// 记录日志
LOG_INFO("Server started on port {}", port);
LOG_ERROR("Connection failed: {}", strerror(errno));
```

---

## 🌐 TCP 服务器 API

### TcpServer 类
多线程 TCP 服务器，支持高并发连接。

#### 构造函数
```cpp
TcpServer(EventLoop* loop, const std::string& name, 
          const std::string& listenAddr, uint16_t listenPort);
```

#### 主要方法
```cpp
// 设置回调函数
void setConnectionCallback(const ConnectionCallback& cb);
void setMessageCallback(const MessageCallback& cb);
void setWriteCompleteCallback(const WriteCompleteCallback& cb);
void setThreadInitCallback(const ThreadInitCallback& cb);

// 设置线程数量
void setThreadNum(int numThreads);

// 启动服务器
void start();

// 获取服务器信息
const std::string& name() const;
const std::string& listenAddress() const;
uint16_t listenPort() const;
EventLoopThreadPool* threadPool();
```

#### 回调函数类型
```cpp
using ConnectionCallback = std::function<void(const std::shared_ptr<TcpConnection>&)>;
using MessageCallback = std::function<void(const std::shared_ptr<TcpConnection>&, void*, size_t)>;
using WriteCompleteCallback = std::function<void(const std::shared_ptr<TcpConnection>&)>;
using ThreadInitCallback = std::function<void(EventLoop*)>;
```

#### 使用示例
```cpp
#include "network/TcpServer.h"

EventLoop loop;
TcpServer server(&loop, "MyServer", "127.0.0.1", 6379);

// 设置连接回调
server.setConnectionCallback([](const std::shared_ptr<TcpConnection>& conn) {
    if (conn->connected()) {
        LOG_INFO("New connection from {}", conn->peerAddress());
    } else {
        LOG_INFO("Connection closed: {}", conn->peerAddress());
    }
});

// 设置消息回调
server.setMessageCallback([](const std::shared_ptr<TcpConnection>& conn, void* data, size_t len) {
    std::string message(static_cast<char*>(data), len);
    LOG_INFO("Received: {}", message);
    
    // 回显消息
    conn->send(message);
});

// 设置多线程
server.setThreadNum(4);

// 启动服务器
server.start();
loop.loop();
```

### TcpConnection 类
TCP 连接管理类，处理连接的生命周期。

#### 主要方法
```cpp
// 连接状态
bool connected() const;
bool disconnected() const;

// 获取连接信息
const std::string& name() const;
const std::string& localAddress() const;
const std::string& peerAddress() const;

// 发送数据
void send(const std::string& message);
void send(const void* data, size_t len);
void send(Buffer* buffer);

// 设置回调函数
void setConnectionCallback(const ConnectionCallback& cb);
void setMessageCallback(const MessageCallback& cb);
void setWriteCompleteCallback(const WriteCompleteCallback& cb);
void setCloseCallback(const CloseCallback& cb);

// 关闭连接
void shutdown();
void forceClose();

// 获取事件循环
EventLoop* getLoop() const;

// 连接建立/销毁
void connectEstablished();
void connectDestroyed();
```

### Acceptor 类
连接接受器，负责监听和接受新连接。

#### 构造函数
```cpp
Acceptor(EventLoop* loop, const std::string& listenAddr, uint16_t listenPort);
```

#### 主要方法
```cpp
// 设置新连接回调
void setNewConnectionCallback(const NewConnectionCallback& cb);

// 开始监听
void listen();

// 检查是否正在监听
bool listening() const;
```

---

## 🧵 多线程 API

### EventLoopThread 类
事件循环线程，在独立线程中运行 EventLoop。

#### 构造函数
```cpp
EventLoopThread(const ThreadInitCallback& cb = ThreadInitCallback(), 
               const std::string& name = "");
```

#### 主要方法
```cpp
// 启动线程并返回 EventLoop
EventLoop* startLoop();

// 获取线程名称
const std::string& name() const;
```

#### 使用示例
```cpp
#include "network/EventLoopThread.h"

EventLoopThread thread([](EventLoop* loop) {
    LOG_INFO("Thread initialized for EventLoop {}", static_cast<void*>(loop));
}, "IOThread");

EventLoop* loop = thread.startLoop();
// 在新线程中运行事件循环
loop->loop();
```

### EventLoopThreadPool 类
事件循环线程池，管理多个 EventLoopThread。

#### 构造函数
```cpp
EventLoopThreadPool(EventLoop* baseLoop, const std::string& name);
```

#### 主要方法
```cpp
// 设置线程数量
void setThreadNum(int numThreads);

// 设置线程初始化回调
void setThreadInitCallback(const ThreadInitCallback& cb);

// 启动线程池
void start();

// 获取下一个 EventLoop（轮询分发）
EventLoop* getNextLoop();

// 获取指定索引的 EventLoop
EventLoop* getLoop(int index);

// 获取所有 EventLoop
std::vector<EventLoop*> getAllLoops();

// 获取线程池信息
const std::string& name() const;
int threadNum() const;
bool started() const;
```

#### 使用示例
```cpp
#include "network/EventLoopThreadPool.h"

EventLoop baseLoop;
EventLoopThreadPool threadPool(&baseLoop, "ThreadPool");

// 设置线程数量
threadPool.setThreadNum(4);

// 设置线程初始化回调
threadPool.setThreadInitCallback([](EventLoop* loop) {
    LOG_INFO("Thread {} started", static_cast<void*>(loop));
});

// 启动线程池
threadPool.start();

// 获取 EventLoop 处理连接
EventLoop* ioLoop = threadPool.getNextLoop();
```

---

## 🔧 Buffer 类
高效的网络缓冲区，支持自动扩容和零拷贝操作。

#### 构造函数
```cpp
Buffer(size_t initialSize = 1024);
```

#### 主要方法
```cpp
// 数据操作
size_t readableBytes() const;
size_t writableBytes() const;
size_t prependableBytes() const;

const char* peek() const;
const char* findCRLF() const;
const char* findCRLF(const char* start) const;

void retrieve(size_t len);
void retrieveUntil(const char* end);
void retrieveAll();

// 写入数据
void append(const std::string& str);
void append(const char* data, size_t len);
void append(const void* data, size_t len);

// 读取数据
ssize_t readFd(int fd, int* savedErrno);
ssize_t writeFd(int fd, int* savedErrno);

// 确保可写空间
void ensureWritableBytes(size_t len);
```

#### 使用示例
```cpp
#include "network/Buffer.h"

Buffer buffer;

// 写入数据
buffer.append("Hello World");

// 读取数据
if (buffer.readableBytes() > 0) {
    std::string message(buffer.peek(), buffer.readableBytes());
    buffer.retrieveAll();
}
```

---

## 📚 使用建议

### 1. 线程安全
- EventLoop 必须在创建线程中运行
- 跨线程操作使用 `runInLoop()` 或 `queueInLoop()`
- TcpServer 的回调函数在对应的 IO 线程中执行

### 2. 内存管理
- 使用智能指针管理对象生命周期
- Buffer 自动管理内存，无需手动分配
- 定时器会自动清理，无需手动删除

### 3. 性能优化
- 合理设置线程池大小（通常为 CPU 核心数的 2-4 倍）
- 使用 Buffer 的零拷贝接口
- 避免在事件循环中执行耗时操作

### 4. 错误处理
- 检查所有系统调用的返回值
- 使用日志记录错误信息
- 在异常情况下优雅关闭连接

---

## 🎯 最佳实践

### 服务器启动流程
```cpp
1. 创建 EventLoop
2. 创建 TcpServer
3. 设置回调函数
4. 配置线程池
5. 启动服务器
6. 运行事件循环
```

### 连接处理流程
```cpp
1. 在 ConnectionCallback 中处理连接建立/断开
2. 在 MessageCallback 中处理数据接收
3. 使用 TcpConnection::send() 发送响应
4. 避免阻塞事件循环线程
```

### 资源清理
```cpp
1. EventLoop 析构时自动清理资源
2. TcpConnection 自动管理连接生命周期
3. 定时器自动取消和清理
4. 日志系统自动刷新和关闭
```

这个 API 文档涵盖了 SunKV 项目中所有核心组件的使用方法，为开发提供了完整的参考！🚀
