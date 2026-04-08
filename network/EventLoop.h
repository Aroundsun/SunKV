/**
 * @file EventLoop.h
 * @brief SunKV 事件循环系统
 * 
 * 本文件包含事件循环的实现，提供：
 * - Reactor 模式的事件循环
 * - 跨线程任务调度
 * - 定时器管理
 * - Channel 生命周期管理
 * - 线程安全的事件处理
 */

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

/**
 * @class EventLoop
 * @brief 事件循环类
 * 
 * 实现 Reactor 模式的事件循环，管理文件描述符事件和定时器
 */
class EventLoop {
public:
    using Functor = std::function<void()>;                 ///< 任务函数类型
    
    /**
     * @brief 构造函数
     */
    EventLoop();
    
    /**
     * @brief 析构函数
     */
    ~EventLoop();
    
    // 禁止拷贝和赋值
    EventLoop(const EventLoop&) = delete;
    EventLoop& operator=(const EventLoop&) = delete;
    
    /**
     * @brief 启动事件循环
     */
    void loop();
    
    /**
     * @brief 退出事件循环
     */
    void quit();
    
    /**
     * @brief 检查是否在事件循环线程
     * @return 是否在事件循环线程
     */
    bool isInLoopThread() const;
    
    /**
     * @brief 断言在事件循环线程
     */
    void assertInLoopThread() const;
    
    /**
     * @brief 检查是否在析构中
     * @return 是否在析构中
     */
    bool isDestructing() const { return destructing_; }
    
    /**
     * @brief 在事件循环线程中执行任务
     * @param cb 要执行的任务
     */
    void runInLoop(Functor cb);
    
    /**
     * @brief 将任务加入队列
     * @param cb 要执行的任务
     */
    void queueInLoop(Functor cb);
    
    /**
     * @brief 唤醒事件循环
     */
    void wakeup();
    
    /**
     * @brief 更新 Channel
     * @param channel 要更新的 Channel
     */
    void updateChannel(Channel* channel);
    
    /**
     * @brief 删除 Channel
     * @param channel 要删除的 Channel
     */
    void removeChannel(Channel* channel);
    
    /**
     * @brief 检查是否有待处理的任务
     * @return 是否有待处理的任务
     */
    bool hasPendingTasks() const;
    
    /**
     * @brief 检查是否包含某个 Channel
     * @param channel 要检查的 Channel
     * @return 是否包含
     */
    bool hasChannel(Channel* channel) const;
    
    /// 定时器相关类型定义
    using TimerCallback = std::function<void()>;                           ///< 定时器回调类型
    using TimePoint = std::chrono::steady_clock::time_point;               ///< 时间点类型
    using Duration = std::chrono::milliseconds;                             ///< 时间间隔类型
    
    /**
     * @brief 在指定时间运行定时器
     * @param time 运行时间
     * @param cb 回调函数
     * @return 定时器 ID
     */
    int64_t runAt(const TimePoint& time, const TimerCallback& cb);
    
    /**
     * @brief 延迟运行定时器
     * @param delay 延迟时间
     * @param cb 回调函数
     * @return 定时器 ID
     */
    int64_t runAfter(Duration delay, const TimerCallback& cb);
    
    /**
     * @brief 定期运行定时器
     * @param interval 运行间隔
     * @param cb 回调函数
     * @return 定时器 ID
     */
    int64_t runEvery(Duration interval, const TimerCallback& cb);
    
    /**
     * @brief 取消定时器
     * @param timerId 定时器 ID
     */
    void cancelTimer(int64_t timerId);

private:
    /// 内部方法
    void handleWakeup();                                 ///< 处理唤醒事件
    void doPendingTasks();                                ///< 执行待处理的任务
    int createEventfd();                                  ///< 创建 eventfd
    
    std::atomic<bool> looping_;                          ///< 是否正在循环
    std::atomic<bool> quit_;                             ///< 是否请求退出
    std::atomic<bool> callingPendingTasks_;               ///< 是否正在调用待处理任务
    std::atomic<bool> destructing_;                      ///< 析构标志
    const std::thread::id threadId_;                     ///< 线程 ID
    
    std::unique_ptr<Poller> poller_;                      ///< Poller 实例
    int wakeupFd_;                                       ///< 用于唤醒的 eventfd
    std::unique_ptr<Channel> wakeupChannel_;              ///< 唤醒 Channel
    std::unique_ptr<TimerQueue> timerQueue_;              ///< 定时器队列
    
    mutable std::mutex mutex_;                           ///< 互斥锁
    std::vector<Functor> pendingTasks_;                   ///< 待处理任务队列
    
    bool eventHandling_;                                 ///< 是否正在处理事件
};