#pragma once

#include <vector>
#include <memory>
#include <string>
#include "EventLoopThread.h"
#include "logger.h"

class EventLoop;

// EventLoop 线程池类
class EventLoopThreadPool {
public:
    using ThreadInitCallback = std::function<void(EventLoop*)>;
    
    EventLoopThreadPool(EventLoop* baseLoop, const std::string& name);
    ~EventLoopThreadPool();
    
    // 禁止拷贝和赋值
    EventLoopThreadPool(const EventLoopThreadPool&) = delete;
    EventLoopThreadPool& operator=(const EventLoopThreadPool&) = delete;
    
    // 设置线程数量
    void setThreadNum(int numThreads) { numThreads_ = numThreads; }
    
    // 设置线程初始化回调
    void setThreadInitCallback(const ThreadInitCallback& cb) {
        threadInitCallback_ = cb;
    }
    
    // 启动线程池
    void start();
    
    // 是否已启动
    bool started() const { return started_; }
    
    // 获取下一个 EventLoop（轮询分发）
    EventLoop* getNextLoop();
    
    // 获取指定索引的 EventLoop
    EventLoop* getLoop(int index);
    
    // 获取所有 EventLoop
    std::vector<EventLoop*> getAllLoops();
    
    // 获取线程池名称
    const std::string& name() const { return name_; }
    
    // 获取线程数量
    int threadNum() const { return numThreads_; }

private:
    EventLoop* baseLoop_;
    std::string name_;
    bool started_;
    int numThreads_;
    int next_;
    std::vector<std::unique_ptr<EventLoopThread>> threads_;
    std::vector<EventLoop*> loops_;
    ThreadInitCallback threadInitCallback_;
};
