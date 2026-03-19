#include "Poller.h"
#include "Channel.h"
#include "EventLoop.h"
#include <unistd.h>
#include <errno.h>
#include <string.h>

// epoll 操作类型
static const int kNew = -1;
static const int kAdded = 1;
static const int kDeleted = 2;

Poller::Poller(EventLoop* loop) 
    : ownerLoop_(loop),
      epollFd_(::epoll_create1(EPOLL_CLOEXEC)),
      events_(kInitEventListSize) {
    
    if (epollFd_ < 0) {
        LOG_ERROR("Failed to create epoll: {}", strerror(errno));
        exit(1);
    }
    
    LOG_DEBUG("Poller created with epollFd {}", epollFd_);
}

Poller::~Poller() {
    ::close(epollFd_);
    LOG_DEBUG("Poller destroyed");
}

int Poller::poll(int timeoutMs, ChannelList* activeChannels) {
    LOG_DEBUG("Poller::poll() waiting for events, timeout = {}ms", timeoutMs);
    
    int numEvents = ::epoll_wait(epollFd_, 
                                events_.data(), 
                                static_cast<int>(events_.size()),
                                timeoutMs);
    
    int savedErrno = errno;
    
    if (numEvents > 0) {
        LOG_DEBUG("{} events happened", numEvents);
        fillActiveChannels(numEvents, activeChannels);
        
        // 如果事件数组满了，扩展数组
        if (static_cast<size_t>(numEvents) == events_.size()) {
            events_.resize(events_.size() * 2);
        }
    } else if (numEvents == 0) {
        LOG_DEBUG("Poller::poll() timeout, nothing happened");
    } else {
        // 不是被信号中断的错误
        if (savedErrno != EINTR) {
            errno = savedErrno;
            LOG_ERROR("Poller::poll() error: {}", strerror(errno));
        }
    }
    
    return numEvents;
}

void Poller::updateChannel(Channel* channel) {
    assertInLoopThread();
    LOG_DEBUG("Poller::updateChannel fd = {}, events = {}", channel->fd(), channel->events());
    
    const int index = channel->index();
    
    if (index == kNew || index == kDeleted) {
        // 新的 Channel 或重新添加的 Channel
        int fd = channel->fd();
        if (index == kNew) {
            assert(channels_.find(fd) == channels_.end());
            channels_[fd] = channel;
        } else {
            assert(channels_.find(fd) != channels_.end());
            assert(channels_[fd] == channel);
        }
        
        channel->set_index(kAdded);
        update(EPOLL_CTL_ADD, channel);
    } else {
        // 已存在的 Channel
        int fd = channel->fd();
        assert(channels_.find(fd) != channels_.end());
        assert(channels_[fd] == channel);
        assert(index == kAdded);
        
        if (channel->isNoneEvent()) {
            update(EPOLL_CTL_DEL, channel);
            channel->set_index(kDeleted);
        } else {
            update(EPOLL_CTL_MOD, channel);
        }
    }
}

void Poller::removeChannel(Channel* channel) {
    assertInLoopThread();
    LOG_DEBUG("Poller::removeChannel fd = {}", channel->fd());
    
    int fd = channel->fd();
    assert(channels_.find(fd) != channels_.end());
    assert(channels_[fd] == channel);
    assert(channel->isNoneEvent());
    
    int index = channel->index();
    assert(index == kAdded || index == kDeleted);
    
    size_t n = channels_.erase(fd);
    assert(n == 1);
    
    if (index == kAdded) {
        update(EPOLL_CTL_DEL, channel);
    }
    
    channel->set_index(kNew);
}

bool Poller::hasChannel(Channel* channel) const {
    assertInLoopThread();
    ChannelMap::const_iterator it = channels_.find(channel->fd());
    return it != channels_.end() && it->second == channel;
}

void Poller::fillActiveChannels(int numEvents, ChannelList* activeChannels) const {
    assert(static_cast<size_t>(numEvents) <= events_.size());
    
    for (int i = 0; i < numEvents; ++i) {
        Channel* channel = static_cast<Channel*>(events_[i].data.ptr);
        channel->setRevents(events_[i].events);
        activeChannels->push_back(channel);
    }
}

void Poller::update(int operation, Channel* channel) {
    struct epoll_event event;
    memset(&event, 0, sizeof(event));
    event.events = channel->events();
    event.data.ptr = channel;
    int fd = channel->fd();
    
    LOG_DEBUG("epoll_ctl op = {}, fd = {}, events = {}", operation, fd, channel->events());
    
    if (::epoll_ctl(epollFd_, operation, fd, &event) < 0) {
        LOG_ERROR("epoll_ctl op = {} failed, fd = {}: {}", operation, fd, strerror(errno));
    }
}

void Poller::assertInLoopThread() const {
    ownerLoop_->assertInLoopThread();
}

const int Poller::kInitEventListSize = 16;