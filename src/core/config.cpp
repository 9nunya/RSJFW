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
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    configPath_ = path;

    if (!std::filesystem::exists(path)) {
        LOG_WARN("Config file not found at " + path.string() + ". Using defaults.");
        save(); 
        return;
    }

    try {
        std::ifstream file(path);
        json j;
        file >> j;

        if (j.contains("general")) {
            auto& g = j["general"];
            general_.renderer = g.value("renderer", "D3D11");
            general_.dxvk = g.value("dxvk", true);
            general_.dxvkSource = (DxvkSource)g.value("dxvk_source", 0);
            general_.dxvkVersion = g.value("dxvk_version", "2.5");
            general_.dxvkCustomPath = g.value("dxvk_custom_path", "");
            general_.dxvkRoot = g.value("dxvk_root", "");

            
            general_.wineSource = (WineSource)g.value("wine_source", 0);
            general_.wineRoot = g.value("wine_root", "");
            general_.vinegarVersion = g.value("vinegar_version", "Latest");
            general_.protonVersion = g.value("proton_version", "Latest");
            
            general_.robloxVersion = g.value("roblox_version", "");
            general_.channel = g.value("channel", "production");
            general_.selectedGpu = g.value("selected_gpu", -1);
            
            if (g.contains("env")) {
                for (auto& [key, val] : g["env"].items()) {
                    general_.customEnv[key] = val;
                }
            }
            
        }

        if (j.contains("wine")) {
            auto& w = j["wine"];
            wine_.desktopMode = w.value("desktop_mode", false);
            wine_.multipleDesktops = w.value("multiple_desktops", false);
            wine_.desktopResolution = w.value("desktop_resolution", "1920x1080");
        }

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
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (configPath_.empty()) return;

    if (configPath_.has_parent_path()) {
        std::filesystem::create_directories(configPath_.parent_path());
    }

    json j;
    j["general"] = {
        {"renderer", general_.renderer},
        {"dxvk", general_.dxvk},
        {"dxvk_source", (int)general_.dxvkSource},
        {"dxvk_version", general_.dxvkVersion},
        {"dxvk_custom_path", general_.dxvkCustomPath},
        {"dxvk_root", general_.dxvkRoot},
        {"wine_source", (int)general_.wineSource},
        {"wine_root", general_.wineRoot},
        {"vinegar_version", general_.vinegarVersion},
        {"proton_version", general_.protonVersion},
        {"roblox_version", general_.robloxVersion},
        {"channel", general_.channel},
        {"selected_gpu", general_.selectedGpu}
    };
    
    j["general"]["env"] = json::object();
    for (const auto& [key, val] : general_.customEnv) {
        j["general"]["env"][key] = val;
    }
    
    j["dxvk"]["enable"] = general_.dxvk;
    j["dxvk"]["version"] = general_.dxvkVersion;

    j["wine"]["source"] = (int)general_.wineSource;
    j["wine"]["root"] = general_.wineRoot;
    
    j["fflags"] = json::object();
    for (const auto& [key, val] : fflags_) {
        j["fflags"][key] = val;
    }
    
    try {
        std::ofstream file(configPath_);
        file << j.dump(4);
        LOG_INFO("Configuration saved to " + configPath_.string());
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to write config file: " + std::string(e.what()));
    }
}

void Config::setFFlag(const std::string& key, const nlohmann::json& value) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    fflags_[key] = value;
}

} // namespace rsjfw
