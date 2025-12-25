#pragma once

#include <string>
#include <map>
#include <mutex>
#include <filesystem>
#include <nlohmann/json.hpp>

namespace rsjfw {

enum class WineSource {
    SYSTEM,
    CUSTOM,
    VINEGAR,
    PROTON_GE
};

enum class DxvkSource {
    OFFICIAL,
    SAREK,
    CUSTOM
};

struct GeneralConfig {
    std::string renderer = "D3D11";
    
    // DXVK
    bool dxvk = true;
    DxvkSource dxvkSource = DxvkSource::OFFICIAL;
    std::string dxvkVersion = "2.5";
    std::string dxvkCustomPath = ""; // For custom source
    std::string dxvkRoot = ""; // For auto-downloaded DXVK
    
    // Wine
    WineSource wineSource = WineSource::SYSTEM;
    std::string wineRoot = ""; // For Custom/Vinegar/Proton
    std::string vinegarVersion = "Latest";
    std::string protonVersion = "Latest";
    
    std::string robloxVersion = ""; // Empty = Latest
    std::string rootDir;
    std::string versionsDir;
    std::string channel = "production";
    
    // GPU Selection (index into detected GPUs, -1 = auto)
    int selectedGpu = -1;
    
    // Custom Environment Variables
    std::map<std::string, std::string> customEnv; // "LIVE" or "production" or specific
};

// ... WineConfig unchanged

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
    
    // Getters
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
    
    std::recursive_mutex mutex_;
};

} // namespace rsjfw
