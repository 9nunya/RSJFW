#include "rsjfw/logger.hpp"

namespace rsjfw {

Logger& Logger::instance() {
    static Logger instance;
    return instance;
}

void Logger::init(const std::filesystem::path& logPath, bool verbose) {
    std::lock_guard<std::mutex> lock(mutex_);
    verbose_ = verbose;

    // Create directory if needed
    if (logPath.has_parent_path()) {
        std::filesystem::create_directories(logPath.parent_path());
    }

    logFile_.open(logPath, std::ios::out | std::ios::app);
    if (!logFile_.is_open()) {
        std::cerr << "[ERROR] Failed to open log file: " << logPath << std::endl;
    } else {
        logFile_ << "\n=== RSJFW Session Started: " << getTimestamp() << " ===\n";
    }
}

Logger::~Logger() {
    if (logFile_.is_open()) {
        logFile_.close();
    }
}

void Logger::log(LogLevel level, const std::string& message) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::string timestamp = getTimestamp();
    std::string levelStr = getLevelString(level);
    std::string formattedMsg = "[" + timestamp + "] [" + levelStr + "] " + message;

    // Write to file
    if (logFile_.is_open()) {
        logFile_ << formattedMsg << std::endl;
    }

    // Write to stdout/stderr if verbose or if it's an error/warning
    if (verbose_ || level == LogLevel::WARNING || level == LogLevel::ERROR) {
        if (level == LogLevel::ERROR) {
            std::cerr << formattedMsg << std::endl;
        } else {
            std::cout << formattedMsg << std::endl;
        }
    }
}

std::string Logger::getTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << std::put_time(std::localtime(&in_time_t), "%Y-%m-%d %H:%M:%S");
    return ss.str();
}

std::string Logger::getLevelString(LogLevel level) {
    switch (level) {
        case LogLevel::DEBUG:   return "DEBUG";
        case LogLevel::INFO:    return "INFO";
        case LogLevel::WARNING: return "WARN";
        case LogLevel::ERROR:   return "ERROR";
        default:                return "UNKNOWN";
    }
}

} // namespace rsjfw
