#include "rsjfw/pages/TroubleshootingPage.hpp"
#include "rsjfw/config.hpp"
#include "rsjfw/gui.hpp"
#include "rsjfw/path_manager.hpp"
#include "rsjfw/process.hpp"
#include "rsjfw/logger.hpp"
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <GL/gl.h>

#include "rsjfw/task_runner.hpp"
#include "rsjfw/launcher.hpp"

namespace rsjfw {

TroubleshootingPage::TroubleshootingPage() {
    refreshLogList();
    // runHealthChecks(); // Lazy load instead
}

void TroubleshootingPage::render() {
    // Lazy load health checks on first view
    static bool firstRender = true;
    if (firstRender) {
        runHealthChecks();
        firstRender = false;
    }
    
    float sidebarWidth = 160.0f;
    
    // Sidebar
    ImGui::BeginChild("TroubleSidebar", ImVec2(sidebarWidth, 0), true, ImGuiWindowFlags_NoScrollbar);
    const char* tabs[] = {"Health", "Maintenance", "Logs"};
    for (int i = 0; i < 3; i++) {
        bool selected = (currentTab_ == i || (currentTab_ != targetTab_ && targetTab_ == i));
        if (selected) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.86f, 0.08f, 0.24f, 1.0f));
        }
        if (ImGui::Button(tabs[i], ImVec2(sidebarWidth - 16, 35))) {
             if (currentTab_ == targetTab_ && currentTab_ != i) {
                targetTab_ = i;
                tabTransition_ = 0.0f;
                // Actions on switch
                if (i == 0) runHealthChecks();
                if (i == 2) refreshLogList();
            }
        }
        if (selected) ImGui::PopStyleColor();
    }
    
    ImGui::SetCursorPosY(ImGui::GetWindowHeight() - 45);
    if (ImGui::Button("Refresh", ImVec2(sidebarWidth - 16, 30))) {
        runHealthChecks();
        refreshLogList();
    }
    ImGui::EndChild();

    ImGui::SameLine();

    // Content container with animation
    ImGui::BeginChild("TroubleContent", ImVec2(0, 0), true);

    // Animation Logic
    float dt = ImGui::GetIO().DeltaTime;
    const float transitionSpeed = 4.0f;
    
    if (currentTab_ != targetTab_) {
        tabTransition_ += dt * transitionSpeed;
        if (tabTransition_ >= 1.0f) {
            currentTab_ = targetTab_;
            tabTransition_ = 0.0f;
        }
    }
    
    float alpha = 1.0f;
    float offsetX = 0.0f;
    int displayTab = currentTab_;
    
    if (currentTab_ != targetTab_) {
        if (tabTransition_ < 0.5f) {
            // Fade out
            alpha = 1.0f - (tabTransition_ * 2.0f);
            offsetX = (targetTab_ > currentTab_ ? -20.0f : 20.0f) * (tabTransition_ * 2.0f);
            displayTab = currentTab_;
        } else {
            // Fade in
            alpha = (tabTransition_ - 0.5f) * 2.0f;
            offsetX = (targetTab_ > currentTab_ ? 20.0f : -20.0f) * (1.0f - (tabTransition_ - 0.5f) * 2.0f);
            displayTab = targetTab_;
        }
    }

    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offsetX);

    if (displayTab == 0) renderDiagnosticsTab();
    else if (displayTab == 1) renderFixesTab();
    else if (displayTab == 2) renderLogsTab();
    
    ImGui::PopStyleVar();
    ImGui::EndChild();

    // Render Fix Modal
    if (showFixModal_) {
        ImGui::OpenPopup("Fixing Issue");
    }

    if (ImGui::BeginPopupModal("Fixing Issue", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove)) {
        ImGui::Text("%s", currentFixName_.c_str());
        ImGui::Spacing();
        ImGui::ProgressBar(fixProgress_, ImVec2(300, 20));
        ImGui::Text("%s", fixStatus_.c_str());
        
        static float autoCloseTimer = 0.0f;
        bool isComplete = (fixProgress_ >= 1.0f); // Just check progress, not status string
        bool isFailed = (fixStatus_.find("Failed") != std::string::npos || fixStatus_.find("Missing") != std::string::npos);
        
        if (isComplete || isFailed) {
            autoCloseTimer += ImGui::GetIO().DeltaTime;
            ImGui::Spacing();
            
            if (!isFailed) {
                ImGui::TextDisabled("Closing in %.1fs...", std::max(0.0f, 1.5f - autoCloseTimer));
            }
            
            if ((autoCloseTimer >= 1.5f && !isFailed) || ImGui::Button(isFailed ? "Close" : "Close Now", ImVec2(120, 30))) {
                showFixModal_ = false;
                autoCloseTimer = 0.0f;
                ImGui::CloseCurrentPopup();
                runHealthChecks();
            }
        } else {
            autoCloseTimer = 0.0f;
        }
        ImGui::EndPopup();
    }
}

