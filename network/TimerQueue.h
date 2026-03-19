#pragma once

#include <set>
#include <vector>
#include <memory>
#include <mutex>
#include <functional>
#include <chrono>
#include <map>
#include "Timer.h"
#include "Channel.h"
#include "logger.h"

class EventLoop;

class TimerQueue {
public:
    using Clock = std::chrono::steady_clock;
    using TimePoint = std::chrono::steady_clock::time_point;
    using Duration = std::chrono::milliseconds;
    
    explicit TimerQueue(EventLoop* loop);
    ~TimerQueue();
    
    // 禁止拷贝和赋值
    TimerQueue(const TimerQueue&) = delete;
    TimerQueue& operator=(const TimerQueue&) = delete;
    
    // 添加定时器
    // 返回定时器的唯一标识，可用于取消定时器
    int64_t addTimer(const Timer::TimerCallback& cb, TimePoint when, Duration interval = Duration{0});
    
    // 取消定时器
    void cancel(int64_t timerId);

private:
    // 定器条目
    struct Entry {
        TimePoint expiration;
        std::shared_ptr<Timer> timer;
        
        Entry(TimePoint exp, std::shared_ptr<Timer> t)
            : expiration(exp), timer(std::move(t)) {}
        
        bool operator<(const Entry& other) const {
            return expiration < other.expiration;
        }
    };
    
    // 用于查找的哨兵条目
    static Entry createSentry(TimePoint when) {
        return Entry(when, std::shared_ptr<Timer>());
    }
    
    // 定时器集合，按过期时间排序
    using TimerList = std::set<Entry>;
    
    // 处理定时器文件描述符的读事件
    void handleRead();
    
    // 获取最早的过期时间
    std::vector<Entry> getExpired(TimePoint now);
    
    // 重置定时器
    void reset(const std::vector<Entry>& expired, TimePoint now);
    
    // 插入定时器到集合中
    bool insert(std::shared_ptr<Timer> timer);
    
    // 创建定时器文件描述符
    int createTimerfd();
    
    // 读取定时器文件描述符
    void readTimerfd();
    
    // 重置定时器文件描述符
    void resetTimerfd(TimePoint when);
    
    EventLoop* loop_;
    const int timerfd_;
    std::unique_ptr<Channel> timerfdChannel_;
    TimerList timers_;
    
    // 用于取消定时器的映射
    std::mutex mutex_;
    std::map<int64_t, std::shared_ptr<Timer>> timerMap_;
    std::atomic<int64_t> nextTimerId_{1};
};
