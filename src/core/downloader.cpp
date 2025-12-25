#include "rsjfw/downloader.hpp"
#include "rsjfw/http.hpp"
#include "rsjfw/zip_util.hpp"
#include "rsjfw/config.hpp"
#include "rsjfw/logger.hpp"
#include <filesystem>
#include <iostream>
#include <fstream>
#include <unordered_map>
#include <algorithm>
#include <cstdio>
#include <nlohmann/json.hpp>

namespace rsjfw {

Downloader::Downloader(const std::string& rootDir) 
    : rootDir_(rootDir) {
    versionsDir_ = (std::filesystem::path(rootDir) / "versions").string();
    downloadsDir_ = (std::filesystem::path(rootDir) / "downloads").string();
    
    std::filesystem::create_directories(versionsDir_);
    std::filesystem::create_directories(downloadsDir_);
}

std::string Downloader::getLatestVersionGUID() {
    auto& cfg = Config::instance().getGeneral();
    
    if (!cfg.robloxVersion.empty()) {
        std::cout << "[RSJFW] Using version override: " << cfg.robloxVersion << "\n";
        return cfg.robloxVersion;
    }

    return RobloxAPI::getLatestVersionGUID(cfg.channel);
}

bool Downloader::installLatest(ProgressCallback callback) {
    try {
        std::string latest = RobloxAPI::getLatestVersionGUID();
        std::cout << "[RSJFW] Latest version: " << latest << "\n";
        return installVersion(latest, callback);
    } catch (const std::exception& e) {
        std::cerr << "[RSJFW] Error fetching latest version: " << e.what() << "\n";
        return false;
    }
}

bool Downloader::installVersion(const std::string& versionGUID, ProgressCallback callback) {
    try {
        auto packages = RobloxAPI::getPackageManifest(versionGUID);
        std::cout << "[RSJFW] Found " << packages.size() << " packages to install.\n";
        
        std::string installDir = (std::filesystem::path(versionsDir_) / versionGUID).string();
        if (std::filesystem::exists(installDir)) {
            std::cout << "[RSJFW] Version " << versionGUID << " already installed.\n";
            if (callback) callback("Already installed", 1.0f, packages.size(), packages.size());
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
            {"content-platform-dictionaries.zip", "PlatformContent/pc/shared_compression_dictionaries/"},
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
            {"ssl.zip", "ssl/"}
        };

        for (size_t i = 0; i < packages.size(); ++i) {
            const auto& pkg = packages[i];
            
            // Notify start of package
            if (callback) callback(pkg.name, 0.0f, i, packages.size());
            
            std::cout << "[RSJFW] Installing " << pkg.name << "...\n";
            bool success = downloadPackage(versionGUID, pkg, [&](size_t cur, size_t tot) {
                 if (callback && tot > 0) {
                     float itemProg = (float)cur / (float)tot;
                     callback(pkg.name, itemProg, i, packages.size());
                 }
            });

            if (!success) {
                return false;
            }
            
            if (callback) callback("Extracting " + pkg.name, 1.0f, i, packages.size());

            std::string subDir = ".";
            auto it = packageMap.find(pkg.name);
            if (it != packageMap.end()) {
                subDir = it->second;
                std::replace(subDir.begin(), subDir.end(), '\\', '/');
            }
            
            std::string pkgPath = (std::filesystem::path(downloadsDir_) / pkg.checksum).string();
            std::string destPath = (std::filesystem::path(installDir) / subDir).string();
            
            if (!ZipUtil::extract(pkgPath, destPath)) {
                std::cerr << "[RSJFW] Failed to extract " << pkg.name << "\n";
                return false;
            }
        }
        
        std::vector<std::string> qtSearchPaths = {
            (std::filesystem::path(installDir) / "Qt5").string(),
            (std::filesystem::path(installDir) / "Plugins" / "Qt5").string()
        };

        for (const auto& searchPath : qtSearchPaths) {
            if (std::filesystem::exists(searchPath)) {
                std::cout << "[RSJFW] Relocating Qt5 plugins from " << searchPath << " to root...\n";
                for (const auto& entry : std::filesystem::directory_iterator(searchPath)) {
                    std::filesystem::path target = std::filesystem::path(installDir) / entry.path().filename();
                    if (std::filesystem::exists(target)) std::filesystem::remove_all(target);
                    std::filesystem::rename(entry.path(), target);
                }
                std::filesystem::remove(searchPath);
            }
        }
        
        // Create AppSettings.xml
        std::filesystem::path appSettingsPath = std::filesystem::path(installDir) / "AppSettings.xml";
        std::ofstream ofs(appSettingsPath);
        if (ofs) {
            ofs << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\r\n"
                << "<Settings>\r\n"
                << "        <ContentFolder>content</ContentFolder>\r\n"
                << "        <BaseUrl>http://www.roblox.com</BaseUrl>\r\n"
                << "        <Channel>production</Channel>\r\n"
                << "</Settings>\r\n";
        }
        
        if (callback) callback("Done", 1.0f, packages.size(), packages.size());
        std::cout << "[RSJFW] Successfully installed version " << versionGUID << "\n";
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "[RSJFW] Error installing version: " << e.what() << "\n";
        return false;
    }
}

bool Downloader::downloadPackage(const std::string& versionGUID, const RobloxPackage& pkg, std::function<void(size_t, size_t)> progressCb) {
    std::string destPath = (std::filesystem::path(downloadsDir_) / pkg.checksum).string();
    
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
    } catch (const std::exception& e) {
        std::cerr << "[RSJFW] Failed to download package " << pkg.name << ": " << e.what() << "\n";
        return false;
    }
}

