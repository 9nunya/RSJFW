#include "rsjfw/path_manager.hpp"
#include "rsjfw/logger.hpp"
#include <cstdlib>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <iostream>
#include <cstring>

namespace rsjfw {

PathManager& PathManager::instance() {
    static PathManager instance;
    return instance;
}

void PathManager::init(const std::string& rootOverride) {
    rootDir_ = resolveRoot(rootOverride);
    
    versionsDir_ = rootDir_ / "versions";
    prefixDir_ = rootDir_ / "prefix";
    logsDir_ = rootDir_ / "logs";
    downloadsDir_ = rootDir_ / "downloads";
    wineDir_ = rootDir_ / "wine";
    dxvkDir_ = rootDir_ / "dxvk";
    inboxDir_ = rootDir_ / "inbox";
    lockFilePath_ = rootDir_ / "rsjfw.lock";

    std::filesystem::create_directories(rootDir_);
    std::filesystem::create_directories(versionsDir_);
    std::filesystem::create_directories(prefixDir_);
    std::filesystem::create_directories(logsDir_);
    std::filesystem::create_directories(downloadsDir_);
    std::filesystem::create_directories(wineDir_);
    std::filesystem::create_directories(dxvkDir_);
    std::filesystem::create_directories(inboxDir_);

    // Legacy Migration: ~/.rsjfw -> XDG
    const char* home = std::getenv("HOME");
    if (home) {
        std::filesystem::path legacyRoot = std::filesystem::path(home) / ".rsjfw";
        if (std::filesystem::exists(legacyRoot) && !std::filesystem::equivalent(legacyRoot, rootDir_)) {
            LOG_INFO("Detected legacy root at " + legacyRoot.string() + ". Migrating contents...");
            for (const auto& entry : std::filesystem::directory_iterator(legacyRoot)) {
                std::filesystem::path target = rootDir_ / entry.path().filename();
                if (!std::filesystem::exists(target)) {
                    try {
                        std::filesystem::rename(entry.path(), target);
                        LOG_INFO("Migrated: " + entry.path().filename().string());
                    } catch (const std::exception& e) {
                        LOG_WARN("Failed to migrate " + entry.path().filename().string() + ": " + e.what());
                    }
                }
            }
            // Optional: remove legacyRoot if empty
            try {
                if (std::filesystem::is_empty(legacyRoot)) {
                    std::filesystem::remove(legacyRoot);
                }
            } catch (...) {}
        }
    }

    // Initial Log Migration
    std::filesystem::path oldLog = rootDir_ / "rsjfw.log";
    if (std::filesystem::exists(oldLog)) {
        std::filesystem::path oldLogsDir = logsDir_ / "old";
        std::filesystem::create_directories(oldLogsDir);
        
        auto now = std::chrono::system_clock::now();
        auto in_time_t = std::chrono::system_clock::to_time_t(now);
        std::stringstream ss;
        ss << std::put_time(std::localtime(&in_time_t), "%Y%m%d_%H%M%S");
        
        std::filesystem::path archivePath = oldLogsDir / ("legacy_" + ss.str() + ".log");
        try {
            std::filesystem::rename(oldLog, archivePath);
            std::cout << "[RSJFW] Migrated legacy log to " << archivePath << "\n";
        } catch (...) {
            // Ignore if rename fails (permissions etc)
        }
    }

    // Generate path for current session log
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << "rsjfw_" << std::put_time(std::localtime(&in_time_t), "%Y%m%d_%H%M%S") << ".log";
    currentLogPath_ = logsDir_ / ss.str();
}

std::filesystem::path PathManager::resolveRoot(const std::string& override) {
    if (!override.empty()) return std::filesystem::absolute(override);

    const char* envPath = std::getenv("RSJFW_PATH");
    if (envPath && strlen(envPath) > 0) return std::filesystem::absolute(envPath);

    // Fallback to XDG / Home
    const char* home = std::getenv("HOME");
    if (!home) home = "/tmp"; // Should never happen on Linux

    const char* xdgDataHome = std::getenv("XDG_DATA_HOME");
    if (xdgDataHome && strlen(xdgDataHome) > 0) {
        return std::filesystem::absolute(xdgDataHome) / "rsjfw";
    }

    return std::filesystem::path(home) / ".local" / "share" / "rsjfw";
}

std::filesystem::path PathManager::layerLib() const {
    // Check production paths only:
    // 1. System-wide install (/usr/lib) - from package manager
    std::filesystem::path systemPath = "/usr/lib/libVkLayer_RSJFW_RsjfwLayer.so";
    if (std::filesystem::exists(systemPath)) return systemPath;
    
    // 2. User data directory (for self-built layers)
    std::filesystem::path userPath = rootDir_ / "lib" / "libVkLayer_RSJFW_RsjfwLayer.so";
    if (std::filesystem::exists(userPath)) return userPath;
    
    // Return the preferred install location (user data dir) for build-from-source target
    return userPath;
}

std::filesystem::path PathManager::rsjfwExe() const {
    try {
        return std::filesystem::canonical("/proc/self/exe");
    } catch (...) {
        return "";
    }
}

bool PathManager::isLocalBuild() const {
    std::string exe = rsjfwExe().string();
    // Common indicators of a local development build
    return (exe.find("/Projects/") != std::string::npos || 
            exe.find("/build/") != std::string::npos || 
            exe.find("/cmake-build-") != std::string::npos ||
            exe.find("/tmp/") != std::string::npos);
}

} // namespace rsjfw
