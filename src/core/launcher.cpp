#include "rsjfw/launcher.hpp"
#include "rsjfw/registry.hpp"
#include <filesystem>
#include <iostream>
#include <cstdlib>
#include <algorithm>
#include <fstream>
#include <cstring>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>

namespace rsjfw {

Launcher::Launcher(const std::string& rootDir)
    : rootDir_(rootDir) {
    versionsDir_ = (std::filesystem::path(rootDir) / "versions").string();
    prefixDir_ = (std::filesystem::path(rootDir) / "prefix").string();
    
    std::filesystem::create_directories(prefixDir_);
}

bool Launcher::setupPrefix() {
    std::cout << "[RSJFW] Setting up Wine prefix...\n";
    Registry reg(prefixDir_);
    
    // Disable Wine crash dialog
    reg.add("HKCU\\Software\\Wine\\WineDbg", "ShowCrashDialog", 0);
    
    // Enable EGL (recommended for Roblox)
    reg.add("HKCU\\Software\\Wine\\X11 Driver", "UseEGL", "Y");
    
    // DXVK Overrides
    reg.add("HKCU\\Software\\Wine\\DllOverrides", "dxgi", "native");
    reg.add("HKCU\\Software\\Wine\\DllOverrides", "d3d11", "native");
    
    return true;
}

bool Launcher::killStudio() {
    std::cout << "[RSJFW] Killing all Studio processes in prefix...\n";
    setenv("WINEPREFIX", prefixDir_.c_str(), 1);
    // wineserver -k kills all processes in the prefix
    return std::system("wineserver -k") == 0;
}

bool Launcher::installDxvk() {
    std::filesystem::path downloadsDir = std::filesystem::path(rootDir_) / "downloads";
    std::filesystem::create_directories(downloadsDir);
    std::string cacheFile = (downloadsDir / "dxvk-2.5.tar.gz").string();

    if (!std::filesystem::exists(cacheFile)) {
        std::cout << "[RSJFW] Downloading DXVK 2.5...\n";
        std::string url = "https://github.com/doitsujin/dxvk/releases/download/v2.5/dxvk-2.5.tar.gz";
        std::string cmd = "curl -L \"" + url + "\" -o \"" + cacheFile + "\"";
        if (std::system(cmd.c_str()) != 0) {
             std::cerr << "[RSJFW] Failed to download DXVK 2.5\n";
             return false;
        }
    }

    std::filesystem::path dxvkRoot = std::filesystem::path(rootDir_) / "dxvk";
    std::filesystem::create_directories(dxvkRoot);

    std::cout << "[RSJFW] Extracting DXVK...\n";
    std::string cmd = "tar -C \"" + dxvkRoot.string() + "\" -xzf \"" + cacheFile + "\"";
    return std::system(cmd.c_str()) == 0;
}

bool Launcher::setupDxvk(const std::string& versionGUID) {
    std::filesystem::path dxvkX64 = std::filesystem::path(rootDir_) / "dxvk" / "dxvk-2.5" / "x64";
    if (!std::filesystem::exists(dxvkX64)) {
        if (!installDxvk()) return false;
    }

    std::filesystem::path studioDir = std::filesystem::path(versionsDir_) / versionGUID;
    std::filesystem::copy_file(dxvkX64 / "dxgi.dll", studioDir / "dxgi.dll", std::filesystem::copy_options::overwrite_existing);
    std::filesystem::copy_file(dxvkX64 / "d3d11.dll", studioDir / "d3d11.dll", std::filesystem::copy_options::overwrite_existing);
    
    return true;
}

bool Launcher::launchLatest(const std::vector<std::string>& extraArgs) {
    // For prototype, we'll just pick the most recently modified version directory
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

bool Launcher::installWebView2(const std::string& versionGUID) {
    // Check if WebView2 is already in the prefix (simple check for existence)
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
    
    // Run installer silently
    std::string cmd = "wine \"" + installerPath.string() + "\" /silent /install";
    int res = std::system(cmd.c_str());
    return res == 0;
}

bool Launcher::setupFFlags(const std::string& versionGUID) {
    std::filesystem::path studioDir = std::filesystem::path(versionsDir_) / versionGUID;
    std::filesystem::path settingsDir = studioDir / "ClientSettings";
    std::filesystem::create_directories(settingsDir);
    
    std::filesystem::path settingsFile = settingsDir / "ClientAppSettings.json";
    
    // Default FFlags for compatibility and D3D11 (DXVK)
    std::string json = R"({
        "FFlagDebugGraphicsPreferD3D11": true,
        "FFlagDebugGraphicsDisableD3D11": false,
        "FFlagDebugGraphicsPreferD3D11FL10": false,
        "FFlagDebugGraphicsDisableD3D11FL10": true,
        "FFlagDebugGraphicsPreferVulkan": false,
        "FFlagDebugGraphicsDisableVulkan": true,
        "FFlagDebugGraphicsPreferOpenGL": false,
        "FFlagDebugGraphicsDisableOpenGL": true,
        "D3D11FL10": true
    })";

    std::ofstream ofs(settingsFile);
    if (ofs) ofs << json;

    // Also inject StudioAppSettings.json into the prefix AppData to bypass 403 flag fetching error
    std::string username = getenv("USER") ? getenv("USER") : "nunya";
    std::filesystem::path appDataDir = std::filesystem::path(prefixDir_) / "drive_c/users" / username / "AppData/Local/Roblox/ClientSettings";
    
    // In some Wine versions/configs, it might be 'Local Settings/Application Data'
    if (!std::filesystem::exists(std::filesystem::path(prefixDir_) / "drive_c/users" / username / "AppData")) {
        appDataDir = std::filesystem::path(prefixDir_) / "drive_c/users" / username / "Local Settings/Application Data/Roblox/ClientSettings";
    }

