#include "rsjfw/wine.hpp"
#include "rsjfw/dxvk.hpp"
#include "rsjfw/launcher.hpp"
#include "rsjfw/downloader.hpp"
#include "rsjfw/registry.hpp"
#include "rsjfw/config.hpp"
#include "rsjfw/logger.hpp"
#include "rsjfw/http.hpp"
#include <filesystem>
#include <cstdlib>
#include <algorithm>
#include <fstream>
#include <cstring>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <random>

namespace rsjfw {

Launcher::Launcher(const std::string& rootDir)
    : rootDir_(rootDir) {
    versionsDir_ = (std::filesystem::path(rootDir) / "versions").string();
    prefixDir_ = (std::filesystem::path(rootDir) / "prefix").string();
    
    std::filesystem::create_directories(prefixDir_);
}

// Configures the Wine prefix registry for Roblox Studio
bool Launcher::setupPrefix(ProgressCb progressCb) {
    if (progressCb) progressCb(0.0f, "Initializing Wine Prefix...");
    auto& genCfg = Config::instance().getGeneral();
    rsjfw::wine::Prefix pfx(genCfg.wineRoot, prefixDir_);
    
    std::filesystem::path marker = std::filesystem::path(prefixDir_) / ".rsjfw_setup_complete";
    if (std::filesystem::exists(marker)) {
        LOG_INFO("Prefix setup already complete. Skipping.");
        if (progressCb) progressCb(1.0f, "Prefix Ready.");
        return true;
    }

    LOG_INFO("Setting up Wine prefix registry...");
    if (progressCb) progressCb(0.2f, "Applying Registry Keys...");
    
    pfx.registryAdd("HKCU\\Software\\Wine\\WineDbg", "ShowCrashDialog", "0", "REG_DWORD");
    pfx.registryAdd("HKCU\\Software\\Wine\\X11 Driver", "UseEGL", "Y");
    pfx.registryAdd("HKCU\\Software\\Wine\\DllOverrides", "dxgi", "native");
    pfx.registryAdd("HKCU\\Software\\Wine\\DllOverrides", "d3d11", "native");
    
    if (progressCb) progressCb(0.5f, "Checking Credentials...");
    Registry reg(prefixDir_);
    bool keyExists = reg.exists("HKCU\\Software\\Wine\\Credential Manager", "EncryptionKey");
    
    if (!keyExists) {
        LOG_INFO("Generating Wine Credential Manager EncryptionKey...");
        if (progressCb) progressCb(0.6f, "Generating Encryption Key...");
        std::vector<unsigned char> key(8);
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(0, 255);
        
        std::stringstream ss;
        ss << std::hex << std::setfill('0');
        for (int i = 0; i < 8; ++i) {
            unsigned char b = static_cast<unsigned char>(dis(gen));
            ss << std::setw(2) << static_cast<int>(b);
        }
        
        pfx.registryAdd("HKCU\\Software\\Wine\\Credential Manager", "EncryptionKey", ss.str(), "REG_BINARY");
    }
    
    std::ofstream(marker).close();
    

    LOG_INFO("Enforcing system browser for authentication...");
    pfx.registryAdd("HKCR\\http\\shell\\open\\command", "", "\"C:\\windows\\system32\\winebrowser.exe\" \"%1\"", "REG_SZ");
    pfx.registryAdd("HKCR\\https\\shell\\open\\command", "", "\"C:\\windows\\system32\\winebrowser.exe\" \"%1\"", "REG_SZ");

    if (progressCb) progressCb(1.0f, "Registry Setup Complete.");
    return true;
}

// Terminates all running Wine processes within the prefix
bool Launcher::killStudio() {
    LOG_INFO("Killing all Studio processes in prefix...");
    auto& genCfg = Config::instance().getGeneral();
    rsjfw::wine::Prefix pfx(genCfg.wineRoot, prefixDir_);
    return pfx.kill();
}

// Opens the Wine configuration tool
bool Launcher::openWineConfiguration() {
    LOG_INFO("Opening Wine Configuration...");
    return runWine("winecfg");
}

// Installs DXVK globally into the prefix
bool Launcher::setupDxvk(const std::string& versionGUID, ProgressCb progressCb) {
    bool useDxvk = Config::instance().getGeneral().dxvk;
    if (!useDxvk) return true;

    auto& genCfg = Config::instance().getGeneral();
    std::string dxvkRoot = "";

    if (genCfg.dxvkSource == DxvkSource::CUSTOM) {
        dxvkRoot = genCfg.dxvkCustomPath;
    } else {
        if (!genCfg.dxvkRoot.empty() && std::filesystem::exists(genCfg.dxvkRoot)) {
            dxvkRoot = genCfg.dxvkRoot;
        } else {
            std::filesystem::path defaultDxvk = std::filesystem::path(rootDir_) / "dxvk";
            if (std::filesystem::exists(defaultDxvk)) {

                 for (const auto& entry : std::filesystem::directory_iterator(defaultDxvk)) {
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
        
        if (progressCb) progressCb(0.0f, "Downloading DXVK...");
        
        rsjfw::Downloader downloader(rootDir_);
        DxvkSource source = genCfg.dxvkSource;
        if (source == DxvkSource::CUSTOM) source = DxvkSource::SAREK;
        
        bool success = downloader.installDxvk(source, "Latest", 
            [&](const std::string& status, float p, size_t, size_t){
                if (progressCb) progressCb(p, status);
            });
            
        if (success) {
            dxvkRoot = Config::instance().getGeneral().dxvkRoot;
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
bool Launcher::launchLatest(const std::vector<std::string>& extraArgs, ProgressCb progressCb) {
    if (!std::filesystem::exists(versionsDir_)) {
        LOG_ERROR("Versions directory not found.");
        return false;
    }

    std::vector<std::string> versions;
    for (const auto& entry : std::filesystem::directory_iterator(versionsDir_)) {
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
    return launchVersion(latestVersion, extraArgs, progressCb);
}

std::string Launcher::findStudioExecutable(const std::string& versionDir) {
    std::filesystem::path dir(versionsDir_);
    dir /= versionDir;
    
    if (std::filesystem::exists(dir / "RobloxStudioBeta.exe")) return (dir / "RobloxStudioBeta.exe").string();
    if (std::filesystem::exists(dir / "RobloxStudio.exe")) return (dir / "RobloxStudio.exe").string();
    
    return "";
}

bool Launcher::setupFFlags(const std::string& versionGUID, ProgressCb progressCb) {
    LOG_INFO("Setting up FFlags for " + versionGUID);
    std::filesystem::path settingsDir = std::filesystem::path(versionsDir_) / versionGUID / "ClientSettings";
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

bool Launcher::launchVersion(const std::string& versionGUID, const std::vector<std::string>& extraArgs, ProgressCb progressCb) {
    std::string exe = findStudioExecutable(versionGUID);
    if (exe.empty()) {
        LOG_ERROR("Could not find Roblox Studio executable in " + versionGUID);
        return false;
    }
    
    LOG_INFO("Launching version " + versionGUID);
    return runWine(exe, extraArgs);
}

// Executes a command using wine with the configured environment
bool Launcher::runWine(const std::string& executablePath, const std::vector<std::string>& args) {
    auto& genCfg = Config::instance().getGeneral();
    
    if (genCfg.wineRoot.empty() && (genCfg.wineSource == WineSource::VINEGAR || genCfg.wineSource == WineSource::PROTON_GE)) {
        std::string folderPattern = (genCfg.wineSource == WineSource::VINEGAR) ? "wine-" : "GE-Proton";
        std::string version = (genCfg.wineSource == WineSource::VINEGAR) ? genCfg.vinegarVersion : genCfg.protonVersion;
        std::filesystem::path wineDir = std::filesystem::path(rootDir_) / "wine";
        if (std::filesystem::exists(wineDir)) {
            for (const auto& entry : std::filesystem::directory_iterator(wineDir)) {
                std::string fname = entry.path().filename().string();
                if (entry.is_directory() && fname.find(folderPattern) != std::string::npos) {
                    if (version != "Latest" && fname.find(version) == std::string::npos) continue;
                    
                    std::filesystem::path binCheck = entry.path() / "bin/wine";
                    if (!std::filesystem::exists(binCheck)) binCheck = entry.path() / "files/bin/wine";

                    if (std::filesystem::exists(binCheck)) {
                        genCfg.wineRoot = entry.path().string();
                        Config::instance().save();
                        std::cout << "[RSJFW] Discovered wineRoot: " << genCfg.wineRoot << "\n";
                        break;
                    }
                }
            }
        }
    }

    rsjfw::wine::Prefix pfx(genCfg.wineRoot, prefixDir_);
    
    bool wineValid = false;
    if (!genCfg.wineRoot.empty()) {
        if (std::filesystem::exists(pfx.bin("wine")) || 
            (genCfg.wineSource == WineSource::PROTON_GE && std::filesystem::exists(std::filesystem::path(genCfg.wineRoot) / "files/bin/wine"))) {
            wineValid = true;
        }
    }

    if (!wineValid && (genCfg.wineSource == WineSource::VINEGAR || genCfg.wineSource == WineSource::PROTON_GE)) {
        LOG_INFO("Wine root invalid or missing. Attempting repair...");
        
        rsjfw::Downloader downloader(rootDir_);
        std::string version = (genCfg.wineSource == WineSource::VINEGAR) ? genCfg.vinegarVersion : genCfg.protonVersion;
        
        bool success = downloader.installWine(genCfg.wineSource, version, [](const std::string&, float, size_t, size_t){});
        if (success) {
            // Reload config
            genCfg.wineRoot = Config::instance().getGeneral().wineRoot;
            // Update prefix object with new root
            pfx = rsjfw::wine::Prefix(genCfg.wineRoot, prefixDir_);
            wineValid = true;
            LOG_INFO("Wine repaired successfully.");
        } else {
            LOG_ERROR("Failed to repair Wine installation.");
            return false;
        }
    }
    
    if (genCfg.wineSource != WineSource::PROTON_GE) {
        std::filesystem::path wineBinPath = pfx.bin("wine");
        if (std::filesystem::exists(wineBinPath)) {
            std::string binDir = wineBinPath.parent_path().string();
            std::string currentPath = getenv("PATH") ? getenv("PATH") : "";
            pfx.appendEnv("PATH", binDir + ":" + currentPath);
        }
    }

    pfx.appendEnv("WINEESYNC", "1");
    pfx.appendEnv("__GL_THREADED_OPTIMIZATIONS", "1");
    pfx.appendEnv("SDL_VIDEODRIVER", "x11");
    pfx.appendEnv("VK_LOADER_LAYERS_ENABLE", "VK_LAYER_RSJFW_RsjfwLayer");
    
    if (genCfg.selectedGpu >= 0) {
        pfx.appendEnv("DRI_PRIME", std::to_string(genCfg.selectedGpu));
    }
    
    for (const auto& [key, val] : genCfg.customEnv) {
        if (!key.empty()) pfx.appendEnv(key, val);
    }

    if (getenv("DBUS_SESSION_BUS_ADDRESS")) {
        pfx.appendEnv("DBUS_SESSION_BUS_ADDRESS", getenv("DBUS_SESSION_BUS_ADDRESS"));
    }
    if (genCfg.wineSource == WineSource::PROTON_GE) {
        pfx.appendEnv("STEAM_COMPAT_DATA_PATH", prefixDir_);
        pfx.appendEnv("STEAM_COMPAT_CLIENT_INSTALL_PATH", "/usr/lib/steam");
        
        // Explicitly set WINESERVER to avoid version mismatches with system wine
        // Proton GE stores its wineserver in files/bin/wineserver
        std::filesystem::path serverPath = std::filesystem::path(genCfg.wineRoot) / "files" / "bin" / "wineserver";
        if (std::filesystem::exists(serverPath)) {
            pfx.appendEnv("WINESERVER", serverPath.string());
        }
    }

    // -all: disable everything
    // err+all: show errors
    // +debugstr: show OutputDebugString
    
    std::string winedebug = "";
    if (debug_) {
        winedebug = "warn+all,err+all,fixme+all,+debugstr";
    } else {
        winedebug = "-all";
    }
    pfx.appendEnv("WINEDEBUG", winedebug);

    std::string dllOverrides = "dxdiagn=;winemenubuilder.exe=;mscoree=;mshtml="; 
    bool useDxvk = Config::instance().getGeneral().dxvk;
    if (useDxvk) {
        dllOverrides = "dxgi,d3d11,d3d10core,d3d9=n,b;" + dllOverrides;
    }
    
    pfx.appendEnv("WINEDLLOVERRIDES", dllOverrides);

    bool isProtocol = false;
    for (const auto& arg : args) {
        if (arg.find("roblox-studio:") != std::string::npos || arg.find("roblox-studio-auth:") != std::string::npos) {
            isProtocol = true;
        }
    }

    if (!isProtocol) {
        pfx.kill(); // wineserver -k
    }

    auto& wineCfg = Config::instance().getWine();
    std::string resolution = wineCfg.desktopResolution;
    std::vector<std::string> finalArgs;

    if (wineCfg.multipleDesktops || wineCfg.desktopMode) {
        finalArgs.push_back("explorer");
        std::string d = "/desktop=RSJFW_" + (wineCfg.multipleDesktops ? std::to_string(getpid()) : "Desktop") + "," + resolution;
        finalArgs.push_back(d);
    }
    
    finalArgs.push_back(executablePath);
    for (const auto& a : args) finalArgs.push_back(a);

    std::cout << "[RSJFW] Launching: " << executablePath << "\n";

    std::string logDir = std::string(getenv("HOME")) + "/.local/share/rsjfw/logs";
    std::filesystem::create_directories(logDir);
    std::string logPath = logDir + "/studio_latest.log";
    
    std::shared_ptr<std::ofstream> logFile = std::make_shared<std::ofstream>(logPath);
    
    std::string studioCwd = std::filesystem::path(executablePath).parent_path().string();

    std::string target = (wineCfg.multipleDesktops || wineCfg.desktopMode) ? "explorer" : executablePath;
    
    std::vector<std::string> launchArgs;
    if (wineCfg.multipleDesktops || wineCfg.desktopMode) {
        if (!finalArgs.empty() && finalArgs[0] == "explorer") {
            launchArgs.assign(finalArgs.begin() + 1, finalArgs.end());
        } else {
            launchArgs = finalArgs;
        }
    } else {
        launchArgs = args;
    }

    return pfx.wine(target, launchArgs, [logFile](const std::string& line) {
        std::cout << line;
        
        // Write to version-specific log
        if (logFile && logFile->is_open()) *logFile << line;
        
        // Also pipe to main rsjfw.log
        std::string rawLine = line;
        if (!rawLine.empty() && rawLine.back() == '\n') rawLine.pop_back();
        if (!rawLine.empty()) {
            LOG_INFO("[WINE] " + rawLine);
        }
        
        if (line.find("Fatal exiting due to Trouble launching Studio") != std::string::npos) {
            std::cerr << "\n[RSJFW] Fatal error detected. Aborting.\n";
        }
    }, studioCwd);
}

} // namespace rsjfw
