#ifndef RSJFW_LOGGER_HPP
#define RSJFW_LOGGER_HPP

#include <string>
#include <fstream>
#include <iostream>
#include <filesystem>
#include <mutex>
#include <chrono>
#include <iomanip>

namespace rsjfw {

enum class LogLevel {
    DEBUG,
    INFO,
    WARNING,
    ERROR
};

class Logger {
public:
    static Logger& instance();

    void init(const std::filesystem::path& logPath, bool verbose);
    void log(LogLevel level, const std::string& message);

    // Forbidden
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

private:
    Logger() = default;
    ~Logger();

    std::ofstream logFile_;
    bool verbose_ = false;
    std::mutex mutex_;

    std::string getTimestamp();
    std::string getLevelString(LogLevel level);
};

// Convenience macros
#define LOG_DEBUG(msg) rsjfw::Logger::instance().log(rsjfw::LogLevel::DEBUG, msg)
#define LOG_INFO(msg) rsjfw::Logger::instance().log(rsjfw::LogLevel::INFO, msg)
#define LOG_WARN(msg) rsjfw::Logger::instance().log(rsjfw::LogLevel::WARNING, msg)
#define LOG_ERROR(msg) rsjfw::Logger::instance().log(rsjfw::LogLevel::ERROR, msg)

} // namespace rsjfw

#endif // RSJFW_LOGGER_HPP
