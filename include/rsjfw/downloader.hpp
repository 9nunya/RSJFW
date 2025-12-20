#pragma once

#include <string>
#include <vector>
#include "rsjfw/roblox_api.hpp"

namespace rsjfw {

class Downloader {
public:
    Downloader(const std::string& rootDir);
    
    bool installLatest();
    bool installVersion(const std::string& versionGUID);
    
private:
    std::string rootDir_;
    std::string versionsDir_;
    std::string downloadsDir_;
    
    bool downloadPackage(const std::string& versionGUID, const RobloxPackage& pkg);
};

} // namespace rsjfw
