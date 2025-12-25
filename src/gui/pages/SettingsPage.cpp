#include "rsjfw/pages/SettingsPage.hpp"
#include "rsjfw/gui.hpp"
#include "rsjfw/config.hpp"
#include "rsjfw/downloader.hpp"
#include "rsjfw/launcher.hpp"
#include <thread>
#include <cstring>

namespace rsjfw {

static std::string pickDirectory(const std::string& title) {
    std::string result;
    std::string cmd = "gdbus call --session --dest org.freedesktop.portal.Desktop "
                      "--object-path /org/freedesktop/portal/desktop "
                      "--method org.freedesktop.portal.FileChooser.OpenFile '' "
                      "'{\"title\": <\"" + title + "\">, \"directory\": <true>}' 2>/dev/null | "
                      "grep -oP \"file://\\K[^']+\" | head -1";
    FILE* f = popen(cmd.c_str(), "r");
    if (f) {
        char path[512];
        if (fgets(path, sizeof(path), f)) {
            result = path;
            if (!result.empty() && result.back() == '\n') result.pop_back();
        }
        pclose(f);
    }
    if (result.empty()) {
        f = popen("zenity --file-selection --directory 2>/dev/null || kdialog --getexistingdirectory ~ 2>/dev/null", "r");
        if (f) {
            char path[512];
            if (fgets(path, sizeof(path), f)) {
                result = path;
                if (!result.empty() && result.back() == '\n') result.pop_back();
            }
            pclose(f);
        }
    }
    return result;
}

static bool checkedVersions = false;
static bool fetchingVersions = false;
static std::vector<std::string> dxvkVersions;
static std::vector<std::string> vinegarVersions;
static std::vector<std::string> protonVersions;
static bool fetchingWineVersions = false;
static bool checkedVinegarVersions = false;
static bool checkedProtonVersions = false;

SettingsPage::SettingsPage(GUI* gui) : gui_(gui) {}

void SettingsPage::render() {
    auto& cfg = Config::instance();
    
    if (!checkedVersions && !fetchingVersions && cfg.getGeneral().dxvkSource != DxvkSource::CUSTOM) {
        fetchingVersions = true;
        std::thread([&]() {
            Downloader dl(std::getenv("HOME") + std::string("/.rsjfw"));
            dxvkVersions = dl.fetchDxvkVersions(Config::instance().getGeneral().dxvkSource);
            if (!dxvkVersions.empty() && Config::instance().getGeneral().dxvkVersion.empty()) {
                Config::instance().getGeneral().dxvkVersion = dxvkVersions[0];
            }
            checkedVersions = true;
            fetchingVersions = false;
        }).detach();
    }

    if (cfg.getGeneral().wineSource == WineSource::VINEGAR && !checkedVinegarVersions && !fetchingWineVersions) {
        fetchingWineVersions = true;
        std::thread([&]() {
            Downloader dl(std::getenv("HOME") + std::string("/.rsjfw"));
            vinegarVersions = dl.fetchWineVersions(WineSource::VINEGAR);
            checkedVinegarVersions = true;
            fetchingWineVersions = false;
        }).detach();
    }

    if (cfg.getGeneral().wineSource == WineSource::PROTON_GE && !checkedProtonVersions && !fetchingWineVersions) {
        fetchingWineVersions = true;
        std::thread([&]() {
            Downloader dl(std::getenv("HOME") + std::string("/.rsjfw"));
            protonVersions = dl.fetchWineVersions(WineSource::PROTON_GE);
            checkedProtonVersions = true;
            fetchingWineVersions = false;
        }).detach();
    }

    if (ImGui::BeginTabBar("ConfigTabs")) {
        if (ImGui::BeginTabItem("General")) {
            renderGeneralTab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Wine")) {
            renderWineTab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("FFlags")) {
            renderFFlagsTab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Environment")) {
            renderEnvTab();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
}

void SettingsPage::renderGeneralTab() {
    auto& cfg = Config::instance();
    bool changed = false;
    
    if (!checkedVersions && !fetchingVersions && cfg.getGeneral().dxvkSource != DxvkSource::CUSTOM) {
        fetchingVersions = true;
        std::thread([]() {
            Downloader dl(std::getenv("HOME") + std::string("/.rsjfw"));
            dxvkVersions = dl.fetchDxvkVersions(Config::instance().getGeneral().dxvkSource);
            if (!dxvkVersions.empty() && Config::instance().getGeneral().dxvkVersion.empty()) {
                Config::instance().getGeneral().dxvkVersion = dxvkVersions[0];
            }
            checkedVersions = true;
            fetchingVersions = false;
        }).detach();
    }
    
    ImGui::Spacing();
    const char* renderers[] = { "D3D11", "Vulkan", "OpenGL", "D3D11FL10" };
    static int currentRendererIdx = 0;
    std::string currentRenderer = cfg.getGeneral().renderer;
    for (int i=0; i < 4; i++) if (currentRenderer == renderers[i]) currentRendererIdx = i;
    
    if (ImGui::Combo("Renderer", &currentRendererIdx, renderers, 4)) {
        cfg.getGeneral().renderer = renderers[currentRendererIdx];
        changed = true;
    }
    
    bool dxvk = cfg.getGeneral().dxvk;
    if (ImGui::Checkbox("Enable DXVK", &dxvk)) {
        cfg.getGeneral().dxvk = dxvk;
        changed = true;
    }
    
    ImGui::Spacing();
    ImGui::TextWrapped("DXVK translates DirectX 11 calls to Vulkan, improving performance. Disable if you want to use native Vulkan or WineD3D.");
    ImGui::Spacing();

    if (dxvk) {
        static int dxvkSourceIdx = (int)cfg.getGeneral().dxvkSource;
        const char* dxvkSources[] = { "Official", "Sarek", "Custom" };
        if (ImGui::Combo("DXVK Source", &dxvkSourceIdx, dxvkSources, 3)) {
            if ((DxvkSource)dxvkSourceIdx != cfg.getGeneral().dxvkSource) {
                cfg.getGeneral().dxvkSource = (DxvkSource)dxvkSourceIdx;
                cfg.getGeneral().dxvkVersion = "Latest"; 
                dxvkVersions.clear();
                checkedVersions = false; 
                changed = true;
            }
        }

        if (cfg.getGeneral().dxvkSource == DxvkSource::CUSTOM) {
             char buf[256];
             strncpy(buf, cfg.getGeneral().dxvkCustomPath.c_str(), sizeof(buf));
             ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 50);
             if (ImGui::InputText("##dxvkpath", buf, sizeof(buf))) {
                 cfg.getGeneral().dxvkCustomPath = std::string(buf);
                 changed = true;
             }
             ImGui::SameLine();
             if (ImGui::Button("...##dxvk")) {
                 std::string p = pickDirectory("Select DXVK Directory");
                 if (!p.empty()) { cfg.getGeneral().dxvkCustomPath = p; changed = true; }
             }
             ImGui::SameLine(); ImGui::Text("Custom DXVK Path");
        } else {
             if (fetchingVersions) {
                 ImGui::TextDisabled("Fetching versions...");
             } else if (!dxvkVersions.empty()) {
                 static int currentVerIdx = 0;
                 std::string curVer = cfg.getGeneral().dxvkVersion;
                 for (size_t i = 0; i < dxvkVersions.size(); ++i) {
                     if (dxvkVersions[i] == curVer) { currentVerIdx = (int)i; break; }
                 }
                 
                 std::vector<const char*> items;
                 for (const auto& v : dxvkVersions) items.push_back(v.c_str());
                 if (ImGui::Combo("DXVK Version", &currentVerIdx, items.data(), (int)items.size())) {
                     cfg.getGeneral().dxvkVersion = dxvkVersions[currentVerIdx];
                     changed = true;
                 }
             }
        }
    }
    
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Text("Versioning");
    
    char verBuf[64];
    strncpy(verBuf, cfg.getGeneral().robloxVersion.c_str(), sizeof(verBuf));
    if (ImGui::InputText("Roblox Version Override", verBuf, sizeof(verBuf))) {
        cfg.getGeneral().robloxVersion = std::string(verBuf);
        changed = true;
    }
    
    const char* channels[] = { "LIVE", "production", "zcanary", "zintegration", "Custom" };
    static int currentChannelIdx = 0; 
    std::string curChan = cfg.getGeneral().channel;
    bool customChannel = true;
    for (int i=0; i<4; i++) if (curChan == channels[i]) { currentChannelIdx = i; customChannel = false; break; }
    if (customChannel) currentChannelIdx = 4;
    
    if (ImGui::Combo("Channel", &currentChannelIdx, channels, 5)) {
        if (currentChannelIdx < 4) {
            cfg.getGeneral().channel = channels[currentChannelIdx];
            changed = true;
        }
    }
    
    if (currentChannelIdx == 4) {
        char chanBuf[64];
        strncpy(chanBuf, cfg.getGeneral().channel.c_str(), sizeof(chanBuf));
        if (ImGui::InputText("Custom Channel", chanBuf, sizeof(chanBuf))) {
            cfg.getGeneral().channel = std::string(chanBuf);
            changed = true;
        }
    }

    if (changed) cfg.save();
}

void SettingsPage::renderWineTab() {
    auto& cfg = Config::instance();
    bool changed = false;
    
    if (cfg.getGeneral().wineSource == WineSource::VINEGAR && !checkedVinegarVersions && !fetchingWineVersions) {
        fetchingWineVersions = true;
        std::thread([]() {
            Downloader dl(std::getenv("HOME") + std::string("/.rsjfw"));
            vinegarVersions = dl.fetchWineVersions(WineSource::VINEGAR);
            checkedVinegarVersions = true;
            fetchingWineVersions = false;
        }).detach();
    }

    if (cfg.getGeneral().wineSource == WineSource::PROTON_GE && !checkedProtonVersions && !fetchingWineVersions) {
        fetchingWineVersions = true;
        std::thread([]() {
            Downloader dl(std::getenv("HOME") + std::string("/.rsjfw"));
            protonVersions = dl.fetchWineVersions(WineSource::PROTON_GE);
            checkedProtonVersions = true;
            fetchingWineVersions = false;
        }).detach();
    }
    
    ImGui::Spacing();
    static int wineSourceIdx = (int)cfg.getGeneral().wineSource;
    const char* wineSources[] = { "System", "Custom Path", "Vinegar (fight me)", "Proton-GE" };
    if (ImGui::Combo("Wine Source", &wineSourceIdx, wineSources, 4)) {
        cfg.getGeneral().wineSource = (WineSource)wineSourceIdx;
        changed = true;
        checkedVinegarVersions = false;
        checkedProtonVersions = false;
    }
    
    if (cfg.getGeneral().wineSource == WineSource::CUSTOM) {
        char pathBuf[256];
        strncpy(pathBuf, cfg.getGeneral().wineRoot.c_str(), sizeof(pathBuf));
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 50);
        if (ImGui::InputText("##winepath", pathBuf, sizeof(pathBuf))) {
            cfg.getGeneral().wineRoot = std::string(pathBuf);
            changed = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("...##wine")) {
            std::string p = pickDirectory("Select Wine Directory");
            if (!p.empty()) { cfg.getGeneral().wineRoot = p; changed = true; }
        }
        ImGui::SameLine(); ImGui::Text("Custom Wine Path");
    } else if (cfg.getGeneral().wineSource == WineSource::VINEGAR || cfg.getGeneral().wineSource == WineSource::PROTON_GE) {
        auto& wineVer = (cfg.getGeneral().wineSource == WineSource::VINEGAR) ? cfg.getGeneral().vinegarVersion : cfg.getGeneral().protonVersion;
        auto& versions = (cfg.getGeneral().wineSource == WineSource::VINEGAR) ? vinegarVersions : protonVersions;

        if (fetchingWineVersions) {
            ImGui::TextDisabled("Fetching versions...");
        } else if (!versions.empty()) {
            static int currentWineVerIdx = 0;
            for (size_t i = 0; i < versions.size(); ++i) {
                if (versions[i] == wineVer) { currentWineVerIdx = (int)i; break; }
            }

            std::vector<const char*> items;
            for (const auto& v : versions) items.push_back(v.c_str());
            if (ImGui::Combo("Wine Version", &currentWineVerIdx, items.data(), (int)items.size())) {
                wineVer = versions[currentWineVerIdx];
                changed = true;
            }
        }
    }
    
    ImGui::Spacing();
    ImGui::TextWrapped("Select the Wine runner. 'Vinegar' and 'Proton-GE' are downloaded automatically. 'System' uses your installed wine.");
    ImGui::Spacing();
    
    bool desktopMode = cfg.getWine().desktopMode;
    if (ImGui::Checkbox("Desktop Mode (Persistent)", &desktopMode)) {
        cfg.getWine().desktopMode = desktopMode;
        changed = true;
    }
    
    bool multiDesktop = cfg.getWine().multipleDesktops;
    if (ImGui::Checkbox("Multi-Desktop (Unique Session)", &multiDesktop)) {
        cfg.getWine().multipleDesktops = multiDesktop;
        changed = true;
    }
    
    char resBuf[64];
    strncpy(resBuf, cfg.getWine().desktopResolution.c_str(), sizeof(resBuf));
    if (ImGui::InputText("Desktop Resolution", resBuf, sizeof(resBuf))) {
        cfg.getWine().desktopResolution = std::string(resBuf);
        changed = true;
    }
    
    ImGui::Spacing();
    if (ImGui::Button("Open Wine Configuration (winecfg)")) {
         std::thread([]() {
             const std::string homeDir = std::getenv("HOME");
             Launcher launcher(homeDir + "/.rsjfw");
             launcher.openWineConfiguration();
         }).detach();
    }

    if (changed) cfg.save();
}

void SettingsPage::renderFFlagsTab() {
    auto& cfg = Config::instance();
    bool changed = false;
    
    ImGui::Spacing();
    ImGui::Text("Fast Flags Configuration");
    ImGui::Separator();
    ImGui::TextWrapped("Common performance and visual tweaks.");
    ImGui::Spacing();
    
    ImGui::Text("Presets / Smart Settings");
    ImGui::Spacing();
    
    // FPS Limit
    int currentFps = 60;
    if (cfg.getFFlags().contains("DFIntTaskSchedulerTargetFps")) {
        try { currentFps = cfg.getFFlags()["DFIntTaskSchedulerTargetFps"].get<int>(); } catch(...) {}
    }
    if (currentFps > 240) currentFps = 241;
    
    int sliderFps = currentFps;
    if (ImGui::SliderInt("FPS Limit", &sliderFps, 30, 241, (sliderFps > 240 ? "Unlock" : "%d"))) {
        if (sliderFps > 240) cfg.getFFlags()["DFIntTaskSchedulerTargetFps"] = 9999;
        else cfg.getFFlags()["DFIntTaskSchedulerTargetFps"] = sliderFps;
        changed = true;
    }

    // DPI Scaling
    bool disableDpi = false;
    if (cfg.getFFlags().contains("DFFlagDisableDPIScale")) {
        try { disableDpi = cfg.getFFlags()["DFFlagDisableDPIScale"].get<bool>(); } catch(...) {}
    }
    if (ImGui::Checkbox("Disable DPI Scaling", &disableDpi)) {
        cfg.getFFlags()["DFFlagDisableDPIScale"] = disableDpi;
        changed = true;
    }

    // Lighting Technology
    bool future = false;
    if (cfg.getFFlags().contains("FFlagDebugForceFutureIsBrightPhase3")) {
        try { future = cfg.getFFlags()["FFlagDebugForceFutureIsBrightPhase3"].get<bool>(); } catch(...) {}
    }
    
    int lightMode = future ? 1 : 0;
    const char* lightModes[] = { "Default (ShadowMap)", "Future" };
    if (ImGui::Combo("Lighting Technology", &lightMode, lightModes, 2)) {
        if (lightMode == 1) cfg.getFFlags()["FFlagDebugForceFutureIsBrightPhase3"] = true;
        else cfg.getFFlags().erase("FFlagDebugForceFutureIsBrightPhase3");
        changed = true;
    }

    ImGui::Spacing();
    ImGui::Separator();
    
    if (ImGui::CollapsingHeader("Advanced / Custom Flags")) {
        static char newFlagName[128] = "";
        static char newFlagValue[128] = "";
        static int newFlagType = 0; 
        ImGui::InputText("Flag Name", newFlagName, sizeof(newFlagName));
        
        std::string nameStr = newFlagName;
        if (newFlagType == 0 && !nameStr.empty()) {
            if (nameStr.find("FFlag") == 0 || nameStr.find("DFFlag") == 0) newFlagType = 1;
            else if (nameStr.find("FInt") == 0 || nameStr.find("DFInt") == 0) newFlagType = 2;
            else if (nameStr.find("FString") == 0 || nameStr.find("DFString") == 0) newFlagType = 3;
            else if (nameStr.find("FFloat") == 0 || nameStr.find("DFFloat") == 0) newFlagType = 4;
        }
        
        const char* types[] = { "Auto", "Bool", "Int", "String", "Float" };
        ImGui::Combo("Type", &newFlagType, types, 5);
        ImGui::InputText("Value", newFlagValue, sizeof(newFlagValue));
        
        if (ImGui::Button("Add Flag")) {
            if (strlen(newFlagName) > 0 && strlen(newFlagValue) > 0) {
                nlohmann::json val;
                try {
                    if (newFlagType == 1) val = (std::string(newFlagValue) == "true" || std::string(newFlagValue) == "1");
                    else if (newFlagType == 2) val = std::stoi(newFlagValue);
                    else if (newFlagType == 4) val = std::stof(newFlagValue);
                    else val = newFlagValue;
                    
                    cfg.getFFlags()[newFlagName] = val;
                    changed = true;
                    newFlagName[0] = '\0';
                    newFlagValue[0] = '\0';
                    newFlagType = 0;
                } catch (...) {}
            }
        }
        
        ImGui::Spacing();
        ImGui::Text("Current Flags:");
        
        if (ImGui::BeginTable("FlagsTable", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY, ImVec2(0, 200))) {
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthFixed, 60.0f);
            ImGui::TableHeadersRow();
            
            std::string toRemove = "";
            for (const auto& [key, val] : cfg.getFFlags()) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0); ImGui::Text("%s", key.c_str());
                ImGui::TableSetColumnIndex(1); ImGui::Text("%s", val.dump().c_str());
                ImGui::TableSetColumnIndex(2); if (ImGui::Button(("Del##" + key).c_str())) toRemove = key;
            }
            if (!toRemove.empty()) { cfg.getFFlags().erase(toRemove); changed = true; }
            ImGui::EndTable();
        }
    }

    if (changed) cfg.save();
}

