#include "rsjfw/pages/SettingsPage.hpp"
#include "rsjfw/config.hpp"
#include "rsjfw/diagnostics.hpp"
#include "rsjfw/downloader.hpp"
#include "rsjfw/gui.hpp"
#include "rsjfw/launcher.hpp"
#include "rsjfw/path_manager.hpp"
#include "rsjfw/task_runner.hpp"
#include <cstring>
#include <thread>

namespace rsjfw {

static std::string pickDirectory(const std::string &title) {
  std::string result;
  std::string cmd =
      "gdbus call --session --dest org.freedesktop.portal.Desktop "
      "--object-path /org/freedesktop/portal/desktop "
      "--method org.freedesktop.portal.FileChooser.OpenFile '' "
      "'{\"title\": <\"" +
      title +
      "\">, \"directory\": <true>}' 2>/dev/null | "
      "grep -oP \"file://\\K[^']+\" | head -1";
  FILE *f = popen(cmd.c_str(), "r");
  if (f) {
    char path[512];
    if (fgets(path, sizeof(path), f)) {
      result = path;
      if (!result.empty() && result.back() == '\n')
        result.pop_back();
    }
    pclose(f);
  }
  if (result.empty()) {
    f = popen("zenity --file-selection --directory 2>/dev/null || kdialog "
              "--getexistingdirectory ~ 2>/dev/null",
              "r");
    if (f) {
      char path[512];
      if (fgets(path, sizeof(path), f)) {
        result = path;
        if (!result.empty() && result.back() == '\n')
          result.pop_back();
      }
      pclose(f);
    }
  }
  return result;
}

// DELETED Globals (Moved to class)

SettingsPage::SettingsPage(GUI *gui) : gui_(gui) {}

void SettingsPage::render() {
  auto &cfg = Config::instance();
  auto &gen = cfg.getGeneral();

  ensureVersions();

  if (ImGui::BeginTabBar("ConfigTabs")) {
    if (ImGui::BeginTabItem("General")) {
      renderGeneralTab();
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Wine")) {
      renderWineTab();
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("DXVK")) {
      renderDxvkTab();
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
  auto &cfg = Config::instance();
  auto &gen = cfg.getGeneral();
  bool changed = false;

  ImGui::Spacing();
  const char *renderers[] = {"D3D11", "Vulkan", "OpenGL", "D3D11FL10"};
  static int currentRendererIdx = 0;
  std::string currentRenderer = cfg.getGeneral().renderer;
  for (int i = 0; i < 4; i++)
    if (currentRenderer == renderers[i])
      currentRendererIdx = i;

  if (ImGui::Combo("Renderer", &currentRendererIdx, renderers, 4)) {
    cfg.getGeneral().renderer = renderers[currentRendererIdx];
    changed = true;
  }

  ImGui::Separator();
  ImGui::Text("Versioning");

  char verBuf[64];
  strncpy(verBuf, cfg.getGeneral().robloxVersion.c_str(), sizeof(verBuf));
  if (ImGui::InputText("Roblox Version Override", verBuf, sizeof(verBuf))) {
    cfg.getGeneral().robloxVersion = std::string(verBuf);
    changed = true;
  }

  const char *channels[] = {"LIVE", "production", "zcanary", "zintegration",
                            "Custom"};
  static int currentChannelIdx = 0;
  std::string curChan = cfg.getGeneral().channel;
  bool customChannel = true;
  for (int i = 0; i < 4; i++)
    if (curChan == channels[i]) {
      currentChannelIdx = i;
      customChannel = false;
      break;
    }
  if (customChannel)
    currentChannelIdx = 4;

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

  if (changed) {
    cfg.save();
    Diagnostics::instance().runChecks();
  }
}

static std::vector<std::string> detectGpus() {
  std::vector<std::string> gpus;
  FILE *f = popen("lspci | grep -E 'VGA|3D|Display' 2>/dev/null", "r");
  if (f) {
    char buf[512];
    while (fgets(buf, sizeof(buf), f)) {
      std::string line(buf);
      if (!line.empty() && line.back() == '\n')
        line.pop_back();
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

void SettingsPage::renderDxvkTab() {
  auto &cfg = Config::instance();
  auto &gen = cfg.getGeneral();
  bool changed = false;

  ImGui::Spacing();
  bool dxvk = gen.dxvk;
  if (ImGui::Checkbox("Enable DXVK", &dxvk)) {
    gen.dxvk = dxvk;
    changed = true;
  }

  if (dxvk) {
    ImGui::Separator();
    const char *dxvkSourceAliases[] = {"Official", "Sarek", "Custom Path",
                                       "Custom Repo"};
    const char *dxvkSourceRepos[] = {
        "doitsujin/dxvk", "pythonlover02/DXVK-Sarek", "CUSTOM_PATH", "CUSTOM"};

    int dxvkSourceIdx = 3;
    for (int i = 0; i < 3; i++)
      if (gen.dxvkSource.repo == dxvkSourceRepos[i])
        dxvkSourceIdx = i;

    if (ImGui::Combo("DXVK Source Repo", &dxvkSourceIdx, dxvkSourceAliases,
                     4)) {
      gen.dxvkSource.repo = dxvkSourceRepos[dxvkSourceIdx];
      gen.dxvkSource.version = "latest";
      gen.dxvkSource.asset = "";
      gen.dxvkSource.installedRoot = ""; // Clear on source change
      changed = true;
    }

    if (gen.dxvkSource.repo == "CUSTOM_PATH") {
      char pathBuf[256];
      strncpy(pathBuf, gen.dxvkSource.installedRoot.c_str(), sizeof(pathBuf));
      if (ImGui::InputText("DXVK Path", pathBuf, sizeof(pathBuf))) {
        gen.dxvkSource.installedRoot = std::string(pathBuf);
        changed = true;
      }
      ImGui::SameLine();
      if (ImGui::Button("...##dxvk")) {
        std::string p = pickDirectory("Select DXVK Directory");
        if (!p.empty()) {
          gen.dxvkSource.installedRoot = p;
          changed = true;
        }
      }
    } else if (gen.dxvkSource.repo == "CUSTOM") {
      char repoBuf[256];
      strncpy(repoBuf, gen.dxvkCustomUrl.c_str(), sizeof(repoBuf));
      if (ImGui::InputText("GitHub Repo (user/repo)", repoBuf,
                           sizeof(repoBuf))) {
        gen.dxvkCustomUrl = std::string(repoBuf);
        changed = true;
      }
    }

    std::string activeRepo = (gen.dxvkSource.repo == "CUSTOM")
                                 ? gen.dxvkCustomUrl
                                 : gen.dxvkSource.repo;
    if (activeRepo.find('/') != std::string::npos &&
        activeRepo.find("://") == std::string::npos) {
      if (fetching_.count(activeRepo)) {
        ImGui::TextDisabled("Fetching versions...");
      } else if (releaseCache_.count(activeRepo)) {
        auto &releases = releaseCache_[activeRepo];
        std::vector<const char *> verItems;
        int currentVerIdx = -1;
        for (size_t i = 0; i < releases.size(); i++) {
          verItems.push_back(releases[i].tag.c_str());
          if (releases[i].tag == gen.dxvkSource.version)
            currentVerIdx = (int)i;
        }
        if (ImGui::Combo("Version", &currentVerIdx, verItems.data(),
                         (int)verItems.size())) {
          gen.dxvkSource.version = releases[currentVerIdx].tag;
          gen.dxvkSource.asset = "";
          gen.dxvkSource.installedRoot = ""; // Clear on version change
          changed = true;
        }

        // Asset Dropdown (Optional)
        if (currentVerIdx != -1) {
          auto &assets = releases[currentVerIdx].assets;
          std::vector<const char *> assetItems;
          int currentAssetIdx = -1;
          for (size_t i = 0; i < assets.size(); i++) {
            assetItems.push_back(assets[i].name.c_str());
            if (assets[i].name == gen.dxvkSource.asset)
              currentAssetIdx = (int)i;
          }
          if (ImGui::Combo("Archive (Optional)", &currentAssetIdx,
                           assetItems.data(), (int)assetItems.size())) {
            gen.dxvkSource.asset = assets[currentAssetIdx].name;
            gen.dxvkSource.installedRoot = ""; // Clear on asset change
            changed = true;
          }
        }
      }
    }

    ImGui::Spacing();
    ImGui::Text("Installed DXVK Versions");
    renderInstalledRoots(false);
  }

  ImGui::Spacing();
  ImGui::Separator();
  ImGui::Text("GPU Selection");
  ImGui::Spacing();

  if (!gpusDetected) {
    cachedGpus = detectGpus();
    gpusDetected = true;
  }

  if (cachedGpus.empty()) {
    ImGui::TextDisabled("No GPUs detected via lspci.");
  } else {
    std::vector<const char *> items = {"Auto (System Default)"};
    for (const auto &gpu : cachedGpus)
      items.push_back(gpu.c_str());
    int currentIdx = gen.selectedGpu + 1;
    if (currentIdx < 0 || currentIdx >= (int)items.size())
      currentIdx = 0;

    if (ImGui::Combo("Preferred GPU", &currentIdx, items.data(),
                     (int)items.size())) {
      gen.selectedGpu = currentIdx - 1;
      changed = true;
    }
    if (ImGui::Button("Refresh GPU List")) {
      gpusDetected = false;
    }
  }

  if (changed) {
    cfg.save();
    Diagnostics::instance().runChecks();
  }
}

void SettingsPage::renderWineTab() {
  auto &cfg = Config::instance();
  auto &gen = cfg.getGeneral();
  bool changed = false;

  ImGui::Spacing();
  ImGui::Text("Wine Source Settings");
  ImGui::Separator();

  const char *wineSourceAliases[] = {"System",  "Vinegar",     "Proton-GE",
                                     "CachyOS", "Custom Path", "Custom Repo"};
  const char *wineSourceRepos[] = {"SYSTEM",
                                   "vinegarhq/wine-builds",
                                   "GloriousEggroll/proton-ge-custom",
                                   "CachyOS/proton-cachyos",
                                   "CUSTOM_PATH",
                                   "CUSTOM"};

  int wineSourceIdx = 5;
  for (int i = 0; i < 5; i++)
    if (gen.wineSource.repo == wineSourceRepos[i])
      wineSourceIdx = i;

  if (ImGui::Combo("Wine Source Repo", &wineSourceIdx, wineSourceAliases, 6)) {
    gen.wineSource.repo = wineSourceRepos[wineSourceIdx];
    gen.wineSource.version = "latest";
    gen.wineSource.asset = "";
    gen.wineSource.installedRoot = ""; // Clear on source change
    changed = true;
  }

  if (gen.wineSource.repo == "CUSTOM_PATH") {
    char pathBuf[256];
    strncpy(pathBuf, gen.wineSource.installedRoot.c_str(), sizeof(pathBuf));
    if (ImGui::InputText("Wine Path", pathBuf, sizeof(pathBuf))) {
      gen.wineSource.installedRoot = std::string(pathBuf);
      changed = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("...##wine")) {
      std::string p = pickDirectory("Select Wine Directory");
      if (!p.empty()) {
        gen.wineSource.installedRoot = p;
        changed = true;
      }
    }
  } else if (gen.wineSource.repo == "CUSTOM") {
    char repoBuf[256];
    strncpy(repoBuf, gen.wineCustomUrl.c_str(), sizeof(repoBuf));
    if (ImGui::InputText("GitHub Repo (user/repo)", repoBuf, sizeof(repoBuf))) {
      gen.wineCustomUrl = std::string(repoBuf);
      changed = true;
    }
  }

  std::string activeRepo = (gen.wineSource.repo == "CUSTOM")
                               ? gen.wineCustomUrl
                               : gen.wineSource.repo;
  if (activeRepo.find('/') != std::string::npos &&
      activeRepo.find("://") == std::string::npos && activeRepo != "SYSTEM") {
    if (fetching_.count(activeRepo)) {
      ImGui::TextDisabled("Fetching versions...");
    } else if (releaseCache_.count(activeRepo)) {
      auto &releases = releaseCache_[activeRepo];

      // Version Dropdown
      std::vector<const char *> verItems;
      int currentVerIdx = -1;
      for (size_t i = 0; i < releases.size(); i++) {
        verItems.push_back(releases[i].tag.c_str());
        if (releases[i].tag == gen.wineSource.version)
          currentVerIdx = (int)i;
      }
      if (ImGui::Combo("Version", &currentVerIdx, verItems.data(),
                       (int)verItems.size())) {
        gen.wineSource.version = releases[currentVerIdx].tag;
        gen.wineSource.asset = "";         // Reset asset on version change
        gen.wineSource.installedRoot = ""; // Clear on version change
        changed = true;
      }

      // Asset Dropdown (Conditional)
      if (currentVerIdx != -1) {
        auto &assets = releases[currentVerIdx].assets;
        std::vector<const char *> assetItems;
        int currentAssetIdx = -1;
        for (size_t i = 0; i < assets.size(); i++) {
          assetItems.push_back(assets[i].name.c_str());
          if (assets[i].name == gen.wineSource.asset)
            currentAssetIdx = (int)i;
        }
        if (ImGui::Combo("Archive (Optional)", &currentAssetIdx,
                         assetItems.data(), (int)assetItems.size())) {
          gen.wineSource.asset = assets[currentAssetIdx].name;
          gen.wineSource.installedRoot = ""; // Clear on asset change
          changed = true;
        }
      }
    }
  }

  ImGui::Spacing();
  ImGui::Separator();
  ImGui::Text("Wine Options");

  bool desktopMode = cfg.getWine().desktopMode;
  if (ImGui::Checkbox("Desktop Mode", &desktopMode)) {
    cfg.getWine().desktopMode = desktopMode;
    changed = true;
  }
  ImGui::SameLine();
  bool multiDesktop = cfg.getWine().multipleDesktops;
  if (ImGui::Checkbox("Multi-Desktop", &multiDesktop)) {
    cfg.getWine().multipleDesktops = multiDesktop;
    changed = true;
  }

  ImGui::Spacing();
  ImGui::Text("Installed Wine Roots");
  renderInstalledRoots(true);

  if (changed) {
    cfg.save();
    Diagnostics::instance().runChecks();
  }
}

void SettingsPage::renderInstalledRoots(bool wine) {
  auto &cfg = Config::instance();
  auto &gen = cfg.getGeneral();
  bool changed = false;
  Downloader dl(PathManager::instance().root().string());
  auto roots = wine ? dl.getInstalledWineRoots() : dl.getInstalledDxvkRoots();

  if (roots.empty()) {
    ImGui::TextDisabled("No installed versions found.");
    return;
  }

  if (ImGui::BeginChild(wine ? "WineRoots" : "DxvkRoots", ImVec2(0, 150),
                        true)) {
    for (const auto &root : roots) {
      float sizeMB = root.sizeBytes / (1024.0f * 1024.0f);
      ImGui::Text("%s (%.1f MB)", root.name.c_str(), sizeMB);
      if (root.isProton) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.4f, 0.7f, 1.0f, 1.0f), "[Proton]");
      }

      ImGui::SameLine(ImGui::GetContentRegionAvail().x - 110);
      if (ImGui::Button(("Use##" + root.path).c_str())) {
        if (wine) {
          gen.wineSource.installedRoot = root.path;
          if (!root.repo.empty())
            gen.wineSource.repo = root.repo;
          if (!root.version.empty())
            gen.wineSource.version = root.version;
          if (!root.asset.empty())
            gen.wineSource.asset = root.asset;
        } else {
          gen.dxvkSource.installedRoot = root.path;
          if (!root.repo.empty())
            gen.dxvkSource.repo = root.repo;
          if (!root.version.empty())
            gen.dxvkSource.version = root.version;
          if (!root.asset.empty())
            gen.dxvkSource.asset = root.asset;
        }
        changed = true;
      }
      ImGui::SameLine(ImGui::GetContentRegionAvail().x - 50);
      if (ImGui::Button(("Delete##" + root.path).c_str())) {
        dl.deleteRoot(root.path);
      }
    }
  }
  ImGui::EndChild();

  if (changed) {
    cfg.save();
    Diagnostics::instance().runChecks();
  }
}

void SettingsPage::renderFFlagsTab() {
  auto &cfg = Config::instance();
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
    try {
      currentFps = cfg.getFFlags()["DFIntTaskSchedulerTargetFps"].get<int>();
    } catch (...) {
    }
  }
  if (currentFps > 240)
    currentFps = 241;

  int sliderFps = currentFps;
  if (ImGui::SliderInt("FPS Limit", &sliderFps, 30, 241,
                       (sliderFps > 240 ? "Unlock" : "%d"))) {
    if (sliderFps > 240)
      cfg.getFFlags()["DFIntTaskSchedulerTargetFps"] = 9999;
    else
      cfg.getFFlags()["DFIntTaskSchedulerTargetFps"] = sliderFps;
    changed = true;
  }

  // DPI Scaling
  bool disableDpi = false;
  if (cfg.getFFlags().contains("DFFlagDisableDPIScale")) {
    try {
      disableDpi = cfg.getFFlags()["DFFlagDisableDPIScale"].get<bool>();
    } catch (...) {
    }
  }
  if (ImGui::Checkbox("Disable DPI Scaling", &disableDpi)) {
    cfg.getFFlags()["DFFlagDisableDPIScale"] = disableDpi;
    changed = true;
  }

  // Lighting Technology
  bool future = false;
  if (cfg.getFFlags().contains("FFlagDebugForceFutureIsBrightPhase3")) {
    try {
      future =
          cfg.getFFlags()["FFlagDebugForceFutureIsBrightPhase3"].get<bool>();
    } catch (...) {
    }
  }

  int lightMode = future ? 1 : 0;
  const char *lightModes[] = {"Default (ShadowMap)", "Future"};
  if (ImGui::Combo("Lighting Technology", &lightMode, lightModes, 2)) {
    if (lightMode == 1)
      cfg.getFFlags()["FFlagDebugForceFutureIsBrightPhase3"] = true;
    else
      cfg.getFFlags().erase("FFlagDebugForceFutureIsBrightPhase3");
    changed = true;
  }

  ImGui::Spacing();
  ImGui::Separator();

  if (ImGui::CollapsingHeader("Advanced / Custom Flags",
                              ImGuiTreeNodeFlags_DefaultOpen)) {
    ImGui::Spacing();

    static char newFlagName[128] = "";
    static char newFlagValue[128] = "";
    static int newFlagType = 0; // 0:Auto, 1:Bool, 2:Int, 3:String, 4:Float

    ImGui::SetNextItemWidth(200);
    if (ImGui::InputTextWithHint("##fname", "Flag Name", newFlagName,
                                 sizeof(newFlagName))) {
      std::string nameStr = newFlagName;
      if (newFlagType == 0 && !nameStr.empty()) {
        if (nameStr.find("FFlag") == 0 || nameStr.find("DFFlag") == 0)
          newFlagType = 1;
        else if (nameStr.find("FInt") == 0 || nameStr.find("DFInt") == 0)
          newFlagType = 2;
        else if (nameStr.find("FString") == 0 || nameStr.find("DFString") == 0)
          newFlagType = 3;
        else if (nameStr.find("FFloat") == 0 || nameStr.find("DFFloat") == 0)
          newFlagType = 4;
      }
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(100);
    const char *types[] = {"Auto", "Bool", "Int", "String", "Float"};
    ImGui::Combo("##ftype", &newFlagType, types, 5);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(200);
    ImGui::InputTextWithHint("##fval", "Value", newFlagValue,
                             sizeof(newFlagValue));
    ImGui::SameLine();
    if (ImGui::Button("Add Flag") && strlen(newFlagName) > 0) {
      nlohmann::json val;
      try {
        std::string sVal = newFlagValue;
        if (newFlagType == 1)
          val = (sVal == "true" || sVal == "1");
        else if (newFlagType == 2)
          val = std::stoi(sVal);
        else if (newFlagType == 4)
          val = std::stof(sVal);
        else
          val = sVal;

        cfg.getFFlags()[newFlagName] = val;
        changed = true;
        newFlagName[0] = '\0';
        newFlagValue[0] = '\0';
        newFlagType = 0;
      } catch (...) {
        // Silently fail for bad types
      }
    }

    ImGui::Spacing();

    if (ImGui::BeginTable("FlagsTable", 3,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                              ImGuiTableFlags_Resizable |
                              ImGuiTableFlags_ScrollY,
                          ImVec2(0, 250))) {
      ImGui::TableSetupColumn("Flag Name", ImGuiTableColumnFlags_WidthStretch,
                              0.5f);
      ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch,
                              0.4f);
      ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthFixed,
                              60.0f);
      ImGui::TableHeadersRow();

      std::string toRemove = "";
      auto &flags = cfg.getFFlags();
      for (auto &[key, val] : flags) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted(key.c_str());

        ImGui::TableSetColumnIndex(1);
        std::string sVal;
        if (val.is_string())
          sVal = val.get<std::string>();
        else
          sVal = val.dump();

        char valBuf[256];
        strncpy(valBuf, sVal.c_str(), sizeof(valBuf));

        ImGui::PushID(key.c_str());
        ImGui::SetNextItemWidth(-1);
        if (ImGui::InputText("##v", valBuf, sizeof(valBuf))) {
          if (val.is_boolean())
            val = (std::string(valBuf) == "true" || std::string(valBuf) == "1");
          else if (val.is_number_integer()) {
            try {
              val = std::stoi(valBuf);
            } catch (...) {
            }
          } else if (val.is_number_float()) {
            try {
              val = std::stof(valBuf);
            } catch (...) {
            }
          } else
            val = std::string(valBuf);
          changed = true;
        }
        ImGui::PopID();

        ImGui::TableSetColumnIndex(2);
        if (ImGui::Button(("Del##" + key).c_str(), ImVec2(-1, 0)))
          toRemove = key;
      }
      if (!toRemove.empty()) {
        flags.erase(toRemove);
        changed = true;
      }
      ImGui::EndTable();
    }
  }

