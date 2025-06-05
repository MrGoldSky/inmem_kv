#include "kv/thread_pool.hpp"

namespace kv {

ThreadPool::ThreadPool(size_t numThreads)
    : stop_(false),
      activeTasks_(0) {
    if (numThreads == 0) {
        numThreads = std::thread::hardware_concurrency();
        if (numThreads == 0) numThreads = 1;
    }

    workers_.reserve(numThreads);
    for (size_t i = 0; i < numThreads; ++i) {
        workers_.emplace_back(&ThreadPool::workerThread, this);
    }
}

ThreadPool::~ThreadPool() {
    shutdown();
}

void ThreadPool::submit(std::function<void()> task) {
    if (stop_) {
        throw std::runtime_error("ThreadPool all ready stoped!");
    }
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        tasks_.push(std::move(task));
        activeTasks_.fetch_add(1, std::memory_order_relaxed);
    }
    condition_.notify_one();
}

void ThreadPool::shutdown() {
    bool expected = false;
    if (!stop_.compare_exchange_strong(expected, true)) {
        return;  // Уже был shutdown()
    }

    condition_.notify_all();
    for (auto& worker : workers_) {
        if (worker.joinable())
            worker.join();
    }
    workers_.clear();
}

void ThreadPool::workerThread() {
    while (true) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(queueMutex_);
            condition_.wait(lock, [this]() { return stop_ || !tasks_.empty(); });

            if (stop_ && tasks_.empty()) {
                return;
            }

            task = std::move(tasks_.front());
            tasks_.pop();
        }
        try {
            task();
        } catch (...) {
        }
        activeTasks_.fetch_sub(1, std::memory_order_relaxed);
    }
}

}  // namespace kv
