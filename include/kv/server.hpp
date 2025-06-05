#pragma once

#include <iostream>
#include <string>
#include <thread>

#include "config.hpp"
#include "kv/coroutine_io.hpp"
#include "kv/logger.hpp"
#include "kv/sharded_hash_map.hpp"

namespace kv {

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
using SOCKET_TYPE = SOCKET;
#else
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
using SOCKET_TYPE = int;
#endif

struct Task {
    struct promise_type {
        Task get_return_object() noexcept { return {}; }
        std::suspend_never initial_suspend() noexcept { return {}; }
        std::suspend_never final_suspend() noexcept { return {}; }
        void return_void() noexcept {}
        void unhandled_exception() noexcept { LOG_ERROR("Необработанное исключение в корутине"); }
    };
};

template <typename Key, typename Value,
          typename Hash = std::hash<Key>,
          typename KeyEqual = std::equal_to<Key>>
class Server {
   public:
    Server(const std::string& address, uint16_t port);
    ~Server();

    void run();

   private:
    std::string address_;
    uint16_t port_;
    SOCKET_TYPE listenFd_;

    void setup_listening_socket();

    void accept_loop();

    Task handle_connection(SOCKET_TYPE clientFd);

    ShardedHashMap<Key, Value, Hash, KeyEqual> shardedMap_;
};

template <typename Key, typename Value, typename Hash, typename KeyEqual>
Server<Key, Value, Hash, KeyEqual>::Server(const std::string& address, uint16_t port)
    : address_(address),
      port_(port),
      listenFd_(-1),
      shardedMap_(kv::config::HASH_MAP_SHARDS) {}

template <typename Key, typename Value, typename Hash, typename KeyEqual>
Server<Key, Value, Hash, KeyEqual>::~Server() {
    if (listenFd_ != -1) {
#ifdef _WIN32
        closesocket(listenFd_);
#else
        close(listenFd_);
#endif
    }
}

// Настройка слушающего сокета
template <typename Key, typename Value, typename Hash, typename KeyEqual>
void Server<Key, Value, Hash, KeyEqual>::setup_listening_socket() {
#ifdef _WIN32
    WSADATA wsaData;
    int startupRes = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (startupRes != 0 && startupRes != WSASYSNOTREADY) {
        LOG_FATAL(std::string("WSAStartup failed: ") + std::to_string(startupRes));
    }

    // Создаём сокет
    listenFd_ = WSASocket(AF_INET, SOCK_STREAM, 0, nullptr, 0, WSA_FLAG_OVERLAPPED);
    if (listenFd_ == INVALID_SOCKET) {
        LOG_FATAL(std::string("WSASocket failed: ") + std::to_string(WSAGetLastError()));
    }
    // Разрешаем переиспользование адреса
    int opt = 1;
    setsockopt(listenFd_, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt));

    // Привязываем к адресу
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    inet_pton(AF_INET, address_.c_str(), &addr.sin_addr);
    addr.sin_port = htons(port_);

    if (bind(listenFd_, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        LOG_FATAL(std::string("bind failed: ") + std::to_string(WSAGetLastError()));
    }

    if (listen(listenFd_, static_cast<int>(kv::config::MAX_CONNECTIONS)) == SOCKET_ERROR) {
        LOG_FATAL(std::string("listen failed: ") + std::to_string(WSAGetLastError()));
    }

    // Устанавливаем неблокирующий режим
    u_long mode = 1;
    ioctlsocket(listenFd_, FIONBIO, &mode);

#else
    listenFd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listenFd_ < 0) {
        LOG_ERROR(std::string("socket() failed: ") + std::strerror(errno));
        std::exit(EXIT_FAILURE);
    }