void TroubleshootingPage::renderDiagnosticsTab() {
    ImGui::Text("System Health & Diagnostics");
    ImGui::Separator();
    ImGui::Spacing();
    
    // Search Box
    static char searchBuf[128] = "";
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##Search", "Search checks (or tag:config)...", searchBuf, IM_ARRAYSIZE(searchBuf));
    ImGui::Spacing();

    // Filtering Logic
    std::string query = searchBuf;
    std::transform(query.begin(), query.end(), query.begin(), ::tolower);
    bool useTag = (query.find("tag:") == 0);
    std::string tagQuery = useTag ? query.substr(4) : "";

    // Grouping
    std::map<std::string, std::vector<std::pair<std::string, HealthStatus>>> grouped;
    for (const auto& check : healthChecks_) {
        // Filter
        bool match = true;
        if (!query.empty()) {
             if (useTag) {
                 match = false;
                 for (const auto& tag : check.second.tags) {
                     if (tag.find(tagQuery) != std::string::npos) { match = true; break; }
                 }
             } else {
                 std::string nameLower = check.first;
                 std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(), ::tolower);
                 std::string msgLower = check.second.message;
                 std::transform(msgLower.begin(), msgLower.end(), msgLower.begin(), ::tolower);
                 
                 match = (nameLower.find(query) != std::string::npos || msgLower.find(query) != std::string::npos);
             }
        }
        
        if (match) grouped[check.second.category].push_back(check);
    }
    
    // Render Groups
    for (const auto& [category, checks] : grouped) {
         if (ImGui::TreeNodeEx((category + " (" + std::to_string(checks.size()) + ")").c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
             for (const auto& check : checks) {
                ImGui::PushID(check.first.c_str());
                
                // Header Row
                ImVec4 color = check.second.ok ? ImVec4(0.3f, 0.9f, 0.3f, 1.0f) : ImVec4(1.0f, 0.4f, 0.4f, 1.0f);
                ImGui::TextColored(color, "%s", check.first.c_str());
                
                ImGui::SameLine();
                float avail = ImGui::GetContentRegionAvail().x;
                
                if (check.second.fixable && !check.second.ok) {
                    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + avail - 80);
                    
                    if (check.first == "Wine Source") {
                        if (ImGui::Button("CONFIGURE", ImVec2(70, 20))) {
                             GUI::instance().navigateToSettingsWine(true);
                        }
                    } else {
                         ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.6f, 0.2f, 1.0f));
                         if (ImGui::Button("FIX", ImVec2(50, 20))) {
                             showFixModal_ = true;
                             currentFixName_ = "Fixing " + check.first;
                             fixProgress_ = 0.0f;
                             fixStatus_ = "Starting...";
                             
                             auto fixFunc = check.second.fixAction;
                             TaskRunner::instance().run([this, fixFunc]() {
                                 fixFunc([this](float p, std::string s) {
                                     fixProgress_ = p;
                                     fixStatus_ = s;
                                 });
                             });
                         }
                         ImGui::PopStyleColor();
                    }
                } else {
                    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + avail - 40);
                    ImGui::TextColored(color, check.second.ok ? "[OK]" : "[FAIL]");
                }
        
                // Body
                if (!check.second.message.empty()) {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.9f, 0.9f, 0.9f));
                    ImGui::TextWrapped("%s", check.second.message.c_str());
                    ImGui::PopStyleColor();
                }
                
                if (!check.second.detail.empty()) {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.7f, 0.6f));
                    ImGui::TextWrapped("%s", check.second.detail.c_str());
                    ImGui::PopStyleColor();
                }
                
                // Tags
                if (!check.second.tags.empty()) {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 0.8f));
                    std::string tagStr = "Tags: ";
                    for(const auto& t : check.second.tags) tagStr += t + " ";
                    ImGui::TextWrapped("%s", tagStr.c_str());
                    ImGui::PopStyleColor();
                }
                
                ImGui::PopID();
                ImGui::Separator();
                ImGui::Spacing();
             }
             ImGui::TreePop();
         }
    }
    
    
    ImGui::Separator();
    if (ImGui::Button("Full Diagnostic Report")) ImGui::OpenPopup("SysInfoPopup");
    
    if (ImGui::BeginPopupModal("SysInfoPopup", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        static std::string sysInfo;
        if (sysInfo.empty()) {
            sysInfo = "=== RSJFW DIAGNOSTIC REPORT ===\n";
            sysInfo += "Paths:\n";
            sysInfo += "  Root: " + PathManager::instance().root().string() + "\n";
            sysInfo += "  Log: " + PathManager::instance().currentLog().string() + "\n";
            
            const GLubyte* renderer = glGetString(GL_RENDERER);
            sysInfo += "Graphics:\n";
            sysInfo += "  GPU: " + std::string(renderer ? (const char*)renderer : "Unknown") + "\n";
            sysInfo += "  GL Ver: " + std::string((const char*)glGetString(GL_VERSION)) + "\n";
        }
        
        ImGui::InputTextMultiline("##sysinfo", &sysInfo[0], sysInfo.size()+1, ImVec2(500, 300), ImGuiInputTextFlags_ReadOnly);
        if (ImGui::Button("Copy to Clipboard")) ImGui::SetClipboardText(sysInfo.c_str());
        ImGui::SameLine();
        if (ImGui::Button("Close")) { sysInfo.clear(); ImGui::CloseCurrentPopup(); }
        ImGui::EndPopup();
    }
}

