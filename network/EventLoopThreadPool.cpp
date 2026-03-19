#include "EventLoopThreadPool.h"
#include "EventLoop.h"
#include <sstream>

EventLoopThreadPool::EventLoopThreadPool(EventLoop* baseLoop, const std::string& name)
    : baseLoop_(baseLoop),
      name_(name),
      started_(false),
      numThreads_(0),
      next_(0) {
    
    if (name_.empty()) {
        static int num = 0;
        std::ostringstream oss;
        oss << "EventLoopThreadPool" << num++;
        name_ = oss.str();
    }
}

EventLoopThreadPool::~EventLoopThreadPool() {
    // EventLoopThread 的析构函数会自动停止线程
    LOG_INFO("EventLoopThreadPool {} destroyed", name_);
}

void EventLoopThreadPool::start() {
    assert(!started_);
    baseLoop_->assertInLoopThread();
    
    started_ = true;
    
    for (int i = 0; i < numThreads_; ++i) {
        std::ostringstream oss;
        oss << name_ << i;
        EventLoopThread* thread = new EventLoopThread(threadInitCallback_, oss.str());
        threads_.emplace_back(thread);
        loops_.emplace_back(thread->startLoop());
    }
    
    if (numThreads_ == 0 && threadInitCallback_) {
        threadInitCallback_(baseLoop_);
    }
}

EventLoop* EventLoopThreadPool::getNextLoop() {
    baseLoop_->assertInLoopThread();
    assert(started_);
    
    EventLoop* loop = baseLoop_;
    
    if (!loops_.empty()) {
        loop = loops_[next_];
        ++next_;
        if (static_cast<size_t>(next_) >= loops_.size()) {
            next_ = 0;
        }
    }
    
    return loop;
}

EventLoop* EventLoopThreadPool::getLoop(int index) {
    baseLoop_->assertInLoopThread();
    assert(started_);
    
    if (static_cast<size_t>(index) < loops_.size()) {
        return loops_[index];
    } else {
        return baseLoop_;
    }
}

std::vector<EventLoop*> EventLoopThreadPool::getAllLoops() {
    baseLoop_->assertInLoopThread();
    assert(started_);
    
    if (loops_.empty()) {
        return { baseLoop_ };
    } else {
        return loops_;
    }
}
