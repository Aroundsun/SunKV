#include "TimerQueue.h"
#include "EventLoop.h"
#include <sys/timerfd.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <iterator>

TimerQueue::TimerQueue(EventLoop* loop)
    : loop_(loop),
      timerfd_(createTimerfd()),
      timerfdChannel_(std::make_unique<Channel>(loop, timerfd_)) {
    
    LOG_DEBUG("TimerQueue created with timerfd {}", timerfd_);
    
    timerfdChannel_->setReadCallback(
        std::bind(&TimerQueue::handleRead, this));
    timerfdChannel_->enableReading();
}

TimerQueue::~TimerQueue() {
    timerfdChannel_->disableAll();
    timerfdChannel_->remove();
    close(timerfd_);
    LOG_DEBUG("TimerQueue destroyed");
}

int64_t TimerQueue::addTimer(const Timer::TimerCallback& cb, TimePoint when, Duration interval) {
    auto timer = std::make_shared<Timer>(cb, when, interval);
    
    int64_t timerId;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        timerId = nextTimerId_++;
        timerMap_[timerId] = timer;
    }
    
    loop_->runInLoop(
        std::bind(&TimerQueue::insert, this, timer));
    
    LOG_DEBUG("Timer added with id {}, expiration {}ms, interval {}ms", 
             timerId, 
             std::chrono::duration_cast<std::chrono::milliseconds>(when - Clock::now()).count(),
             interval.count());
    
    return timerId;
}

void TimerQueue::cancel(int64_t timerId) {
    loop_->runInLoop(
        [this, timerId]() {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = timerMap_.find(timerId);
            if (it != timerMap_.end()) {
                auto timer = it->second;
                timerMap_.erase(it);
                
                // 从定时器集合中移除
                for (auto timerIt = timers_.begin(); timerIt != timers_.end(); ++timerIt) {
                    if (timerIt->timer == timer) {
                        timers_.erase(timerIt);
                        break;
                    }
                }
                
                LOG_DEBUG("Timer {} cancelled", timerId);
            }
        });
}

void TimerQueue::handleRead() {
    loop_->assertInLoopThread();
    
    TimePoint now = Clock::now();
    readTimerfd();
    
    std::vector<Entry> expired = getExpired(now);
    
    LOG_DEBUG("{} timers expired", expired.size());
    
    // 执行过期定时器的回调
    for (const Entry& entry : expired) {
        entry.timer->run();
    }
    
    reset(expired, now);
}

std::vector<TimerQueue::Entry> TimerQueue::getExpired(TimePoint now) {
    std::vector<Entry> expired;
    
    // 找到所有过期的定时器
    Entry sentry(now, std::shared_ptr<Timer>());
    auto end = timers_.lower_bound(sentry);
    
    std::copy(timers_.begin(), end, std::back_inserter(expired));
    timers_.erase(timers_.begin(), end);
    
    return expired;
}

void TimerQueue::reset(const std::vector<Entry>& expired, TimePoint now) {
    TimePoint nextExpire;
    
    for (const Entry& entry : expired) {
        if (entry.timer->repeat()) {
            entry.timer->restart(now);
            insert(entry.timer);
        }
    }
    
    if (!timers_.empty()) {
        nextExpire = timers_.begin()->expiration;
        resetTimerfd(nextExpire);
    }
}

bool TimerQueue::insert(std::shared_ptr<Timer> timer) {
    loop_->assertInLoopThread();
    
    bool earliestChanged = false;
    TimePoint when = timer->expiration();
    
    if (timers_.empty() || when < timers_.begin()->expiration) {
        earliestChanged = true;
    }
    
    timers_.emplace(Entry(when, std::move(timer)));
    
    if (earliestChanged) {
        resetTimerfd(when);
    }
    
    return earliestChanged;
}

int TimerQueue::createTimerfd() {
    int fd = ::timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    if (fd < 0) {
        LOG_ERROR("Failed to create timerfd: {}", strerror(errno));
        exit(1);
    }
    return fd;
}

void TimerQueue::readTimerfd() {
    uint64_t howmany;
    ssize_t n = ::read(timerfd_, &howmany, sizeof(howmany));
    if (n != sizeof(howmany)) {
        LOG_ERROR("TimerQueue::readTimerfd() reads {} bytes instead of 8", n);
    }
}

void TimerQueue::resetTimerfd(TimePoint when) {
    struct itimerspec newValue;
    struct itimerspec oldValue;
    
    // 计算从现在到过期时间的纳秒数
    auto now = Clock::now();
    auto duration = when - now;
    
    if (duration.count() < 0) {
        duration = std::chrono::milliseconds{0};
    }
    
    auto seconds = std::chrono::duration_cast<std::chrono::seconds>(duration);
    auto nanoseconds = std::chrono::duration_cast<std::chrono::nanoseconds>(duration - seconds);
    
    newValue.it_value.tv_sec = seconds.count();
    newValue.it_value.tv_nsec = nanoseconds.count();
    newValue.it_interval.tv_sec = 0;  // 不重复
    newValue.it_interval.tv_nsec = 0;
    
    int ret = ::timerfd_settime(timerfd_, 0, &newValue, &oldValue);
    if (ret < 0) {
        LOG_ERROR("timerfd_settime error: {}", strerror(errno));
    }
    
    LOG_DEBUG("Timerfd reset to expire in {}ms", 
             std::chrono::duration_cast<std::chrono::milliseconds>(duration).count());
}