void TroubleshootingPage::renderFixesTab() {
    ImGui::Text("Maintenance & Repairs");
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::Text("Active Instances");
    ImGui::Spacing();
    if (ImGui::Button("Kill All Roblox Studio Processes", ImVec2(250, 35))) {
        for (const auto& proc : Process::findByName("RobloxStudioBeta.exe")) {
            Process::kill(proc.pid, true);
        }
        for (const auto& proc : Process::findByName("wine")) {
            Process::kill(proc.pid, true);
        }
    }
    ImGui::TextDisabled("Force closes any hanging Studio or Wine instances.");
    
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::Text("Data Management");
    ImGui::Spacing();
    
    auto& pm = PathManager::instance();
    
    if (ImGui::Button("Clear All Versions", ImVec2(200, 30))) {
        std::filesystem::remove_all(pm.versions());
        std::filesystem::create_directories(pm.versions());
    }
    ImGui::SameLine();
    if (ImGui::Button("Nuke Wine Prefix", ImVec2(200, 30))) {
        std::filesystem::remove_all(pm.prefix());
    }
    
    if (ImGui::Button("Remove Cached Wine", ImVec2(200, 30))) {
        std::filesystem::remove_all(pm.wine());
        std::filesystem::create_directories(pm.wine());
    }
    ImGui::SameLine();
    if (ImGui::Button("Remove DXVK Cache", ImVec2(200, 30))) {
        std::filesystem::remove_all(pm.dxvk());
        std::filesystem::create_directories(pm.dxvk());
    }
    
    ImGui::Spacing();
    if (ImGui::Button("Reset All Configuration", ImVec2(200, 35))) {
        // This will be re-created on next boot
        std::filesystem::path cfgPath = pm.root() / "config.json";
        std::filesystem::remove(cfgPath);
    }
    ImGui::TextDisabled("Recommended if settings become corrupted.");
    
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.1f, 0.1f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 0.2f, 0.2f, 1.0f));
    if (ImGui::Button("NUKE RSJFW INSTALLATION (DANGER)", ImVec2(300, 35))) {
        ImGui::OpenPopup("NukeConfirm");
    }
    ImGui::PopStyleColor(2);
    ImGui::TextDisabled("Deletes EVERYTHING (Prefix, Versions, Config, Cache). Irreversible.");

    if (ImGui::BeginPopupModal("NukeConfirm", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Are you absolutely sure?");
        ImGui::Text("This will delete all RSJFW data including your Wine prefix and Roblox Studio install.");
        ImGui::Text("RSJFW will close immediately after.");
        ImGui::Spacing();
        
        if (ImGui::Button("Yes, Delete Everything", ImVec2(200, 30))) {
            // Delete all RSJFW data (including lib directory inside root)
            std::filesystem::remove_all(PathManager::instance().root());
            
            // Delete user Vulkan layer JSON if present
            std::filesystem::path vulkanJson = std::filesystem::path(getenv("HOME")) / ".local/share/vulkan/implicit_layer.d/VkLayer_RSJFW_RsjfwLayer.json";
            std::filesystem::remove(vulkanJson);
            
            // Delete desktop file
            std::filesystem::remove(std::filesystem::path(getenv("HOME")) / ".local/share/applications/rsjfw.desktop");
            
            exit(0);
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(100, 30))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void TroubleshootingPage::renderLogsTab() {
    ImGui::Text("Application Logs");
    ImGui::Separator();
    ImGui::Spacing();

    if (logFiles_.empty()) {
        ImGui::TextDisabled("No logs found.");
    } else {
        std::vector<const char*> items;
        for (const auto& log : logFiles_) items.push_back(log.c_str());
        
        ImGui::SetNextItemWidth(300);
        ImGui::Combo("##logselect", &selectedLog_, items.data(), (int)items.size());
        
        ImGui::SameLine();
        static bool liveMode = false;
        if (ImGui::Checkbox("Live Tail", &liveMode)) {
            // When enabling live mode, scroll to bottom
        }
        
        ImGui::Spacing();
        
        // Filter toggles
        static bool showErrors = true;
        static bool showWarnings = true;
        static bool showInfo = true;
        
        ImGui::Text("Filters:");
        ImGui::SameLine();
        ImGui::Checkbox("Errors", &showErrors);
        ImGui::SameLine();
        ImGui::Checkbox("Warnings", &showWarnings);
        ImGui::SameLine();
        ImGui::Checkbox("Info", &showInfo);
        
        ImGui::Spacing();
        
        // Log content area
        static std::string logContent;
        static std::string lastLogPath;
        static auto lastRefresh = std::chrono::steady_clock::now();
        static bool autoScroll = true;
        
        std::string currentLogPath = (PathManager::instance().logs() / logFiles_[selectedLog_]).string();
        auto now = std::chrono::steady_clock::now();
        bool needsRefresh = (currentLogPath != lastLogPath) || 
                           (liveMode && std::chrono::duration_cast<std::chrono::milliseconds>(now - lastRefresh).count() > 200);
        
        if (needsRefresh) {
            std::ifstream ifs(currentLogPath);
            std::stringstream buffer; 
            buffer << ifs.rdbuf();
            logContent = buffer.str();
            lastLogPath = currentLogPath;
            lastRefresh = now;
        }
        
        // Auto-scroll toggle
        ImGui::Checkbox("Auto-scroll", &autoScroll);
        ImGui::SameLine();
        if (ImGui::Button("Copy All", ImVec2(80, 0))) {
            ImGui::SetClipboardText(logContent.c_str());
        }
        ImGui::SameLine();
        if (ImGui::Button("Open Folder", ImVec2(100, 0))) {
            std::string cmd = "xdg-open " + PathManager::instance().logs().string() + " &";
            system(cmd.c_str());
        }
        
        ImGui::Spacing();
        
        // Log viewer with filtering
        ImGui::BeginChild("LogContent", ImVec2(0, 0), true, ImGuiWindowFlags_HorizontalScrollbar);
        
        std::istringstream stream(logContent);
        std::string line;
        while (std::getline(stream, line)) {
            bool isError = line.find("[ERROR]") != std::string::npos || line.find("error") != std::string::npos;
            bool isWarning = line.find("[WARN]") != std::string::npos || line.find("warn") != std::string::npos;
            bool isInfo = !isError && !isWarning;
            
            if ((isError && showErrors) || (isWarning && showWarnings) || (isInfo && showInfo)) {
                if (isError) {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
                } else if (isWarning) {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.8f, 0.2f, 1.0f));
                } else {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.7f, 1.0f));
                }
                ImGui::TextUnformatted(line.c_str());
                ImGui::PopStyleColor();
            }
        }
        
        if (autoScroll && liveMode) {
            ImGui::SetScrollHereY(1.0f);
        }
        
        ImGui::EndChild();
    }
}

// ...
void TroubleshootingPage::runHealthChecks() {
    auto& diag = Diagnostics::instance();
    diag.runChecks();
    healthChecks_ = diag.getResults();
}

void TroubleshootingPage::refreshLogList() {
    logFiles_.clear();
    auto logsDir = PathManager::instance().logs();
    if (std::filesystem::exists(logsDir)) {
        for (const auto& entry : std::filesystem::directory_iterator(logsDir)) {
            if (entry.path().extension() == ".log") {
                logFiles_.push_back(entry.path().filename().string());
            }
        }
        std::sort(logFiles_.rbegin(), logFiles_.rend());
    }
}

} // namespace rsjfw
