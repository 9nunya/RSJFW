#include "rsjfw/launcher.hpp"
#include "rsjfw/config.hpp"
#include "rsjfw/diagnostics.hpp"
#include "rsjfw/downloader.hpp"
#include "rsjfw/dxvk.hpp"
#include "rsjfw/http.hpp"
#include "rsjfw/logger.hpp"
#include "rsjfw/registry.hpp"
#include "rsjfw/wine.hpp"
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>
#include <signal.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

namespace rsjfw {

Launcher::Launcher(const std::string &rootDir) : rootDir_(rootDir) {
  versionsDir_ = (std::filesystem::path(rootDir) / "versions").string();
  prefixDir_ = (std::filesystem::path(rootDir) / "prefix").string();
  compatDataDir_ = (std::filesystem::path(rootDir) / "compatdata").string();

  std::filesystem::create_directories(prefixDir_);
}

bool Launcher::setupPrefix(ProgressCb progressCb) {
  if (progressCb)
    progressCb(0.0f, "Initializing Wine Prefix...");
  auto &genCfg = Config::instance().getGeneral();

  bool isProton = (genCfg.wineSource.repo.find("proton") != std::string::npos ||
                   genCfg.wineSource.repo == "GE-PROTON" ||
                   genCfg.wineSource.repo == "CACHY-PROTON");

  // For Proton: use compatdata/pfx as prefix, for Wine: use prefix directly
  std::string winePrefix =
      isProton ? (std::filesystem::path(compatDataDir_) / "pfx").string()
               : prefixDir_;

  if (isProton) {
    // Create the compatdata directory structure for Proton
    std::filesystem::create_directories(compatDataDir_);
    std::filesystem::create_directories(std::filesystem::path(compatDataDir_) /
                                        "pfx");
  }

  rsjfw::wine::Prefix pfx(genCfg.wineRoot, winePrefix);
  configureEnvironment(pfx, isProton);

  std::filesystem::path marker =
      std::filesystem::path(prefixDir_) / ".rsjfw_setup_complete";
  if (std::filesystem::exists(marker)) {
    LOG_INFO("Prefix setup already complete. Skipping.");
    if (progressCb)
      progressCb(1.0f, "Prefix Ready.");
    return true;
  }

  LOG_INFO("Setting up Wine prefix registry...");
  if (progressCb)
    progressCb(-1.0f, "Applying Registry Keys...");

  std::vector<rsjfw::wine::Prefix::RegistryEntry> entries = {
      {"HKEY_CURRENT_USER\\Software\\Wine\\WineDbg", "ShowCrashDialog", "0",
       "REG_DWORD"},
      {"HKEY_CURRENT_USER\\Software\\Wine\\X11 Driver", "UseEGL", "Y",
       "REG_SZ"},
      {"HKEY_CURRENT_USER\\Software\\Wine\\DllOverrides", "dxgi", "native",
       "REG_SZ"},
      {"HKEY_CURRENT_USER\\Software\\Wine\\DllOverrides", "d3d11", "native",
       "REG_SZ"},
      {"HKEY_CURRENT_USER\\Software\\Wine\\DllOverrides", "mscoree", "",
       "REG_SZ"},
      {"HKEY_CURRENT_USER\\Software\\Wine\\DllOverrides", "mshtml", "",
       "REG_SZ"},
      {"HKEY_CURRENT_USER\\Software\\Wine\\DllOverrides", "winemenubuilder.exe",
       "", "REG_SZ"},
      {"HKEY_CLASSES_ROOT\\http\\shell\\open\\command", "",
       "\"C:\\windows\\system32\\winebrowser.exe\" \"%1\"", "REG_SZ"},
      {"HKEY_CLASSES_ROOT\\https\\shell\\open\\command", "",
       "\"C:\\windows\\system32\\winebrowser.exe\" \"%1\"", "REG_SZ"}};

  if (progressCb)
    progressCb(-1.0f, "Checking Credentials...");
  Registry reg(pfx);
  bool keyExists =
      reg.exists("HKCU\\Software\\Wine\\Credential Manager", "EncryptionKey");

  if (!keyExists) {
    LOG_INFO("Generating Wine Credential Manager EncryptionKey...");
    if (progressCb)
      progressCb(-1.0f, "Generating Encryption Key...");

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 255);

    std::stringstream ss;
    for (int i = 0; i < 8; ++i) {
      ss << std::hex << std::setfill('0') << std::setw(2) << dis(gen)
         << (i < 7 ? "," : "");
    }

    entries.push_back({"HKEY_CURRENT_USER\\Software\\Wine\\Credential Manager",
                       "EncryptionKey", ss.str(), "REG_BINARY"});
  }

