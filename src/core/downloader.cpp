#include "rsjfw/downloader.hpp"
#include "rsjfw/config.hpp"
#include "rsjfw/http.hpp"
#include "rsjfw/logger.hpp"
#include "rsjfw/path_manager.hpp"
#include "rsjfw/task_runner.hpp"
#include "rsjfw/zip_util.hpp"
#include <algorithm>
#include <condition_variable>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <nlohmann/json.hpp>
#include <queue>
#include <set>
#include <unordered_map>

namespace rsjfw {

Downloader::Downloader(const std::string &rootDir) {
  auto &pathMgr = PathManager::instance();
  rootDir_ = pathMgr.root().string();
  versionsDir_ = pathMgr.versions().string();
  downloadsDir_ = pathMgr.downloads().string();
}

std::string Downloader::getLatestVersionGUID() {
  auto &cfg = Config::instance().getGeneral();

  if (!cfg.robloxVersion.empty()) {
    std::cout << "[RSJFW] Using version override: " << cfg.robloxVersion
              << "\n";
    return cfg.robloxVersion;
  }

  return RobloxAPI::getLatestVersionGUID(cfg.channel);
}

std::vector<std::string> Downloader::getInstalledVersions() {
  std::vector<std::string> versions;
  if (!std::filesystem::exists(versionsDir_))
    return versions;

  for (const auto &entry : std::filesystem::directory_iterator(versionsDir_)) {
    if (entry.is_directory()) {
      std::string name = entry.path().filename().string();
      if (isVersionInstalled(name)) {
        versions.push_back(name);
      }
    }
  }

  std::sort(versions.rbegin(), versions.rend());
  return versions;
}

bool Downloader::installLatest(ProgressCallback callback) {
  try {
    std::string latest = RobloxAPI::getLatestVersionGUID();
    std::cout << "[RSJFW] Latest version: " << latest << "\n";
    return installVersion(latest, callback);
  } catch (const std::exception &e) {
    std::cerr << "[RSJFW] Error fetching latest version: " << e.what() << "\n";
    return false;
  }
}
bool Downloader::isVersionInstalled(const std::string &versionGUID) {
  if (versionGUID.empty())
    return false;
  std::string installDir =
      (std::filesystem::path(versionsDir_) / versionGUID).string();
  // Basic check: directory exists and has AppSettings.xml (indicates successful
  // post-install)
  return std::filesystem::exists(installDir) &&
         std::filesystem::exists(std::filesystem::path(installDir) /
                                 "AppSettings.xml");
}

bool Downloader::installVersion(const std::string &versionGUID,
                                ProgressCallback callback) {
  try {
    auto packages = RobloxAPI::getPackageManifest(versionGUID);
    std::cout << "[RSJFW] Found " << packages.size()
              << " packages to install.\n";

    std::string installDir =
        (std::filesystem::path(versionsDir_) / versionGUID).string();
    if (std::filesystem::exists(installDir)) {
      std::cout << "[RSJFW] Version " << versionGUID << " already installed.\n";
      if (callback)
        callback("Already installed", 1.0f, packages.size(), packages.size());
      return true;
    }

    std::filesystem::create_directories(installDir);

    static const std::unordered_map<std::string, std::string> packageMap = {
        {"ApplicationConfig.zip", "ApplicationConfig/"},
        {"redist.zip", ""},
        {"RobloxStudio.zip", ""},
        {"Libraries.zip", ""},
        {"content-avatar.zip", "content/avatar/"},
        {"content-configs.zip", "content/configs/"},
        {"content-fonts.zip", "content/fonts/"},
        {"content-sky.zip", "content/sky/"},
        {"content-sounds.zip", "content/sounds/"},
        {"content-textures2.zip", "content/textures/"},
        {"content-studio_svg_textures.zip", "content/studio_svg_textures/"},
        {"content-models.zip", "content/models/"},
        {"content-textures3.zip", "PlatformContent/pc/textures/"},
        {"content-terrain.zip", "PlatformContent/pc/terrain/"},
        {"content-platform-fonts.zip", "PlatformContent/pc/fonts/"},
        {"content-platform-dictionaries.zip",
         "PlatformContent/pc/shared_compression_dictionaries/"},
        {"content-qt_translations.zip", "content/qt_translations/"},
        {"content-api-docs.zip", "content/api_docs/"},
        {"extracontent-scripts.zip", "ExtraContent/scripts/"},
        {"extracontent-luapackages.zip", "ExtraContent/LuaPackages/"},
        {"extracontent-translations.zip", "ExtraContent/translations/"},
        {"extracontent-models.zip", "ExtraContent/models/"},
        {"extracontent-textures.zip", "ExtraContent/textures/"},
        {"studiocontent-models.zip", "StudioContent/models/"},
        {"studiocontent-textures.zip", "StudioContent/textures/"},
        {"shaders.zip", "shaders/"},
        {"BuiltInPlugins.zip", "BuiltInPlugins/"},
        {"BuiltInStandalonePlugins.zip", "BuiltInStandalonePlugins/"},
        {"LibrariesQt5.zip", ""},
        {"Plugins.zip", "Plugins/"},
        {"RibbonConfig.zip", "RibbonConfig/"},
        {"StudioFonts.zip", "StudioFonts/"},
        {"ssl.zip", "ssl/"}};

    std::mutex queueMutex;
    std::condition_variable cv;
    std::queue<size_t> packageQueue;
    for (size_t i = 0; i < packages.size(); ++i)
      packageQueue.push(i);

    std::atomic<int> completedPackages{0};
    std::atomic<bool> failed{false};
    std::mutex callbackMutex;

    const int numThreads = 4;
    std::vector<std::jthread> workers;

    for (int t = 0; t < numThreads; ++t) {
      workers.emplace_back([&]() {
        while (true) {
          size_t pkgIdx;
          {
            std::unique_lock<std::mutex> lock(queueMutex);
            if (packageQueue.empty() || failed)
              return;
            pkgIdx = packageQueue.front();
            packageQueue.pop();
          }

          const auto &pkg = packages[pkgIdx];

          {
            std::lock_guard<std::mutex> lock(callbackMutex);
            if (callback)
              callback(pkg.name, 0.0f, completedPackages, packages.size());
          }

          bool success =
              downloadPackage(versionGUID, pkg, [&](size_t cur, size_t tot) {
                if (failed)
                  return;
                std::lock_guard<std::mutex> lock(callbackMutex);
                if (callback && tot > 0) {
                  float itemProg = (float)cur / (float)tot;
                  callback(pkg.name, itemProg, completedPackages,
                           packages.size());
                }
              });

          if (!success) {
            failed = true;
            return;
          }

          std::string subDir = ".";
          auto it = packageMap.find(pkg.name);
          if (it != packageMap.end()) {
            subDir = it->second;
            std::replace(subDir.begin(), subDir.end(), '\\', '/');
          }

          std::string pkgPath =
              (std::filesystem::path(downloadsDir_) / pkg.checksum).string();
          std::string destPath =
              (std::filesystem::path(installDir) / subDir).string();

          std::filesystem::create_directories(destPath);
          if (!ZipUtil::extract(pkgPath, destPath)) {
            LOG_ERROR("Failed to extract " + pkg.name);
            failed = true;
            return;
          }

          // Post-extraction Zip Cleanup (As per user strategy)
          std::filesystem::remove(pkgPath);

          completedPackages++;
          {
            std::lock_guard<std::mutex> lock(callbackMutex);
            if (callback)
              callback(pkg.name, 1.0f, completedPackages, packages.size());
          }
        }
      });
    }

    // Wait for workers
    workers.clear(); // std::jthread joins on destruction

    if (failed)
      return false;

    std::vector<std::string> qtSearchPaths = {
        (std::filesystem::path(installDir) / "Qt5").string(),
        (std::filesystem::path(installDir) / "Plugins" / "Qt5").string()};

    for (const auto &searchPath : qtSearchPaths) {
      if (std::filesystem::exists(searchPath)) {
        std::cout << "[RSJFW] Relocating Qt5 plugins from " << searchPath
                  << " to root...\n";
        for (const auto &entry :
             std::filesystem::directory_iterator(searchPath)) {
          std::filesystem::path target =
              std::filesystem::path(installDir) / entry.path().filename();
          if (std::filesystem::exists(target))
            std::filesystem::remove_all(target);
          std::filesystem::rename(entry.path(), target);
        }
        std::filesystem::remove(searchPath);
      }
    }

    // Create AppSettings.xml
    std::filesystem::path appSettingsPath =
        std::filesystem::path(installDir) / "AppSettings.xml";
    std::ofstream ofs(appSettingsPath);
    if (ofs) {
      ofs << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\r\n"
          << "<Settings>\r\n"
          << "        <ContentFolder>content</ContentFolder>\r\n"
          << "        <BaseUrl>http://www.roblox.com</BaseUrl>\r\n"
          << "        <Channel>production</Channel>\r\n"
          << "</Settings>\r\n";
    }

    if (callback)
      callback("Done", 1.0f, packages.size(), packages.size());
    std::cout << "[RSJFW] Successfully installed version " << versionGUID
              << "\n";
    return true;

  } catch (const std::exception &e) {
    std::cerr << "[RSJFW] Error installing version: " << e.what() << "\n";
    return false;
  }
}

bool Downloader::downloadPackage(
    const std::string &versionGUID, const RobloxPackage &pkg,
    std::function<void(size_t, size_t)> progressCb) {
  std::string destPath =
      (std::filesystem::path(downloadsDir_) / pkg.checksum).string();

  if (std::filesystem::exists(destPath)) {
    if (progressCb) {
      size_t sz = std::filesystem::file_size(destPath);
      progressCb(sz, sz);
    }
    return true;
  }

  std::string url = RobloxAPI::BASE_URL + versionGUID + "-" + pkg.name;
  try {
    return HTTP::download(url, destPath, progressCb);
  } catch (const std::exception &e) {
    std::cerr << "[RSJFW] Failed to download package " << pkg.name << ": "
              << e.what() << "\n";
    return false;
  }
}

// Unified GitHub API support (v2.1)
std::vector<Downloader::GitHubRelease>
Downloader::fetchReleases(const std::string &repo) {
  std::vector<GitHubRelease> releases;
  if (repo.empty() || repo == "SYSTEM" || repo == "CUSTOM_PATH")
    return releases;

  std::string url = "https://api.github.com/repos/" + repo + "/releases";
  try {
    std::string response = HTTP::get(url);
    auto j = nlohmann::json::parse(response);
    if (!j.is_array())
      return releases;

    for (const auto &rel : j) {
      GitHubRelease release;
      release.tag = rel["tag_name"];
      for (const auto &asset : rel["assets"]) {
        GitHubAsset ga;
        ga.name = asset["name"];
        ga.url = asset["browser_download_url"];
        ga.size = asset["size"];
        release.assets.push_back(ga);
      }
      releases.push_back(release);
    }
  } catch (...) {
  }
  return releases;
}

bool Downloader::validateRepo(const std::string &repo, std::string &outError) {
  if (repo.empty()) {
    outError = "Repo cannot be empty";
    return false;
  }
  if (repo.find('/') == std::string::npos) {
    outError = "Invalid repo format (user/repo expected)";
    return false;
  }

  std::string url = "https://api.github.com/repos/" + repo;
  try {
    std::string response = HTTP::get(url);
    auto j = nlohmann::json::parse(response);
    if (j.contains("message") && j["message"] == "Not Found") {
      outError = "Repository not found";
      return false;
    }
    return true;
  } catch (const std::exception &e) {
    outError = e.what();
    return false;
  }
}

bool Downloader::deleteRoot(const std::string &path) {
  if (path.empty() || !std::filesystem::exists(path))
    return false;
  try {
    std::filesystem::remove_all(path);
    return true;
  } catch (...) {
    return false;
  }
}

std::vector<Downloader::InstalledRoot> Downloader::getInstalledWineRoots() {
  std::vector<InstalledRoot> roots;
  std::filesystem::path wineDir = std::filesystem::path(rootDir_) / "wine";
  if (!std::filesystem::exists(wineDir))
    return roots;

  for (const auto &entry : std::filesystem::directory_iterator(wineDir)) {
    if (entry.is_directory()) {
      std::filesystem::path bin = entry.path() / "bin/wine";
      if (!std::filesystem::exists(bin))
        bin = entry.path() / "files/bin/wine";

      if (std::filesystem::exists(bin)) {
        InstalledRoot ir;
        ir.name = entry.path().filename().string();
        ir.path = entry.path().string();
        ir.isProton = std::filesystem::exists(entry.path() / "proton");

        size_t total = 0;
        try {
          for (const auto &p : std::filesystem::recursive_directory_iterator(
                   entry.path(), std::filesystem::directory_options::
                                     skip_permission_denied)) {
            if (p.is_regular_file())
              total += p.file_size();
          }
        } catch (...) {
        }
        ir.sizeBytes = total;

        // Try to read metadata
        try {
          std::filesystem::path metaPath = entry.path() / "rsjfw_meta.json";
          if (std::filesystem::exists(metaPath)) {
            std::ifstream ifs(metaPath);
            auto meta = nlohmann::json::parse(ifs);
            ir.repo = meta.value("repo", "");
            ir.version = meta.value("tag", "");
            ir.asset = meta.value("asset", "");
          }
        } catch (...) {
        }

        roots.push_back(ir);
      }
    }
  }
  return roots;
}

std::vector<Downloader::InstalledRoot> Downloader::getInstalledDxvkRoots() {
  std::vector<InstalledRoot> roots;
  std::filesystem::path dxvkDir = std::filesystem::path(rootDir_) / "dxvk";
  if (!std::filesystem::exists(dxvkDir))
    return roots;

  for (const auto &entry : std::filesystem::directory_iterator(dxvkDir)) {
    if (entry.is_directory()) {
      if (std::filesystem::exists(entry.path() / "x64") ||
          std::filesystem::exists(entry.path() / "x86")) {
        InstalledRoot ir;
        ir.name = entry.path().filename().string();
        ir.path = entry.path().string();
        ir.isProton = false;

        size_t total = 0;
        try {
          for (const auto &p : std::filesystem::recursive_directory_iterator(
                   entry.path(), std::filesystem::directory_options::
                                     skip_permission_denied)) {
            if (p.is_regular_file())
              total += p.file_size();
          }
        } catch (...) {
        }
        ir.sizeBytes = total;

        // Try to read metadata
        try {
          std::filesystem::path metaPath = entry.path() / "rsjfw_meta.json";
          if (std::filesystem::exists(metaPath)) {
            std::ifstream ifs(metaPath);
            auto meta = nlohmann::json::parse(ifs);
            ir.repo = meta.value("repo", "");
            ir.version = meta.value("tag", "");
            ir.asset = meta.value("asset", "");
          }
        } catch (...) {
        }

        roots.push_back(ir);
      }
    }
  }
  return roots;
}

bool Downloader::installWine(const std::string &repo,
                             const std::string &version,
                             const std::string &assetName,
                             ProgressCallback callback) {
  auto &genCfg = Config::instance().getGeneral();
  std::filesystem::path wineDir = std::filesystem::path(rootDir_) / "wine";

  // Skip if already correct
  if (!genCfg.wineSource.installedRoot.empty() &&
      std::filesystem::exists(
          std::filesystem::path(genCfg.wineSource.installedRoot) /
          "bin/wine")) {
    // ... (Optional: Check version)
  }

  std::string url;
  if (repo == "CUSTOM" && !genCfg.wineCustomUrl.empty()) {
    url = genCfg.wineCustomUrl;
  } else if (repo.find("://") != std::string::npos) {
    url = repo;
  } else {
    // Fetch from GitHub
    auto releases = fetchReleases(repo);
    Downloader::GitHubRelease *targetRel = nullptr;
    if (version == "latest" && !releases.empty()) {
      targetRel = &releases[0];
    } else {
      for (auto &r : releases) {
        if (r.tag == version) {
          targetRel = &r;
          break;
        }
      }
    }

    if (!targetRel) {
      LOG_ERROR("Version " + version + " not found in repo " + repo);
      return false;
    }

    if (!assetName.empty()) {
      for (auto &a : targetRel->assets) {
        if (a.name == assetName) {
          url = a.url;
          break;
        }
      }
    } else {
      // Auto-pick asset: Prefer .tar.xz, then .tar.gz, then .tar
      std::vector<std::string> preferences = {".tar.xz", ".tar.gz", ".tar"};
      for (const auto &pref : preferences) {
        for (auto &a : targetRel->assets) {
          std::string lName = a.name;
          for (auto &c : lName)
            c = tolower(c);
          if (lName.find(pref) != std::string::npos &&
              lName.find(".sha512") == std::string::npos &&
              lName.find(".asc") == std::string::npos &&
              lName.find(".sig") == std::string::npos &&
              lName.find(".sum") == std::string::npos) {
            url = a.url;
            break;
          }
        }
        if (!url.empty())
          break;
      }
    }
  }

  if (url.empty()) {
    LOG_ERROR("Failed to find Wine asset for " + repo + " version " + version);
    return false;
  }

  try {
    std::string filename = url.substr(url.find_last_of('/') + 1);
    if (filename.find('?') != std::string::npos)
      filename = filename.substr(0, filename.find('?'));

    std::filesystem::create_directories(wineDir);
    std::filesystem::path destFile = wineDir / filename;

    if (!std::filesystem::exists(destFile)) {
      if (callback)
        callback("Downloading " + filename + "...", 0.0f, 0, 1);
      if (!HTTP::download(url, destFile.string(), [&](size_t cur, size_t tot) {
            if (callback && tot > 0)
              callback(filename, (float)cur / (float)tot, 0, 1);
          })) {
        throw std::runtime_error("Download failed");
      }
    }
    if (callback)
      callback("Extracting (this may take a moment)...", -1.0f, 0, 1);

    std::set<std::filesystem::path> before;
    for (const auto &entry : std::filesystem::directory_iterator(wineDir)) {
      if (entry.is_directory())
        before.insert(entry.path());
    }

    if (!ZipUtil::extract(destFile.string(), wineDir.string())) {
      throw std::runtime_error("Failed to extract Wine archive");
    }

    // Find the extracted root
    std::string extractedRoot = "";
    for (const auto &entry : std::filesystem::directory_iterator(wineDir)) {
      if (entry.is_directory() && before.find(entry.path()) == before.end()) {
        std::filesystem::path bin = entry.path() / "bin/wine";
        if (!std::filesystem::exists(bin))
          bin = entry.path() / "files/bin/wine";

        if (std::filesystem::exists(bin)) {
          extractedRoot = entry.path().string();
          break;
        }
      }
    }

    if (extractedRoot.empty()) {
      // Fallback
      for (const auto &entry : std::filesystem::directory_iterator(wineDir)) {
        if (entry.is_directory()) {
          std::filesystem::path bin = entry.path() / "bin/wine";
          if (!std::filesystem::exists(bin))
            bin = entry.path() / "files/bin/wine";
          bool matches = entry.path().filename().string().find("wine-") !=
                             std::string::npos ||
                         entry.path().filename().string().find("kombucha-") !=
                             std::string::npos ||
                         entry.path().filename().string().find("GE-Proton") !=
                             std::string::npos ||
                         entry.path().filename().string().find(
                             "proton-cachyos") != std::string::npos;
          if (std::filesystem::exists(bin) && (matches || repo == "CUSTOM")) {
            extractedRoot = entry.path().string();
            break;
          }
        }
      }
    }

    if (!extractedRoot.empty()) {
      // Write Metadata
      try {
        nlohmann::json meta;
        meta["repo"] = repo;
        meta["tag"] = version;
        meta["asset"] = assetName;
        meta["isProton"] = (extractedRoot.find("proton") != std::string::npos);
        std::ofstream ofs(std::filesystem::path(extractedRoot) /
                          "rsjfw_meta.json");
        ofs << meta.dump(2);
      } catch (...) {
      }

      genCfg.wineSource.installedRoot = extractedRoot;
      Config::instance().save();
      if (std::filesystem::exists(destFile))
        std::filesystem::remove(destFile);
      if (callback)
        callback("Wine installed to " + extractedRoot, 1.0f, 1, 1);
      LOG_INFO("Successfully installed Wine to " + extractedRoot);
      return true;
    }

  } catch (const std::exception &e) {
    LOG_ERROR("Wine error: " + std::string(e.what()));
    if (callback)
      callback("Error: " + std::string(e.what()), 0.0f, 1, 1);
  }

  return false;
}

bool Downloader::installDxvk(const std::string &repo,
                             const std::string &version,
                             const std::string &assetName,
                             ProgressCallback callback) {
  auto &genCfg = Config::instance().getGeneral();
  std::filesystem::path dxvkDir = std::filesystem::path(rootDir_) / "dxvk";

  // Check if already installed
  if (genCfg.dxvk && !genCfg.dxvkSource.installedRoot.empty() &&
      std::filesystem::exists(
          std::filesystem::path(genCfg.dxvkSource.installedRoot) /
          "x64/dxgi.dll")) {
    return true;
  }

  std::string url;
  if (repo == "CUSTOM" && !genCfg.dxvkCustomUrl.empty()) {
    url = genCfg.dxvkCustomUrl;
  } else if (repo.find("://") != std::string::npos) {
    url = repo;
  } else {
    // Fetch from GitHub
    auto releases = fetchReleases(repo);
    Downloader::GitHubRelease *targetRel = nullptr;
    if (version == "latest" && !releases.empty()) {
      targetRel = &releases[0];
    } else {
      for (auto &r : releases) {
        if (r.tag == version) {
          targetRel = &r;
          break;
        }
      }
    }

    if (!targetRel) {
      LOG_ERROR("Version " + version + " not found in repo " + repo);
      return false;
    }

    if (!assetName.empty()) {
      for (auto &a : targetRel->assets) {
        if (a.name == assetName) {
          url = a.url;
          break;
        }
      }
    } else {
      // Auto-pick asset: Prefer .tar.gz, avoid .sig/.asc/.sum
      for (auto &a : targetRel->assets) {
        std::string lName = a.name;
        for (auto &c : lName)
          c = tolower(c);
        if (lName.find(".tar.gz") != std::string::npos &&
            lName.find(".sig") == std::string::npos &&
            lName.find(".asc") == std::string::npos &&
            lName.find(".sum") == std::string::npos &&
            lName.find(".sha256") == std::string::npos) {
          url = a.url;
          break;
        }
      }
    }
  }

  if (url.empty()) {
    LOG_ERROR("Failed to find DXVK asset for source " + repo + " version " +
              version);
    return false;
  }

  try {
    std::string filename = url.substr(url.find_last_of('/') + 1);
    std::filesystem::create_directories(dxvkDir);
    std::filesystem::path destFile = dxvkDir / filename;

    if (!std::filesystem::exists(destFile)) {
      if (callback)
        callback("Downloading " + filename + "...", 0.0f, 0, 1);
      if (!HTTP::download(url, destFile.string(), [&](size_t cur, size_t tot) {
            if (callback && tot > 0)
              callback(filename, (float)cur / (float)tot, 0, 1);
          })) {
        throw std::runtime_error("Download failed");
      }
    }

    if (callback)
      callback("Extracting (this may take a moment)...", -1.0f, 0, 1);

    // Track directories before extraction
    std::set<std::filesystem::path> before;
    for (const auto &entry : std::filesystem::directory_iterator(dxvkDir)) {
      if (entry.is_directory())
        before.insert(entry.path());
    }

    if (!ZipUtil::extract(destFile.string(), dxvkDir.string())) {
      throw std::runtime_error("Failed to extract DXVK archive");
    }

    // Find the newly extracted root
    std::string extractedRoot = "";
    for (const auto &entry : std::filesystem::directory_iterator(dxvkDir)) {
      if (entry.is_directory() && before.find(entry.path()) == before.end()) {
        if (std::filesystem::exists(entry.path() / "x64") ||
            std::filesystem::exists(entry.path() / "x86")) {
          extractedRoot = entry.path().string();
          break;
        }
      }
    }

    // Fallback: If no new directory found, look for ANY matching one (might
    // happen if we re-extract over same folder)
    if (extractedRoot.empty()) {
      for (const auto &entry : std::filesystem::directory_iterator(dxvkDir)) {
        if (entry.is_directory()) {
          if (std::filesystem::exists(entry.path() / "x64") ||
              std::filesystem::exists(entry.path() / "x86")) {
            extractedRoot = entry.path().string();
            break;
          }
        }
      }
    }

    if (!extractedRoot.empty()) {
      // Write Metadata
      try {
        nlohmann::json meta;
        meta["repo"] = repo;
        meta["tag"] = version;
        meta["asset"] = assetName;
        meta["isProton"] = false;
        std::ofstream ofs(std::filesystem::path(extractedRoot) /
                          "rsjfw_meta.json");
        ofs << meta.dump(2);
      } catch (...) {
      }

      genCfg.dxvkSource.installedRoot =
          extractedRoot; // Standardize on dxvkRoot
      Config::instance().save();
      std::filesystem::remove(destFile);
      LOG_INFO("Successfully installed DXVK to " + extractedRoot);
      if (callback)
        callback("DXVK installed to " + extractedRoot, 1.0f, 1, 1);
      return true;
    }

  } catch (const std::exception &e) {
    LOG_ERROR("DXVK error: " + std::string(e.what()));
    if (callback)
      callback("Error: " + std::string(e.what()), 0.0f, 1, 1);
  }
  return false;
}

// DELETED fetchWineVersions and fetchDxvkVersions
} // namespace rsjfw