    int opt = 1;
    setsockopt(listenFd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    inet_pton(AF_INET, address_.c_str(), &addr.sin_addr);
    addr.sin_port = htons(port_);

    if (::bind(listenFd_, (sockaddr*)&addr, sizeof(addr)) < 0) {
        LOG_ERROR(std::string("bind() failed: ") + std::strerror(errno));
        std::exit(EXIT_FAILURE);
    }
    if (::listen(listenFd_, static_cast<int>(kv::config::MAX_CONNECTIONS)) < 0) {
        LOG_ERROR(std::string("listen() failed: ") + std::strerror(errno));
        std::exit(EXIT_FAILURE);
    }

    // Устанавливаем неблокирующий режим
    int flags = fcntl(listenFd_, F_GETFL, 0);
    fcntl(listenFd_, F_SETFL, flags | O_NONBLOCK);
#endif

    if constexpr (kv::config::ENABLE_DEBUG_LOG) {
        LOG_INFO("The server is listening on " + address_ + ":" + std::to_string(port_));
        LOG_INFO("  -> Thread pool size: " + std::to_string(kv::config::THREAD_POOL_SIZE));
        LOG_INFO("  -> Shards in HashMap: " + std::to_string(kv::config::HASH_MAP_SHARDS));
        LOG_INFO("  -> Max connections (backlog): " + std::to_string(kv::config::MAX_CONNECTIONS));
    }
}

template <typename Key, typename Value, typename Hash, typename KeyEqual>
void Server<Key, Value, Hash, KeyEqual>::run() {
    setup_listening_socket();
    EventLoop::instance().run();
}

// Цикл принятия новых подключений
template <typename Key, typename Value, typename Hash, typename KeyEqual>
void Server<Key, Value, Hash, KeyEqual>::accept_loop() {
    while (true) {
#ifdef _WIN32
        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(listenFd_, &readSet);

        int ret = select(static_cast<int>(listenFd_ + 1), &readSet, nullptr, nullptr, nullptr);
        if (ret == SOCKET_ERROR) {
            int err = WSAGetLastError();
            LOG_ERROR("select(listen) failed: " + std::to_string(err));
            continue;
        }
        if (FD_ISSET(listenFd_, &readSet)) {
            SOCKET_TYPE clientFd = accept(listenFd_, nullptr, nullptr);
            if (clientFd == INVALID_SOCKET) {
                int err = WSAGetLastError();
                if (err == WSAEWOULDBLOCK) {
                    continue;
                }
                LOG_ERROR("accept failed: " + std::to_string(err));
                continue;
            }
            u_long mode = 1;
            ioctlsocket(clientFd, FIONBIO, &mode);

            handle_connection(clientFd);
        }
#else
        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(listenFd_, &readSet);
        int ret = select(listenFd_ + 1, &readSet, nullptr, nullptr, nullptr);
        if (ret < 0) {
            if (errno == EINTR) continue;
            LOG_ERROR(std::string("select(listen) failed: ") + std::strerror(errno));
            continue;
        }
        if (FD_ISSET(listenFd_, &readSet)) {
            int clientFd = ::accept(listenFd_, nullptr, nullptr);
            if (clientFd < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    continue;
                }
                LOG_ERROR(std::string("accept() failed: ") + std::strerror(errno));
                continue;
            }
            int flags = fcntl(clientFd, F_GETFL, 0);
            fcntl(clientFd, F_SETFL, flags | O_NONBLOCK);

            handle_connection(clientFd);
        }
#endif
    }
}

template <typename Key, typename Value, typename Hash, typename KeyEqual>
Task Server<Key, Value, Hash, KeyEqual>::handle_connection(SOCKET_TYPE clientFd) {
    char buffer[4096];

    while (true) {
        // Ждём данные для чтения, при этом корутина автоматически управляет неблокирующим I/O
        ssize_t n = co_await async_read(clientFd, buffer, sizeof(buffer));
        if (n <= 0) {
            if constexpr (kv::config::ENABLE_DEBUG_LOG) {
                LOG_INFO("Connection closed or read error, fd=" + std::to_string(clientFd));
            }
            break;
        }

        // Преобразуем в std::string и убираем '\r' и '\n'
        std::string req(buffer, buffer + n);
        while (!req.empty() && (req.back() == '\r' || req.back() == '\n')) {
            req.pop_back();
        }

        if (req.rfind("GET ", 0) == 0) {
            std::string key = req.substr(4);
            auto opt = shardedMap_.get(key);
            std::string resp = opt.has_value() ? opt.value() + "\n" : "NOT_FOUND\n";
            co_await async_write(clientFd, resp.c_str(), resp.size());

        } else if (req.rfind("SET ", 0) == 0) {
            size_t pos = req.find(' ', 4);
            if (pos == std::string::npos) {
                std::string resp = "ERROR\n";
                co_await async_write(clientFd, resp.c_str(), resp.size());
            } else {
                std::string key = req.substr(4, pos - 4);
                std::string val = req.substr(pos + 1);
                if (key.size() > kv::config::MAX_KEY_SIZE || val.size() > kv::config::MAX_VALUE_SIZE) {
                    std::string resp = "ERROR_TOO_LARGE\n";
                    co_await async_write(clientFd, resp.c_str(), resp.size());
                } else {
                    shardedMap_.put(key, val);
                    std::string resp = "STORED\n";
                    co_await async_write(clientFd, resp.c_str(), resp.size());
                }
            }

        } else if (req.rfind("DEL ", 0) == 0) {
            std::string key = req.substr(4);
            bool erased = shardedMap_.erase(key);
            std::string resp = erased ? "DELETED\n" : "NOT_FOUND\n";
            co_await async_write(clientFd, resp.c_str(), resp.size());

        } else {
            std::string resp = "ERROR\n";
            co_await async_write(clientFd, resp.c_str(), resp.size());
        }
    }

#ifdef _WIN32
    closesocket(clientFd);
#else
    close(clientFd);
#endif
    co_return;
}

}  // namespace kv
