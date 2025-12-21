#include "rsjfw/launcher.hpp"
#include "rsjfw/registry.hpp"
#include "rsjfw/config.hpp"
#include "rsjfw/logger.hpp"
#include <filesystem>
#include <cstdlib>
#include <algorithm>
#include <fstream>
#include <cstring>
#include <unistd.h>
#include <sys/wait.h>
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
bool Launcher::setupPrefix() {
    LOG_INFO("Setting up Wine prefix...");
    Registry reg(prefixDir_);
    
    reg.add("HKCU\\Software\\Wine\\WineDbg", "ShowCrashDialog", 0);
    reg.add("HKCU\\Software\\Wine\\X11 Driver", "UseEGL", "Y");
    reg.add("HKCU\\Software\\Wine\\DllOverrides", "dxgi", "native");
    reg.add("HKCU\\Software\\Wine\\DllOverrides", "d3d11", "native");
    
    if (!reg.exists("HKCU\\Software\\Wine\\Credential Manager", "EncryptionKey")) {
        LOG_INFO("Generating Wine Credential Manager EncryptionKey...");
        std::vector<unsigned char> key(8);
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(0, 255);
        for (int i = 0; i < 8; ++i) {
            key[i] = static_cast<unsigned char>(dis(gen));
        }
        reg.addBinary("HKCU\\Software\\Wine\\Credential Manager", "EncryptionKey", key);
    }
    
    return true;
}

// Terminates all running Wine processes within the prefix
bool Launcher::killStudio() {
    LOG_INFO("Killing all Studio processes in prefix...");
    setenv("WINEPREFIX", prefixDir_.c_str(), 1);
    return std::system("wineserver -k") == 0;
}

// Downloads and extracts a specific DXVK version
bool Launcher::installDxvk(const std::string& version) {
    std::filesystem::path downloadsDir = std::filesystem::path(rootDir_) / "downloads";
    std::filesystem::create_directories(downloadsDir);
    std::string filename = "dxvk-" + version + ".tar.gz";
    std::string cacheFile = (downloadsDir / filename).string();

    if (!std::filesystem::exists(cacheFile)) {
        LOG_INFO("Downloading DXVK " + version + "...");
        std::string url = "https://github.com/doitsujin/dxvk/releases/download/v" + version + "/" + filename;
        std::string cmd = "curl -L \"" + url + "\" -o \"" + cacheFile + "\"";
        if (std::system(cmd.c_str()) != 0) {
             LOG_ERROR("Failed to download DXVK " + version);
             return false;
        }
    }

    std::filesystem::path dxvkRoot = std::filesystem::path(rootDir_) / "dxvk";
    std::filesystem::create_directories(dxvkRoot);

    LOG_INFO("Extracting DXVK...");
    std::string cmd = "tar -C \"" + dxvkRoot.string() + "\" -xzf \"" + cacheFile + "\"";
    return std::system(cmd.c_str()) == 0;
}

// Opens the Wine configuration tool
bool Launcher::openWineConfiguration() {
    LOG_INFO("Opening Wine Configuration...");
    return runWine("winecfg");
}

// Installs DXVK DLLs into the specific Studio version directory
bool Launcher::setupDxvk(const std::string& versionGUID) {
    std::string version = Config::instance().getGeneral().dxvkVersion;
    std::filesystem::path dxvkX64 = std::filesystem::path(rootDir_) / "dxvk" / ("dxvk-" + version) / "x64";
    if (!std::filesystem::exists(dxvkX64)) {
        if (!installDxvk(version)) return false;
    }

    std::filesystem::path studioDir = std::filesystem::path(versionsDir_) / versionGUID;
    std::filesystem::copy_file(dxvkX64 / "dxgi.dll", studioDir / "dxgi.dll", std::filesystem::copy_options::overwrite_existing);
    std::filesystem::copy_file(dxvkX64 / "d3d11.dll", studioDir / "d3d11.dll", std::filesystem::copy_options::overwrite_existing);
    
    return true;
}