// Helper for GitHub Releases
static std::string fetchReleaseAssetUrl(const std::string& repo, const std::string& filter, const std::string& version = "Latest") {
    std::string url = "https://api.github.com/repos/" + repo + "/releases";
    if (version == "Latest") url += "/latest";
    else url += "/tags/" + version;

    try {
        std::string response = HTTP::get(url);
        auto j = nlohmann::json::parse(response);
        
        if (j.contains("assets")) {
            for (const auto& asset : j["assets"]) {
                std::string name = asset["name"];
                if (name.find(filter) != std::string::npos) {
                    return asset["browser_download_url"];
                }
            }
        }
    } catch (...) {}
    return "";
}

static std::string fetchLatestReleaseAssetUrl(const std::string& repo, const std::string& filter) {
    return fetchReleaseAssetUrl(repo, filter, "Latest");
}

bool Downloader::installWine(WineSource source, const std::string& version, ProgressCallback callback) {
    std::filesystem::path wineDir = std::filesystem::path(rootDir_) / "wine";
    std::string folderPattern = (source == WineSource::VINEGAR) ? "wine-" : "GE-Proton";
    
    auto& genCfg = Config::instance().getGeneral();
    
    if (!genCfg.wineRoot.empty() && std::filesystem::exists(genCfg.wineRoot)) {
        if (std::filesystem::exists(std::filesystem::path(genCfg.wineRoot) / "bin/wine")) {
             if (version == "Latest" || genCfg.wineRoot.find(version) != std::string::npos) {
                 if (callback) callback("Wine already installed", 1.0f, 1, 1);
                 return true;
             }
        }
    }

    if (std::filesystem::exists(wineDir)) {
        for (const auto& entry : std::filesystem::directory_iterator(wineDir)) {
            std::string fname = entry.path().filename().string();
            if (entry.is_directory() && fname.find(folderPattern) != std::string::npos) {
                 if (version == "Latest" || fname.find(version) != std::string::npos) {
                    std::filesystem::path bin = entry.path() / "bin/wine";
                    if (!std::filesystem::exists(bin)) bin = entry.path() / "files/bin/wine";

                    if (std::filesystem::exists(bin)) {
                        genCfg.wineRoot = entry.path().string();
                        Config::instance().save();
                        if (callback) callback("Wine found: " + fname, 1.0f, 1, 1);
                        return true;
                    }
                 }
            }
        }
    }

    std::string repo;
    std::string filter;
    std::string name;
    
    if (source == WineSource::VINEGAR) {
        repo = "vinegarhq/wine-builds";
        filter = "wine-"; 
        name = "Vinegar Wine";
    } else if (source == WineSource::PROTON_GE) {
        repo = "GloriousEggroll/proton-ge-custom";
        filter = ".tar.gz"; 
        name = "Proton-GE";
    } else {
        return false;
    }

    if (callback) callback("Fetching " + name + " " + version + " release...", 0.0f, 0, 1);
    
    try {
        std::string url = fetchReleaseAssetUrl(repo, filter, version);
        if (url.empty()) throw std::runtime_error("No asset found for version " + version);
        
        std::string filename = url.substr(url.find_last_of('/') + 1);
        std::filesystem::create_directories(wineDir);
        std::filesystem::path destFile = wineDir / filename;
        
        if (!std::filesystem::exists(destFile)) {
            if (callback) callback("Downloading " + filename + "...", 0.0f, 0, 1);
            HTTP::download(url, destFile.string(), [&](size_t cur, size_t tot) {
                 if (callback && tot > 0) callback(filename, (float)cur/(float)tot, 0, 1);
            });
        }
        
        if (callback) callback("Extracting " + filename + "...", 0.0f, 0, 1);
        std::string extractedRoot = extractArchive(destFile.string(), wineDir.string(), callback);
        
        if (!extractedRoot.empty()) {
            std::filesystem::path bin = std::filesystem::path(extractedRoot) / "bin/wine";
            if (!std::filesystem::exists(bin)) bin = std::filesystem::path(extractedRoot) / "files/bin/wine";

            if (std::filesystem::exists(bin)) {
                genCfg.wineRoot = extractedRoot;
                Config::instance().save();
                std::cout << "[RSJFW] Updated wineRoot to: " << extractedRoot << "\n";
            }
            std::filesystem::remove(destFile);
        }

        if (callback) callback("Done", 1.0f, 1, 1);
        return !extractedRoot.empty();

        
    } catch (const std::exception& e) {
        if (callback) callback("Wine Error: " + std::string(e.what()), 1.0f, 0, 1);
        return false;
    }
}

