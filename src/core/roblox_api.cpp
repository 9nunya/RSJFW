#include "rsjfw/roblox_api.hpp"
#include "rsjfw/http.hpp"
#include "json.hpp"
#include <sstream>
#include <iostream>

namespace rsjfw {

static std::string trim(const std::string& s) {
    auto start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    auto end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

const std::string RobloxAPI::BASE_URL = "https://setup.rbxcdn.com/";

std::string RobloxAPI::getLatestVersionGUID(const std::string& channel) {
    std::string url = "https://clientsettings.roblox.com/v2/client-version/WindowsStudio64";
    if (channel != "LIVE") {
        url += "?channel=" + channel;
    }
    
    std::string response = HTTP::get(url);
    auto j = nlohmann::json::parse(response);
    
    if (j.contains("clientVersionUpload")) {
        return j["clientVersionUpload"];
    }
    
    throw std::runtime_error("Failed to find clientVersionUpload in response: " + response);
}

std::vector<RobloxPackage> RobloxAPI::getPackageManifest(const std::string& versionGUID) {
    std::string url = BASE_URL + versionGUID + "-rbxPkgManifest.txt";
    std::string response = HTTP::get(url);
    
    std::vector<RobloxPackage> packages;
    std::stringstream ss(response);
    std::string line;
    
    // Skip the first line (e.g., "v0")
    if (!std::getline(ss, line)) return packages;
    
    while (std::getline(ss, line)) {
        if (line.empty()) continue;
        
        RobloxPackage pkg;
        pkg.name = trim(line);
        
        if (!std::getline(ss, line)) break;
        pkg.checksum = trim(line);
        
        if (!std::getline(ss, line)) break;
        try {
            pkg.size = std::stoull(trim(line));
        } catch (...) { pkg.size = 0; }
        
        if (!std::getline(ss, line)) break;
        try {
            pkg.packedSize = std::stoull(trim(line));
        } catch (...) { pkg.packedSize = 0; }
        
        packages.push_back(pkg);
    }
    
    return packages;
}

} // namespace rsjfw
