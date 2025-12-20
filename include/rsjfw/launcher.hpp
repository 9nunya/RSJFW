#pragma once

#include <string>
#include <vector>

namespace rsjfw {

class Launcher {
public:
    Launcher(const std::string& rootDir);
    
    bool launchLatest(const std::vector<std::string>& extraArgs = {});
    bool launchVersion(const std::string& versionGUID, const std::vector<std::string>& extraArgs = {});
    bool setupPrefix();
    bool killStudio();
    bool setupFFlags(const std::string& versionGUID);
    bool installWebView2(const std::string& versionGUID);
    bool openWineConfiguration();
    bool setupDxvk(const std::string& versionGUID);
    
private: 
    bool installDxvk(const std::string& version);

    std::string rootDir_;
    std::string versionsDir_;
    std::string prefixDir_;
    
    std::string findStudioExecutable(const std::string& versionDir);
    bool runWine(const std::string& executablePath, const std::vector<std::string>& args = {});
};

} // namespace rsjfw
