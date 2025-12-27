#ifndef RSJFW_DIAGNOSTICS_HPP
#define RSJFW_DIAGNOSTICS_HPP

#include <string>
#include <vector>
#include <functional>
#include <map>

namespace rsjfw {

struct HealthStatus {
    bool ok;
    std::string message;
    std::string detail;
    bool fixable = false;
    std::function<void(std::function<void(float, std::string)>)> fixAction;
    
    std::string category = "General";
    std::vector<std::string> tags;
};

// Category Constants
namespace HealthCategory {
    const std::string CRITICAL = "Critical";
    const std::string CONFIG = "Configuration";
    const std::string WINE = "Wine & Runners";
    const std::string SYSTEM = "System Assets";
    const std::string LEGACY = "Legacy Cleanup";
    const std::string NETWORK = "Network & API";
}

class Diagnostics {
public:
    static Diagnostics& instance();

    // Runs all health checks and returns true if all are OK
    bool runChecks();
    
    const std::vector<std::pair<std::string, HealthStatus>>& getResults() const { return results_; }
    
    // Returns number of failing checks
    int failureCount() const;
    
    // Helper to fix a specific failing check by name
    void fixIssue(const std::string& name, std::function<void(float, std::string)> progressCb);

private:
    Diagnostics() = default;
    std::vector<std::pair<std::string, HealthStatus>> results_;

    // Helper methods
    void checkRoot();
    void checkConfig();
    void checkWine();
    void checkLayer();
    void checkPrefix();
    void checkDesktop();
    void checkProtocol();
    void checkLegacy();
    void checkFlatpak();
    void checkSystem();
    
    // Advanced Helpers
    void buildLayerFromSource(std::function<void(float, std::string)> cb);
};

} // namespace rsjfw

#endif // RSJFW_DIAGNOSTICS_HPP
