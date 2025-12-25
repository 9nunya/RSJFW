#include "rsjfw/pages/TroubleshootingPage.hpp"
#include "rsjfw/config.hpp"
#include "rsjfw/gui.hpp"
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <GL/gl.h>

namespace rsjfw {

void TroubleshootingPage::render() {
    ImGui::Spacing();
    ImGui::TextWrapped("Tools to manage installed components and diagnose issues. Use 'Reset Configuration' if you encounter persistent errors.");
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    
    const std::string homeVal = std::getenv("HOME");
    const std::string rootPath = homeVal + "/.rsjfw";
    
    if (ImGui::Button("Delete All Versions", ImVec2(180, 0))) {
        try {
            std::filesystem::remove_all(rootPath + "/versions");
            std::filesystem::create_directories(rootPath + "/versions");
        } catch (...) {}
    }
    ImGui::SameLine();
    if (ImGui::Button("Delete Wine Prefix", ImVec2(180, 0))) {
        try { std::filesystem::remove_all(rootPath + "/prefix"); } catch (...) {}
    }
    
    if (ImGui::Button("Delete Downloaded Wine", ImVec2(180, 0))) {
        try { std::filesystem::remove_all(rootPath + "/wine"); } catch (...) {}
    }
    ImGui::SameLine();
    if (ImGui::Button("Delete DXVK", ImVec2(180, 0))) {
        try { std::filesystem::remove_all(rootPath + "/dxvk"); } catch (...) {}
    }
    
    ImGui::Spacing();
    if (ImGui::Button("Reset Configuration", ImVec2(180, 0))) {
        try { std::filesystem::remove(homeVal + "/.config/rsjfw/config.json"); } catch (...) {}
    }
    
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Text("Diagnostics");
    ImGui::Separator();
    ImGui::Spacing();
    
    if (ImGui::Button("Show System Info")) ImGui::OpenPopup("SystemInfoPopup");
    
    bool p_open = true;
    if (ImGui::BeginPopupModal("SystemInfoPopup", &p_open, ImGuiWindowFlags_AlwaysAutoResize)) {
        static std::string sysInfoText;
        if (sysInfoText.empty()) {
            std::string os = "Unknown";
            FILE* pipe = popen("uname -sr", "r");
            if (pipe) {
                char buffer[128];
                if (fgets(buffer, 128, pipe)) os = buffer;
                pclose(pipe);
                if (!os.empty() && os.back() == '\n') os.pop_back();
            }
            
            std::string cpu = "Unknown";
            std::ifstream cpuInfo("/proc/cpuinfo");
            if (cpuInfo) {
                std::string line;
                while (std::getline(cpuInfo, line)) {
                    if (line.find("model name") != std::string::npos) { cpu = line.substr(line.find(":") + 2); break; }
                }
            }
            
            const GLubyte* renderer = glGetString(GL_RENDERER);
            const GLubyte* version = glGetString(GL_VERSION);
            sysInfoText = "OS: " + os + "\nCPU: " + cpu + "\nGPU: " + (renderer ? (const char*)renderer : "Unknown") + "\nOpenGL: " + (version ? (const char*)version : "Unknown") + "\nRSJFW: 1.0.0\n";
        }
        
        ImGui::InputTextMultiline("##sysinfo", &sysInfoText[0], sysInfoText.size() + 1, ImVec2(400, 200), ImGuiInputTextFlags_ReadOnly);
        if (ImGui::Button("Copy to Clipboard")) ImGui::SetClipboardText(sysInfoText.c_str());
        ImGui::SameLine();
        if (ImGui::Button("Close")) { sysInfoText.clear(); ImGui::CloseCurrentPopup(); }
        ImGui::EndPopup();
    }
    
    ImGui::Spacing();
    ImGui::Text("Logs");
    static std::vector<std::string> logFiles;
    static int selectedLog = 0;
    const std::string logDir = homeVal + "/.local/share/rsjfw";
    
    if (ImGui::Button("Refresh Logs")) {
        logFiles.clear();
        if (std::filesystem::exists(logDir)) {
            for (const auto& entry : std::filesystem::directory_iterator(logDir)) 
                if (entry.path().extension() == ".log") logFiles.push_back(entry.path().filename().string());
            std::sort(logFiles.rbegin(), logFiles.rend());
        }
    }
    
    if (!logFiles.empty()) {
        std::vector<const char*> logItems;
        for (const auto& f : logFiles) logItems.push_back(f.c_str());
        ImGui::Combo("Log File", &selectedLog, logItems.data(), (int)logItems.size());
        if (ImGui::Button("Copy Log")) {
            std::ifstream ifs(std::filesystem::path(logDir) / logFiles[selectedLog]);
            std::stringstream buffer; buffer << ifs.rdbuf();
            ImGui::SetClipboardText(buffer.str().c_str());
        }
    }
}

}
