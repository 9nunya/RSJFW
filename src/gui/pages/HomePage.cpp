#include "rsjfw/pages/HomePage.hpp"
#include "rsjfw/gui.hpp"
#include "rsjfw/config.hpp"
#include "rsjfw/launcher.hpp"
#include <thread>
#include <cstdlib>
#include <filesystem>
#include <cstdio>
#include <unistd.h>

namespace rsjfw {

static bool isStudioRunning() {
    // Use pidof which doesn't match itself, look for the actual exe name
    int ret = system("pgrep -x 'RobloxStudioBeta.exe' >/dev/null 2>&1");
    if (ret == 0) return true;
    
    // Fallback: check with grep, excluding grep itself
    FILE* f = popen("ps aux 2>/dev/null | grep -i 'RobloxStudio' | grep -v grep | wc -l", "r");
    if (!f) return false;
    char buf[16] = {0};
    fgets(buf, sizeof(buf), f);
    pclose(f);
    return (atoi(buf) > 0);
}

static bool studioRunning = false;
static float lastCheckTime = -10.0f; // Force immediate check

HomePage::HomePage(GUI* gui, GLuint logoTexture, int logoWidth, int logoHeight)
    : gui_(gui), logoTexture_(logoTexture), logoWidth_(logoWidth), logoHeight_(logoHeight) {}

void HomePage::render() {
    ImVec2 windowSize = ImGui::GetContentRegionAvail();
    
    // Check Studio status every 3 seconds
    float currentTime = (float)ImGui::GetTime();
    if (currentTime - lastCheckTime > 3.0f) {
        studioRunning = isStudioRunning();
        lastCheckTime = currentTime;
    }
    
    // Compact logo
    if (logoTexture_ != 0) {
        float logoScale = 0.08f;
        float scaledWidth = logoWidth_ * logoScale;
        float scaledHeight = logoHeight_ * logoScale;
        float logoX = (windowSize.x - scaledWidth) * 0.5f;
        ImGui::SetCursorPosX(logoX);
        ImGui::Image((ImTextureID)(intptr_t)logoTexture_, ImVec2(scaledWidth, scaledHeight));
    }
    
    ImGui::Spacing();
    
    // Quick Actions
    float buttonWidth = 150.0f;
    float totalWidth = buttonWidth * 3 + 20;
    float startX = (windowSize.x - totalWidth) * 0.5f;
    
    ImGui::SetCursorPosX(startX);
    
    static float pulseTime = 0.0f;
    pulseTime += ImGui::GetIO().DeltaTime;
    float pulse = (sinf(pulseTime * 2.0f) + 1.0f) * 0.05f;
    
    if (studioRunning) {
        // Kill Studio - red warning color
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.1f, 0.1f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.9f, 0.2f, 0.2f, 1.0f));
        if (ImGui::Button("KILL STUDIO", ImVec2(buttonWidth, 40))) {
            system("wineserver -k 2>/dev/null");
            system("pkill -f RobloxStudio 2>/dev/null");
            studioRunning = false;
        }
        ImGui::PopStyleColor(2);
    } else {
        // Launch Studio - spawn new process using same binary
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.86f + pulse, 0.08f, 0.24f + pulse * 0.5f, 1.0f));
        if (ImGui::Button("LAUNCH STUDIO", ImVec2(buttonWidth, 40))) {
            // Get path to current executable
            char selfPath[1024] = {0};
            ssize_t len = readlink("/proc/self/exe", selfPath, sizeof(selfPath) - 1);
            if (len > 0) {
                selfPath[len] = '\0';
                pid_t pid = fork();
                if (pid == 0) {
                    execl(selfPath, "rsjfw", "launch", nullptr);
                    _exit(1);
                }
            }
        }
        ImGui::PopStyleColor();
    }
    
    ImGui::SameLine();
    if (ImGui::Button("OPEN PREFIX", ImVec2(buttonWidth, 40))) {
        std::string cmd = "xdg-open " + std::string(getenv("HOME")) + "/.rsjfw/wineprefix &";
        system(cmd.c_str());
    }
    
    ImGui::SameLine();
    if (ImGui::Button("WINECFG", ImVec2(buttonWidth, 40))) {
        std::thread([]() {
            std::string homeDir = std::getenv("HOME");
            Launcher launcher(homeDir + "/.rsjfw");
            launcher.openWineConfiguration();
        }).detach();
    }
    
    ImGui::Spacing();
    ImGui::SetCursorPosX(startX);
    
    if (ImGui::Button("OPEN LOGS", ImVec2(buttonWidth, 40))) {
        std::string cmd = "xdg-open " + std::string(getenv("HOME")) + "/.local/share/rsjfw &";
        system(cmd.c_str());
    }
    
    ImGui::SameLine();
    if (ImGui::Button("OPEN CONFIG", ImVec2(buttonWidth, 40))) {
        std::string cmd = "xdg-open " + std::string(getenv("HOME")) + "/.config/rsjfw &";
        system(cmd.c_str());
    }
    
    ImGui::SameLine();
    if (ImGui::Button("REINSTALL", ImVec2(buttonWidth, 40))) {
        char selfPath[1024] = {0};
        ssize_t len = readlink("/proc/self/exe", selfPath, sizeof(selfPath) - 1);
        if (len > 0) {
            selfPath[len] = '\0';
            pid_t pid = fork();
            if (pid == 0) {
                execl(selfPath, "rsjfw", "reinstall", nullptr);
                _exit(1);
            }
        }
    }
    
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    
    // Status info with running indicator
    if (studioRunning) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.3f, 0.9f, 0.3f, 1.0f));
        ImGui::Text("! STUDIO RUNNING");
        ImGui::PopStyleColor();
    }
    
    auto& cfg = Config::instance().getGeneral();
    
    const char* wineSourceStr = "System";
    if (cfg.wineSource == WineSource::CUSTOM) wineSourceStr = "Custom";
    else if (cfg.wineSource == WineSource::VINEGAR) wineSourceStr = "Vinegar";
    else if (cfg.wineSource == WineSource::PROTON_GE) wineSourceStr = "Proton-GE";
    
    ImGui::Text("Wine: %s", wineSourceStr);
    if (cfg.wineSource == WineSource::VINEGAR) ImGui::SameLine(), ImGui::TextDisabled("(%s)", cfg.vinegarVersion.c_str());
    if (cfg.wineSource == WineSource::PROTON_GE) ImGui::SameLine(), ImGui::TextDisabled("(%s)", cfg.protonVersion.c_str());
    
    ImGui::Text("DXVK: %s", cfg.dxvk ? (cfg.dxvkVersion.empty() ? "Latest" : cfg.dxvkVersion.c_str()) : "Disabled");
    ImGui::Text("Channel: %s", cfg.channel.c_str());
}

}
