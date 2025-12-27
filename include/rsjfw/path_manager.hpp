#ifndef RSJFW_PATH_MANAGER_HPP
#define RSJFW_PATH_MANAGER_HPP

#include <string>
#include <filesystem>

namespace rsjfw {

class PathManager {
public:
    static PathManager& instance();

    // Initializes paths based on optional root override.
    // If rootOverride is empty, it checks RSJFW_PATH env, then XDG defaults.
    void init(const std::string& rootOverride = "");

    std::filesystem::path root() const { return rootDir_; }
    std::filesystem::path versions() const { return versionsDir_; }
    std::filesystem::path prefix() const { return prefixDir_; }
    std::filesystem::path logs() const { return logsDir_; }
    std::filesystem::path downloads() const { return downloadsDir_; }
    std::filesystem::path wine() const { return wineDir_; }
    std::filesystem::path dxvk() const { return dxvkDir_; }
    
    // Returns the path where the Vulkan layer .so should be found
    std::filesystem::path layerLib() const;
    
    // Returns the absolute path to the running RSJFW executable
    std::filesystem::path rsjfwExe() const;
    
    // Returns the path to the current session's log file
    std::filesystem::path currentLog() const { return currentLogPath_; }

    // Returns coordination paths
    std::filesystem::path inbox() const { return inboxDir_; }
    std::filesystem::path lockFile() const { return lockFilePath_; }

    // Returns true if running from a local development path
    bool isLocalBuild() const;

private:
    PathManager() = default;
    
    std::filesystem::path rootDir_;
    std::filesystem::path versionsDir_;
    std::filesystem::path prefixDir_;
    std::filesystem::path logsDir_;
    std::filesystem::path downloadsDir_;
    std::filesystem::path wineDir_;
    std::filesystem::path dxvkDir_;
    std::filesystem::path currentLogPath_;
    std::filesystem::path inboxDir_;
    std::filesystem::path lockFilePath_;
    
    std::filesystem::path resolveRoot(const std::string& override);
};

} // namespace rsjfw

#endif // RSJFW_PATH_MANAGER_HPP
