#include "EventLoop.h"
#include "Poller.h"
#include "network/logger.h"
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
    LOG_INFO("EventLoop 已创建，线程 {}", oss.str());
    
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
    LOG_INFO("EventLoop 已销毁，线程 {}", oss.str());
    
    // 必须通过 Channel::remove() 离开 Poller，以便将 addedToLoop_ 置 false；
    // 仅调用 poller_->removeChannel() 会跳过该状态，~Channel 将触发 assert(!addedToLoop_)。
    if (wakeupChannel_) {
        wakeupChannel_->disableAll();
        wakeupChannel_->remove();
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
    LOG_INFO("EventLoop {} 开始循环", oss.str());
    
    while (!quit_ || hasPendingTasks()) {
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

    // quit_ 已置位且 pendingTasks_ 清空后，仍执行一次 doPendingTasks 覆盖最后一轮入队时序。
    doPendingTasks();
    
    LOG_INFO("EventLoop {} 停止循环", oss.str());
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
        // 只在真正的错误时记录日志，忽略 EAGAIN
        if (n != -1 || errno != EAGAIN) {
            LOG_ERROR("EventLoop::wakeup() 写入字节数异常: {}, 期望 8, errno: {}", n, errno);
        }
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
        LOG_ERROR("EventLoop::handleWakeup() 读取字节数异常: {}, 期望 8", n);
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
        LOG_ERROR("创建 eventfd 失败: {}", strerror(errno));
        exit(1);
    }
    return fd;
}

void EventLoop::assertInLoopThread() const {
    // 在构造函数和析构函数中不要检查线程
    if (!isInLoopThread() && looping_) {
        std::ostringstream oss1, oss2, oss3;
        oss1 << threadId_;
        oss2 << threadId_;
        oss3 << std::this_thread::get_id();
        LOG_ERROR("EventLoop::assertInLoopThread() - EventLoop {} 创建于线程 {}，当前线程为 {}", 
                 oss1.str(), oss2.str(), oss3.str());
        // 不要调用 exit(1)，而是抛出异常
        throw std::runtime_error("EventLoop 在错误线程中被调用");
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