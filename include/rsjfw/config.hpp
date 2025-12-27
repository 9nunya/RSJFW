#ifndef RSJFW_CONFIG_HPP
#define RSJFW_CONFIG_HPP

#include <filesystem>
#include <map>
#include <mutex>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace rsjfw {

// v2.1: Source configuration for GitHub-based Wine/Proton/DXVK downloads
struct WineSourceConfig {
  std::string repo = "vinegarhq/wine-builds"; // GitHub "user/repo" format
  std::string version = "latest";             // Release tag or "latest"
  std::string asset = "";         // Selected asset filename (empty = auto)
  std::string installedRoot = ""; // Path to installed wine root
};

struct DxvkSourceConfig {
  std::string repo = "doitsujin/dxvk"; // GitHub "user/repo" format
  std::string version = "latest";      // Release tag or "latest"
  std::string asset = "";              // Selected asset filename (empty = auto)
  std::string installedRoot = "";      // Path to installed DXVK
};

// Preset repos for quick selection
namespace Presets {
inline const std::vector<std::string> WineRepos = {
    "vinegarhq/wine-builds", "GloriousEggroll/proton-ge-custom",
    "CachyOS/proton-cachyos"};
inline const std::vector<std::string> DxvkRepos = {"doitsujin/dxvk",
                                                   "Sarek-S/dxvk-guerilla"};
} // namespace Presets

struct GeneralConfig {
  std::string renderer = "D3D11";

  // DXVK (v2.1 format)
  bool dxvk = true;
  DxvkSourceConfig dxvkSource;

  // Wine/Proton (v2.1 format)
  WineSourceConfig wineSource;

  // Legacy fields (for migration, will be removed in v2.2)
  std::string wineSourceLegacy = ""; // Old string format
  std::string dxvkSourceLegacy = ""; // Old string format
  std::string dxvkVersion = "";
  std::string dxvkCustomPath = "";
  std::string dxvkCustomUrl = "";
  std::string dxvkRoot = "";
  std::string wineVersion = "";
  std::string wineRoot = "";
  std::string wineCustomUrl = "";

  std::string robloxVersion = "";
  std::string rootDir;
  std::string versionsDir;
  std::string channel = "production";

  int selectedGpu = -1;
  std::map<std::string, std::string> customEnv;
};

// ... WineConfig unchanged

struct WineConfig {
  bool desktopMode = false;
  bool multipleDesktops = false;
  std::string desktopResolution = "1920x1080";
};

class Config {
public:
  static Config &instance();

  void load(const std::filesystem::path &configPath);
  void save();

  // Getters
  GeneralConfig &getGeneral() { return general_; }
  WineConfig &getWine() { return wine_; }

  // FFlags are dynamic, just expose the map
  std::map<std::string, nlohmann::json> &getFFlags() { return fflags_; }
  void setFFlag(const std::string &key, const nlohmann::json &value);
  std::recursive_mutex &getMutex() { return mutex_; }

  // Forbidden
  Config(const Config &) = delete;
  Config &operator=(const Config &) = delete;

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

#endif // RSJFW_CONFIG_HPP