// Finds the latest installed version and launches it
bool Launcher::launchLatest(const std::vector<std::string>& extraArgs) {
    std::string latestVersion;
    std::filesystem::file_time_type latestTime;
    
    if (!std::filesystem::exists(versionsDir_)) {
        std::cerr << "[RSJFW] No versions installed.\n";
        return false;
    }
    
    for (const auto& entry : std::filesystem::directory_iterator(versionsDir_)) {
        if (entry.is_directory()) {
            auto time = std::filesystem::last_write_time(entry);
            if (latestVersion.empty() || time > latestTime) {
                latestVersion = entry.path().filename().string();
                latestTime = time;
            }
        }
    }
    
    if (latestVersion.empty()) {
        std::cerr << "[RSJFW] No versions found to launch.\n";
        return false;
    }
    
    setupFFlags(latestVersion);
    setupDxvk(latestVersion);
    installWebView2(latestVersion);
    return launchVersion(latestVersion, extraArgs);
}

// Installs the WebView2 runtime into the Wine prefix
bool Launcher::installWebView2(const std::string& versionGUID) {
    std::filesystem::path prefixWebViewDir = std::filesystem::path(prefixDir_) / "drive_c" / "Program Files (x86)" / "Microsoft" / "EdgeWebView";
    if (std::filesystem::exists(prefixWebViewDir)) {
        std::cout << "[RSJFW] WebView2 already installed in prefix, skipping.\n";
        return true;
    }

    std::filesystem::path studioDir = std::filesystem::path(versionsDir_) / versionGUID;
    std::filesystem::path installerPath = studioDir / "WebView2RuntimeInstaller" / "MicrosoftEdgeWebview2Setup.exe";
    
    if (!std::filesystem::exists(installerPath)) {
        std::cerr << "[RSJFW] WebView2 installer not found at " << installerPath << "\n";
        return false;
    }

    std::cout << "[RSJFW] Installing WebView2 into prefix...\n";
    setenv("WINEPREFIX", prefixDir_.c_str(), 1);
    setenv("WINEDEBUG", "-all", 1);
    
    std::string cmd = "wine \"" + installerPath.string() + "\" /silent /install";
    int res = std::system(cmd.c_str());
    return res == 0;
}

// Configures ClientAppSettings.json based on configuration
bool Launcher::setupFFlags(const std::string& versionGUID) {
    std::filesystem::path studioDir = std::filesystem::path(versionsDir_) / versionGUID;
    std::filesystem::path settingsDir = studioDir / "ClientSettings";
    std::filesystem::create_directories(settingsDir);
    
    std::filesystem::path settingsFile = settingsDir / "ClientAppSettings.json";
    
    auto& cfg = Config::instance();
    auto& fflags = cfg.getFFlags();
    
    std::string renderer = cfg.getGeneral().renderer; 
    
    fflags["FFlagDebugGraphicsPreferD3D11"] = (renderer == "D3D11");
    fflags["FFlagDebugGraphicsDisableD3D11"] = (renderer != "D3D11");
    
    fflags["FFlagDebugGraphicsPreferD3D11FL10"] = (renderer == "D3D11FL10");
    fflags["FFlagDebugGraphicsDisableD3D11FL10"] = (renderer != "D3D11FL10");
    
    fflags["FFlagDebugGraphicsPreferVulkan"] = (renderer == "Vulkan");
    fflags["FFlagDebugGraphicsDisableVulkan"] = (renderer != "Vulkan");
    
    fflags["FFlagDebugGraphicsPreferOpenGL"] = (renderer == "OpenGL");
    fflags["FFlagDebugGraphicsDisableOpenGL"] = (renderer != "OpenGL");
    
    fflags["D3D11FL10"] = (renderer == "D3D11FL10"); 
    
    nlohmann::json root;
    for (const auto& [key, val] : fflags) {
        root[key] = val;
    }
    std::string jsonStr = root.dump(4);

    std::ofstream ofs(settingsFile);
    if (ofs) ofs << jsonStr;

    std::string username = getenv("USER") ? getenv("USER") : "nunya";
    std::filesystem::path appDataDir = std::filesystem::path(prefixDir_) / "drive_c/users" / username / "AppData/Local/Roblox/ClientSettings";
    
    if (!std::filesystem::exists(std::filesystem::path(prefixDir_) / "drive_c/users" / username / "AppData")) {
        appDataDir = std::filesystem::path(prefixDir_) / "drive_c/users" / username / "Local Settings/Application Data/Roblox/ClientSettings";
    }

    LOG_INFO("Injecting StudioAppSettings.json into prefix AppData...");
    std::filesystem::create_directories(appDataDir);
    std::ofstream ofsCache(appDataDir / "StudioAppSettings.json");
    if (ofsCache) {
        ofsCache << jsonStr;
    }

    return true;
}

