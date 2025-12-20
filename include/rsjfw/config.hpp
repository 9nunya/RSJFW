#pragma once

#include <string>
#include <map>
#include <mutex>
#include <filesystem>
#include <nlohmann/json.hpp>

namespace rsjfw {

struct GeneralConfig {
    std::string renderer = "D3D11";
    bool dxvk = true;
    std::string dxvkVersion = "2.5";
    std::string wineRoot = "";
};

struct WineConfig {
    bool desktopMode = false;
    bool multipleDesktops = false;
    std::string desktopResolution = "1920x1080";
};

class Config {
public:
    static Config& instance();

    void load(const std::filesystem::path& configPath);
    void save();

    GeneralConfig& getGeneral() { return general_; }
    WineConfig& getWine() { return wine_; }
    
    // FFlags are dynamic, just expose the map
    std::map<std::string, nlohmann::json>& getFFlags() { return fflags_; }
    void setFFlag(const std::string& key, const nlohmann::json& value);

    // Forbidden
    Config(const Config&) = delete;
    Config& operator=(const Config&) = delete;

private:
    Config() = default;
    ~Config() = default;

    std::filesystem::path configPath_;
    GeneralConfig general_;
    WineConfig wine_;
    std::map<std::string, nlohmann::json> fflags_;
    
    std::mutex mutex_;
};

} // namespace rsjfw
