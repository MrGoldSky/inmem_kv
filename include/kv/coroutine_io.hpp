#pragma once

#include <coroutine>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "kv/logger.hpp"
#ifdef _WIN32
// Windows-специфичные заголовки
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "Ws2_32.lib")
using SOCKET_TYPE = SOCKET;  // SOCKET — typedef WinSock2
#else
// POSIX
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <unistd.h>
using SOCKET_TYPE = int;  // POSIX-дескриптор

#endif

namespace kv {

class EventLoop {
   public:
    EventLoop();
    ~EventLoop();

    void run();

    void add_reader(SOCKET_TYPE fd, std::coroutine_handle<> h);
    void add_writer(SOCKET_TYPE fd, std::coroutine_handle<> h);

    void remove(SOCKET_TYPE fd);

    static EventLoop& instance();

   private:
#ifdef _WIN32
    // Для Windows сделаем select-базированный loop
    std::mutex handlersMutex_;
    std::unordered_map<SOCKET_TYPE, std::coroutine_handle<>> readHandlers_;
    std::unordered_map<SOCKET_TYPE, std::coroutine_handle<>> writeHandlers_;

    void wait_and_handle_select();

#else
    // Для Linux: epoll
    int epollFd_;
    std::mutex handlersMutex_;

    struct Handler {
        std::coroutine_handle<> handle;
        uint32_t events;
    };
    std::unordered_map<int, Handler> handlers_;

    void wait_and_handle_epoll();
#endif
};

struct ReadAwaitable {
    SOCKET_TYPE fd_;
    char* buffer_;
    size_t size_;
    ssize_t bytesRead_;

    bool await_ready() const noexcept { return false; }
    void await_suspend(std::coroutine_handle<> h);
    ssize_t await_resume();
};

inline ReadAwaitable async_read(SOCKET_TYPE fd, char* buffer, size_t size) {
    return ReadAwaitable{fd, buffer, size, 0};
}

struct WriteAwaitable {
    SOCKET_TYPE fd_;
    const char* buffer_;
    size_t size_;
    ssize_t bytesWritten_;

    bool await_ready() const noexcept { return false; }
    void await_suspend(std::coroutine_handle<> h);
    ssize_t await_resume();
};

inline WriteAwaitable async_write(SOCKET_TYPE fd, const char* buffer, size_t size) {
    return WriteAwaitable{fd, buffer, size, 0};
}

}  // namespace kv