// Launches a specific version of Roblox Studio
bool Launcher::launchVersion(const std::string& versionGUID, const std::vector<std::string>& extraArgs) {
    std::filesystem::path studioDir = std::filesystem::path(versionsDir_) / versionGUID;
    
    std::string username = getenv("USER") ? getenv("USER") : "nunya";
    std::filesystem::path officialRobloxDir = std::filesystem::path(prefixDir_) / "drive_c/users" / username / "AppData/Local/Roblox";
    std::filesystem::path officialVersionsDir = officialRobloxDir / "Versions";
    std::filesystem::path officialVersionDir = officialVersionsDir / versionGUID;

    std::cout << "[RSJFW] Ensuring official installation path...\n";
    std::filesystem::create_directories(officialVersionsDir);
    
    if (!std::filesystem::exists(officialVersionDir)) {
        try {
            std::filesystem::create_directory_symlink(studioDir, officialVersionDir);
        } catch (const std::exception& e) {
            std::cerr << "[RSJFW] Failed to create symlink: " << e.what() << "\n";
        }
    }

    std::string exePath = findStudioExecutable(officialVersionDir.string());
    if (exePath.empty()) {
        exePath = findStudioExecutable(studioDir.string());
    }
    
    if (exePath.empty()) {
        std::cerr << "[RSJFW] Could not find RobloxStudioBeta.exe\n";
        return false;
    }
    
    std::cout << "[RSJFW] Launching Studio from " << exePath << "...\n";
    return runWine(exePath, extraArgs);
}

// Helper to locate the Studio executable in a directory
std::string Launcher::findStudioExecutable(const std::string& versionDir) {
    std::filesystem::path p(versionDir);
    p /= "RobloxStudioBeta.exe";
    if (std::filesystem::exists(p)) {
        return p.string();
    }
    return "";
}

