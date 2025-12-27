#ifndef RSJFW_SETTINGSPAGE_HPP
#define RSJFW_SETTINGSPAGE_HPP

#include "imgui.h"
#include "rsjfw/downloader.hpp"
#include "rsjfw/page.hpp"
#include <map>
#include <set>
#include <string>
#include <vector>

namespace rsjfw {

class GUI;

class SettingsPage : public Page {
public:
  SettingsPage(GUI *gui);

  void render() override;
  std::string title() const override { return "Settings"; }

  void renderGeneralTab();
  void renderWineTab();
  void renderDxvkTab();
  void renderFFlagsTab();
  void renderEnvTab();

  void renderInstalledRoots(bool wine);

  void update();
  void ensureVersions();
  void ensureVersions(const std::string &repo);

private:
  GUI *gui_;

  // Version/Asset caching (v2.1)
  std::map<std::string, std::vector<Downloader::GitHubRelease>> releaseCache_;
  std::set<std::string> fetching_; // Repo strings currently being fetched
};

} // namespace rsjfw

#endif // RSJFW_SETTINGSPAGE_HPP
