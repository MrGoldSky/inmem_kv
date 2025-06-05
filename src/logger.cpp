#include "kv/logger.hpp"

#include <ctime>
#include <iomanip>
#include <iostream>

namespace kv::log {

Logger& Logger::instance() {
    static Logger inst;
    return inst;
}

void Logger::init(const LoggerConfig& config) {
    std::lock_guard<std::mutex> lock(mtx_);
    cfg_ = config;
    if (cfg_.to_file && !cfg_.filename.empty()) {
        ofs_ = std::make_unique<std::ofstream>(cfg_.filename, std::ios::app);
        if (!ofs_->is_open()) {
            std::cerr << "Logger: cannot open file " << cfg_.filename << " for writing\n";
            std::exit(EXIT_FAILURE);
        }
    }
}

Logger::~Logger() {
    if (ofs_) {
        ofs_->close();
    }
}

std::string Logger::level_to_string(Level lvl) {
    switch (lvl) {
        case Level::TRACE:
            return "TRACE";
        case Level::DEBUG:
            return "DEBUG";
        case Level::INFO:
            return "INFO";
        case Level::WARN:
            return "WARN";
        case Level::ERROR:
            return "ERROR";
        case Level::FATAL:
            return "FATAL";
    }
    return "UNKNOWN";
}

void Logger::log(Level lvl, const std::string& msg, const char* file, int line) {
    if (lvl < cfg_.level) {
        return;
    }

    std::time_t t = std::time(nullptr);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");

    // Формат: [2025-06-05 14:23:11] [INFO] [server.cpp:132] Сообщение
    std::ostringstream full;
    full << "[" << oss.str() << "] "
         << "[" << level_to_string(lvl) << "] "
         << "[" << file << ":" << line << "] "
         << msg << "\n";

    std::lock_guard<std::mutex> lock(mtx_);

    if (cfg_.to_console) {
        if (lvl >= Level::WARN) {
            std::cerr << full.str();
        } else {
            std::cout << full.str();
        }
    }
    if (cfg_.to_file && ofs_) {
        (*ofs_) << full.str();
        ofs_->flush();
    }
    if (lvl == Level::FATAL) {
        std::exit(EXIT_FAILURE);
    }
}

void Logger::trace(const std::string& msg, const char* file, int line) {
    log(Level::TRACE, msg, file, line);
}
void Logger::debug(const std::string& msg, const char* file, int line) {
    log(Level::DEBUG, msg, file, line);
}
void Logger::info(const std::string& msg, const char* file, int line) {
    log(Level::INFO, msg, file, line);
}
void Logger::warn(const std::string& msg, const char* file, int line) {
    log(Level::WARN, msg, file, line);
}
void Logger::error(const std::string& msg, const char* file, int line) {
    log(Level::ERROR, msg, file, line);
}
void Logger::fatal(const std::string& msg, const char* file, int line) {
    log(Level::FATAL, msg, file, line);
}

}  // namespace kv::log
