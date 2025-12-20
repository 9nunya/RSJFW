#include "rsjfw/downloader.hpp"
#include "rsjfw/http.hpp"
#include "rsjfw/zip_util.hpp"
#include <filesystem>
#include <iostream>
#include <fstream>
#include <unordered_map>
#include <algorithm>

namespace rsjfw {

Downloader::Downloader(const std::string& rootDir) 
    : rootDir_(rootDir) {
    versionsDir_ = (std::filesystem::path(rootDir) / "versions").string();
    downloadsDir_ = (std::filesystem::path(rootDir) / "downloads").string();
    
    std::filesystem::create_directories(versionsDir_);
    std::filesystem::create_directories(downloadsDir_);
}

bool Downloader::installLatest() {
    try {
        std::string latest = RobloxAPI::getLatestVersionGUID();
        std::cout << "[RSJFW] Latest version: " << latest << "\n";
        return installVersion(latest);
    } catch (const std::exception& e) {
        std::cerr << "[RSJFW] Error fetching latest version: " << e.what() << "\n";
        return false;
    }
}

bool Downloader::installVersion(const std::string& versionGUID) {
    try {
        auto packages = RobloxAPI::getPackageManifest(versionGUID);
        std::cout << "[RSJFW] Found " << packages.size() << " packages to install.\n";
        
        std::string installDir = (std::filesystem::path(versionsDir_) / versionGUID).string();
        if (std::filesystem::exists(installDir)) {
            std::cout << "[RSJFW] Version " << versionGUID << " already installed.\n";
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
            {"ssl.zip", "ssl/"},
            {"WebView2.zip", ""},
            {"WebView2RuntimeInstaller.zip", "WebView2RuntimeInstaller/"}
        };

        for (const auto& pkg : packages) {
            std::cout << "[RSJFW] Installing " << pkg.name << "...\n";
            if (!downloadPackage(versionGUID, pkg)) {
                return false;
            }
            
            std::string subDir = ".";
            auto it = packageMap.find(pkg.name);
            if (it != packageMap.end()) {
                subDir = it->second;
                // Replace backslashes with forward slashes
                std::replace(subDir.begin(), subDir.end(), '\\', '/');
            }
            
            std::string pkgPath = (std::filesystem::path(downloadsDir_) / pkg.checksum).string();
            std::string destPath = (std::filesystem::path(installDir) / subDir).string();
            
            if (!ZipUtil::extract(pkgPath, destPath)) {
                std::cerr << "[RSJFW] Failed to extract " << pkg.name << "\n";
                return false;
            }
        }
        
        // Post-install: Relocate Qt5 plugins to root.
        // Roblox puts them in various places (root, Plugins/Qt5, etc.) but we need them in root/platforms etc.
        std::vector<std::string> qtSearchPaths = {
            (std::filesystem::path(installDir) / "Qt5").string(),
            (std::filesystem::path(installDir) / "Plugins" / "Qt5").string()
        };

        for (const auto& searchPath : qtSearchPaths) {
            if (std::filesystem::exists(searchPath)) {
                std::cout << "[RSJFW] Relocating Qt5 plugins from " << searchPath << " to root...\n";
                for (const auto& entry : std::filesystem::directory_iterator(searchPath)) {
                    std::filesystem::path target = std::filesystem::path(installDir) / entry.path().filename();
                    if (std::filesystem::exists(target)) {
                        std::filesystem::remove_all(target);
                    }
                    std::filesystem::rename(entry.path(), target);
                }
                std::filesystem::remove(searchPath);
            }
        }
        
        // Create AppSettings.xml
        std::filesystem::path appSettingsPath = std::filesystem::path(installDir) / "AppSettings.xml";
        std::cout << "[RSJFW] Creating AppSettings.xml...\n";
        std::ofstream ofs(appSettingsPath);
        if (ofs) {
            ofs << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\r\n"
                << "<Settings>\r\n"
                << "        <ContentFolder>content</ContentFolder>\r\n"
                << "        <BaseUrl>http://www.roblox.com</BaseUrl>\r\n"
                << "        <Channel>production</Channel>\r\n"
                << "</Settings>\r\n";
        }
        
        std::cout << "[RSJFW] Successfully installed version " << versionGUID << "\n";
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "[RSJFW] Error installing version: " << e.what() << "\n";
        return false;
    }
}

bool Downloader::downloadPackage(const std::string& versionGUID, const RobloxPackage& pkg) {
    std::string destPath = (std::filesystem::path(downloadsDir_) / pkg.checksum).string();
    
    if (std::filesystem::exists(destPath)) {
        return true; // Already downloaded (Vinegar uses checksum as filename)
    }
    
    std::string url = RobloxAPI::BASE_URL + versionGUID + "-" + pkg.name;
    try {
        std::string data = HTTP::get(url);
        std::ofstream ofs(destPath, std::ios::binary);
        ofs.write(data.data(), data.size());
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[RSJFW] Failed to download package " << pkg.name << ": " << e.what() << "\n";
        return false;
    }
}

} // namespace rsjfw
