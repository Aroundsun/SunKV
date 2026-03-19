#include "EventLoop.h"
#include "Poller.h"
#include "Channel.h"
#include "TimerQueue.h"
#include <unistd.h>
#include <sys/eventfd.h>
#include <errno.h>
#include <string.h>
#include <sstream>

EventLoop::EventLoop() 
    : looping_(false),
      quit_(false),
      callingPendingTasks_(false),
      destructing_(false),
      threadId_(std::this_thread::get_id()),
      poller_(new Poller(this)),
      wakeupFd_(createEventfd()),
      timerQueue_(new TimerQueue(this)),
      eventHandling_(false) {
    
    std::ostringstream oss;
    oss << threadId_;
    LOG_INFO("EventLoop created in thread {}", oss.str());
    
    // 创建并配置唤醒 Channel
    wakeupChannel_ = std::make_unique<Channel>(this, wakeupFd_);
    wakeupChannel_->setReadCallback(std::bind(&EventLoop::handleWakeup, this));
    wakeupChannel_->enableReading();
}

EventLoop::~EventLoop() {
    // 设置析构标志
    destructing_ = true;
    
    std::ostringstream oss;
    oss << threadId_;
    LOG_INFO("EventLoop destroyed in thread {}", oss.str());
    
    // 在析构函数中直接调用 Poller 的方法，避免线程检查
    if (wakeupChannel_) {
        wakeupChannel_->disableAll();
        // 直接调用 Poller 的 removeChannel，避免 EventLoop::removeChannel 的线程检查
        poller_->removeChannel(wakeupChannel_.get());
    }
    
    close(wakeupFd_);
}

void EventLoop::loop() {
    assert(!looping_);
    assertInLoopThread();
    
    looping_ = true;
    quit_ = false;
    std::ostringstream oss;
    oss << threadId_;
    LOG_INFO("EventLoop {} start looping", oss.str());
    
    while (!quit_) {
        // 等待事件
        std::vector<Channel*> activeChannels;
        int timeoutMs = poller_->poll(1000, &activeChannels);
        
        eventHandling_ = true;
        for (Channel* channel : activeChannels) {
            channel->handleEvent();
        }
        eventHandling_ = false;
        
        // 执行待处理的任务
        doPendingTasks();
    }
    
    LOG_INFO("EventLoop {} stop looping", oss.str());
    looping_ = false;
}

void EventLoop::quit() {
    quit_ = true;
    if (!isInLoopThread() && !destructing_) {
        wakeup();
    }
}

bool EventLoop::isInLoopThread() const {
    return threadId_ == std::this_thread::get_id();
}

void EventLoop::runInLoop(Functor cb) {
    if (isInLoopThread()) {
        cb();
    } else {
        queueInLoop(cb);
    }
}

void EventLoop::queueInLoop(Functor cb) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        pendingTasks_.push_back(std::move(cb));
    }
    
    // 如果不在事件循环线程中，或者正在执行待处理任务，需要唤醒
    if ((!isInLoopThread() || callingPendingTasks_) && !destructing_) {
        wakeup();
    }
}

void EventLoop::wakeup() {
    uint64_t one = 1;
    ssize_t n = write(wakeupFd_, &one, sizeof(one));
    if (n != sizeof(one)) {
        LOG_ERROR("EventLoop::wakeup() writes {} bytes instead of 8", n);
    }
}

void EventLoop::updateChannel(Channel* channel) {
    assert(channel->ownerLoop() == this);
    assertInLoopThread();
    poller_->updateChannel(channel);
}

void EventLoop::removeChannel(Channel* channel) {
    assert(channel->ownerLoop() == this);
    assertInLoopThread();
    
    if (eventHandling_) {
        assert(channel->isNoneEvent());
    }
    
    poller_->removeChannel(channel);
}

bool EventLoop::hasPendingTasks() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return !pendingTasks_.empty();
}

bool EventLoop::hasChannel(Channel* channel) const {
    assertInLoopThread();
    return poller_->hasChannel(channel);
}

void EventLoop::handleWakeup() {
    uint64_t one = 1;
    ssize_t n = read(wakeupFd_, &one, sizeof(one));
    if (n != sizeof(one)) {
        LOG_ERROR("EventLoop::handleWakeup() reads {} bytes instead of 8", n);
    }
}

void EventLoop::doPendingTasks() {
    std::vector<Functor> tasks;
    callingPendingTasks_ = true;
    
    {
        std::lock_guard<std::mutex> lock(mutex_);
        tasks.swap(pendingTasks_);
    }
    
    for (const Functor& task : tasks) {
        task();
    }
    
    callingPendingTasks_ = false;
}

int EventLoop::createEventfd() {
    int fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (fd < 0) {
        LOG_ERROR("Failed to create eventfd: {}", strerror(errno));
        exit(1);
    }
    return fd;
}

void EventLoop::assertInLoopThread() const {
    if (!isInLoopThread()) {
        std::ostringstream oss1, oss2, oss3;
        oss1 << threadId_;
        oss2 << threadId_;
        oss3 << std::this_thread::get_id();
        LOG_ERROR("EventLoop::assertInLoopThread() - EventLoop {} was created in thread {} but current thread is {}", 
                 oss1.str(), oss2.str(), oss3.str());
        exit(1);
    }
}

// 定时器相关方法实现
int64_t EventLoop::runAt(const TimePoint& time, const TimerCallback& cb) {
    return timerQueue_->addTimer(cb, time, Duration{0});
}

int64_t EventLoop::runAfter(Duration delay, const TimerCallback& cb) {
    TimePoint when = std::chrono::steady_clock::now() + delay;
    return runAt(when, cb);
}

int64_t EventLoop::runEvery(Duration interval, const TimerCallback& cb) {
    TimePoint when = std::chrono::steady_clock::now() + interval;
    return timerQueue_->addTimer(cb, when, interval);
}

void EventLoop::cancelTimer(int64_t timerId) {
    timerQueue_->cancel(timerId);
}