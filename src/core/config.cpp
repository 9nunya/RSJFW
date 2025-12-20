#include "rsjfw/config.hpp"
#include "rsjfw/logger.hpp"
#include <fstream>
#include <iostream>

namespace rsjfw {

using json = nlohmann::json;

Config& Config::instance() {
    static Config instance;
    return instance;
}

void Config::load(const std::filesystem::path& path) {
    std::lock_guard<std::mutex> lock(mutex_);
    configPath_ = path;

    if (!std::filesystem::exists(path)) {
        LOG_WARN("Config file not found at " + path.string() + ". Using defaults.");
        // Create default file
        save(); 
        return;
    }

    try {
        std::ifstream file(path);
        json j;
        file >> j;

        // General
        if (j.contains("general")) {
            auto& g = j["general"];
            general_.renderer = g.value("renderer", "D3D11");
            general_.dxvk = g.value("dxvk", true);
            general_.dxvkVersion = g.value("dxvk_version", "2.5");
            general_.wineRoot = g.value("wine_root", "");
        }

        // Wine
        if (j.contains("wine")) {
            auto& w = j["wine"];
            wine_.desktopMode = w.value("desktop_mode", false);
            wine_.multipleDesktops = w.value("multiple_desktops", false);
            wine_.desktopResolution = w.value("desktop_resolution", "1920x1080");
        }

        // FFlags
        if (j.contains("fflags")) {
            fflags_.clear();
            for (auto& [key, val] : j["fflags"].items()) {
                fflags_[key] = val;
            }
        }
        
        LOG_INFO("Configuration loaded from " + path.string());

    } catch (const std::exception& e) {
        LOG_ERROR("Failed to parse config file: " + std::string(e.what()));
    }
}

void Config::save() {
    // This function acts as "dump defaults" if called without load, 
    // or "save changes" if dynamic updates happen.
    // For now, mainly used to create the initial config.
    
    if (configPath_.empty()) return;

    // Create directories
    if (configPath_.has_parent_path()) {
        std::filesystem::create_directories(configPath_.parent_path());
    }

    // Don't overwrite existing user config with defaults if we just failed to parse it?
    // But load() calls save() only if file doesn't exist. So safe.

    json j;
    j["general"] = {
        {"renderer", general_.renderer},
        {"dxvk", general_.dxvk},
        {"dxvk_version", general_.dxvkVersion},
        {"wine_root", general_.wineRoot}
    };
    j["wine"] = {
        {"desktop_mode", wine_.desktopMode},
        {"multiple_desktops", wine_.multipleDesktops},
        {"desktop_resolution", wine_.desktopResolution}
    };
    
    // Convert fflags map back to json object
    json fflagsJson;
    for (const auto& [key, val] : fflags_) {
        fflagsJson[key] = val;
    }
    j["fflags"] = fflagsJson;

    try {
        std::ofstream file(configPath_);
        file << j.dump(4);
        LOG_INFO("Configuration saved to " + configPath_.string());
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to write config file: " + std::string(e.what()));
    }
}

void Config::setFFlag(const std::string& key, const nlohmann::json& value) {
    std::lock_guard<std::mutex> lock(mutex_);
    fflags_[key] = value;
}

} // namespace rsjfw
