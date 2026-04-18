#include <atomic>
#include <chrono>
#include <functional>
#include <iostream>
#include <thread>

#include "network/EventLoop.h"

namespace {

bool waitUntil(const std::function<bool()>& pred, std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (pred()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return pred();
}

} // namespace

int main() {
    std::atomic<EventLoop*> loop_ptr{nullptr};
    std::atomic<bool> executed{false};

    std::thread loop_thread([&]() {
        auto* loop = new EventLoop();
        loop_ptr.store(loop, std::memory_order_release);
        loop->loop();
        delete loop;
    });

    if (!waitUntil([&]() { return loop_ptr.load(std::memory_order_acquire) != nullptr; },
                    std::chrono::seconds(2))) {
        std::cerr << "failed to start event loop" << std::endl;
        loop_ptr.load()->quit();
        loop_thread.join();
        return 1;
    }

    EventLoop* loop = loop_ptr.load(std::memory_order_acquire);

    const auto start = std::chrono::steady_clock::now();
    // queueInLoop should wake the loop (eventfd) so that the task runs quickly,
    // even if loop thread is currently blocked in poll().
    loop->queueInLoop([&]() {
        executed.store(true, std::memory_order_release);
        auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
        // If wakeup path is broken, this callback will typically run after ~1s poll timeout.
        if (elapsedMs >= 990) {
            std::cerr << "queueInLoop too slow: elapsedMs=" << elapsedMs << std::endl;
            std::abort();
        }
        loop->quit();
    });

    if (!waitUntil([&]() { return executed.load(std::memory_order_acquire); },
                    std::chrono::seconds(2))) {
        std::cerr << "queued task not executed in time" << std::endl;
        loop->quit();
        loop_thread.join();
        return 1;
    }

    loop_thread.join();
    std::cout << "network event loop queue_in_loop test passed." << std::endl;
    return 0;
}

