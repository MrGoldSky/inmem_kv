#pragma once
#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

#include "config.hpp"

namespace kv {

/*
    ThreadPool — базовый пул потоков, который принимает задачи
    std::function<void()> и исполняет их в следующих свободных воркерах.
    После вызова shutdown() пул завершает все задачи и больше не принимает новые.
*/
class ThreadPool {
   public:
    explicit ThreadPool(std::size_t num_workers);
    ThreadPool() : ThreadPool(kv::config::THREAD_POOL_SIZE) {}

    ~ThreadPool();

    // Отправить задачу в пул. Можно вызывать из любой нити.
    // Если пул уже остановлен (shutdown() был вызван), задача игнорируется или бросает исключение.
    void submit(std::function<void()> task);

    void shutdown();

   private:
    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> tasks_;
    std::mutex queueMutex_;
    std::condition_variable condition_;
    std::atomic<bool> stop_;

    std::atomic<size_t> activeTasks_;

    void workerThread();
};

}  // namespace kv
