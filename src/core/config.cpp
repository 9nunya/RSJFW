#include "rsjfw/config.hpp"
#include "rsjfw/logger.hpp"
#include <fstream>
#include <iostream>

namespace rsjfw {

using json = nlohmann::json;

Config &Config::instance() {
  static Config instance;
  return instance;
}

void Config::load(const std::filesystem::path &path) {
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
      auto &g = j["general"];
      general_.renderer = g.value("renderer", "D3D11");
      general_.dxvk = g.value("dxvk", true);

      // v2.1: Load new source config format, or migrate from old
      if (g.contains("wine_source_config")) {
        auto &ws = g["wine_source_config"];
        general_.wineSource.repo = ws.value("repo", "vinegarhq/wine-builds");
        general_.wineSource.version = ws.value("version", "latest");
        general_.wineSource.asset = ws.value("asset", "");
        general_.wineSource.installedRoot = ws.value("installed_root", "");
      } else {
        // Migrate from old format
        std::string oldSource = "";
        if (g.contains("wine_source")) {
          if (g["wine_source"].is_number()) {
            int old = g["wine_source"];
            if (old == 0)
              oldSource = "SYSTEM";
            else if (old == 1)
              oldSource = "CUSTOM";
            else if (old == 2)
              oldSource = "vinegarhq/wine-builds";
            else if (old == 3)
              oldSource = "GloriousEggroll/proton-ge-custom";
          } else {
            oldSource = g.value("wine_source", "vinegarhq/wine-builds");
          }
        }
        // Convert known keywords to repos
        if (oldSource == "VINEGAR")
          general_.wineSource.repo = "vinegarhq/wine-builds";
        else if (oldSource == "GE-PROTON")
          general_.wineSource.repo = "GloriousEggroll/proton-ge-custom";
        else if (oldSource == "CACHY-PROTON")
          general_.wineSource.repo = "CachyOS/proton-cachyos";
        else if (!oldSource.empty() && oldSource.find('/') != std::string::npos)
          general_.wineSource.repo = oldSource;
        else
          general_.wineSource.repo = "vinegarhq/wine-builds";

        general_.wineSource.version = g.value("wine_version", "latest");
        general_.wineSource.installedRoot = g.value("wine_root", "");
      }

      // v2.1: Load DXVK source config
      if (g.contains("dxvk_source_config")) {
        auto &ds = g["dxvk_source_config"];
        general_.dxvkSource.repo = ds.value("repo", "doitsujin/dxvk");
        general_.dxvkSource.version = ds.value("version", "latest");
        general_.dxvkSource.asset = ds.value("asset", "");
        general_.dxvkSource.installedRoot = ds.value("installed_root", "");
      } else {
        // Migrate from old format
        std::string oldSource = "";
        if (g.contains("dxvk_source")) {
          if (g["dxvk_source"].is_number()) {
            int old = g["dxvk_source"];
            if (old == 0)
              oldSource = "doitsujin/dxvk";
            else if (old == 1)
              oldSource = "Sarek-S/dxvk-guerilla";
            else
              oldSource = "CUSTOM";
          } else {
            oldSource = g.value("dxvk_source", "doitsujin/dxvk");
          }
        }
        if (!oldSource.empty() && oldSource.find('/') != std::string::npos)
          general_.dxvkSource.repo = oldSource;
        else
          general_.dxvkSource.repo = "doitsujin/dxvk";

        general_.dxvkSource.version = g.value("dxvk_version", "latest");
        general_.dxvkSource.installedRoot = g.value("dxvk_root", "");
      }

      // Keep legacy fields for backwards compat with older code paths
      general_.dxvkVersion = general_.dxvkSource.version;
      general_.dxvkRoot = general_.dxvkSource.installedRoot;
      general_.wineVersion = general_.wineSource.version;
      general_.wineRoot = general_.wineSource.installedRoot;
      general_.dxvkCustomPath = g.value("dxvk_custom_path", "");
      general_.dxvkCustomUrl = g.value("dxvk_custom_url", "");
      general_.wineCustomUrl = g.value("wine_custom_url", "");

      general_.robloxVersion = g.value("roblox_version", "");
      general_.channel = g.value("channel", "production");
      general_.selectedGpu = g.value("selected_gpu", -1);

      if (g.contains("env")) {
        for (auto &[key, val] : g["env"].items()) {
          general_.customEnv[key] = val;
        }
      }
    }

    if (j.contains("wine")) {
      auto &w = j["wine"];
      wine_.desktopMode = w.value("desktop_mode", false);
      wine_.multipleDesktops = w.value("multiple_desktops", false);
      wine_.desktopResolution = w.value("desktop_resolution", "1920x1080");
    }

    if (j.contains("fflags")) {
      fflags_.clear();
      for (auto &[key, val] : j["fflags"].items()) {
        fflags_[key] = val;
      }
    }

    LOG_INFO("Configuration loaded from " + path.string());
    save();

  } catch (const std::exception &e) {
    LOG_ERROR("Failed to parse config file: " + std::string(e.what()));
  }
}

void Config::save() {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (configPath_.empty())
    return;

  if (configPath_.has_parent_path()) {
    std::filesystem::create_directories(configPath_.parent_path());
  }

  json j;

  // v2.1: Save new source config format
  j["general"] = {{"renderer", general_.renderer},
                  {"dxvk", general_.dxvk},
                  {"wine_source_config",
                   {{"repo", general_.wineSource.repo},
                    {"version", general_.wineSource.version},
                    {"asset", general_.wineSource.asset},
                    {"installed_root", general_.wineSource.installedRoot}}},
                  {"dxvk_source_config",
                   {{"repo", general_.dxvkSource.repo},
                    {"version", general_.dxvkSource.version},
                    {"asset", general_.dxvkSource.asset},
                    {"installed_root", general_.dxvkSource.installedRoot}}},
                  {"roblox_version", general_.robloxVersion},
                  {"channel", general_.channel},
                  {"selected_gpu", general_.selectedGpu}};

  j["general"]["env"] = json::object();
  for (const auto &[key, val] : general_.customEnv) {
    j["general"]["env"][key] = val;
  }

  j["wine"]["desktop_mode"] = wine_.desktopMode;
  j["wine"]["multiple_desktops"] = wine_.multipleDesktops;
  j["wine"]["desktop_resolution"] = wine_.desktopResolution;

  j["fflags"] = json::object();
  for (const auto &[key, val] : fflags_) {
    j["fflags"][key] = val;
  }

  try {
    std::ofstream file(configPath_);
    file << j.dump(4);
    LOG_INFO("Configuration saved to " + configPath_.string());
  } catch (const std::exception &e) {
    LOG_ERROR("Failed to write config file: " + std::string(e.what()));
  }
}

void Config::setFFlag(const std::string &key, const nlohmann::json &value) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  fflags_[key] = value;
}

} // namespace rsjfw