void SettingsPage::renderEnvTab() {
    auto& cfg = Config::instance();
    auto& env = cfg.getGeneral().customEnv;
    bool changed = false;
    
    ImGui::Spacing();
    ImGui::Text("Custom Environment Variables");
    ImGui::Separator();
    ImGui::Spacing();
    
    static char envKey[128] = "";
    static char envVal[128] = "";
    
    ImGui::InputText("Key", envKey, sizeof(envKey));
    ImGui::InputText("Value", envVal, sizeof(envVal));
    
    if (ImGui::Button("Add Variable") && strlen(envKey) > 0) {
        env[envKey] = envVal;
        envKey[0] = '\0';
        envVal[0] = '\0';
        changed = true;
    }
    
    ImGui::Spacing();
    ImGui::Separator();
    
    if (ImGui::BeginTable("EnvVars", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("Key");
        ImGui::TableSetupColumn("Value");
        ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthFixed, 50.0f);
        ImGui::TableHeadersRow();
        
        std::string toRemove;
        for (const auto& [key, val] : env) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("%s", key.c_str());
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%s", val.c_str());
            ImGui::TableSetColumnIndex(2);
            if (ImGui::Button(("X##" + key).c_str())) toRemove = key;
        }
        if (!toRemove.empty()) {
            env.erase(toRemove);
            changed = true;
        }
        ImGui::EndTable();
    }

    if (changed) cfg.save();
}

