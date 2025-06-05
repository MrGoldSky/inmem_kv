#include <cstdlib>
#include <iostream>

#include "config.hpp"
#include "kv/logger.hpp"
#include "kv/server.hpp"
#include "kv/thread_pool.hpp"

int main(int argc, char* argv[]) {
    std::uint16_t port = kv::config::SERVER_PORT;
    if (argc >= 2) {
        try {
            int p = std::stoi(argv[1]);
            if (p > 0 && p < 65536)
                port = static_cast<std::uint16_t>(p);
        } catch (...) {
            std::cerr << "Неверный порт: " << argv[1] << ", будет использован порт по умолчанию "
                      << kv::config::SERVER_PORT << "\n";
        }
    }
    kv::ThreadPool pool(kv::config::THREAD_POOL_SIZE);

    kv::log::LoggerConfig cfg;
    cfg.level = kv::log::Level::DEBUG;  // выводим отладочные сообщения и выше
    cfg.to_console = true;              // логируем в консоль
    cfg.to_file = true;                 // и в файл
    cfg.filename = "kv_server.log";     // имя файла
    kv::log::Logger::instance().init(cfg);

    LOG_INFO("LaunchKV server on 0.0.0.0:5555");
    kv::Server<std::string, std::string> server("0.0.0.0", 5555);
    server.run();
    pool.shutdown();

    return EXIT_SUCCESS;
}
