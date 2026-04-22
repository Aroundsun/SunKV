#pragma once

#include <vector>
#include <map>
#include <sys/epoll.h>


class Channel;
class EventLoop;

// Poller 类封装了 epoll 操作
class Poller {
public:
    using ChannelList = std::vector<Channel*>; //  Channel 列表
    
    explicit Poller(EventLoop* loop);
    ~Poller();
    
    // 禁止拷贝和赋值
    Poller(const Poller&) = delete;
    Poller& operator=(const Poller&) = delete;
    
    // 等待事件发生，返回活跃的 Channel 列表
    int poll(int timeoutMs, ChannelList* activeChannels);
    
    // 更新 Channel（添加或修改）
    void updateChannel(Channel* channel);
    
    // 移除 Channel
    void removeChannel(Channel* channel);
    
    // 检查是否包含某个 Channel
    bool hasChannel(Channel* channel) const;

private:
    // 填充活跃的 Channel 列表
    void fillActiveChannels(int numEvents, ChannelList* activeChannels) const;
    
    // 更新 epoll 中的事件
    void update(int operation, Channel* channel);
    
    // 断言在事件循环线程中
    void assertInLoopThread() const;
    
    EventLoop* ownerLoop_;
    int epollFd_;
    std::vector<struct epoll_event> events_;
    
    // fd 到 Channel 的映射
    using ChannelMap = std::map<int, Channel*>;
    ChannelMap channels_;

    // 采样用计数器：降低 Debug 下高频 poll 日志量
    uint64_t poll_calls_{0};
    
    // 初始事件列表大小
    static const int kInitEventListSize;
};