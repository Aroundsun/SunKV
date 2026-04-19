#include "Poller.h"
#include "Channel.h"
#include "EventLoop.h"
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdexcept>

namespace {
// 将 Channel 事件转换为 epoll 事件
static uint32_t channelEventsToEpoll(int ch_events) {
    uint32_t ev = 0;
    if (ch_events & Channel::kReadEventStatic) {
        ev |= (EPOLLIN | EPOLLPRI | EPOLLRDHUP);
    }
    if (ch_events & Channel::kWriteEventStatic) {
        ev |= EPOLLOUT;
    }
    // EPOLLERR/EPOLLHUP 一般会被内核自动上报；这里不强制订阅。
    return ev;
}

// 将 epoll 事件转换为 Channel 事件
static int epollReventsToChannel(uint32_t ep) {
    int rev = Channel::kNoneEventStatic;
    if (ep & (EPOLLERR)) {
        rev |= Channel::kErrorEventStatic;
    }
    if (ep & (EPOLLIN | EPOLLPRI)) {
        rev |= Channel::kReadEventStatic;
    }
    if (ep & (EPOLLOUT)) {
        rev |= Channel::kWriteEventStatic;
    }
    if (ep & (EPOLLHUP | EPOLLRDHUP)) {
        rev |= Channel::kCloseEventStatic;
    }
    return rev;
}

} // namespace

// epoll 操作类型
static const int kNew = -1; // 新的 Channel，未添加到 epoll
static const int kAdded = 1; // 已添加的 Channel，已添加到 epoll
static const int kDeleted = 2; // 已删除的 Channel，已从 epoll 中删除

Poller::Poller(EventLoop* loop) 
    : ownerLoop_(loop),
      epollFd_(::epoll_create1(EPOLL_CLOEXEC)),
      events_(kInitEventListSize) {
    
    if (epollFd_ < 0) {
        LOG_ERROR("创建 epoll 失败: {}", strerror(errno));
        throw std::runtime_error("create epoll failed");
    }
    
    LOG_DEBUG("Poller 已创建，epollFd={}", epollFd_);
}

Poller::~Poller() {
    ::close(epollFd_);
    LOG_DEBUG("Poller 已销毁");
}

int Poller::poll(int timeoutMs, ChannelList* activeChannels) {
    // Debug 下该日志非常高频：按固定频率采样，避免日志 I/O 扭曲性能
    ++poll_calls_;
    const uint64_t sample_every = 1000;
    if ((poll_calls_ % sample_every) == 1) {
        LOG_DEBUG("Poller::poll() 等待事件，timeout={}ms (sampled: 1/{} polls)", timeoutMs, sample_every);
    }
    
    int numEvents = ::epoll_wait(epollFd_, 
                                events_.data(), 
                                static_cast<int>(events_.size()),
                                timeoutMs);
    
    int savedErrno = errno;
    
    if (numEvents > 0) {
        LOG_DEBUG("发生 {} 个事件", numEvents);
        fillActiveChannels(numEvents, activeChannels);
        
        // 如果事件数组满了，扩展数组
        if (static_cast<size_t>(numEvents) == events_.size()) {
            events_.resize(events_.size() * 2);
        }
    } else if (numEvents == 0) {
        if ((poll_calls_ % sample_every) == 1) {
            LOG_DEBUG("Poller::poll() 超时，没有事件 (sampled)");
        }
    } else {
        // 不是被信号中断的错误
        if (savedErrno != EINTR) {
            errno = savedErrno;
            LOG_ERROR("Poller::poll() 出错: {}", strerror(errno));
        }
    }
    
    return numEvents;
}

void Poller::updateChannel(Channel* channel) {
    assertInLoopThread();
    LOG_DEBUG("Poller::updateChannel fd={}, events={}", channel->fd(), channel->events());
    
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
        assert(channels_.find(channel->fd()) != channels_.end());
        assert(channels_[channel->fd()] == channel);
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
    LOG_DEBUG("Poller::removeChannel fd={}", channel->fd());
    
    int fd = channel->fd();
    assert(channels_.find(fd) != channels_.end());
    assert(channels_[fd] == channel);
    assert(channel->isNoneEvent());
    
    int index = channel->index();
    assert(index == kAdded || index == kDeleted);
    
    channels_.erase(fd);
    assert(channels_.find(fd) == channels_.end());
    
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
        channel->setRevents(epollReventsToChannel(events_[i].events));
        activeChannels->push_back(channel);
    }
}

void Poller::update(int operation, Channel* channel) {
    struct epoll_event event;
    memset(&event, 0, sizeof(event));
    event.events = channelEventsToEpoll(channel->events());
    event.data.ptr = channel;
    int fd = channel->fd();
    
    const uint32_t ep_events = event.events;
    LOG_DEBUG("epoll_ctl op={}, fd={}, ch_events={}, ep_events={}", operation, fd, channel->events(), ep_events);
    
    if (::epoll_ctl(epollFd_, operation, fd, &event) < 0) {
        LOG_ERROR("epoll_ctl op={} 失败，fd={}: {}", operation, fd, strerror(errno));
    }
}

void Poller::assertInLoopThread() const {
    ownerLoop_->assertInLoopThread();
}

const int Poller::kInitEventListSize = 16;