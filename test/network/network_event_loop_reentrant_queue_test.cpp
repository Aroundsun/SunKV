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
    std::atomic<int> step{0};
    std::atomic<int> second_observed{0};
    std::atomic<bool> done{false};

    std::thread loop_thread([&]() {
        auto* loop = new EventLoop();
        loop_ptr.store(loop, std::memory_order_release);

        // Main thread will enqueue the first task.
        loop->loop();

        delete loop;
    });

    if (!waitUntil([&]() { return loop_ptr.load(std::memory_order_acquire) != nullptr; },
                    std::chrono::seconds(2))) {
        std::cerr << "failed to start event loop" << std::endl;
        return 1;
    }

    EventLoop* loop = loop_ptr.load(std::memory_order_acquire);

    loop->queueInLoop([&]() {
        step.store(1, std::memory_order_release);
        // During doPendingTasks(), calling queueInLoop() will enqueue for next
        // doPendingTasks() round (not same batch), because tasks vector is already swapped.
        loop->queueInLoop([&]() {
            second_observed.store(step.load(std::memory_order_acquire), std::memory_order_release);
            step.store(2, std::memory_order_release);
            done.store(true, std::memory_order_release);
            loop->quit();
        });
    });

    if (!waitUntil([&]() { return done.load(std::memory_order_acquire); }, std::chrono::seconds(2))) {
        std::cerr << "reentrant queued task not executed" << std::endl;
        loop->quit();
        loop_thread.join();
        return 1;
    }

    loop_thread.join();

    if (step.load(std::memory_order_acquire) != 2) {
        std::cerr << "unexpected final step=" << step.load() << std::endl;
        return 1;
    }
    if (second_observed.load(std::memory_order_acquire) != 1) {
        std::cerr << "second task observed wrong order, second_observed="
                  << second_observed.load() << std::endl;
        return 1;
    }

    std::cout << "network event loop reentrant queue test passed." << std::endl;
    return 0;
}