  if (!pfx.registryApply(entries)) {
    LOG_ERROR("Failed to apply registry batch.");
    return false;
  }

  std::ofstream(marker).close();
  if (progressCb)
    progressCb(1.0f, "Registry Setup Complete.");
  return true;
}

// Terminates all running Wine processes within the prefix
bool Launcher::killStudio() {
  LOG_INFO("Killing all Studio processes in prefix...");
  auto &genCfg = Config::instance().getGeneral();

  bool isProton = (genCfg.wineSource.repo.find("proton") != std::string::npos ||
                   genCfg.wineSource.repo == "GE-PROTON" ||
                   genCfg.wineSource.repo == "CACHY-PROTON");

  // For Proton: use compatdata/pfx as prefix, for Wine: use prefix directly
  std::string winePrefix =
      isProton ? (std::filesystem::path(compatDataDir_) / "pfx").string()
               : prefixDir_;

  rsjfw::wine::Prefix pfx(genCfg.wineRoot, winePrefix);
  configureEnvironment(pfx, isProton);
  return pfx.kill();
}

// Opens the Wine configuration tool
bool Launcher::openWineConfiguration() {
  LOG_INFO("Opening Wine Configuration...");
  return runWine("winecfg");
}

// Installs DXVK globally into the prefix
bool Launcher::setupDxvk(const std::string &versionGUID,
                         ProgressCb progressCb) {
  bool useDxvk = Config::instance().getGeneral().dxvk;
  if (!useDxvk)
    return true;

  auto &genCfg = Config::instance().getGeneral();
  std::string dxvkRoot = "";

  if (genCfg.dxvkSource.repo == "CUSTOM_PATH") {
    dxvkRoot = genCfg.dxvkCustomPath;
  } else {
    if (!genCfg.dxvkSource.installedRoot.empty() &&
        std::filesystem::exists(genCfg.dxvkSource.installedRoot)) {
      dxvkRoot = genCfg.dxvkSource.installedRoot;
    } else {
      std::filesystem::path defaultDxvk =
          std::filesystem::path(rootDir_) / "dxvk";
      if (std::filesystem::exists(defaultDxvk)) {

        for (const auto &entry :
             std::filesystem::directory_iterator(defaultDxvk)) {
          if (entry.is_directory()) {
            dxvkRoot = entry.path().string();
            break;
          }
        }
      }
    }
  }

  if (dxvkRoot.empty() || !std::filesystem::exists(dxvkRoot)) {
    LOG_INFO("DXVK root not found. Attempting to download default DXVK...");

    if (progressCb)
      progressCb(0.0f, "Downloading DXVK...");

    rsjfw::Downloader downloader(rootDir_);

    bool success = downloader.installDxvk(
        genCfg.dxvkSource.repo, genCfg.dxvkSource.version,
        genCfg.dxvkSource.asset,
        [&](const std::string &status, float p, size_t, size_t) {
          if (progressCb)
            progressCb(p, status);
        });

    if (success) {
      dxvkRoot = Config::instance().getGeneral().dxvkSource.installedRoot;
    } else {
      LOG_ERROR("Failed to download DXVK.");
      return false;
    }
  }

  rsjfw::wine::Prefix pfx(genCfg.wineRoot, prefixDir_);
  bool dxvkInstallSuccess = rsjfw::dxvk::install(pfx, dxvkRoot);
  if (!dxvkInstallSuccess) {
    LOG_ERROR("Failed to install DXVK.");
    return false;
  }

  LOG_INFO("DXVK setup complete.");
  return true;
}

// Finds the latest installed version and launches it
bool Launcher::launchLatest(const std::vector<std::string> &extraArgs,
                            ProgressCb progressCb, OutputCb outputCb,
                            bool wait) {
  if (!std::filesystem::exists(versionsDir_)) {
    LOG_ERROR("Versions directory not found.");
    return false;
  }

  std::vector<std::string> versions;
  for (const auto &entry : std::filesystem::directory_iterator(versionsDir_)) {
    if (entry.is_directory()) {
      std::string fname = entry.path().filename().string();
      if (fname.find("version-") == 0) {
        versions.push_back(fname);
      }
    }
  }

  if (versions.empty()) {
    LOG_ERROR("No Roblox Studio versions found.");
    return false;
  }

  std::sort(versions.begin(), versions.end());
  std::string latestVersion = versions.back();

  LOG_INFO("Launching latest version: " + latestVersion);

  setupFFlags(latestVersion, progressCb);
  setupDxvk(latestVersion, progressCb);
  return launchVersion(latestVersion, extraArgs, progressCb, outputCb, wait);
}

