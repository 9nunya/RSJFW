#ifndef RSJFW_GUI_HPP
#define RSJFW_GUI_HPP

#include "rsjfw/diagnostics.hpp"
#include "rsjfw/page.hpp"
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace rsjfw {

class GUI {
public:
  enum Mode { MODE_CONFIG, MODE_LAUNCHER };

  static GUI &instance();

  bool init(int width, int height, const std::string &title, bool resizable);

  void setMode(Mode mode) { mode_ = mode; }

  void run(const std::function<void()> &renderCallback);

  void setProgress(float progress, const std::string &status);
  void setTaskProgress(const std::string &name, float progress,
                       const std::string &status);
  void removeTask(const std::string &name);
  bool hasTask(const std::string &name);
  void setSubProgress(float progress, const std::string &status);
  void updateFixProgress(float progress, const std::string &status);

  void showMessage(const std::string &title, const std::string &message);
  void setError(const std::string &errorMsg);
  void showHealthWarning(
      const std::vector<std::pair<std::string, HealthStatus>> &failures);

  // Quick access to tabs state
  int &getCurrentMainTab() { return currentMainTab_; }
  void navigateToTroubleshooting();
  void navigateToSettingsWine(bool flash = true);

  // Flashing/Pulsing support
  void flashWidget(const std::string &id);
  bool shouldFlash(const std::string &id); // Call this in render loop

  float getProgress() const { return progress_; }
  std::string getStatus() const { return status_; }
  std::string getError() const { return error_; }
  bool hasError() const { return !error_.empty(); }

  void close();
  void shutdown();

  // Page Navigation
  PageStack &pages() { return pages_; }
  void navigateTo(std::shared_ptr<Page> page) { pages_.push(page); }
  void goBack() { pages_.pop(); }

  // Accessors for pages
  unsigned int logoTexture() const { return logoTexture_; }
  int logoWidth() const { return logoWidth_; }
  int logoHeight() const { return logoHeight_; }

  GUI(const GUI &) = delete;
  GUI &operator=(const GUI &) = delete;

  struct TaskInfo {
    float progress;
    std::string status;
  };

private:
  GUI() = default;
  ~GUI();

  Mode mode_ = MODE_CONFIG;
  float progress_ = 0.0f;
  std::string status_ = "Initializing...";

  std::vector<std::pair<std::string, TaskInfo>> tasks_;

  std::string error_ = "";
  bool showHealthModal_ = false;
  std::vector<std::pair<std::string, HealthStatus>> healthFailures_;

  // Fix Modal State
  bool performingFix_ = false;
  bool openFixPopup_ = false; // Trigger for opening the popup in correct scope
  float fixProgress_ = 0.0f;
  std::string fixStatus_ = "";

  // Widget Flashing State
  std::string flashWidgetId_;
  float flashTimer_ = 0.0f;
  int flashCycles_ = 0;

  // Message Modal State
  bool showMessageModal_ = false;
  std::string messageTitle_;
  std::string messageText_;

  // Tab state (moved here so we can control navigation from modal)
  int currentMainTab_ = 0;
  int targetMainTab_ = 0; // Configured as member to allow external control

  // Settings Sub-tab state
  int currentSettingsTab_ = 0;
  int targetSettingsTab_ = 0;

  bool shouldClose_ = false;
  bool initialized_ = false;
  unsigned int logoTexture_ = 0;
  int logoWidth_ = 0;
  int logoHeight_ = 0;

  void *window_ = nullptr;
  void *glContext_ = nullptr;

  PageStack pages_;

  mutable std::mutex mutex_;
};

} // namespace rsjfw

#endif // RSJFW_GUI_HPP
