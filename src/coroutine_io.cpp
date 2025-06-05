#include "kv/coroutine_io.hpp"

#include "kv/logger.hpp"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/epoll.h>
#include <unistd.h>
#endif

#include <mutex>
#include <vector>

namespace kv {

#ifdef _WIN32

EventLoop::EventLoop() {
    WSADATA wsaData;
    int startupRes = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (startupRes != 0 && startupRes != WSASYSNOTREADY && startupRes != WSAEALREADY) {
        LOG_FATAL(std::string("WSAStartup failed: ") + std::to_string(startupRes));
    }
}

EventLoop::~EventLoop() {
    int cleanupRes = WSACleanup();
    if (cleanupRes != 0) {
        LOG_WARN(std::string("WSACleanup returned error: ") + std::to_string(WSAGetLastError()));
    }
}

EventLoop& EventLoop::instance() {
    static EventLoop loop;
    return loop;
}

void EventLoop::add_reader(SOCKET_TYPE fd, std::coroutine_handle<> h) {
    std::lock_guard<std::mutex> lock(handlersMutex_);
    readHandlers_[fd] = h;
}

void EventLoop::add_writer(SOCKET_TYPE fd, std::coroutine_handle<> h) {
    std::lock_guard<std::mutex> lock(handlersMutex_);
    writeHandlers_[fd] = h;
}

void EventLoop::remove(SOCKET_TYPE fd) {
    std::lock_guard<std::mutex> lock(handlersMutex_);
    readHandlers_.erase(fd);
    writeHandlers_.erase(fd);
    closesocket(fd);
    LOG_DEBUG(std::string("Closesocket fd=") + std::to_string(fd));
}

void EventLoop::run() {
    wait_and_handle_select();
}

void EventLoop::wait_and_handle_select() {
    while (true) {
        fd_set readSet, writeSet;
        FD_ZERO(&readSet);
        FD_ZERO(&writeSet);

        SOCKET maxFd = 0;
        {
            if (readHandlers_.empty() && writeHandlers_.empty()) {
                Sleep(1);
                continue;
            }

            std::lock_guard<std::mutex> lock(handlersMutex_);
            for (auto& [fd, handle] : readHandlers_) {
                FD_SET(fd, &readSet);
                if (fd > maxFd) maxFd = fd;
            }
            for (auto& [fd, handle] : writeHandlers_) {
                FD_SET(fd, &writeSet);
                if (fd > maxFd) maxFd = fd;
            }
        }

        // Блокируем до тех пор, пока какой-нибудь сокет не станет готов
        int readyCount = select(0, &readSet, &writeSet, nullptr, nullptr);
        if (readyCount == SOCKET_ERROR) {
            int err = WSAGetLastError();
            LOG_ERROR(std::string("select() failed: ") + std::to_string(err));
            continue;
        }

        std::vector<std::pair<SOCKET_TYPE, bool /*isRead*/>> readyList;
        {
            std::lock_guard<std::mutex> lock(handlersMutex_);
            for (auto& [fd, handle] : readHandlers_) {
                if (FD_ISSET(fd, &readSet)) {
                    readyList.emplace_back(fd, true);
                }
            }
            for (auto& [fd, handle] : writeHandlers_) {
                if (FD_ISSET(fd, &writeSet)) {
                    readyList.emplace_back(fd, false);
                }
            }
        }

        // Вызываем resume() для готовых корутин
        for (auto& [fd, isRead] : readyList) {
            std::coroutine_handle<> handle;
            {
                std::lock_guard<std::mutex> lock(handlersMutex_);
                if (isRead) {
                    handle = readHandlers_[fd];
                    readHandlers_.erase(fd);
                } else {
                    handle = writeHandlers_[fd];
                    writeHandlers_.erase(fd);
                }
            }
            if (handle) {
                LOG_TRACE(std::string("Resuming handle for fd=") + std::to_string(fd) +
                          (isRead ? " (read)" : " (write)"));
                handle.resume();
            }
        }
    }
}

void ReadAwaitable::await_suspend(std::coroutine_handle<> h) {
    EventLoop::instance().add_reader(fd_, h);
}

ssize_t ReadAwaitable::await_resume() {
    int n = recv(fd_, buffer_, static_cast<int>(size_), 0);
    if (n < 0) {
        int err = WSAGetLastError();
        if (err != WSAEWOULDBLOCK) {
            LOG_WARN(std::string("recv() returned error on fd=") +
                     std::to_string(fd_) + ": " + std::to_string(err));
        }
        bytesRead_ = -1;
    } else {
        bytesRead_ = n;
    }
    return bytesRead_;
}

void WriteAwaitable::await_suspend(std::coroutine_handle<> h) {
    EventLoop::instance().add_writer(fd_, h);
}

ssize_t WriteAwaitable::await_resume() {
    int n = send(fd_, buffer_, static_cast<int>(size_), 0);
    if (n < 0) {
        int err = WSAGetLastError();
        if (err != WSAEWOULDBLOCK) {
            LOG_WARN(std::string("send() returned error on fd=") +
                     std::to_string(fd_) + ": " + std::to_string(err));
        }
        bytesWritten_ = -1;
    } else {
        bytesWritten_ = n;
    }
    return bytesWritten_;
}

#else

EventLoop::EventLoop() {
    epollFd_ = epoll_create1(0);
    if (epollFd_ < 0) {
        LOG_FATAL(std::string("epoll_create1() failed: ") + std::strerror(errno));
    }
}

EventLoop::~EventLoop() {
    close(epollFd_);
    LOG_DEBUG("Closed epollFd_");
}

EventLoop& EventLoop::instance() {
    static EventLoop loop;
    return loop;
}

void EventLoop::add_reader(int fd, std::coroutine_handle<> h) {
    // переводим в non-blocking
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        LOG_ERROR(std::string("fcntl(F_GETFL) failed on fd=") +
                  std::to_string(fd) + ": " + std::strerror(errno));
        return;
    }
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        LOG_ERROR(std::string("fcntl(F_SETFL,O_NONBLOCK) failed on fd=") +
                  std::to_string(fd) + ": " + std::strerror(errno));
        return;
    }

    epoll_event ev{};
    ev.events = EPOLLIN | EPOLLET;
    ev.data.fd = fd;
    if (epoll_ctl(epollFd_, EPOLL_CTL_ADD, fd, &ev) < 0) {
        LOG_ERROR(std::string("epoll_ctl(ADD,EPOLLIN) failed on fd=") +
                  std::to_string(fd) + ": " + std::strerror(errno));
    }

    std::lock_guard<std::mutex> lock(handlersMutex_);
    handlers_[fd] = Handler{h, EPOLLIN};
    LOG_TRACE(std::string("Registered fd=") + std::to_string(fd) + " for EPOLLIN");
}

