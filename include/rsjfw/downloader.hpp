#ifndef RSJFW_DOWNLOADER_HPP
#define RSJFW_DOWNLOADER_HPP

#include "rsjfw/config.hpp"
#include "rsjfw/roblox_api.hpp"
#include <functional>
#include <string>
#include <vector>

namespace rsjfw {

class Downloader {
public:
  Downloader(const std::string &rootDir);

  using ProgressCallback =
      std::function<void(const std::string &currentItem, float itemProgress,
                         size_t itemIndex, size_t totalItems)>;
  bool installLatest(ProgressCallback callback = nullptr);
  bool isVersionInstalled(const std::string &versionGUID);
  bool installVersion(const std::string &versionGUID,
                      ProgressCallback callback = nullptr);

  // v2.1: Unified GitHub API support
  struct GitHubAsset {
    std::string name;
    std::string url;
    size_t size;
  };
  struct GitHubRelease {
    std::string tag;
    std::vector<GitHubAsset> assets;
    bool isNative; // e.g., contains 'wine-' or 'proton-' but in a specific way?
  };

  // Installation methods
  bool installWine(const std::string &repo,
                   const std::string &version = "latest",
                   const std::string &assetName = "",
                   ProgressCallback callback = nullptr);
  bool installDxvk(const std::string &repo,
                   const std::string &version = "latest",
                   const std::string &assetName = "",
                   ProgressCallback callback = nullptr);

  // GitHub API interactions
  std::vector<GitHubRelease> fetchReleases(const std::string &repo);
  bool validateRepo(const std::string &repo, std::string &outError);

  // Management
  struct InstalledRoot {
    std::string name;
    std::string path;
    size_t sizeBytes;
    bool isProton;

    // Metadata for UI Sync
    std::string repo;
    std::string version;
    std::string asset;
  };
  std::vector<InstalledRoot> getInstalledWineRoots();
  std::vector<InstalledRoot> getInstalledDxvkRoots();
  bool deleteRoot(const std::string &path);

  std::string getLatestVersionGUID();
  std::vector<std::string> getInstalledVersions();

private:
  std::string rootDir_;
  std::string versionsDir_;
  std::string downloadsDir_;

  std::string downloadLatestRobloxStudio(const std::string &versionGUID);

  bool downloadPackage(const std::string &versionGUID, const RobloxPackage &pkg,
                       std::function<void(size_t, size_t)> progressCb);
  std::string extractArchive(const std::string &archivePath,
                             const std::string &destDir,
                             ProgressCallback callback);
};

} // namespace rsjfw

#endif // RSJFW_DOWNLOADER_HPP