// Executes a command using wine with the configured environment
bool Launcher::runWine(const std::string& executablePath, const std::vector<std::string>& args) {
    setenv("WINEPREFIX", prefixDir_.c_str(), 1);
    
    bool useDxvk = Config::instance().getGeneral().dxvk;
    std::string dllOverrides = "dxdiagn,winemenubuilder.exe,mscoree,mshtml=;msedgewebview2=n,b"; 
    
    if (useDxvk) {
        dllOverrides = "dxgi,d3d11=n,b;" + dllOverrides;
    }

    setenv("WINEDLLOVERRIDES", dllOverrides.c_str(), 1);
    
    setenv("WINEESYNC", "1", 1);
    setenv("MESA_GL_VERSION_OVERRIDE", "4.4", 1);
    setenv("__GL_THREADED_OPTIMIZATIONS", "1", 1);
    
    setenv("VK_LOADER_LAYERS_ENABLE", "VK_LAYER_RSJFW_RsjfwLayer", 1);
    
    std::string username = getenv("USER") ? getenv("USER") : "nunya";
    std::string webviewPath = "";
    
    // Dynamically find installed WebView2 version by looking for the executable
    std::filesystem::path edgeAppDir = std::filesystem::path(prefixDir_) / "drive_c" / "Program Files (x86)" / "Microsoft" / "EdgeWebView" / "Application";
    
    if (std::filesystem::exists(edgeAppDir)) {
        // Recursive search for msedgewebview2.exe
        for (const auto& entry : std::filesystem::recursive_directory_iterator(edgeAppDir)) {
            if (entry.is_regular_file() && entry.path().filename() == "msedgewebview2.exe") {
                 // Found it! Use the parent directory.
                 webviewPath = entry.path().parent_path().string();
                 LOG_INFO("Found WebView2 Runtime: " + webviewPath);
                 break;
            }
        }
    }
    
    if (webviewPath.empty()) {
        std::cerr << "[RSJFW] ERROR: Could not find msedgewebview2.exe in prefix. WebView2 may fail.\n";
    } else {
        setenv("WEBVIEW2_BROWSER_EXECUTABLE_FOLDER", webviewPath.c_str(), 1);
    }
    
    std::string webviewUserData = "C:\\users\\" + username + "\\AppData\\Local\\Roblox\\WebView2";
    setenv("WEBVIEW2_USER_DATA_FOLDER", webviewUserData.c_str(), 1);
    
    // Additional Chromium flags
    setenv("WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS", "--no-sandbox --disable-features=WebRtcHideLocalIpsWithMdns --disable-gpu-sandbox --user-agent=\"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36 Edge/120.0.0.0\"", 1);

    std::system(("WINEPREFIX=" + prefixDir_ + " wine reg add \"HKCU\\Software\\Wine\\WineDbg\" /v ShowCrashDialog /t REG_DWORD /d 0 /f > /dev/null 2>&1").c_str());
    std::system(("WINEPREFIX=" + prefixDir_ + " wine reg add \"HKCU\\Software\\Wine\\X11 Driver\" /v UseEGL /t REG_SZ /d \"Y\" /f > /dev/null 2>&1").c_str());
    
    std::system(("WINEPREFIX=" + prefixDir_ + " wine reg add \"HKCU\\Software\\Wine\\Credential Manager\" /f > /dev/null 2>&1").c_str());
    
    setenv("WINEDEBUG", "fixme-all,err-kerberos,err-ntlm,err-combase,+debugstr", 1); 
    
    std::cout << "[RSJFW] [ENV] WEBVIEW2_BROWSER_EXECUTABLE_FOLDER=" << (getenv("WEBVIEW2_BROWSER_EXECUTABLE_FOLDER") ? getenv("WEBVIEW2_BROWSER_EXECUTABLE_FOLDER") : "NULL") << "\n";
    std::cout << "[RSJFW] [ENV] WINEPREFIX=" << prefixDir_ << "\n";
    std::cout << "[RSJFW] Running: wine \"" << executablePath << "\"\n";
    
    int pipefd[2];
    if (pipe(pipefd) == -1) {
        perror("pipe");
        return false;
    }

    pid_t pid = fork();
    if (pid == -1) {
        perror("fork");
        return false;
    }

    if (pid == 0) { 
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[1]);

        auto& cfg = Config::instance().getWine();
        std::string resolution = cfg.desktopResolution;
        
        std::string desktopArg;
        if (cfg.multipleDesktops) {
            desktopArg = "/desktop=RSJFW_" + std::to_string(getpid()) + "," + resolution;
        } else if (cfg.desktopMode) {
             desktopArg = "/desktop=RSJFW_Desktop," + resolution;
        }

        std::vector<std::string> finalArgs;
        finalArgs.push_back("wine");
        
        if (!desktopArg.empty()) {
            finalArgs.push_back("explorer");
            finalArgs.push_back(desktopArg);
        }
        
        finalArgs.push_back(executablePath);
        for (const auto& arg : args) {
            finalArgs.push_back(arg);
        }
        
        std::vector<char*> cArgs;
        for (const auto& s : finalArgs) {
             cArgs.push_back(const_cast<char*>(s.c_str()));
        }
        cArgs.push_back(nullptr);
        
        std::filesystem::path exeDir = std::filesystem::path(executablePath).parent_path();
        if (chdir(exeDir.c_str()) != 0) {
            perror("chdir");
        }

        execvp("wine", cArgs.data());
        perror("execvp");
        _exit(1);
    }

    close(pipefd[1]);
    FILE* stream = fdopen(pipefd[0], "r");
    char buffer[1024];

    std::string logDir = std::string(getenv("HOME")) + "/.local/share/rsjfw/logs";
    std::filesystem::create_directories(logDir);
    std::string logPath = logDir + "/studio_" + std::to_string(pid) + ".log";
    std::ofstream logFile(logPath);

    while (fgets(buffer, sizeof(buffer), stream) != nullptr) {
        std::string line(buffer);
        std::cout << line << std::flush;
        if (logFile.is_open()) {
            logFile << line << std::flush;
        }

        if (line.find("Fatal exiting due to Trouble launching Studio") != std::string::npos) {
            std::cerr << "\n[RSJFW] Detected fatal Roblox error. Killing Studio (PID: " << pid << ")...\n";
            if (logFile.is_open()) logFile << "\n[RSJFW] Detected fatal Roblox error. Killing Studio...\n";
            kill(pid, SIGTERM);
            sleep(1);
            kill(pid, SIGKILL);
            break;
        }
    }
    
    if (logFile.is_open()) logFile.close();

    fclose(stream);
    int status;
    waitpid(pid, &status, 0);
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

} // namespace rsjfw
