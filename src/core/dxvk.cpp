#include "rsjfw/dxvk.hpp"
#include "rsjfw/logger.hpp"
#include <filesystem>
#include <cstdlib>
#include <vector>

namespace fs = std::filesystem;

namespace rsjfw {
namespace dxvk {

bool install(wine::Prefix& pfx, const std::string& dxvkRootDir) {
    if (!fs::exists(dxvkRootDir)) {
        LOG_ERROR("DXVK root dir not found: " + dxvkRootDir);
        return false;
    }

    auto copyDlls = [&](const fs::path& sourceDir, const fs::path& destDir) {
        if (!fs::exists(sourceDir)) return;
        fs::create_directories(destDir);
        for (const auto& entry : fs::directory_iterator(sourceDir)) {
            if (entry.path().extension() == ".dll") {
                try {
                    fs::copy_file(entry.path(), destDir / entry.path().filename(), fs::copy_options::overwrite_existing);
                    LOG_INFO("Installed " + entry.path().filename().string() + " to " + destDir.string());
                } catch (const std::exception& e) {
                    LOG_ERROR("Failed to copy DLL: " + std::string(e.what()));
                }
            }
        }
    };

    fs::path system32 = fs::path(pfx.dir()) / "drive_c" / "windows" / "system32";
    fs::path syswow64 = fs::path(pfx.dir()) / "drive_c" / "windows" / "syswow64";
    
    fs::path rootPath(dxvkRootDir);
    
    copyDlls(rootPath / "x64", system32);
    
    if (fs::exists(rootPath / "x32")) {
        copyDlls(rootPath / "x32", syswow64);
    } else if (fs::exists(rootPath / "x86")) {
        copyDlls(rootPath / "x86", syswow64);
    }

    return true;
}

void envOverride(wine::Prefix& pfx, bool enabled) {
    std::string val = "d3d9,d3d10core,d3d11,dxgi=";
    val += (enabled ? "n,b" : "b,n");
    
    pfx.appendEnv("WINEDLLOVERRIDES", val);
}

} // namespace dxvk
} // namespace rsjfw
