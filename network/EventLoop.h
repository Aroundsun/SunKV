#pragma once

#include <functional>
#include <vector>
#include <memory>
#include <atomic>
#include <mutex>
#include <thread>
#include <sys/eventfd.h>
#include <chrono>
#include "logger.h"
#include "Channel.h"
#include "Poller.h"
#include "TimerQueue.h"

class EventLoop {
public:
    using Functor = std::function<void()>;
    
    EventLoop();
    ~EventLoop();
    
    // 禁止拷贝和赋值
    EventLoop(const EventLoop&) = delete;
    EventLoop& operator=(const EventLoop&) = delete;
    
    // 事件循环
    void loop();
    void quit();
    
    // 检查是否在事件循环线程
    bool isInLoopThread() const;
    void assertInLoopThread() const;
    
    // 检查是否在析构中
    bool isDestructing() const { return destructing_; }
    
    // 在事件循环线程中执行任务
    void runInLoop(Functor cb);
    
    // 将任务加入队列
    void queueInLoop(Functor cb);
    
    // 唤醒事件循环
    void wakeup();
    
    // 添加/更新/删除 Channel
    void updateChannel(Channel* channel);
    void removeChannel(Channel* channel);
    
    // 检查是否有待处理的任务
    bool hasPendingTasks() const;
    
    // 检查是否包含某个 Channel
    bool hasChannel(Channel* channel) const;
    
    // 定时器相关方法
    using TimerCallback = std::function<void()>;
    using TimePoint = std::chrono::steady_clock::time_point;
    using Duration = std::chrono::milliseconds;
    
    // 添加定时器
    int64_t runAt(const TimePoint& time, const TimerCallback& cb);
    int64_t runAfter(Duration delay, const TimerCallback& cb);
    int64_t runEvery(Duration interval, const TimerCallback& cb);
    
    // 取消定时器
    void cancelTimer(int64_t timerId);

private:
    // 处理唤醒事件
    void handleWakeup();
    
    // 执行待处理的任务
    void doPendingTasks();
    
    // 创建 eventfd
    int createEventfd();
    
    std::atomic<bool> looping_;
    std::atomic<bool> quit_;
    std::atomic<bool> callingPendingTasks_;
    std::atomic<bool> destructing_;  // 析构标志
    const std::thread::id threadId_;
    
    std::unique_ptr<Poller> poller_;
    int wakeupFd_;  // 用于唤醒的 eventfd
    std::unique_ptr<Channel> wakeupChannel_;
    std::unique_ptr<TimerQueue> timerQueue_;
    
    mutable std::mutex mutex_;
    std::vector<Functor> pendingTasks_;
    
    bool eventHandling_;
};