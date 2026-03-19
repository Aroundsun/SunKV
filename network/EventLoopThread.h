#pragma once

#include <thread>
#include <mutex>
#include <condition_variable>
#include <memory>
#include "EventLoop.h"
#include "logger.h"

// EventLoop 线程类，用于在独立线程中运行 EventLoop
class EventLoopThread {
public:
    using ThreadInitCallback = std::function<void(EventLoop*)>;
    
    EventLoopThread(const ThreadInitCallback& cb = ThreadInitCallback(), const std::string& name = "");
    ~EventLoopThread();
    
    // 禁止拷贝和赋值
    EventLoopThread(const EventLoopThread&) = delete;
    EventLoopThread& operator=(const EventLoopThread&) = delete;
    
    // 启动线程并返回 EventLoop
    EventLoop* startLoop();
    
    // 获取线程名称
    const std::string& name() const { return name_; }

private:
    // 线程函数
    void threadFunc();
    
    EventLoop* loop_;
    bool exiting_;
    std::thread thread_;
    std::mutex mutex_;
    std::condition_variable cond_;
    ThreadInitCallback callback_;
    std::string name_;
};
