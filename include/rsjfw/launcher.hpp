#pragma once

#include <string>
#include <vector>

namespace rsjfw {

class Launcher {
public:
    Launcher(const std::string& rootDir);
    
    // Progress callback type: (progress 0.0-1.0, status message)
    using ProgressCb = std::function<void(float, std::string)>;

    bool launchLatest(const std::vector<std::string>& extraArgs = {}, ProgressCb progressCb = nullptr);
    bool launchVersion(const std::string& versionGUID, const std::vector<std::string>& extraArgs = {}, ProgressCb progressCb = nullptr);
    bool setupPrefix(ProgressCb progressCb = nullptr);
    bool killStudio();
    bool setupFFlags(const std::string& versionGUID, ProgressCb progressCb = nullptr);
    bool openWineConfiguration();
    bool setupDxvk(const std::string& versionGUID, ProgressCb progressCb = nullptr);
    
private: 
    std::string rootDir_;
    std::string versionsDir_;
    std::string prefixDir_;
    
    std::string findStudioExecutable(const std::string& versionDir);
    bool runWine(const std::string& executablePath, const std::vector<std::string>& args = {});

public:
    void setDebug(bool debug) { debug_ = debug; }

private:
    bool debug_ = false;
};

} // namespace rsjfw
