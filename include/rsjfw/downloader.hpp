#pragma once

#include <string>
#include <vector>
#include <functional>
#include "rsjfw/roblox_api.hpp"
#include "rsjfw/config.hpp"

namespace rsjfw {

class Downloader {
public:
    Downloader(const std::string& rootDir);
    
    using ProgressCallback = std::function<void(const std::string& currentItem, float itemProgress, size_t itemIndex, size_t totalItems)>;
    bool installLatest(ProgressCallback callback = nullptr);
    bool installVersion(const std::string& versionGUID, ProgressCallback callback = nullptr);

    
    
    // New methods
    bool installWine(WineSource source, const std::string& version = "Latest", ProgressCallback callback = nullptr);
    bool installDxvk(DxvkSource source, const std::string& version, ProgressCallback callback = nullptr); 
    
    std::vector<std::string> fetchDxvkVersions(DxvkSource source);
    std::vector<std::string> fetchWineVersions(WineSource source);

    std::string getLatestVersionGUID();
    
private:
    std::string rootDir_;
    std::string versionsDir_;
    std::string downloadsDir_;
    
    std::string downloadLatestRobloxStudio(const std::string& versionGUID);

    
    bool downloadPackage(const std::string& versionGUID, const RobloxPackage& pkg, std::function<void(size_t, size_t)> progressCb);
    std::string extractArchive(const std::string& archivePath, const std::string& destDir, ProgressCallback callback);
};

} // namespace rsjfw