std::string Launcher::findStudioExecutable(const std::string &versionDir) {
  std::vector<std::filesystem::path> searchDirs;
  searchDirs.push_back(std::filesystem::path(versionsDir_) / versionDir);

  const char *home = getenv("HOME");
  if (home) {
    std::filesystem::path defaultVersions = std::filesystem::path(home) /
                                            ".local/share/rsjfw/versions" /
                                            versionDir;
    if (std::filesystem::absolute(defaultVersions) !=
        std::filesystem::absolute(searchDirs[0])) {
      searchDirs.push_back(defaultVersions);
    }
  }

  for (const auto &dir : searchDirs) {
    LOG_DEBUG("Checking for Studio executable in: " + dir.string());

    if (std::filesystem::exists(dir / "RobloxStudioBeta.exe")) {
      LOG_DEBUG("Found RobloxStudioBeta.exe at " + dir.string());
      return (dir / "RobloxStudioBeta.exe").string();
    }
    if (std::filesystem::exists(dir / "RobloxStudio.exe")) {
      LOG_DEBUG("Found RobloxStudio.exe at " + dir.string());
      return (dir / "RobloxStudio.exe").string();
    }
  }

  LOG_WARN("No Studio executable found in any of the search locations.");
  return "";
}

bool Launcher::setupFFlags(const std::string &versionGUID,
                           ProgressCb progressCb) {
  LOG_INFO("Setting up FFlags for " + versionGUID);
  std::filesystem::path settingsDir =
      std::filesystem::path(versionsDir_) / versionGUID / "ClientSettings";
  std::filesystem::create_directories(settingsDir);

  std::filesystem::path jsonPath = settingsDir / "ClientAppSettings.json";

  nlohmann::json fflags = Config::instance().getFFlags();

  std::ofstream file(jsonPath);
  if (file.is_open()) {
    file << fflags.dump(4);
    return true;
  }
  return false;
}

bool Launcher::launchVersion(const std::string &versionGUID,
                             const std::vector<std::string> &extraArgs,
                             ProgressCb progressCb, OutputCb outputCb,
                             bool wait) {
  std::string exe = findStudioExecutable(versionGUID);
  if (exe.empty()) {
    LOG_ERROR("Could not find Roblox Studio executable in " + versionGUID);
    return false;
  }

  LOG_INFO("Launching version " + versionGUID);
  return runWine(exe, extraArgs, outputCb, wait);
}

