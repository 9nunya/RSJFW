#ifndef RSJFW_TROUBLESHOOTINGPAGE_HPP
#define RSJFW_TROUBLESHOOTINGPAGE_HPP

#include "rsjfw/page.hpp"
#include "rsjfw/diagnostics.hpp"
#include "imgui.h"

namespace rsjfw {

class TroubleshootingPage : public Page {
public:
    TroubleshootingPage();
    void render() override;
    std::string title() const override { return "Troubleshooting"; }

    void renderDiagnosticsTab();
    void renderFixesTab();
    void renderLogsTab();
    void renderDebugTab();

private:
    int currentTab_ = 0;
    int targetTab_ = 0;
    float tabTransition_ = 0.0f;
    
    // Fix Modal State
    bool showFixModal_ = false;
    std::string fixStatus_ = "";
    float fixProgress_ = 0.0f;
    std::string currentFixName_ = "";
    
    std::vector<std::pair<std::string, HealthStatus>> healthChecks_;
    void runHealthChecks();
    
    std::vector<std::string> logFiles_;
    int selectedLog_ = 0;
    void refreshLogList();
};

} // namespace rsjfw

#endif // RSJFW_TROUBLESHOOTINGPAGE_HPP
