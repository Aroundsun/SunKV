#pragma once

#include <functional>
#include <atomic>
#include <chrono>
#include "logger.h"

class Timer {
public:
    using TimerCallback = std::function<void()>;
    using Clock = std::chrono::steady_clock;
    using TimePoint = std::chrono::steady_clock::time_point;
    using Duration = std::chrono::milliseconds;
    
    Timer(TimerCallback cb, TimePoint when, Duration interval)
        : callback_(std::move(cb)),
          expiration_(when),
          interval_(interval),
          repeat_(interval.count() > 0),
          sequence_(++s_numCreated_) {
        LOG_DEBUG("Timer created with expiration {}ms, interval {}ms, repeat {}", 
                 std::chrono::duration_cast<std::chrono::milliseconds>(when - Clock::now()).count(),
                 interval.count(), repeat_);
    }
    
    ~Timer() {
        LOG_DEBUG("Timer destroyed");
    }
    
    // 禁止拷贝和赋值
    Timer(const Timer&) = delete;
    Timer& operator=(const Timer&) = delete;
    
    // 执行定时器回调
    void run() const {
        callback_();
    }
    
    // 重启定时器（用于重复定时器）
    void restart(TimePoint now) {
        if (repeat_) {
            expiration_ = now + interval_;
        } else {
            expiration_ = TimePoint{};
        }
    }
    
    // 获取过期时间
    TimePoint expiration() const { return expiration_; }
    
    // 是否重复
    bool repeat() const { return repeat_; }
    
    // 获取序列号
    int64_t sequence() const { return sequence_; }
    
    // 判断是否过期
    bool expired(TimePoint now) const {
        return now >= expiration_;
    }

private:
    const TimerCallback callback_;
    TimePoint expiration_;      // 过期时间
    const Duration interval_;    // 重复间隔
    const bool repeat_;          // 是否重复
    const int64_t sequence_;     // 序列号
    
    static std::atomic<int64_t> s_numCreated_;
};