static std::vector<std::string> detectGpus() {
    std::vector<std::string> gpus;
    FILE* f = popen("lspci | grep -E 'VGA|3D|Display' 2>/dev/null", "r");
    if (f) {
        char buf[512];
        while (fgets(buf, sizeof(buf), f)) {
            std::string line(buf);
            if (!line.empty() && line.back() == '\n') line.pop_back();
            size_t pos = line.find(": ");
            if (pos != std::string::npos) {
                gpus.push_back(line.substr(pos + 2));
            } else {
                gpus.push_back(line);
            }
        }
        pclose(f);
    }
    return gpus;
}

static std::vector<std::string> cachedGpus;
static bool gpusDetected = false;

void SettingsPage::renderGpuTab() {
    auto& cfg = Config::instance();
    bool changed = false;
    
    if (!gpusDetected) {
        cachedGpus = detectGpus();
        gpusDetected = true;
    }
    
    ImGui::Spacing();
    ImGui::Text("GPU Selection");
    ImGui::Separator();
    ImGui::Spacing();
    
    if (cachedGpus.empty()) {
        ImGui::TextDisabled("No GPUs detected via lspci.");
    } else {
        ImGui::TextWrapped("Select which GPU to use for Roblox Studio. 'Auto' lets the system decide.");
        ImGui::Spacing();
        
        std::vector<const char*> items;
        items.push_back("Auto (System Default)");
        for (const auto& gpu : cachedGpus) {
            items.push_back(gpu.c_str());
        }
        
        int currentIdx = cfg.getGeneral().selectedGpu + 1;
        if (currentIdx < 0 || currentIdx >= (int)items.size()) currentIdx = 0;
        
        if (ImGui::Combo("GPU", &currentIdx, items.data(), (int)items.size())) {
            cfg.getGeneral().selectedGpu = currentIdx - 1;
            changed = true;
        }
        
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        
        ImGui::Text("Detected GPUs:");
        for (size_t i = 0; i < cachedGpus.size(); ++i) {
            bool isSelected = (cfg.getGeneral().selectedGpu == (int)i);
            if (isSelected) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.3f, 0.9f, 0.3f, 1.0f));
            }
            ImGui::BulletText("[%zu] %s", i, cachedGpus[i].c_str());
            if (isSelected) {
                ImGui::PopStyleColor();
            }
        }
        
        ImGui::Spacing();
        ImGui::TextDisabled("Note: GPU selection uses DRI_PRIME environment variable.");
    }
    
    if (ImGui::Button("Refresh GPU List")) {
        gpusDetected = false;
    }
    
    if (changed) cfg.save();
}

}