void EventLoop::add_writer(int fd, std::coroutine_handle<> h) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        LOG_ERROR(std::string("fcntl(F_GETFL) failed on fd=") +
                  std::to_string(fd) + ": " + std::strerror(errno));
        return;
    }
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        LOG_ERROR(std::string("fcntl(F_SETFL,O_NONBLOCK) failed on fd=") +
                  std::to_string(fd) + ": " + std::strerror(errno));
        return;
    }

    epoll_event ev{};
    ev.events = EPOLLOUT | EPOLLET;
    ev.data.fd = fd;
    if (epoll_ctl(epollFd_, EPOLL_CTL_ADD, fd, &ev) < 0) {
        LOG_ERROR(std::string("epoll_ctl(ADD,EPOLLOUT) failed on fd=") +
                  std::to_string(fd) + ": " + std::strerror(errno));
    }

    std::lock_guard<std::mutex> lock(handlersMutex_);
    handlers_[fd] = Handler{h, EPOLLOUT};
    LOG_TRACE(std::string("Registered fd=") + std::to_string(fd) + " for EPOLLOUT");
}

void EventLoop::remove(int fd) {
    if (epoll_ctl(epollFd_, EPOLL_CTL_DEL, fd, nullptr) < 0) {
        LOG_WARN(std::string("epoll_ctl(DEL) failed on fd=") +
                 std::to_string(fd) + ": " + std::strerror(errno));
    }
    {
        std::lock_guard<std::mutex> lock(handlersMutex_);
        handlers_.erase(fd);
    }
    if (close(fd) < 0) {
        LOG_WARN(std::string("close(fd) failed on fd=") +
                 std::to_string(fd) + ": " + std::strerror(errno));
    } else {
        LOG_DEBUG(std::string("Closed fd=") + std::to_string(fd));
    }
}

void EventLoop::run() {
    wait_and_handle_epoll();
}

void EventLoop::wait_and_handle_epoll() {
    const int MAX_EVENTS = 64;
    std::vector<epoll_event> events(MAX_EVENTS);

    while (true) {
        int n = epoll_wait(epollFd_, events.data(), MAX_EVENTS, -1);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            LOG_ERROR(std::string("epoll_wait() failed: ") + std::strerror(errno));
            continue;
        }

        for (int i = 0; i < n; ++i) {
            int fd = events[i].data.fd;
            std::coroutine_handle<> handle;
            {
                std::lock_guard<std::mutex> lock(handlersMutex_);
                auto it = handlers_.find(fd);
                if (it != handlers_.end()) {
                    handle = it->second.handle;
                    handlers_.erase(it);
                }
            }
            if (handle) {
                LOG_TRACE(std::string("Resuming handle for fd=") + std::to_string(fd));
                handle.resume();
            }
        }
    }
}

void ReadAwaitable::await_suspend(std::coroutine_handle<> h) {
    EventLoop::instance().add_reader(fd_, h);
}

ssize_t ReadAwaitable::await_resume() {
    bytesRead_ = ::read(fd_, buffer_, size_);
    if (bytesRead_ < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            LOG_WARN(std::string("read() returned error on fd=") +
                     std::to_string(fd_) + ": " + std::strerror(errno));
        }
    }
    return bytesRead_;
}

void WriteAwaitable::await_suspend(std::coroutine_handle<> h) {
    EventLoop::instance().add_writer(fd_, h);
}

ssize_t WriteAwaitable::await_resume() {
    bytesWritten_ = ::write(fd_, buffer_, size_);
    if (bytesWritten_ < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            LOG_WARN(std::string("write() returned error on fd=") +
                     std::to_string(fd_) + ": " + std::strerror(errno));
        }
    }
    return bytesWritten_;
}

#endif  // _WIN32

}  // namespace kv