  if (changed)
    cfg.save();
}

void SettingsPage::renderEnvTab() {
  auto &cfg = Config::instance();
  auto &env = cfg.getGeneral().customEnv;
  bool changed = false;

  ImGui::Spacing();

  // === WINEDEBUG Helper Section ===
  if (ImGui::CollapsingHeader("Wine Debug Helper",
                              ImGuiTreeNodeFlags_DefaultOpen)) {
    ImGui::Indent(10);

    // Preset buttons
    ImGui::Text("Presets:");
    ImGui::SameLine();
    if (ImGui::SmallButton("Off (-all)")) {
      env["WINEDEBUG"] = "-all";
      changed = true;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Errors (+err)")) {
      env["WINEDEBUG"] = "-all,+err";
      changed = true;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Warnings (+warn)")) {
      env["WINEDEBUG"] = "-all,+err,+warn";
      changed = true;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Full (+all)")) {
      env["WINEDEBUG"] = "+all";
      changed = true;
    }

    // Category toggles
    ImGui::Spacing();
    std::string currentDebug = env.count("WINEDEBUG") ? env["WINEDEBUG"] : "";

    static struct {
      const char *name;
      const char *flag;
      bool enabled;
    } debugCategories[] = {
        {"loaddll", "loaddll", false}, {"fixme", "fixme", false},
        {"relay", "relay", false},     {"heap", "heap", false},
        {"tid", "tid", false},         {"timestamp", "timestamp", false}};

    // Parse current state
    for (auto &cat : debugCategories) {
      cat.enabled =
          currentDebug.find(std::string("+") + cat.flag) != std::string::npos;
    }

    ImGui::Text("Categories:");
    bool catChanged = false;
    for (auto &cat : debugCategories) {
      ImGui::SameLine();
      if (ImGui::Checkbox(cat.name, &cat.enabled)) {
        catChanged = true;
      }
    }

    if (catChanged) {
      // Rebuild WINEDEBUG string
      std::string newDebug = "-all";
      for (const auto &cat : debugCategories) {
        if (cat.enabled) {
          newDebug += std::string(",+") + cat.flag;
        }
      }
      env["WINEDEBUG"] = newDebug;
      changed = true;
    }

    // Preview
    ImGui::Spacing();
    ImGui::TextDisabled("Current WINEDEBUG: %s", currentDebug.empty()
                                                     ? "(not set)"
                                                     : currentDebug.c_str());

    ImGui::Unindent(10);
  }

  // === DXVK_HUD Helper Section ===
  if (ImGui::CollapsingHeader("DXVK HUD Options")) {
    ImGui::Indent(10);

    std::string currentHud = env.count("DXVK_HUD") ? env["DXVK_HUD"] : "";

    static struct {
      const char *name;
      const char *option;
      bool enabled;
    } hudOptions[] = {{"FPS", "fps", false},
                      {"Device Info", "devinfo", false},
                      {"Memory", "memory", false},
                      {"GPU Load", "gpuload", false},
                      {"Frametime", "frametimes", false},
                      {"Submissions", "submissions", false}};

    // Parse current state
    for (auto &opt : hudOptions) {
      opt.enabled = currentHud.find(opt.option) != std::string::npos;
    }

    bool hudChanged = false;
    for (auto &opt : hudOptions) {
      ImGui::SameLine();
      if (ImGui::Checkbox(opt.name, &opt.enabled)) {
        hudChanged = true;
      }
    }

    if (hudChanged) {
      std::string newHud = "";
      for (const auto &opt : hudOptions) {
        if (opt.enabled) {
          if (!newHud.empty())
            newHud += ",";
          newHud += opt.option;
        }
      }
      if (newHud.empty()) {
        env.erase("DXVK_HUD");
      } else {
        env["DXVK_HUD"] = newHud;
      }
      changed = true;
    }

    ImGui::TextDisabled("Current DXVK_HUD: %s",
                        currentHud.empty() ? "(not set)" : currentHud.c_str());

    ImGui::Unindent(10);
  }

  ImGui::Spacing();
  ImGui::Separator();
  ImGui::Spacing();

  ImGui::Text("Custom Environment Variables");
  ImGui::TextDisabled("These are passed to Wine when launching Roblox.");
  ImGui::Spacing();

  // Addition row at the top
  static char newKey[128] = "";
  static char newVal[128] = "";
  ImGui::SetNextItemWidth(200);
  ImGui::InputTextWithHint("##newkey", "Name (e.g. WINEDEBUG)", newKey,
                           sizeof(newKey));
  ImGui::SameLine();
  ImGui::SetNextItemWidth(300);
  ImGui::InputTextWithHint("##newval", "Value", newVal, sizeof(newVal));
  ImGui::SameLine();
  if (ImGui::Button("Add Variable") && strlen(newKey) > 0) {
    env[newKey] = newVal;
    newKey[0] = '\0';
    newVal[0] = '\0';
    changed = true;
  }

  ImGui::Spacing();

  if (ImGui::BeginTable("EnvTable", 3,
                        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                            ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY,
                        ImVec2(0, 300))) {
    ImGui::TableSetupColumn("Variable Name", ImGuiTableColumnFlags_WidthStretch,
                            0.4f);
    ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 0.6f);
    ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, 60.0f);
    ImGui::TableHeadersRow();

    std::string toRemove = "";
    for (auto &[key, val] : env) {
      ImGui::TableNextRow();
      ImGui::TableSetColumnIndex(0);
      ImGui::TextUnformatted(key.c_str());

      ImGui::TableSetColumnIndex(1);
      char valBuf[512];
      strncpy(valBuf, val.c_str(), sizeof(valBuf));
      ImGui::PushID(key.c_str());
      ImGui::SetNextItemWidth(-1);
      if (ImGui::InputText("##v", valBuf, sizeof(valBuf))) {
        val = std::string(valBuf);
        changed = true;
      }
      ImGui::PopID();

      ImGui::TableSetColumnIndex(2);
      if (ImGui::Button(("Del##" + key).c_str(), ImVec2(-1, 0))) {
        toRemove = key;
      }
    }

    if (!toRemove.empty()) {
      env.erase(toRemove);
      changed = true;
    }

    ImGui::EndTable();
  }

  if (changed)
    cfg.save();
}