    std::cout << "[RSJFW] Injecting StudioAppSettings.json into prefix AppData...\n";
    std::filesystem::create_directories(appDataDir);
    std::ofstream ofsCache(appDataDir / "StudioAppSettings.json");
    if (ofsCache) {
        ofsCache << json;
    }

    return true;
}

bool Launcher::launchVersion(const std::string& versionGUID, const std::vector<std::string>& extraArgs) {
    std::filesystem::path studioDir = std::filesystem::path(versionsDir_) / versionGUID;
    
    // Official path in Wine prefix
    std::string username = getenv("USER") ? getenv("USER") : "nunya";
    std::filesystem::path officialRobloxDir = std::filesystem::path(prefixDir_) / "drive_c/users" / username / "AppData/Local/Roblox";
    std::filesystem::path officialVersionsDir = officialRobloxDir / "Versions";
    std::filesystem::path officialVersionDir = officialVersionsDir / versionGUID;

    std::cout << "[RSJFW] Ensuring official installation path...\n";
    std::filesystem::create_directories(officialVersionsDir);
    
    // Symlink if not already there
    if (!std::filesystem::exists(officialVersionDir)) {
        try {
            std::filesystem::create_directory_symlink(studioDir, officialVersionDir);
        } catch (const std::exception& e) {
            std::cerr << "[RSJFW] Failed to create symlink: " << e.what() << "\n";
            // Fallback: use the direct path
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

std::string Launcher::findStudioExecutable(const std::string& versionDir) {
    std::filesystem::path p(versionDir);
    p /= "RobloxStudioBeta.exe";
    if (std::filesystem::exists(p)) {
        return p.string();
    }
    return "";
}

bool Launcher::runWine(const std::string& executablePath, const std::vector<std::string>& args) {
    setenv("WINEPREFIX", prefixDir_.c_str(), 1);
    
    // Vinegar style DLL overrides (essential for WebView2 and performance)
    // Disabling mshtml and mscoree prevents Wine's internal browser prompts
    // dxgi,d3d11=n,b for DXVK
    setenv("WINEDLLOVERRIDES", "dxgi,d3d11=n,b;dxdiagn,winemenubuilder.exe,mscoree,mshtml=;msedgewebview2=n,b", 1);
    
    // Performance and stability variables from Vinegar
    setenv("WINEESYNC", "1", 1);
    setenv("MESA_GL_VERSION_OVERRIDE", "4.4", 1);
    setenv("__GL_THREADED_OPTIMIZATIONS", "1", 1);
    
    // WebView2 stability on Linux/Wine
    // Using a specific browser folder and user data path
    std::string username = getenv("USER") ? getenv("USER") : "nunya";
    std::string webviewPath = "C:\\Program Files (x86)\\Microsoft\\EdgeWebView\\Application\\143.0.3650.96";
    std::string webviewUserData = "C:\\users\\" + username + "\\AppData\\Local\\Roblox\\WebView2";
    
    setenv("WEBVIEW2_BROWSER_EXECUTABLE_FOLDER", webviewPath.c_str(), 1);
    setenv("WEBVIEW2_USER_DATA_FOLDER", webviewUserData.c_str(), 1);
    
    // Additional Chromium flags from Vinegar community
    setenv("WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS", "--no-sandbox --disable-features=WebRtcHideLocalIpsWithMdns --disable-gpu-sandbox --user-agent=\"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36 Edge/120.0.0.0\"", 1);

    // Hide Wine debugger and use EGL (Vinegar defaults)
    std::system(("WINEPREFIX=" + prefixDir_ + " wine reg add \"HKCU\\Software\\Wine\\WineDbg\" /v ShowCrashDialog /t REG_DWORD /d 0 /f > /dev/null 2>&1").c_str());
    std::system(("WINEPREFIX=" + prefixDir_ + " wine reg add \"HKCU\\Software\\Wine\\X11 Driver\" /v UseEGL /t REG_SZ /d \"Y\" /f > /dev/null 2>&1").c_str());
    
    // Credential Manager key (found in Vinegar code)
    std::system(("WINEPREFIX=" + prefixDir_ + " wine reg add \"HKCU\\Software\\Wine\\Credential Manager\" /f > /dev/null 2>&1").c_str());
    
    // Enable logging
    setenv("WINEDEBUG", "fixme-all,err-kerberos,err-ntlm,err-combase,+debugstr", 1); 
    
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

    if (pid == 0) { // Child
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[1]);

        std::vector<char*> wineArgs;
        wineArgs.push_back(const_cast<char*>("wine"));
        wineArgs.push_back(const_cast<char*>(executablePath.c_str()));
        for (const auto& arg : args) {
            wineArgs.push_back(const_cast<char*>(arg.c_str()));
        }
        wineArgs.push_back(nullptr);
        
        execvp("wine", wineArgs.data());
        perror("execvp");
        _exit(1);
    }

    // Parent
    close(pipefd[1]);
    FILE* stream = fdopen(pipefd[0], "r");
    char buffer[1024];

    while (fgets(buffer, sizeof(buffer), stream) != nullptr) {
        std::string line(buffer);
        std::cout << line << std::flush;

        if (line.find("Fatal exiting due to Trouble launching Studio") != std::string::npos) {
            std::cerr << "\n[RSJFW] Detected fatal Roblox error. Killing Studio (PID: " << pid << ")...\n";
            kill(pid, SIGTERM);
            // Give it a moment to die gracefully, then SIGKILL if needed
            sleep(1);
            kill(pid, SIGKILL);
            break;
        }
    }

    fclose(stream);
    int status;
    waitpid(pid, &status, 0);
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

} // namespace rsjfw