bool Downloader::installDxvk(DxvkSource source, const std::string& version, ProgressCallback callback) {
    if (source == DxvkSource::CUSTOM) return true;

    std::string repo;
    std::string name;
    if (source == DxvkSource::SAREK) {
        repo = "pythonlover02/DXVK-Sarek";
        name = "DXVK-Sarek";
    } else { 
        repo = "doitsujin/dxvk";
        name = "DXVK";
    }

    std::filesystem::path dxvkDir = std::filesystem::path(rootDir_) / "dxvk";
    
    // Check if already installed
    auto& genCfg = Config::instance().getGeneral();
    if (!genCfg.dxvkRoot.empty() && std::filesystem::exists(std::filesystem::path(genCfg.dxvkRoot) / "x64/dxgi.dll")) {
        if (version == "Latest" || genCfg.dxvkRoot.find(version) != std::string::npos) {
             if (callback) callback("DXVK already installed", 1.0f, 1, 1);
             return true;
        }
    }

    try {
        std::string url;
        std::string targetVersion = version;
        
        if (targetVersion.empty() || targetVersion == "Latest" || targetVersion == "latest") {
             url = fetchLatestReleaseAssetUrl(repo, ".tar.gz");
             if (url.empty()) throw std::runtime_error("No asset found for latest release");
        } else {
             std::string tagUrl = "https://api.github.com/repos/" + repo + "/releases/tags/" + targetVersion;
             std::string response = HTTP::get(tagUrl);
             auto j = nlohmann::json::parse(response);
             if (j.contains("assets")) {
                 for (const auto& asset : j["assets"]) {
                     std::string aname = asset["name"];
                     if (aname.find(".tar.gz") != std::string::npos) {
                         url = asset["browser_download_url"];
                         break;
                     }
                 }
             }
             if (url.empty()) throw std::runtime_error("No asset found for version " + targetVersion);
        }

        std::string filename = url.substr(url.find_last_of('/') + 1);
        std::filesystem::create_directories(dxvkDir);
        std::filesystem::path destFile = dxvkDir / filename;

        if (callback) callback("Downloading " + filename + "...", 0.0f, 0, 1);
        HTTP::download(url, destFile.string(), [&](size_t cur, size_t tot) {
             if (callback && tot > 0) callback(filename, (float)cur/(float)tot, 0, 1);
        });
         
        if (callback) callback("Extracting " + filename + "...", 0.0f, 0, 1);
        std::string extractedRoot = extractArchive(destFile.string(), dxvkDir.string(), callback);
        
        if (!extractedRoot.empty()) {
            if (std::filesystem::exists(std::filesystem::path(extractedRoot) / "x64") || 
                std::filesystem::exists(std::filesystem::path(extractedRoot) / "x86")) {
                Config::instance().getGeneral().dxvkRoot = extractedRoot;
                Config::instance().save();
                std::cout << "[RSJFW] Updated dxvkRoot to: " << extractedRoot << "\n";
            }
            std::filesystem::remove(destFile); // Cleanup
        }

        if (callback) callback("Done", 1.0f, 1, 1);
        return !extractedRoot.empty();

    } catch(const std::exception& e) {
         if (callback) callback("DXVK Error: " + std::string(e.what()), 1.0f, 0, 1);
         return false;
    }
}