// Executes a command using wine with the configured environment
bool Launcher::runWine(const std::string &executablePath,
                       const std::vector<std::string> &args, OutputCb outputCb,
                       bool wait) {
  auto &genCfg = Config::instance().getGeneral();

  if (genCfg.wineSource.installedRoot.empty() &&
      genCfg.wineSource.repo != "SYSTEM" &&
      genCfg.wineSource.repo != "CUSTOM_PATH") {
    std::string folderPattern =
        (genCfg.wineSource.repo.find("vinegar") != std::string::npos)
            ? "wine-"
            : "GE-Proton";
    std::string version = genCfg.wineSource.version;
    std::filesystem::path wineDir = std::filesystem::path(rootDir_) / "wine";
    if (std::filesystem::exists(wineDir)) {
      for (const auto &entry : std::filesystem::directory_iterator(wineDir)) {
        std::string fname = entry.path().filename().string();
        if (entry.is_directory() &&
            (fname.find(folderPattern) != std::string::npos ||
             genCfg.wineSource.repo == "CUSTOM")) {
          if (version != "latest" && fname.find(version) == std::string::npos)
            continue;

          std::filesystem::path binCheck = entry.path() / "bin/wine";
          if (!std::filesystem::exists(binCheck))
            binCheck = entry.path() / "files/bin/wine";

          if (std::filesystem::exists(binCheck)) {
            genCfg.wineSource.installedRoot = entry.path().string();
            Config::instance().save();
            std::cout << "[RSJFW] Discovered wineRoot: "
                      << genCfg.wineSource.installedRoot << "\n";
            break;
          }
        }
      }
    }
  }

  bool isProton = (genCfg.wineSource.repo.find("proton") != std::string::npos ||
                   genCfg.wineSource.repo == "GE-PROTON" ||
                   genCfg.wineSource.repo == "CACHY-PROTON");

  // For Proton: use compatdata/pfx as prefix, for Wine: use prefix directly
  std::string winePrefix =
      isProton ? (std::filesystem::path(compatDataDir_) / "pfx").string()
               : prefixDir_;

  if (isProton) {
    // Ensure compatdata structure exists
    std::filesystem::create_directories(compatDataDir_);
    std::filesystem::create_directories(winePrefix);
  }

  rsjfw::wine::Prefix pfx(genCfg.wineSource.installedRoot, winePrefix);

  bool wineValid = false;
  if (!genCfg.wineSource.installedRoot.empty()) {
    bool rootIsProton = std::filesystem::exists(
        std::filesystem::path(genCfg.wineSource.installedRoot) / "proton");
    bool wantProton = isProton; // Reuse flag

    if (wantProton) {
      if (rootIsProton &&
          std::filesystem::exists(
              std::filesystem::path(genCfg.wineSource.installedRoot) /
              "files/bin/wine")) {
        wineValid = true;
      }
    } else {
      if (!rootIsProton && std::filesystem::exists(pfx.bin("wine"))) {
        wineValid = true;
      }
    }
  }

  if (!wineValid && genCfg.wineSource.repo != "SYSTEM" &&
      genCfg.wineSource.repo != "CUSTOM_PATH") {
    LOG_INFO("Wine root invalid or missing. Attempting repair...");

    rsjfw::Downloader downloader(rootDir_);
    std::string version = genCfg.wineSource.version;

    bool success = downloader.installWine(
        genCfg.wineSource.repo, version, genCfg.wineSource.asset,
        [](const std::string &, float, size_t, size_t) {});
    if (success) {
      // Reload config
      genCfg.wineSource.installedRoot =
          Config::instance().getGeneral().wineSource.installedRoot;
      // Update prefix object with new root
      pfx = rsjfw::wine::Prefix(genCfg.wineSource.installedRoot, winePrefix);
      wineValid = true;
      LOG_INFO("Wine repaired successfully.");
    } else {
      LOG_ERROR("Failed to repair Wine installation.");
      return false;
    }
  }

  configureEnvironment(pfx, isProton);

  bool isProtocol = false;
  for (const auto &arg : args) {
    if (arg.find("roblox-studio:") != std::string::npos ||
        arg.find("roblox-studio-auth:") != std::string::npos) {
      isProtocol = true;
    }
  }

  if (!isProtocol) {
    pfx.kill();
  }

  auto &wineCfg = Config::instance().getWine();
  std::string resolution = wineCfg.desktopResolution;
  // Sanitize resolution
  resolution.erase(std::remove(resolution.begin(), resolution.end(), ' '),
                   resolution.end());
  if (resolution.empty())
    resolution = "1920x1080";

  std::vector<std::string> launchArgs;
  std::string target;

  if (wineCfg.multipleDesktops || wineCfg.desktopMode) {
    target = "explorer";
    std::string d =
        "/desktop=RSJFW_" +
        (wineCfg.multipleDesktops ? std::to_string(getpid()) : "Desktop") +
        "," + resolution;
    launchArgs.push_back(d);
    // Auto-fix: Ensure RSJFW Layer is present
    auto &diag = Diagnostics::instance();
    diag.runChecks(); // Update status

    // We specifically care about "RSJFW Layer" for launch
    bool layerOk = false;
    for (const auto &res : diag.getResults()) {
      if (res.first == "RSJFW Layer") {
        layerOk = res.second.ok;
        break;
      }
    }

    if (!layerOk) {
      LOG_WARN("RSJFW Layer missing. Attempting auto-fix...");
      diag.fixIssue("RSJFW Layer",
                    nullptr); // Synchronous fix (nullptr callback)
    }

    launchArgs.push_back(executablePath);
  } else {
    target = executablePath;
  }

  for (const auto &a : args)
    launchArgs.push_back(a);

  std::string argStr;
  for (const auto &a : launchArgs)
    argStr += "\"" + a + "\" ";
  LOG_INFO("Wine execution target: " + target);
  LOG_INFO("Wine execution args: " + argStr);

  LOG_DEBUG("WINEDLLOVERRIDES: " + pfx.getEnv("WINEDLLOVERRIDES"));
  LOG_DEBUG("VK_LOADER_LAYERS_ENABLE: " +
            pfx.getEnv("VK_LOADER_LAYERS_ENABLE"));
  LOG_DEBUG("WINEPREFIX: " + winePrefix);
  if (isProton)
    LOG_DEBUG("STEAM_COMPAT_DATA_PATH: " +
              pfx.getEnv("STEAM_COMPAT_DATA_PATH"));

  std::cout << "[RSJFW] Launching: " << executablePath
            << (target == "explorer" ? " (Desktop Mode)" : "") << "\n";

  std::string logDir = std::string(getenv("HOME")) + "/.local/share/rsjfw/logs";
  std::filesystem::create_directories(logDir);
  std::string logPath = logDir + "/studio_latest.log";

  std::shared_ptr<std::ofstream> logFile =
      std::make_shared<std::ofstream>(logPath);

  std::string studioCwd =
      std::filesystem::path(executablePath).parent_path().string();

  return pfx.wine(
      target, launchArgs,
      [logFile, outputCb](const std::string &line) {
        std::cout << line;

        if (logFile && logFile->is_open())
          *logFile << line;

        if (outputCb)
          outputCb(line);

        std::string rawLine = line;
        if (!rawLine.empty() && rawLine.back() == '\n')
          rawLine.pop_back();
        if (!rawLine.empty()) {
          LOG_INFO("[WINE] " + rawLine);
        }

        if (line.find("Fatal exiting due to Trouble launching Studio") !=
            std::string::npos) {
          std::cerr << "\n[RSJFW] Fatal error detected. Aborting.\n";
        }
      },
      studioCwd, wait);
}

