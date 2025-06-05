#pragma once

#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace kv::log {

enum class Level {
    TRACE,
    DEBUG,
    INFO,
    WARN,
    ERROR,
    FATAL
};

struct LoggerConfig {
    Level level = Level::INFO;  // минимальный уровень, который выводится
    bool to_console = true;     // выводить в stdout/stderr
    bool to_file = false;       // выводить в файл
    std::string filename;       // имя файла для логирования
};

class Logger {
   public:
    static Logger& instance();

    void init(const LoggerConfig& config);

    void log(Level lvl, const std::string& msg, const char* file, int line);

    void trace(const std::string& msg, const char* file, int line);
    void debug(const std::string& msg, const char* file, int line);
    void info(const std::string& msg, const char* file, int line);
    void warn(const std::string& msg, const char* file, int line);
    void error(const std::string& msg, const char* file, int line);
    void fatal(const std::string& msg, const char* file, int line);

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

   private:
    Logger() = default;
    ~Logger();

    std::string level_to_string(Level lvl);

    LoggerConfig cfg_;
    std::mutex mtx_;
    std::unique_ptr<std::ofstream> ofs_;
};

}  // namespace kv::log

// Макросы для более удобного вызова
#define LOG_TRACE(msg) kv::log::Logger::instance().trace((msg), __FILE__, __LINE__)
#define LOG_DEBUG(msg) kv::log::Logger::instance().debug((msg), __FILE__, __LINE__)
#define LOG_INFO(msg) kv::log::Logger::instance().info((msg), __FILE__, __LINE__)
#define LOG_WARN(msg) kv::log::Logger::instance().warn((msg), __FILE__, __LINE__)
#define LOG_ERROR(msg) kv::log::Logger::instance().error((msg), __FILE__, __LINE__)
#define LOG_FATAL(msg) kv::log::Logger::instance().fatal((msg), __FILE__, __LINE__)