std::vector<std::string> Downloader::fetchDxvkVersions(DxvkSource source) {
    std::vector<std::string> versions;
    if (source == DxvkSource::CUSTOM) return versions;

    std::string repo = (source == DxvkSource::SAREK) ? "pythonlover02/DXVK-Sarek" : "doitsujin/dxvk";
    std::string url = "https://api.github.com/repos/" + repo + "/releases"; // Lists last 30 releases
    
    try {
        std::string response = HTTP::get(url);
        auto j = nlohmann::json::parse(response);
        
        if (j.is_array()) {
            for (const auto& release : j) {
                if (release.contains("tag_name")) {
                    versions.push_back(release["tag_name"]);
                }
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "[RSJFW] Error fetching DXVK versions: " << e.what() << "\n";
    }
    
    return versions;
}
std::vector<std::string> Downloader::fetchWineVersions(WineSource source) {
    std::vector<std::string> versions;
    if (source == WineSource::SYSTEM || source == WineSource::CUSTOM) return versions;

    std::string repo = (source == WineSource::VINEGAR) ? "vinegarhq/wine-builds" : "GloriousEggroll/proton-ge-custom";
    std::string url = "https://api.github.com/repos/" + repo + "/releases";
    
    try {
        std::string response = HTTP::get(url);
        auto j = nlohmann::json::parse(response);
        
        if (j.is_array()) {
            for (const auto& release : j) {
                if (release.contains("tag_name")) {
                    versions.push_back(release["tag_name"]);
                }
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "[RSJFW] Error fetching Wine versions: " << e.what() << "\n";
    }
    
    return versions;
}

std::string Downloader::extractArchive(const std::string& archivePath, const std::string& destDir, ProgressCallback callback) {
    if (callback) callback("Counting files...", 0.0f, 0, 1);
    
    std::string rootFolder = "";
    int totalFiles = 0;
    
    {
        FILE* fp = popen(("tar -tf \"" + archivePath + "\"").c_str(), "r");
        if (fp) {
            char line[1024];
            while (fgets(line, sizeof(line), fp)) {
                std::string s(line);
                if (!s.empty() && s.back() == '\n') s.pop_back();
                if (s.empty()) continue;
                
                if (totalFiles == 0) {
                    size_t firstSlash = s.find_first_of('/');
                    if (firstSlash != std::string::npos) {
                        rootFolder = s.substr(0, firstSlash);
                    } else {
                        rootFolder = s;
                    }
                }
                totalFiles++;
            }
            pclose(fp);
        }
    }

    if (totalFiles == 0) return "";

    FILE* fp = popen(("tar -C \"" + destDir + "\" -xvf \"" + archivePath + "\"").c_str(), "r");
    if (!fp) return "";

    char line[4096];
    int currentFile = 0;
    while (fgets(line, sizeof(line), fp)) {
        currentFile++;
        std::string s(line);
        if (!s.empty() && s.back() == '\n') s.pop_back();
        
        if (callback) {
            callback(s, (float)currentFile / (float)totalFiles, 0, 1);
        }
    }
    int status = pclose(fp);
    
    if (status == 0 && !rootFolder.empty()) {
        return (std::filesystem::path(destDir) / rootFolder).string();
    }
    return "";
}



} // namespace rsjfw