void SettingsPage::update() { ensureVersions(); }

void SettingsPage::ensureVersions() {
  auto &gen = Config::instance().getGeneral();
  if (gen.dxvk) {
    if (gen.dxvkSource.repo == "CUSTOM")
      ensureVersions(gen.dxvkCustomUrl);
    else
      ensureVersions(gen.dxvkSource.repo);
  }
  if (gen.wineSource.repo == "CUSTOM")
    ensureVersions(gen.wineCustomUrl);
  else if (gen.wineSource.repo != "SYSTEM" &&
           gen.wineSource.repo != "CUSTOM_PATH")
    ensureVersions(gen.wineSource.repo);
}

void SettingsPage::ensureVersions(const std::string &repo) {
  if (repo.empty() || repo == "CUSTOM" || repo == "SYSTEM" ||
      repo == "CUSTOM_PATH")
    return;
  if (repo.find('/') == std::string::npos ||
      repo.find("://") != std::string::npos)
    return;

  auto &cfg = Config::instance();
  std::lock_guard<std::recursive_mutex> lock(cfg.getMutex());
  if (releaseCache_.count(repo) == 0 && fetching_.count(repo) == 0) {
    fetching_.insert(repo);
    TaskRunner::instance().run([this, repo]() {
      Downloader dl(PathManager::instance().root().string());
      auto releases = dl.fetchReleases(repo);

      std::lock_guard<std::recursive_mutex> lock(Config::instance().getMutex());
      releaseCache_[repo] = releases;
      fetching_.erase(repo);
    });
  }
}

} // namespace rsjfw
