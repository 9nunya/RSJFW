#pragma once

#include <string>
#include <vector>

namespace rsjfw {

struct RobloxPackage {
    std::string name;
    std::string checksum;
    size_t size;
    size_t packedSize;
};

class RobloxAPI {
public:
    static std::string getLatestVersionGUID(const std::string& channel = "LIVE");
    static std::vector<RobloxPackage> getPackageManifest(const std::string& versionGUID);
    
    static const std::string BASE_URL;
};

} // namespace rsjfw
