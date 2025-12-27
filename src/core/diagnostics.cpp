#include "rsjfw/diagnostics.hpp"
#include "rsjfw/config.hpp"
#include "rsjfw/downloader.hpp"
#include "rsjfw/gui.hpp"
#include "rsjfw/launcher.hpp"
#include "rsjfw/logger.hpp"
#include "rsjfw/path_manager.hpp"
#include "rsjfw/state.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>

namespace rsjfw {

// Define expected versions
const int CURRENT_CONFIG_VERSION = 1;
const std::string CURRENT_PREFIX_VERSION =
    "1.0"; // Increment this when prefix logic changes

Diagnostics &Diagnostics::instance() {
  static Diagnostics instance;
  return instance;
}

int Diagnostics::failureCount() const {
  int count = 0;
  for (const auto &check : results_) {
    if (!check.second.ok)
      count++;
  }
  return count;
}

void Diagnostics::fixIssue(const std::string &name,
                           std::function<void(float, std::string)> progressCb) {
  // Provide a no-op callback if nullptr to prevent bad_function_call
  auto safeCb = progressCb ? progressCb : [](float, std::string) {};

  for (const auto &check : results_) {
    if (check.first == name && check.second.fixable && check.second.fixAction) {
      check.second.fixAction(safeCb);
      return;
    }
  }
  safeCb(0.0f, "Issue not found or not fixable");
}

bool Diagnostics::runChecks() {
  results_.clear();

  checkRoot();
  checkConfig();
  checkWine();
  checkLayer();
  checkPrefix();
  checkDesktop();
  checkProtocol();
  checkLegacy();
  checkFlatpak();
  checkSystem();

  return failureCount() == 0;
}

void Diagnostics::checkRoot() {
  auto &pm = PathManager::instance();
  bool rootOk = std::filesystem::exists(pm.root());
  results_.push_back({"RSJFW Root",
                      {rootOk,
                       rootOk ? "Accessible" : "Missing/Inaccessible",
                       pm.root().string(),
                       false,
                       nullptr,
                       HealthCategory::CRITICAL,
                       {"filesystem", "core"}}});
}

void Diagnostics::checkConfig() {
  auto &pm = PathManager::instance();
  bool configOk = std::filesystem::exists(pm.root() / "config.json");
  HealthStatus configStatus = {
      configOk,
      configOk ? "Valid" : "Missing",
      (pm.root() / "config.json").string(),
      !configOk,
      [pm](std::function<void(float, std::string)> cb) {
        cb(0.1f, "Regenerating Config...");
        Config::instance().save();
        cb(1.0f, "Complete");
      },
      HealthCategory::CONFIG,
      {"json", "settings"}};
  results_.push_back({"Configuration", configStatus});
}

void Diagnostics::checkWine() {
  auto &cfg = Config::instance().getGeneral();
  auto appState = State::instance().get();
  bool downloadingWine = (appState == AppState::DOWNLOADING_WINE);

  bool wineSourceOk = false;
  std::string wineMsg = "Not configured";
  bool wineSystemFail = false;

  if (downloadingWine) {
    wineSourceOk = true;
    wineMsg = "Downloading Wine...";
  } else if (cfg.wineSource.repo == "SYSTEM") {
    if (system("wine --version > /dev/null 2>&1") == 0) {
      wineSourceOk = true;
      wineMsg = "System Wine Found";
    } else {
      wineMsg = "System Wine Missing";
      wineSystemFail = true;
    }
  } else {
    std::filesystem::path root(cfg.wineRoot);
    if (!cfg.wineRoot.empty()) {
      bool isProton = std::filesystem::exists(root / "proton");
      std::filesystem::path bin = root / "bin/wine";
      if (!std::filesystem::exists(bin))
        bin = root / "files/bin/wine";
      if (!std::filesystem::exists(bin))
        bin = root / "dist/bin/wine";

      if (std::filesystem::exists(bin)) {
        wineSourceOk = true;
        wineMsg = isProton ? "Proton Detected" : "Wine Root Valid";
        if (isProton)
          wineMsg += " (via script)";
      } else if (isProton) {
        wineSourceOk = false;
        wineMsg = "Proton: Missing embedded wine binary";
      } else {
        wineSourceOk = false;
        wineMsg = "Binary Missing";
      }
    } else {
      wineSourceOk = false;
      wineMsg = "Root Not Specified";
    }
  }

  HealthStatus wineStatus = {
      wineSourceOk,
      wineSourceOk ? "Valid" : wineMsg,
      cfg.wineRoot,
      !wineSourceOk,
      [wineSystemFail](std::function<void(float, std::string)> cb) {
        if (wineSystemFail) {
          if (cb)
            cb(1.0f, "Failed");
          GUI::instance().showMessage("System Wine Missing",
                                      "Install wine via your package manager");
          return;
        }

        auto &cfgInst = Config::instance().getGeneral();

        // Determine the Wine source to download from
        std::string repo = "";
        if (cfgInst.wineSource.repo == "CUSTOM") {
          repo = cfgInst.wineCustomUrl;
        } else if (cfgInst.wineSource.repo.find("/") != std::string::npos) {
          // GitHub repo format like user/repo
          repo = cfgInst.wineSource.repo;
        } else if (cfgInst.wineSource.repo != "SYSTEM" &&
                   cfgInst.wineSource.repo != "CUSTOM_PATH") {
          // Fallback to a known repo if source is misconfigured
          repo = "vinegarhq/wine-builds";
        }

        if (repo.empty()) {
          if (cb)
            cb(1.0f, "No source configured");
          GUI::instance().showMessage("Wine Source Error",
                                      "Configure a Wine source in Settings");
          return;
        }

        if (cb)
          cb(0.1f, "Starting Download from " + repo + "...");
        Downloader dl(PathManager::instance().root().string());

        // Get version (or use "latest" if not set)
        std::string ver =
            cfgInst.wineVersion.empty() ? "latest" : cfgInst.wineVersion;

        bool res = dl.installWine(
            cfgInst.wineSource.repo, "latest", "",
            [cb](const std::string &item, float p, size_t, size_t) {
              if (cb)
                cb(p, "Downloading: " + item);
            });
        if (cb)
          cb(1.0f, res ? "Installed" : "Failed");
        if (!res)
          GUI::instance().showMessage("Download Failed", "Check logs/network.");
      },
      HealthCategory::WINE,
      {"runner", "dxvk"}};
  results_.push_back({"Wine Source", wineStatus});
}

void Diagnostics::checkLayer() {
  auto &pm = PathManager::instance();
  std::filesystem::path layer = pm.layerLib();
  bool layerOk = std::filesystem::exists(layer);

  // Check if we can offer a build fix
  bool canBuild = (system("which cmake > /dev/null 2>&1") == 0 &&
                   system("which g++ > /dev/null 2>&1") == 0);

  HealthStatus layerStatus = {
      layerOk,
      layerOk ? "Present" : "Missing",
      layer.string(),
      !layerOk,
      [this, layer, canBuild](std::function<void(float, std::string)> cb) {
        if (cb)
          cb(0.1f, "Restoring Library...");
        auto &pmInst = PathManager::instance();
        std::filesystem::path exeDir = pmInst.rsjfwExe().parent_path();
        std::filesystem::path source =
            exeDir / "libVkLayer_RSJFW_RsjfwLayer.so";
        if (!std::filesystem::exists(source))
          source = exeDir.parent_path() / "libVkLayer_RSJFW_RsjfwLayer.so";

        if (std::filesystem::exists(source)) {
          std::filesystem::create_directories(layer.parent_path());
          std::filesystem::copy_file(
              source, layer, std::filesystem::copy_options::overwrite_existing);
          if (cb)
            cb(1.0f, "Complete");
        } else if (canBuild) {
          buildLayerFromSource(cb);
        } else {
          if (cb)
            cb(0.0f, "Source Library Missing & No Build Tools");
        }
      },
      HealthCategory::SYSTEM,
      {"vulkan", "layer"}};

  if (!layerOk && !canBuild)
    layerStatus.detail = "Library missing and build tools (cmake, g++) not "
                         "found. Please install them or reinstall RSJFW.";
  else if (!layerOk && canBuild)
    layerStatus.detail = "Library missing. Click FIX to build from source.";

  results_.push_back({"RSJFW Layer", layerStatus});
}

void Diagnostics::checkPrefix() {
  auto &pm = PathManager::instance();
  std::filesystem::path pfxMarker = pm.prefix() / ".rsjfw_setup_complete";
  bool pfxOk = std::filesystem::exists(pfxMarker) &&
               std::filesystem::exists(pm.prefix() / "drive_c");
  HealthStatus pfxHealth = {pfxOk,
                            pfxOk ? "Ready" : "Not Initialized",
                            pm.prefix().string(),
                            !pfxOk,
                            [pm](std::function<void(float, std::string)> cb) {
                              Launcher launcher(pm.root().string());
                              launcher.setupPrefix(
                                  [cb](float p, std::string m) {
                                    if (cb)
                                      cb(p, m);
                                  });
                            },
                            HealthCategory::WINE,
                            {"prefix", "registry"}};
  results_.push_back({"Wine Prefix", pfxHealth});
}

void Diagnostics::checkDesktop() {
  auto &pm = PathManager::instance();
  bool isLocal = pm.isLocalBuild();
  std::string desktopSuffix = isLocal ? "-local" : "";
  std::string desktopFilename = "rsjfw" + desktopSuffix + ".desktop";
  std::filesystem::path desktopFile = std::filesystem::path(getenv("HOME")) /
                                      ".local/share/applications" /
                                      desktopFilename;

  // In local mode, we should check if the Exec path in the desktop file matches
  // our current EXE
  bool desktopExists = std::filesystem::exists(desktopFile);
  bool desktopPathOk = false;
  if (desktopExists) {
    std::ifstream in(desktopFile);
    std::string line;
    std::string currentExe = pm.rsjfwExe().string();
    while (std::getline(in, line)) {
      if (line.find("Exec=") == 0 &&
          line.find(currentExe) != std::string::npos) {
        desktopPathOk = true;
        break;
      }
    }
  }

  bool desktopOk = desktopExists && desktopPathOk;
  HealthStatus desktopStatus = {
      desktopOk,
      desktopOk ? "Installed" : (desktopExists ? "Stale Exec Path" : "Missing"),
      desktopFile.string(),
      !desktopOk,
      [desktopFile, isLocal](std::function<void(float, std::string)> cb) {
        if (cb)
          cb(0.1f, "Generating Desktop Entry...");
        std::ofstream out(desktopFile);
        auto &pmInst = PathManager::instance();
        std::string name = isLocal ? "RSJFW Local Build" : "Roblox Studio";
        out << "[Desktop Entry]\nType=Application\nName=" << name
            << "\nExec=" << pmInst.rsjfwExe().string()
            << " launch "
               "%u\nIcon=rsjfw\nTerminal=false\nCategories=GNOME;GTK;Game;"
               "PackageManager;\nMimeType=application/"
               "x-roblox-place;application/x-roblox-model;x-scheme-handler/"
               "roblox-studio;x-scheme-handler/"
               "roblox-studio-auth;\nComment=Launch Roblox Studio via "
            << (isLocal ? "RSJFW Local Build" : "RSJFW")
            << "\nActions=manage;\nKeywords=gaming;launcher;flatpak;\n\n["
               "Desktop Action manage]\nName=Manage\nExec="
            << pmInst.rsjfwExe().string() << " config\n";
        out.close();

        LOG_INFO("Created desktop entry: " + desktopFile.string());
        if (cb)
          cb(0.5f, "Updating Desktop Database...");
        system("update-desktop-database ~/.local/share/applications");
        if (cb)
          cb(1.0f, "Complete");
      },
      HealthCategory::SYSTEM,
      {"integration", "desktop"}};

  if (isLocal) {
    desktopStatus.detail =
        "Enforcing local helper for dev build: " + pm.rsjfwExe().string();
  }

  results_.push_back({"Desktop Entry", desktopStatus});
}

void Diagnostics::checkProtocol() {
  auto &pm = PathManager::instance();
  bool isLocal = pm.isLocalBuild();
  std::string desktopFilename =
      isLocal ? "rsjfw-local.desktop" : "rsjfw.desktop";

  std::string queryCmd =
      "xdg-mime query default x-scheme-handler/roblox-studio | grep " +
      (isLocal ? std::string("local") : std::string("rsjfw"));
  bool protoOk = (system((queryCmd + " > /dev/null").c_str()) == 0);

  HealthStatus protoStatus = {
      protoOk,
      protoOk ? "Registered" : "Not Registered",
      "x-scheme-handler/roblox-studio",
      !protoOk,
      [desktopFilename](std::function<void(float, std::string)> cb) {
        std::string cmd1 = "xdg-mime default " + desktopFilename +
                           " x-scheme-handler/roblox-studio";
        std::string cmd2 = "xdg-mime default " + desktopFilename +
                           " x-scheme-handler/roblox-studio-auth";
        LOG_INFO("Executing protocol registration: " + cmd1);
        system(cmd1.c_str());
        system(cmd2.c_str());
        if (cb)
          cb(1.0f, "Complete");
      },
      HealthCategory::SYSTEM,
      {"integration", "protocol"}};
  results_.push_back({"Protocol Handlers", protoStatus});
}

void Diagnostics::checkLegacy() {
  auto &pm = PathManager::instance();
  std::filesystem::path legacyRoot =
      std::filesystem::path(getenv("HOME")) / ".rsjfw";
  if (std::filesystem::exists(legacyRoot)) {
    HealthStatus legacyStatus = {
        false,
        "Legacy Install Detected",
        legacyRoot.string(),
        true,
        [legacyRoot](std::function<void(float, std::string)> cb) {
          cb(0.5f, "Removing Legacy Data...");
          std::filesystem::remove_all(legacyRoot);
          cb(1.0f, "Cleaned");
        },
        HealthCategory::LEGACY,
        {"migration", "cleanup"}};
    results_.push_back({"Legacy Data", legacyStatus});
  }

  std::filesystem::path legacyConfig = std::filesystem::path(getenv("HOME")) /
                                       ".config" / "rsjfw" / "config.json";
  if (std::filesystem::exists(legacyConfig)) {
    HealthStatus legacyCfgStatus = {
        false,
        "Legacy Config File",
        legacyConfig.string(),
        true,
        [legacyConfig](std::function<void(float, std::string)> cb) {
          cb(0.5f, "Migrating...");
          try {
            std::filesystem::path target =
                PathManager::instance().root() / "config_migrated.json";
            std::filesystem::copy_file(
                legacyConfig, target,
                std::filesystem::copy_options::overwrite_existing);
            std::filesystem::remove(legacyConfig);
          } catch (...) {
          }
          cb(1.0f, "Moved");
        },
        HealthCategory::LEGACY,
        {"migration", "cleanup"}};
    results_.push_back({"Legacy Config", legacyCfgStatus});
  }
}

void Diagnostics::checkFlatpak() {
  bool isFlatpak = std::filesystem::exists("/.flatpak-info");

  if (isFlatpak) {
    // 1. Check if we can see the host filesystem adequately
    // Flatpak usually mounts host /home at /var/run/host/home or similar if
    // requested, but for us, we just need our data dir to be writable.

    auto &pm = PathManager::instance();
    bool canWrite = false;
    try {
      std::filesystem::path testFile = pm.root() / ".write_test";
      std::ofstream f(testFile);
      if (f.good()) {
        f.close();
        std::filesystem::remove(testFile);
        canWrite = true;
      }
    } catch (...) {
    }

    HealthStatus fpStatus = {canWrite,
                             canWrite ? "Sandboxed (Writable)"
                                      : "Read-Only (Error)",
                             "Flatpak container",
                             !canWrite,
                             nullptr,
                             HealthCategory::SYSTEM,
                             {"container", "flatpak"}};
    if (!canWrite)
      fpStatus.detail = "RSJFW cannot write to its data directory inside "
                        "Flatpak. Check permissions.";
    results_.push_back({"Environment", fpStatus});

    // 2. Check XDG Portal (rough check)
    bool hasPortal = (std::getenv("DBUS_SESSION_BUS_ADDRESS") != nullptr);
    if (!hasPortal) {
      results_.push_back({"Desktop Portal",
                          {false,
                           "Missing DBus",
                           "xdg-desktop-portal",
                           false,
                           nullptr,
                           HealthCategory::SYSTEM,
                           {"container", "integration"}}});
    }
  }
}

// ... (other checks remain same, just skip to checkSystem) ...

void Diagnostics::checkSystem() {
  // 1. Build Tools (Git, CMake, Make, G++)
  struct Tool {
    std::string name;
    std::string exe;
    bool critical;
  };
  std::vector<Tool> tools = {{"Git", "git", false},
                             {"CMake", "cmake", false},
                             {"Make", "make", false},
                             {"G++", "g++", false}};

  bool buildToolsOk = true;
  std::string missingTools;

  for (const auto &tool : tools) {
    if (system(("which " + tool.exe + " > /dev/null 2>&1").c_str()) != 0) {
      buildToolsOk = false;
      if (!missingTools.empty())
        missingTools += ", ";
      missingTools += tool.name;
    }
  }

  HealthStatus buildStatus = {buildToolsOk,
                              buildToolsOk ? "Installed"
                                           : "Missing: " + missingTools,
                              "Required for auto-building components",
                              false,
                              nullptr,
                              HealthCategory::SYSTEM,
                              {"dependency", "build"}};
  if (!buildToolsOk)
    buildStatus.detail = "Install these packages to enable source-based fixes.";
  results_.push_back({"Build Tools", buildStatus});

  // 2. Vulkan Tools - use 'which' for faster detection
  bool vkToolsOk = (system("which vulkaninfo > /dev/null 2>&1") == 0);

  HealthStatus vkToolsStatus = {vkToolsOk,
                                vkToolsOk ? "Installed" : "Missing (Optional)",
                                "vulkan-tools",
                                false,
                                nullptr,
                                HealthCategory::SYSTEM,
                                {"dependency", "vulkan"}};
  if (!vkToolsOk)
    vkToolsStatus.detail = "Install vulkan-tools for better diagnostics.";
  results_.push_back({"Vulkan Tools", vkToolsStatus});

  // 3. Graphics Info - use 'which' for faster detection
  bool glxOk = (system("which glxinfo > /dev/null 2>&1") == 0);
  if (!glxOk) {
    results_.push_back({"Graphics Info",
                        {false,
                         "Missing glxinfo",
                         "mesa-utils",
                         false,
                         nullptr,
                         HealthCategory::SYSTEM,
                         {"dependency", "gpu"}}});
  }

  // 2. Smart GPU Detection
  if (vkToolsOk) {
    // Run vulkaninfo to get API Version
    // This is a crude check but effective
    std::string cmd = "timeout 2s vulkaninfo --summary 2>&1 | grep "
                      "'apiVersion' | head -n 1 | awk '{print $3}'";
    FILE *pipe = popen(cmd.c_str(), "r");
    if (pipe) {
      char buffer[128];
      std::string result = "";
      if (fgets(buffer, 128, pipe) != NULL)
        result = buffer;
      pclose(pipe);

      // Clean result (1.3.xxx)
      while (!result.empty() &&
             (result.back() == '\n' || result.back() == '\r'))
        result.pop_back();

      if (!result.empty()) {
        // Parse version
        int major = 0, minor = 0;
        sscanf(result.c_str(), "%d.%d", &major, &minor);

        std::string configDxvk = Config::instance().getGeneral().dxvkVersion;

        // Logic: DXVK 2.0+ requires Vulkan 1.3
        // DXVK 1.10.x requires Vulkan 1.1
        // Handle version strings that may have 'v' prefix (e.g., "v2.7.1")
        std::string dxvkClean = configDxvk;
        if (!dxvkClean.empty() &&
            (dxvkClean[0] == 'v' || dxvkClean[0] == 'V')) {
          dxvkClean = dxvkClean.substr(1);
        }

        bool gpuIssue = false;
        std::string gpuMsg = "Compatible";

        if (major == 1 && minor < 3) {
          // GPU is < 1.3
          if (dxvkClean.find("2.") == 0 || configDxvk == "latest") {
            gpuIssue = true;
            gpuMsg = "Incompatible DXVK Config";
          }
        }

        HealthStatus gpuStatus = {
            !gpuIssue,
            gpuMsg,
            "GPU supports Vulkan " + result,
            gpuIssue,
            [](std::function<void(float, std::string)> cb) {
              cb(0.5f, "Configuring Sarek/Legacy DXVK...");
              auto &cfg = Config::instance().getGeneral();
              cfg.dxvkSource.version = "v1.10.3";
              cfg.dxvkSource.repo =
                  "doitsujin/dxvk"; // Ensure using official repo
              cfg.dxvkSource.installedRoot =
                  ""; // Clear root to trigger re-download of new version
              Config::instance().save();
              cb(1.0f,
                 "Set DXVK to v1.10.3 (Sarek) - Will download on next save");
            },
            HealthCategory::CONFIG,
            {"gpu", "dxvk"}};

        if (gpuIssue) {
          gpuStatus.detail = "Your GPU (Vulkan " + result +
                             ") does not support DXVK " + configDxvk +
                             " (Requires 1.3). Recommend: Sarek/1.10.3.";
        }
        // Always push back status so user sees checks passed
        results_.push_back({"GPU Compatibility", gpuStatus});
      }
    }
  }
}

void Diagnostics::buildLayerFromSource(
    std::function<void(float, std::string)> cb) {
  auto &pm = PathManager::instance();
  std::filesystem::path buildRoot = pm.root() / "cache" / "src";
  std::filesystem::path logFile = pm.root() / "cache" / "build.log";
  std::filesystem::create_directories(buildRoot);

  // Clear old log
  std::ofstream(logFile, std::ios::trunc).close();

  auto runCmd = [&](const std::string &cmd) -> int {
    std::string fullCmd = cmd + " >> " + logFile.string() + " 2>&1";
    return system(fullCmd.c_str());
  };

  cb(0.1f, "Cloning RSJFW...");

  std::string repoUrl = "https://github.com/9nunya/RSJFW";

  // If old clone exists, delete it and start fresh
  if (std::filesystem::exists(buildRoot / ".git")) {
    std::filesystem::remove_all(buildRoot);
    std::filesystem::create_directories(buildRoot);
  }

  std::string cloneCmd =
      "git clone --depth 1 " + repoUrl + " " + buildRoot.string();

  if (runCmd(cloneCmd) != 0) {
    cb(0.0f, "Git Clone Failed - Check " + logFile.string());
    return;
  }

  cb(0.25f, "Initializing Submodules...");
  if (runCmd("cd " + buildRoot.string() +
             " && git submodule update --init --depth 1") != 0) {
    cb(0.0f, "Submodule Init Failed - Check " + logFile.string());
    return;
  }

  cb(0.4f, "Configuring...");
  if (runCmd("cd " + buildRoot.string() + " && cmake -B build -S .") != 0) {
    cb(0.0f, "CMake Failed - Check " + logFile.string());
    return;
  }

  cb(0.6f, "Compiling Layer...");
  if (runCmd("cd " + buildRoot.string() +
             "/build && make VkLayer_RSJFW_RsjfwLayer -j$(nproc)") != 0) {
    cb(0.0f, "Compilation Failed - Check " + logFile.string());
    return;
  }

  cb(0.85f, "Installing...");
  std::filesystem::path builtLib =
      buildRoot / "build/libVkLayer_RSJFW_RsjfwLayer.so";
  if (!std::filesystem::exists(builtLib))
    builtLib = buildRoot / "build/src/layer/libVkLayer_RSJFW_RsjfwLayer.so";

  if (std::filesystem::exists(builtLib)) {
    std::filesystem::path target = pm.layerLib();
    std::filesystem::create_directories(target.parent_path());
    std::filesystem::copy_file(
        builtLib, target, std::filesystem::copy_options::overwrite_existing);

    // Generate and install JSON manifest with absolute path
    std::string manifestJson =
        "{\n"
        "    \"file_format_version\" : \"1.0.0\",\n"
        "    \"layer\" : {\n"
        "        \"name\": \"VK_LAYER_RSJFW_RsjfwLayer\",\n"
        "        \"type\": \"GLOBAL\",\n"
        "        \"library_path\": \"" +
        target.string() +
        "\",\n"
        "        \"api_version\": \"1.0.0\",\n"
        "        \"implementation_version\": \"1\",\n"
        "        \"description\": \"RSJFW Vulkan Layer (User-built)\",\n"
        "        \"disable_environment\": {\n"
        "            \"DISABLE_RSJFW_LAYER\": \"1\"\n"
        "        }\n"
        "    }\n"
        "}";

    std::filesystem::path vulkanLayerDir =
        std::filesystem::path(getenv("HOME")) / ".local" / "share" / "vulkan" /
        "implicit_layer.d";
    std::filesystem::create_directories(vulkanLayerDir);
    std::filesystem::path jsonTarget =
        vulkanLayerDir / "VkLayer_RSJFW_RsjfwLayer.json";

    std::ofstream jsonFile(jsonTarget);
    if (jsonFile.good()) {
      jsonFile << manifestJson;
      jsonFile.close();
    }

    // Cleanup: Remove cloned repo after successful build
    cb(0.95f, "Cleaning up...");
    try {
      std::filesystem::remove_all(buildRoot);
    } catch (...) {
      // Ignore cleanup errors
    }

    cb(1.0f, "Build Complete! Log: " + logFile.string());
  } else {
    cb(0.0f, "Build Artifact Missing - Check " + logFile.string());
  }
}

} // namespace rsjfw
