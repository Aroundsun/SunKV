#include "Channel.h"
#include "EventLoop.h"
#include <poll.h>
#include <unistd.h>

const int Channel::kNoneEventStatic;
const int Channel::kReadEventStatic;
const int Channel::kWriteEventStatic;
const int Channel::kErrorEventStatic;

Channel::Channel(EventLoop* loop, int fd)
    : loop_(loop),
      fd_(fd),
      events_(kNoneEventStatic),
      revents_(kNoneEventStatic),
      index_(-1),
      tied_(false),
      eventHandling_(false),
      addedToLoop_(false) {
    LOG_DEBUG("Channel created for fd {}", fd_);
}

Channel::~Channel() {
    LOG_DEBUG("Channel destroyed for fd {}", fd_);
    assert(!eventHandling_);
    assert(!addedToLoop_);
    if (loop_->isInLoopThread()) {
        assert(!loop_->hasChannel(this));
    }
}

void Channel::handleEvent() {
    eventHandling_ = true;
    LOG_DEBUG("Channel::handleEvent for fd {}, revents_ = {}", fd_, revents_);
    
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
    // 处理错误事件
    if ((revents_ & POLLHUP) && !(revents_ & POLLIN)) {
        if (closeCallback_) {
            closeCallback_();
        }
    }
    
    // 处理错误
    if (revents_ & (POLLERR | POLLNVAL)) {
        if (errorCallback_) {
            errorCallback_();
        }
    }
    
    // 处理可读事件
    if (revents_ & (POLLIN | POLLPRI | POLLRDHUP)) {
        if (readCallback_) {
            readCallback_();
        }
    }
    
    // 处理可写事件
    if (revents_ & POLLOUT) {
        if (writeCallback_) {
            writeCallback_();
        }
    }
}

void Channel::tie(const std::shared_ptr<void>& obj) {
    tie_ = obj;
    tied_ = true;
}

void Channel::update() {
    addedToLoop_ = true;
    loop_->updateChannel(this);
}

void Channel::remove() {
    assert(isNoneEvent());
    addedToLoop_ = false;
    loop_->removeChannel(this);
}