void Launcher::configureEnvironment(rsjfw::wine::Prefix &pfx, bool isProton) {
  auto &genCfg = Config::instance().getGeneral();

  if (!isProton) {
    std::filesystem::path wineBinPath = std::filesystem::path(pfx.bin("wine"));
    if (std::filesystem::exists(wineBinPath)) {
      std::string binDir = wineBinPath.parent_path().string();
      std::string currentPath = getenv("PATH") ? getenv("PATH") : "";
      pfx.appendEnv("PATH", binDir + ":" + currentPath);
    }
  }

  pfx.appendEnv("WINEESYNC", "1");
  pfx.appendEnv("SDL_VIDEODRIVER", "x11");
  pfx.appendEnv("VK_LOADER_LAYERS_ENABLE", "VK_LAYER_RSJFW_RsjfwLayer");

  if (genCfg.selectedGpu >= 0) {
    pfx.appendEnv("DRI_PRIME", std::to_string(genCfg.selectedGpu));
  }

  for (const auto &[key, val] : genCfg.customEnv) {
    if (!key.empty())
      pfx.appendEnv(key, val);
  }

  if (getenv("DBUS_SESSION_BUS_ADDRESS")) {
    pfx.appendEnv("DBUS_SESSION_BUS_ADDRESS",
                  getenv("DBUS_SESSION_BUS_ADDRESS"));
  }

  if (isProton) {
    // STEAM_COMPAT_DATA_PATH points to the compatdata directory (NOT prefix)
    pfx.appendEnv("STEAM_COMPAT_DATA_PATH", compatDataDir_);

    // Ensure this path exists or Proton might complain
    std::filesystem::path steamRoot =
        std::string(getenv("HOME")) + "/.steam/steam";
    if (std::filesystem::exists(steamRoot)) {
      pfx.appendEnv("STEAM_COMPAT_CLIENT_INSTALL_PATH", steamRoot.string());
    } else {
      pfx.appendEnv("STEAM_COMPAT_CLIENT_INSTALL_PATH", "/usr/lib/steam");
    }
  }

  std::string winedebug = "";
  if (debug_) {
    winedebug = "warn+all,err+all,fixme+all,+debugstr";
  } else {
    winedebug = "-all";
  }
  pfx.appendEnv("WINEDEBUG", winedebug);

  std::string dllOverrides = "dxdiagn=;winemenubuilder.exe=;mscoree=;mshtml=;"
                             "gameoverlayrenderer=;gameoverlayrenderer64=;";
  bool useDxvk = Config::instance().getGeneral().dxvk;
  if (useDxvk) {
    dllOverrides = "dxgi,d3d11,d3d10core,d3d9=n,b;" + dllOverrides;
  }

  pfx.appendEnv("WINEDLLOVERRIDES", dllOverrides);
}

} // namespace rsjfw
