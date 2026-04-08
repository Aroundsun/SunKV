#include "EventLoopThread.h"
#include <sstream>

EventLoopThread::EventLoopThread(const ThreadInitCallback& cb, const std::string& name)
    : loop_(nullptr),
      exiting_(false),
      callback_(cb),
      name_(name) {
    
    if (name_.empty()) {
        static int num = 0;
        std::ostringstream oss;
        oss << "EventLoopThread" << num++;
        name_ = oss.str();
    }
}

EventLoopThread::~EventLoopThread() {
    exiting_ = true;
    if (loop_ != nullptr) {
        loop_->quit();
        thread_.join();
    }
}

EventLoop* EventLoopThread::startLoop() {
    thread_ = std::thread(&EventLoopThread::threadFunc, this);
    
    EventLoop* loop = nullptr;
    {
        std::unique_lock<std::mutex> lock(mutex_);
        while (loop_ == nullptr) {
            cond_.wait(lock);
        }
        loop = loop_;
    }
    
    return loop;
}

void EventLoopThread::threadFunc() {
    LOG_INFO("EventLoop 线程 {} 启动中", name_);
    
    EventLoop loop;
    
    if (callback_) {
        callback_(&loop);
    }
    
    {
        std::lock_guard<std::mutex> lock(mutex_);
        loop_ = &loop;
        cond_.notify_one();
    }
    
    loop.loop();
    
    {
        std::lock_guard<std::mutex> lock(mutex_);
        loop_ = nullptr;
    }
    
    LOG_INFO("EventLoop 线程 {} 已停止", name_);
}

void EventLoopThread::stop() {
    if (loop_) {
        LOG_INFO("EventLoopThread::stop [{}] - 正在停止事件循环", name_);
        loop_->quit();
    }
    
    if (thread_.joinable()) {
        thread_.join();
    }
    
    exiting_ = true;
}
