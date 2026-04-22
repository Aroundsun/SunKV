#pragma once

#include <functional>
#include <memory>

class EventLoop;

// Channel 类负责管理一个文件描述符的事件
class Channel {
public:
    using EventCallback = std::function<void()>;
    using ReadEventCallback = std::function<void()>;
    
    Channel(EventLoop* loop, int fd);
    ~Channel();
    
    // 禁止拷贝和赋值
    Channel(const Channel&) = delete;
    Channel& operator=(const Channel&) = delete;
    
    // 处理事件
    void handleEvent();
    
    // 设置回调函数
    void setReadCallback(ReadEventCallback cb) { readCallback_ = std::move(cb); }
    void setWriteCallback(EventCallback cb) { writeCallback_ = std::move(cb); }
    void setCloseCallback(EventCallback cb) { closeCallback_ = std::move(cb); }
    void setErrorCallback(EventCallback cb) { errorCallback_ = std::move(cb); }
    
    // 绑定一个对象，用于智能指针管理
    void tie(const std::shared_ptr<void>& obj);
    
    // 获取文件描述符
    int fd() const { return fd_; }
    
    // 获取事件
    int events() const { return events_; }
    
    // 设置接收到的事件
    void setRevents(int revt) { revents_ = revt; }
    
    // 获取/设置索引（用于 Poller 内部状态管理）
    int index() const { return index_; }
    void set_index(int idx) { index_ = idx; }
    
    // 判断是否没有事件
    bool isNoneEvent() const { return events_ == kNoneEventStatic; }
    
    // 启用/禁用读写事件
    void enableReading() { events_ |= kReadEventStatic; update(); }
    void disableReading() { events_ &= ~kReadEventStatic; update(); }
    void enableWriting() { events_ |= kWriteEventStatic; update(); }
    void disableWriting() { events_ &= ~kWriteEventStatic; update(); }
    void disableAll() { events_ = kNoneEventStatic; update(); }
    
    // 判断事件状态
    bool isReading() const { return events_ & kReadEventStatic; }
    bool isWriting() const { return events_ & kWriteEventStatic; }
    
    // 获取事件循环
    EventLoop* ownerLoop() { return loop_; }
    
    // 从事件循环中移除
    void remove();

    // 事件状态
    enum EventType {
        kNoneEvent = 0,
        // 这些是 Channel 内部“抽象事件位”，与 epoll/poll 常量无关
        kReadEvent = 1 << 0,
        kWriteEvent = 1 << 1,
        kErrorEvent = 1 << 2,
        kCloseEvent = 1 << 3
    };

    static const int kNoneEventStatic = 0; // 无事件
    static const int kReadEventStatic = 1 << 0; // 读事件
    static const int kWriteEventStatic = 1 << 1; // 写事件
    static const int kErrorEventStatic = 1 << 2; // 错误事件
    static const int kCloseEventStatic = 1 << 3; // 关闭事件

private:
    void update();
    void handleEventWithGuard();
    
    EventLoop* loop_;
    const int fd_;  // 文件描述符
    int events_;    // 关注的事件
    int revents_;   // 接收到的事件
    int index_;     // 用于 Poller 内部状态管理
    
    std::weak_ptr<void> tie_; // 绑定一个对象，用于智能指针管理
    bool tied_; // 是否绑定
    bool eventHandling_; // 是否正在处理事件
    bool addedToLoop_; // 是否添加到事件循环
    
    ReadEventCallback readCallback_; // 读事件回调
    EventCallback writeCallback_; // 写事件回调
    EventCallback closeCallback_; // 关闭事件回调
    EventCallback errorCallback_; // 错误事件回调
};