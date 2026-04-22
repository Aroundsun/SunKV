#include "Channel.h"
#include "EventLoop.h"
#include <unistd.h>

const int Channel::kNoneEventStatic; // 无事件
const int Channel::kReadEventStatic; // 读事件
const int Channel::kWriteEventStatic; // 写事件
const int Channel::kErrorEventStatic; // 错误事件
const int Channel::kCloseEventStatic; // 关闭事件

Channel::Channel(EventLoop* loop, int fd)
    : loop_(loop),
      fd_(fd),
      events_(kNoneEventStatic),
      revents_(kNoneEventStatic),
      index_(-1),
      tied_(false),
      eventHandling_(false),
      addedToLoop_(false) {
    LOG_DEBUG("Channel 已创建，fd={}", fd_);
}

Channel::~Channel() {
    LOG_DEBUG("Channel 已销毁，fd={}", fd_);
    assert(!eventHandling_);
    assert(!addedToLoop_);
    if (loop_->isInLoopThread()) {
        assert(!loop_->hasChannel(this));
    }
}

void Channel::handleEvent() {
    eventHandling_ = true;
    LOG_DEBUG("Channel::handleEvent 处理事件，fd={}, revents_={}", fd_, revents_);
    
    // 如果有绑定的对象，提升引用
    std::shared_ptr<void> guard;
    if (tied_) {
        guard = tie_.lock();
        if (guard) {
            handleEventWithGuard();
        }
    } else {
        handleEventWithGuard();
    }
    
    eventHandling_ = false;
}

void Channel::handleEventWithGuard() {
    // close/hup 优先：保持与历史行为一致，避免在非读事件下误触发 readCallback
    if ((revents_ & kCloseEventStatic) && !(revents_ & kReadEventStatic)) {
        if (closeCallback_) {
            closeCallback_();
        }
    }
    
    // 处理错误
    if (revents_ & kErrorEventStatic) {
        if (errorCallback_) {
            errorCallback_();
        }
    }
    
    // 处理可读事件
    if (revents_ & kReadEventStatic) {
        if (readCallback_) {
            readCallback_();
        }
    }
    
    // 处理可写事件
    if (revents_ & kWriteEventStatic) {
        if (writeCallback_) {
            writeCallback_();
        }
    }
}
// 绑定一个对象，用于智能指针管理
void Channel::tie(const std::shared_ptr<void>& obj) {
    tie_ = obj;
    tied_ = true;
}

void Channel::update() {
    // 添加到事件循环
    addedToLoop_ = true;
    // 更新事件循环
    loop_->updateChannel(this);
}

void Channel::remove() {
    // 断言没有事件
    assert(isNoneEvent());
    // 从事件循环中移除
    addedToLoop_ = false;
    loop_->removeChannel(this);
}