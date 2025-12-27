#include "rsjfw/gui.hpp"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"
#include "imgui.h"
#include "rsjfw/config.hpp"
#include "rsjfw/diagnostics.hpp"
#include "rsjfw/downloader.hpp"
#include "rsjfw/launcher.hpp"
#include "rsjfw/logger.hpp"
#include "rsjfw/pages/HomePage.hpp"
#include "rsjfw/pages/SettingsPage.hpp"
#include "rsjfw/pages/TroubleshootingPage.hpp"
#include "rsjfw/path_manager.hpp"
#include "rsjfw/task_runner.hpp"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <sstream>
#include <thread>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

namespace rsjfw {

static bool configDirty = false;

static void glfw_error_callback(int error, const char *description) {
  if (error == 65548)
    return;
  LOG_ERROR("GLFW Error " + std::to_string(error) + ": " +
            std::string(description));
}

GUI &GUI::instance() {
  static GUI instance;
  return instance;
}

bool loadTextureFromFile(const char *filename, GLuint *out_texture,
                         int *out_width, int *out_height) {
  int image_width = 0;
  int image_height = 0;
  unsigned char *image_data =
      stbi_load(filename, &image_width, &image_height, NULL, 4);
  if (image_data == NULL)
    return false;

  GLuint image_texture;
  glGenTextures(1, &image_texture);
  glBindTexture(GL_TEXTURE_2D, image_texture);

  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

#if defined(GL_UNPACK_ROW_LENGTH) && !defined(__EMSCRIPTEN__)
  glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
#endif
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, image_width, image_height, 0, GL_RGBA,
               GL_UNSIGNED_BYTE, image_data);
  stbi_image_free(image_data);

  *out_texture = image_texture;
  *out_width = image_width;
  *out_height = image_height;

  return true;
}

bool GUI::init(int width, int height, const std::string &title,
               bool resizable) {
  glfwSetErrorCallback(glfw_error_callback);
  if (!glfwInit()) {
    LOG_ERROR("Failed to initialize GLFW");
    return false;
  }

  const char *glsl_version = "#version 130";
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
  glfwWindowHint(GLFW_RESIZABLE, resizable ? GLFW_TRUE : GLFW_FALSE);
  glfwWindowHint(GLFW_VISIBLE, GLFW_TRUE);
  glfwWindowHint(GLFW_TRANSPARENT_FRAMEBUFFER,
                 GLFW_FALSE); // Prevent invisible window on Xwayland
  glfwWindowHint(GLFW_DOUBLEBUFFER,
                 GLFW_TRUE); // Ensure proper double buffering

  if (!resizable) {
    glfwWindowHint(GLFW_FLOATING, GLFW_TRUE);
    glfwWindowHint(GLFW_FOCUS_ON_SHOW, GLFW_TRUE);
    glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
  }

  GLFWwindow *window =
      glfwCreateWindow(width, height, title.c_str(), NULL, NULL);
  if (window == nullptr) {
    LOG_ERROR("Failed to create GLFW window");
    glfwTerminate();
    return false;
  }

  if (!resizable)
    glfwSetWindowSizeLimits(window, width, height, width, height);

  glfwMakeContextCurrent(window);
  glfwSwapInterval(0);    // Disable vsync - fixes Xwayland blocking issue
  glfwShowWindow(window); // Explicitly show window (fixes Xwayland issues)
  glfwFocusWindow(window);

  // Set window icon from production paths
  {
    // Production paths: /usr/share/rsjfw/logo.png or bundled with package
    std::filesystem::path iconPath = "/usr/share/rsjfw/logo.png";
    if (!std::filesystem::exists(iconPath)) {
      // User data directory fallback
      iconPath = PathManager::instance().root() / "assets" / "logo.png";
    }
    if (std::filesystem::exists(iconPath)) {
      int iconW, iconH, iconChannels;
      unsigned char *iconData = stbi_load(iconPath.string().c_str(), &iconW,
                                          &iconH, &iconChannels, 4);
      if (iconData) {
        GLFWimage icon = {iconW, iconH, iconData};
        glfwSetWindowIcon(window, 1, &icon);
        stbi_image_free(iconData);
      }
    }
  }

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO &io = ImGui::GetIO();
  (void)io;
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  io.IniFilename = nullptr;

  io.Fonts->AddFontFromFileTTF("external/imgui/misc/fonts/Roboto-Medium.ttf",
                               18.0f);

  ImGui::StyleColorsDark();
  ImGuiStyle &style = ImGui::GetStyle();

  style.WindowRounding = 10.0f;
  style.FrameRounding = 6.0f;
  style.PopupRounding = 6.0f;
  style.ScrollbarRounding = 6.0f;
  style.GrabRounding = 4.0f;
  style.TabRounding = 6.0f;

  style.WindowPadding = ImVec2(16, 16);
  style.FramePadding = ImVec2(12, 6);
  style.ItemSpacing = ImVec2(10, 8);

  // RSJFW Theme - Crimson accent, black background
  ImVec4 *colors = style.Colors;
  colors[ImGuiCol_WindowBg] = ImVec4(0.02f, 0.02f, 0.02f, 1.00f);
  colors[ImGuiCol_ChildBg] = ImVec4(0.04f, 0.04f, 0.04f, 1.00f);
  colors[ImGuiCol_PopupBg] = ImVec4(0.06f, 0.06f, 0.06f, 0.95f);
  colors[ImGuiCol_Border] = ImVec4(0.20f, 0.20f, 0.20f, 0.50f);
  colors[ImGuiCol_FrameBg] = ImVec4(0.08f, 0.08f, 0.08f, 1.00f);
  colors[ImGuiCol_FrameBgHovered] = ImVec4(0.12f, 0.12f, 0.12f, 1.00f);
  colors[ImGuiCol_FrameBgActive] = ImVec4(0.16f, 0.16f, 0.16f, 1.00f);
  colors[ImGuiCol_TitleBg] = ImVec4(0.02f, 0.02f, 0.02f, 1.00f);
  colors[ImGuiCol_TitleBgActive] = ImVec4(0.06f, 0.06f, 0.06f, 1.00f);
  colors[ImGuiCol_Button] = ImVec4(0.86f, 0.08f, 0.24f, 1.00f);
  colors[ImGuiCol_ButtonHovered] = ImVec4(0.96f, 0.18f, 0.34f, 1.00f);
  colors[ImGuiCol_ButtonActive] = ImVec4(0.76f, 0.02f, 0.18f, 1.00f);
  colors[ImGuiCol_Header] = ImVec4(0.86f, 0.08f, 0.24f, 0.50f);
  colors[ImGuiCol_HeaderHovered] = ImVec4(0.96f, 0.18f, 0.34f, 0.70f);
  colors[ImGuiCol_HeaderActive] = ImVec4(0.86f, 0.08f, 0.24f, 1.00f);
  colors[ImGuiCol_Tab] = ImVec4(0.10f, 0.10f, 0.10f, 1.00f);
  colors[ImGuiCol_TabHovered] = ImVec4(0.86f, 0.08f, 0.24f, 0.80f);
  colors[ImGuiCol_TabActive] = ImVec4(0.86f, 0.08f, 0.24f, 1.00f);
  colors[ImGuiCol_CheckMark] = ImVec4(0.96f, 0.28f, 0.44f, 1.00f);
  colors[ImGuiCol_SliderGrab] = ImVec4(0.86f, 0.08f, 0.24f, 1.00f);
  colors[ImGuiCol_SliderGrabActive] = ImVec4(0.96f, 0.18f, 0.34f, 1.00f);

  ImGui_ImplGlfw_InitForOpenGL(window, true);
  ImGui_ImplOpenGL3_Init(glsl_version);

  if (!loadTextureFromFile("assets/logo.png", &logoTexture_, &logoWidth_,
                           &logoHeight_)) {
    loadTextureFromFile("/usr/share/pixmaps/rsjfw.png", &logoTexture_,
                        &logoWidth_, &logoHeight_);
  }

  window_ = window;
  initialized_ = true;

  pages_.push(
      std::make_shared<HomePage>(this, logoTexture_, logoWidth_, logoHeight_));

  return true;
}

void GUI::run(const std::function<void()> &renderCallback) {
  if (!initialized_)
    return;

  GLFWwindow *window = (GLFWwindow *)window_;

  // Startup Diagnostics (One-time)
  // Startup Diagnostics (One-time) - DISABLED to prevent freezing on VMs
  static bool checkedDiagnostics = false;
  /*
  if (mode_ == MODE_CONFIG && !checkedDiagnostics) {
      checkedDiagnostics = true;
      auto& diag = Diagnostics::instance();
      diag.runChecks();
      int fails = diag.failureCount();
      if (fails > 0) {
           std::vector<std::pair<std::string, HealthStatus>> failures;
           for (const auto& res : diag.getResults()) {
               if (!res.second.ok) failures.push_back(res);
           }
           showHealthWarning(failures);
      }
  }
  */

  while (!glfwWindowShouldClose(window) && !shouldClose_) {
    glfwPollEvents();

    if (mode_ == MODE_LAUNCHER) {
      int w, h;
      glfwGetWindowSize(window, &w, &h);
      if (w != 500 || h != 300)
        glfwSetWindowSize(window, 500, 300);
    }

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    const ImGuiViewport *viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);

    if (mode_ == MODE_LAUNCHER)
      ImGui::SetNextWindowSize({500, 300});
    else {
      int w, h;
      glfwGetWindowSize(window, &w, &h);
      ImGui::SetNextWindowSize(ImVec2((float)w, (float)h));
      ImGui::SetNextWindowPos(ImVec2(0, 0));
    }

    float display_w = viewport->WorkSize.x;
    float display_h = viewport->WorkSize.y;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoDecoration |
                                   ImGuiWindowFlags_NoMove |
                                   ImGuiWindowFlags_NoBringToFrontOnFocus |
                                   ImGuiWindowFlags_NoSavedSettings;
    if (mode_ == MODE_LAUNCHER)
      windowFlags |= ImGuiWindowFlags_NoResize;

    ImGui::Begin("RSJFW_Container", nullptr, windowFlags);

    if (mode_ == MODE_LAUNCHER) {
      std::lock_guard<std::mutex> lock(mutex_);

      // Clean up completed tasks
      auto it = tasks_.begin();
      while (it != tasks_.end()) {
        if (it->second.progress >= 1.0f && it->second.status == "Done") {
          it = tasks_.erase(it);
        } else {
          ++it;
        }
      }

      // Calculate bar heights for footer
      int activeTaskCount =
          tasks_.empty() ? 1 : (int)tasks_.size() + 1; // Main + tasks
      float barHeight = 12.0f;
      float barSpacing = 4.0f;
      float footerHeight =
          (barHeight + barSpacing) * std::min(activeTaskCount, 2) +
          10.0f; // Max 2 bars visible
      float contentAreaHeight = display_h - footerHeight;

      // === TOP CONTENT AREA ===
      // Layout: Logo pinned LEFT, centered status text

      float logoDisplayHeight = 80.0f;
      float logoDisplayWidth = 0.0f;
      if (logoTexture_) {
        float scale = logoDisplayHeight / (float)logoHeight_;
        logoDisplayWidth = logoWidth_ * scale;
      }

      float contentY = (contentAreaHeight - logoDisplayHeight) * 0.5f;

      // Logo on LEFT
      if (logoTexture_) {
        ImGui::SetCursorPos(ImVec2(20.0f, contentY));
        ImGui::Image((void *)(intptr_t)logoTexture_,
                     ImVec2(logoDisplayWidth, logoDisplayHeight));
      }

      // Status text CENTERED in remaining space
      float textAreaX = logoDisplayWidth + 40.0f;
      float textAreaWidth = display_w - textAreaX - 20.0f;
      std::string statusMsg = status_;
      ImVec2 textSize = ImGui::CalcTextSize(statusMsg.c_str());
      ImGui::SetCursorPos(
          ImVec2(textAreaX + (textAreaWidth - textSize.x) * 0.5f,
                 contentY + (logoDisplayHeight - textSize.y) * 0.5f));
      ImGui::Text("%s", statusMsg.c_str());

      // === FOOTER PROGRESS BARS ===
      // Full-width edge-to-edge bars at bottom

      float footerY = contentAreaHeight;
      ImGui::SetCursorPosY(footerY);

      // Tweened progress (smooth interpolation)
      static float lerpedProgress = 0.0f;
      float targetProgress = progress_ >= 0.0f ? progress_ : 0.5f;
      lerpedProgress += (targetProgress - lerpedProgress) *
                        ImGui::GetIO().DeltaTime * 8.0f; // Smooth lerp

      // Bar 1: Main progress (full width, no padding)
      {
        ImVec2 barPos = ImVec2(0, footerY);
        ImVec2 barSize = ImVec2(display_w, barHeight);
        ImVec2 screenPos = ImGui::GetWindowPos();

        ImDrawList *draw = ImGui::GetWindowDrawList();
        ImVec2 p1(screenPos.x, screenPos.y + footerY);
        ImVec2 p2(screenPos.x + display_w, screenPos.y + footerY + barHeight);

        // Background
        draw->AddRectFilled(p1, p2, IM_COL32(25, 25, 30, 255));

        if (progress_ < 0.0f) {
          // Indeterminate shimmer
          float t = (float)ImGui::GetTime();
          float shimmerWidth = display_w * 0.3f;
          float shimmerX = p1.x + (display_w - shimmerWidth) * 0.5f *
                                      (1.0f + std::sin(t * 3.0f));
          draw->AddRectFilled(ImVec2(shimmerX, p1.y),
                              ImVec2(shimmerX + shimmerWidth, p2.y),
                              IM_COL32(180, 0, 30, 255)); // Deep crimson
        } else {
          // Filled progress with tweening - deep crimson red
          float fillWidth = display_w * lerpedProgress;
          draw->AddRectFilled(p1, ImVec2(p1.x + fillWidth, p2.y),
                              IM_COL32(180, 0, 30, 255)); // Deep crimson
        }

        ImGui::Dummy(barSize);
      }

      // Bar 2: First task progress (if any tasks exist) - NO GAP
      if (!tasks_.empty()) {
        auto &firstTask = tasks_.begin()->second;

        static float lerpedTaskProgress = 0.0f;
        float taskTarget =
            firstTask.progress >= 0.0f ? firstTask.progress : 0.5f;
        lerpedTaskProgress +=
            (taskTarget - lerpedTaskProgress) * ImGui::GetIO().DeltaTime * 8.0f;

        ImVec2 barSize = ImVec2(display_w, barHeight);
        ImVec2 screenPos = ImGui::GetWindowPos();
        float barY = ImGui::GetCursorPosY();

        ImDrawList *draw = ImGui::GetWindowDrawList();
        ImVec2 p1(screenPos.x, screenPos.y + barY);
        ImVec2 p2(screenPos.x + display_w, screenPos.y + barY + barHeight);

        // Background
        draw->AddRectFilled(p1, p2, IM_COL32(20, 25, 30, 255));

        if (firstTask.progress < 0.0f) {
          float t = (float)ImGui::GetTime();
          float shimmerWidth = display_w * 0.25f;
          float shimmerX = p1.x + (display_w - shimmerWidth) * 0.5f *
                                      (1.0f + std::sin(t * 4.0f));
          draw->AddRectFilled(ImVec2(shimmerX, p1.y),
                              ImVec2(shimmerX + shimmerWidth, p2.y),
                              IM_COL32(0, 160, 180, 255)); // Teal
        } else {
          float fillWidth = display_w * lerpedTaskProgress;
          draw->AddRectFilled(p1, ImVec2(p1.x + fillWidth, p2.y),
                              IM_COL32(0, 160, 180, 255)); // Teal
        }

        // Task label overlaid on bar
        ImGui::SetCursorPos(ImVec2(10.0f, barY + 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 255, 255, 200));
        ImGui::TextUnformatted(firstTask.status.c_str());
        ImGui::PopStyleColor();

        ImGui::Dummy(barSize);
      }
    }

    if (mode_ == MODE_CONFIG) {
      // Startup health check - run once on first frame
      static bool startupCheckDone = false;
      if (!startupCheckDone) {
        startupCheckDone = true;
        TaskRunner::instance().run([this]() {
          auto &diag = Diagnostics::instance();
          diag.runChecks();

          // Collect failures
          std::vector<std::pair<std::string, HealthStatus>> failures;
          for (const auto &res : diag.getResults()) {
            if (!res.second.ok)
              failures.push_back(res);
          }

          if (!failures.empty()) {
            showHealthWarning(failures);
          }
        });
      }

      // Animation state
      // currentMainTab_ is now a member
      // targetMainTab_ is now a member
      static float mainTabTransition = 0.0f;
      // currentSettingsTab_ is now a member
      // targetSettingsTab_ is now a member
      static float settingsTabTransition = 0.0f;
      const float transitionSpeed = 3.0f; // Slower animation
      float dt = ImGui::GetIO().DeltaTime;

      // Animate main tab transition
      if (currentMainTab_ != targetMainTab_) {
        mainTabTransition += dt * transitionSpeed;
        if (mainTabTransition >= 1.0f) {
          currentMainTab_ = targetMainTab_;
          mainTabTransition = 0.0f;
        }
      }

      // Animate settings tab transition
      if (currentSettingsTab_ != targetSettingsTab_) {
        settingsTabTransition += dt * transitionSpeed;
        if (settingsTabTransition >= 1.0f) {
          currentSettingsTab_ = targetSettingsTab_;
          settingsTabTransition = 0.0f;
        }
      }

      // Full-width accent navigation bar - no borders
      float navHeight = 40.0f;

      // Draw crimson background behind entire nav bar (extend up to cover top
      // padding)
      ImVec2 navStart = ImGui::GetCursorScreenPos();
      ImVec2 windowPos = ImGui::GetWindowPos();
      ImDrawList *drawList = ImGui::GetWindowDrawList();
      drawList->AddRectFilled(
          ImVec2(windowPos.x, windowPos.y),
          ImVec2(windowPos.x + display_w, windowPos.y + navHeight),
          IM_COL32(220, 20, 60, 255));

      // Push styles to remove borders and spacing
      ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.0f);
      ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
      ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
      ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));

      // Move cursor to very top of window content area
      ImGui::SetCursorPos(ImVec2(0, 0));

      // Navigation buttons evenly spaced
      const char *mainTabs[] = {"HOME", "SETTINGS", "TROUBLESHOOT"};
      float tabWidth = display_w / 3.0f;

      for (int i = 0; i < 3; i++) {
        if (i > 0)
          ImGui::SameLine(0, 0);
        bool selected = (targetMainTab_ == i);

        // Selected = darker red, unselected = crimson
        if (selected) {
          ImGui::PushStyleColor(ImGuiCol_Button,
                                ImVec4(0.45f, 0.02f, 0.10f, 1.0f));
          ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                                ImVec4(0.50f, 0.04f, 0.12f, 1.0f));
        } else {
          ImGui::PushStyleColor(ImGuiCol_Button,
                                ImVec4(0.86f, 0.08f, 0.24f, 1.0f));
          ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                                ImVec4(0.75f, 0.05f, 0.18f, 1.0f));
        }
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                              ImVec4(0.40f, 0.02f, 0.08f, 1.0f));

        if (ImGui::Button(mainTabs[i], ImVec2(tabWidth, navHeight))) {
          if (targetMainTab_ != i) {
            targetMainTab_ = i;
            mainTabTransition = 0.0f;
          }
        }
        ImGui::PopStyleColor(3);
      }

      ImGui::PopStyleColor(); // Border
      ImGui::PopStyleVar(3);

      ImGui::Spacing();

      // Two-phase animation: 0-0.5 = fade out + slide old, 0.5-1 = fade in +
      // slide new
      float fadeAlpha = 1.0f;
      float slideX = 0.0f;
      int displayTab = currentMainTab_;

      if (currentMainTab_ != targetMainTab_) {
        if (mainTabTransition < 0.5f) {
          // Phase 1: Fade out + slide away old content
          float phase = mainTabTransition * 2.0f; // 0 to 1
          fadeAlpha = 1.0f - phase;
          int direction = (targetMainTab_ > currentMainTab_) ? -1 : 1;
          slideX = direction * phase * 80.0f;
          displayTab = currentMainTab_;
        } else {
          // Phase 2: Fade in + slide in new content
          float phase = (mainTabTransition - 0.5f) * 2.0f; // 0 to 1
          fadeAlpha = phase;
          int direction = (targetMainTab_ > currentMainTab_) ? 1 : -1;
          slideX = direction * (1.0f - phase) * 80.0f;
          displayTab = targetMainTab_;
        }
      }

      // Apply fade via style
      ImGui::PushStyleVar(ImGuiStyleVar_Alpha,
                          ImGui::GetStyle().Alpha * fadeAlpha);
      ImGui::SetCursorPosX(ImGui::GetCursorPosX() + slideX);

      if (displayTab == 0) {
        // Home
        auto homePage = std::make_shared<HomePage>(this, logoTexture_,
                                                   logoWidth_, logoHeight_);
        homePage->render();
      } else if (displayTab == 1) {
        // Settings with sidebar
        const char *settingsItems[] = {"General", "Wine", "DXVK", "FFlags",
                                       "Environment"};
        float sidebarWidth = 120.0f;

        // Sidebar
        ImGui::BeginChild("SettingsSidebar", ImVec2(sidebarWidth, 0), true);
        for (int i = 0; i < 5; i++) {
          bool selected = (targetSettingsTab_ == i); // Use member
          if (selected) {
            ImGui::PushStyleColor(ImGuiCol_Button,
                                  ImVec4(0.86f, 0.08f, 0.24f, 1.0f));
          }
          if (ImGui::Button(settingsItems[i], ImVec2(sidebarWidth - 16, 35))) {
            if (targetSettingsTab_ != i) { // Use member
              targetSettingsTab_ = i;
              settingsTabTransition = 0.0f;
            }
          }
          if (selected)
            ImGui::PopStyleColor();
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        if (ImGui::Button("Save", ImVec2(sidebarWidth - 16, 35))) {
          Config::instance().save();
          status_ = "Configuration saved.";

          auto &cfg = Config::instance();
          auto &gen = cfg.getGeneral();

          // Only download Wine if source isn't SYSTEM/CUSTOM_PATH AND wineRoot
          // doesn't exist or is empty
          if (gen.wineSource.repo != "SYSTEM" &&
              gen.wineSource.repo != "CUSTOM_PATH") {
            bool needsDownload =
                gen.wineSource.installedRoot.empty() ||
                !std::filesystem::exists(gen.wineSource.installedRoot) ||
                std::filesystem::is_empty(gen.wineSource.installedRoot);
            if (needsDownload) {
              std::string wineTask =
                  "Wine:" + gen.wineSource.repo + ":" + gen.wineSource.version;
              if (!gen.wineSource.asset.empty())
                wineTask += ":" + gen.wineSource.asset;

              if (GUI::instance().hasTask(wineTask)) {
                status_ = "Download already in progress: " + wineTask;
              } else {
                TaskRunner::instance().run([=]() {
                  Downloader dl(PathManager::instance().root().string());
                  auto &configInst = Config::instance();
                  std::string ver = configInst.getGeneral().wineSource.version;
                  std::string repo = configInst.getGeneral().wineSource.repo;
                  std::string asset = configInst.getGeneral().wineSource.asset;

                  GUI::instance().setTaskProgress(wineTask, 0.05f,
                                                  "Preparing...");
                  bool res = dl.installWine(
                      repo, ver, asset,
                      [&](const std::string &item, float p, size_t, size_t) {
                        GUI::instance().setTaskProgress(wineTask, p, item);
                      });
                  if (res)
                    GUI::instance().removeTask(wineTask);
                  else
                    GUI::instance().setTaskProgress(wineTask, 1.0f, "Error");
                });
              }
            }
          }

          // Only download DXVK if enabled AND dxvkRoot doesn't exist or is
          // empty
          if (gen.dxvk && gen.dxvkSource.repo != "CUSTOM_PATH") {
            // GPU Compatibility Check BEFORE downloading
            std::string vkCmd = "vulkaninfo --summary 2>/dev/null | grep "
                                "'apiVersion' | head -n 1 | awk '{print $3}'";
            FILE *vkPipe = popen(vkCmd.c_str(), "r");
            if (vkPipe) {
              char vkBuf[64] = {0};
              if (fgets(vkBuf, sizeof(vkBuf), vkPipe)) {
                int major = 0, minor = 0;
                sscanf(vkBuf, "%d.%d", &major, &minor);

                // Check DXVK version from config OR from dxvkRoot path
                std::string dxvkVer = gen.dxvkSource.version;
                std::string dxvkClean = dxvkVer;
                if (!dxvkClean.empty() &&
                    (dxvkClean[0] == 'v' || dxvkClean[0] == 'V')) {
                  dxvkClean = dxvkClean.substr(1);
                }

                // Also check dxvkRoot for version number (handles "Latest"
                // case)
                std::string rootPath = gen.dxvkSource.installedRoot;
                bool isV2FromRoot =
                    rootPath.find("dxvk-2.") != std::string::npos;
                bool isV2FromConfig = dxvkClean.find("2.") == 0;
                bool isLatest =
                    (dxvkClean == "Latest" || dxvkClean == "latest");

                // VK 1.2 or below + DXVK 2.x = INCOMPATIBLE
                if (major == 1 && minor < 3 &&
                    (isV2FromConfig || isV2FromRoot || isLatest)) {
                  status_ = "FUCK! Can't use DXVK 2.x with VK 1." +
                            std::to_string(minor) + " - switching to v1.10.3";
                  cfg.getGeneral().dxvkSource.version = "v1.10.3";
                  cfg.getGeneral().dxvkSource.repo = "doitsujin/dxvk";
                  cfg.getGeneral().dxvkSource.installedRoot =
                      ""; // Force re-download
                  cfg.save();
                }
              }
              pclose(vkPipe);
            }

            // Now check if download needed (re-read after possible change)
            auto &genUpdated = cfg.getGeneral();
            bool needsDownload =
                genUpdated.dxvkSource.installedRoot.empty() ||
                !std::filesystem::exists(genUpdated.dxvkSource.installedRoot) ||
                std::filesystem::is_empty(genUpdated.dxvkSource.installedRoot);
            if (needsDownload) {
              std::string dxvkTask = "DXVK:" + genUpdated.dxvkSource.repo +
                                     ":" + genUpdated.dxvkSource.version;
              if (!genUpdated.dxvkSource.asset.empty())
                dxvkTask += ":" + genUpdated.dxvkSource.asset;

              if (GUI::instance().hasTask(dxvkTask)) {
                status_ = "Download already in progress: " + dxvkTask;
              } else {
                TaskRunner::instance().run([=]() {
                  Downloader dl(PathManager::instance().root().string());
                  auto &configInst = Config::instance();
                  std::string repo = configInst.getGeneral().dxvkSource.repo;
                  std::string ver = configInst.getGeneral().dxvkSource.version;
                  std::string asset = configInst.getGeneral().dxvkSource.asset;

                  GUI::instance().setTaskProgress(dxvkTask, 0.05f,
                                                  "Preparing...");
                  bool res = dl.installDxvk(
                      repo, ver, asset,
                      [&](const std::string &item, float p, size_t, size_t) {
                        GUI::instance().setTaskProgress(dxvkTask, p, item);
                      });
                  if (res)
                    GUI::instance().removeTask(dxvkTask);
                  else
                    GUI::instance().setTaskProgress(dxvkTask, 1.0f, "Error");
                });
              }
            }
          }
        }
        ImGui::EndChild();

        ImGui::SameLine();

        // Settings content with two-phase animation (same as main tabs)
        ImGui::BeginChild("SettingsContent", ImVec2(0, 0), true);

        // Two-phase: 0-0.5 fade out + slide old, 0.5-1 fade in + slide new
        // Determine whether to use transitions for the sub-page
        int displaySubTab = currentSettingsTab_;
        float subFade = 1.0f;
        float subSlide = 0.0f;

        if (currentSettingsTab_ != targetSettingsTab_) {
          if (settingsTabTransition < 0.5f) {
            float phase = settingsTabTransition * 2.0f;
            subFade = 1.0f - phase;
            int dir = (targetSettingsTab_ > currentSettingsTab_) ? -1 : 1;
            subSlide = dir * phase * 40.0f;
            displaySubTab = currentSettingsTab_;
          } else {
            float phase = (settingsTabTransition - 0.5f) * 2.0f;
            subFade = phase;
            int dir = (targetSettingsTab_ > currentSettingsTab_) ? 1 : -1;
            subSlide = dir * (1.0f - phase) * 40.0f;
            displaySubTab = targetSettingsTab_;
          }
        }

        ImGui::PushStyleVar(ImGuiStyleVar_Alpha,
                            ImGui::GetStyle().Alpha * subFade);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + subSlide);

        static auto settingsPage = std::make_shared<SettingsPage>(this);
        settingsPage->update();

        if (displaySubTab == 0)
          settingsPage->renderGeneralTab();
        else if (displaySubTab == 1)
          settingsPage->renderWineTab();
        else if (displaySubTab == 2)
          settingsPage->renderDxvkTab();
        else if (displaySubTab == 3)
          settingsPage->renderFFlagsTab();
        else if (displaySubTab == 4)
          settingsPage->renderEnvTab();

        ImGui::PopStyleVar();

        ImGui::Spacing();
        ImGui::Separator();

        {
          std::lock_guard<std::mutex> lock(mutex_);
          for (const auto &task : tasks_) {
            if (task.second.progress >= 0.0f || !task.second.status.empty()) {
              ImGui::Text("%s: %s", task.first.c_str(),
                          task.second.status.c_str());

              if (task.second.progress < 0.0f) {
                // Indeterminate Pulse
                float t = (float)ImGui::GetTime();
                float width = ImGui::GetContentRegionAvail().x;
                float height = 20.0f;
                ImVec2 p1 = ImGui::GetCursorScreenPos();
                ImVec2 p2 = ImVec2(p1.x + width, p1.y + height);

                ImDrawList *draw = ImGui::GetWindowDrawList();
                draw->AddRectFilled(p1, p2, IM_COL32(30, 30, 35, 255),
                                    4.0f); // BG

                float pulseWidth = width * 0.3f;
                float pulseX = p1.x + (width - pulseWidth) * 0.5f *
                                          (1.0f + std::sin(t * 4.0f));
                draw->AddRectFilled(ImVec2(pulseX, p1.y),
                                    ImVec2(pulseX + pulseWidth, p2.y),
                                    IM_COL32(180, 0, 30, 255), 4.0f);

                ImGui::Dummy(ImVec2(width, height));
              } else {
                ImGui::ProgressBar(task.second.progress, ImVec2(0.0f, 0.0f));
              }
            }
          }
        }
        ImGui::EndChild();
      } else if (displayTab == 2) {
        // Troubleshooting
        static auto troublePage = std::make_shared<TroubleshootingPage>();
        troublePage->render();
      }

      // Pop fade style
      ImGui::PopStyleVar();
    }

    if (showHealthModal_)
      ImGui::OpenPopup("Health Warning");
    if (ImGui::BeginPopupModal("Health Warning", NULL,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
      ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
      ImGui::Text("Startup Health Verification Failed");
      ImGui::PopStyleColor();
      ImGui::Separator();
      ImGui::Spacing();

      ImGui::Text(
          "The following issues may prevent RSJFW from working correctly:");
      ImGui::Spacing();

      ImGui::BeginChild("HealthFailures", ImVec2(500, 200), true);

      // Group failures by category
      std::map<std::string, std::vector<std::pair<std::string, HealthStatus>>>
          grouped;
      for (const auto &fail : healthFailures_) {
        grouped[fail.second.category].push_back(fail);
      }

      for (const auto &[category, categoryFails] : grouped) {
        // Count active failures in this category
        int failCount = categoryFails.size();
        std::string headerName =
            category + " (" + std::to_string(failCount) + ")";

        ImGui::SetNextItemOpen(true, ImGuiCond_Appearing); // Default open
        if (ImGui::TreeNode(headerName.c_str())) {
          for (const auto &fail : categoryFails) {
            // Display Title
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s",
                               fail.first.c_str());

            // Button logic (keep on same line as title)
            if (fail.second.fixable) {
              ImGui::SameLine();
              float avail = ImGui::GetContentRegionAvail().x;
              ImGui::SetCursorPosX(ImGui::GetCursorPosX() + avail - 80);

              if (fail.first == "Wine Source") {
                if (ImGui::Button("CONFIGURE", ImVec2(70, 20))) {
                  showHealthModal_ = false;
                  ImGui::CloseCurrentPopup();
                  navigateToSettingsWine(true);
                }
              } else {
                if (ImGui::Button(("FIX##" + fail.first).c_str(),
                                  ImVec2(50, 20))) {
                  performingFix_ = true;
                  fixProgress_ = 0.0f;
                  fixStatus_ = "Starting...";
                  ImGui::OpenPopup("Performing Fix"); // Open nested immediately

                  // Launch fix
                  auto action = fail.second.fixAction;
                  std::string taskName =
                      fail.first; // Use failure name as task name

                  if (GUI::instance().hasTask(taskName)) {
                    // Do nothing, task is already running
                  } else {
                    TaskRunner::instance().run([=]() {
                      action([](float p, std::string s) {
                        GUI::instance().updateFixProgress(p, s);
                      });
                    });
                  }
                }
              }
            }

            // Display Message/Detail below title
            if (!fail.second.message.empty()) {
              ImGui::PushStyleColor(ImGuiCol_Text,
                                    ImVec4(0.9f, 0.9f, 0.9f, 0.9f));
              ImGui::TextWrapped("%s", fail.second.message.c_str());
              ImGui::PopStyleColor();
            }
            if (!fail.second.detail.empty()) {
              ImGui::PushStyleColor(ImGuiCol_Text,
                                    ImVec4(0.7f, 0.7f, 0.7f, 0.8f));
              ImGui::TextWrapped("%s", fail.second.detail.c_str());
              ImGui::PopStyleColor();
            }

            ImGui::Separator();
          }
          ImGui::TreePop();
        }
      }
      ImGui::EndChild();

      ImGui::Spacing();

      if (ImGui::Button("Go to Troubleshooting", ImVec2(240, 35))) {
        showHealthModal_ = false;
        ImGui::CloseCurrentPopup();
        navigateToTroubleshooting();
      }
      ImGui::SameLine();
      if (ImGui::Button("Ignore & Continue", ImVec2(240, 35))) {
        showHealthModal_ = false;
        ImGui::CloseCurrentPopup();
      }

      // Nested Fix Modal
      if (ImGui::BeginPopupModal("Performing Fix", NULL,
                                 ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("%s", fixStatus_.c_str());
        ImGui::ProgressBar(fixProgress_, ImVec2(300, 20));

        if (fixProgress_ >= 1.0f) {
          ImGui::Spacing();
          if (ImGui::Button("Close", ImVec2(300, 30))) {
            performingFix_ = false;
            ImGui::CloseCurrentPopup();

            // Refresh Diagnostics
            auto &diag = Diagnostics::instance();
            diag.runChecks();

            // Update failure list
            healthFailures_.clear();
            for (const auto &res : diag.getResults()) {
              if (!res.second.ok)
                healthFailures_.push_back(res);
            }

            // If no failures left, close parent modal
            if (healthFailures_.empty()) {
              showHealthModal_ = false;
              ImGui::CloseCurrentPopup();
            }
          }
        }
        ImGui::EndPopup();
      }

      ImGui::Spacing();
      ImGui::Separator();
      ImGui::Spacing();

      // Action buttons at bottom
      if (ImGui::Button("Go to Troubleshooting", ImVec2(180, 30))) {
        showHealthModal_ = false;
        ImGui::CloseCurrentPopup();
        navigateToTroubleshooting();
      }
      ImGui::SameLine();
      if (ImGui::Button("Dismiss", ImVec2(100, 30))) {
        showHealthModal_ = false;
        ImGui::CloseCurrentPopup();
      }

      ImGui::EndPopup();
    }

    if (showMessageModal_)
      ImGui::OpenPopup(messageTitle_.c_str());
    if (ImGui::BeginPopupModal(messageTitle_.c_str(), NULL,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
      ImGui::TextWrapped("%s", messageText_.c_str());
      ImGui::Spacing();
      ImGui::Separator();
      ImGui::Spacing();
      if (ImGui::Button("OK", ImVec2(120, 30))) {
        showMessageModal_ = false;
        ImGui::CloseCurrentPopup();
      }
      ImGui::EndPopup();
    }

    if (!error_.empty())
      ImGui::OpenPopup("Error");
    if (ImGui::BeginPopupModal("Error", NULL,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
      ImGui::Text("An error occurred:");
      ImGui::Separator();
      ImGui::TextWrapped("%s", error_.c_str());
      ImGui::Spacing();
      if (ImGui::Button("Copy"))
        ImGui::SetClipboardText(error_.c_str());
      ImGui::SameLine();
      if (ImGui::Button("Close")) {
        close();
        ImGui::CloseCurrentPopup();
      }
      ImGui::EndPopup();
    }

    if (renderCallback)
      renderCallback();

    ImGui::End();
    ImGui::PopStyleVar();

    ImGui::Render();
    int fb_width, fb_height;
    glfwGetFramebufferSize(window, &fb_width, &fb_height);
    glViewport(0, 0, fb_width, fb_height);
    glClearColor(0.1f, 0.1f, 0.12f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    glfwSwapBuffers(window);
  }
}

void GUI::showHealthWarning(
    const std::vector<std::pair<std::string, HealthStatus>> &failures) {
  std::lock_guard<std::mutex> lock(mutex_);
  healthFailures_ = failures;
  showHealthModal_ = true;
}

void GUI::navigateToTroubleshooting() {
  std::lock_guard<std::mutex> lock(mutex_);
  // Force switch to tab 2 (Troubleshooting)
  currentMainTab_ = 2; // Immediate switch
  targetMainTab_ = 2;  // Sync target
                       // Reset any transition
}

void GUI::navigateToSettingsWine(bool flash) {
  std::lock_guard<std::mutex> lock(mutex_);
  currentMainTab_ = 1; // Settings
  targetMainTab_ = 1;

  // Switch to Wine tab (index 1 in Settings)
  // We need to access settings tab state. Since it's inside GUI::run (static
  // vars), we might need a way to set it. For now, let's assume default is
  // General or user navigates. IMPROVEMENT: Make settings tab state a member to
  // control it too.

  if (flash)
    flashWidget("WineSourceCombo");
}

void GUI::flashWidget(const std::string &id) {
  // No lock needed if called from main thread logic
  flashWidgetId_ = id;
  flashTimer_ = 0.0f;
  flashCycles_ = 0;
}

bool GUI::shouldFlash(const std::string &id) {
  if (flashWidgetId_ != id)
    return false;

  float dt = ImGui::GetIO().DeltaTime;
  flashTimer_ += dt * 10.0f; // Speed

  if (flashTimer_ >= 3.14159f * 2.0f) {
    flashTimer_ = 0.0f;
    flashCycles_++;
    if (flashCycles_ >= 2) { // 2 cycles
      flashWidgetId_ = "";
      return false;
    }
  }

  // Return true if high part of sine wave
  return std::sin(flashTimer_) > 0.0f;
}

void GUI::showMessage(const std::string &title, const std::string &message) {
  std::lock_guard<std::mutex> lock(mutex_);
  messageTitle_ = title;
  messageText_ = message;
  showMessageModal_ = true;
}

void GUI::updateFixProgress(float progress, const std::string &status) {
  std::lock_guard<std::mutex> lock(mutex_);
  fixProgress_ = progress;
  fixStatus_ = status;
}

void GUI::setProgress(float progress, const std::string &status) {
  std::lock_guard<std::mutex> lock(mutex_);
  progress_ = progress;
  status_ = status;
}

void GUI::setError(const std::string &errorMsg) {
  std::lock_guard<std::mutex> lock(mutex_);
  error_ = errorMsg;
}

void GUI::close() { shouldClose_ = true; }

void GUI::shutdown() {
  if (!initialized_)
    return;

  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();

  if (window_) {
    glfwDestroyWindow((GLFWwindow *)window_);
    window_ = nullptr;
  }
  glfwTerminate();
  initialized_ = false;
}

GUI::~GUI() { shutdown(); }

static int findTask(std::vector<std::pair<std::string, GUI::TaskInfo>> &tasks,
                    const std::string &name) {
  for (size_t i = 0; i < tasks.size(); ++i)
    if (tasks[i].first == name)
      return (int)i;
  return -1;
}

void GUI::setTaskProgress(const std::string &name, float progress,
                          const std::string &status) {
  std::lock_guard<std::mutex> lock(mutex_);
  int idx = findTask(tasks_, name);
  if (idx >= 0)
    tasks_[idx].second = {progress, status};
  else
    tasks_.push_back({name, {progress, status}});
}

void GUI::removeTask(const std::string &name) {
  std::lock_guard<std::mutex> lock(mutex_);
  int idx = findTask(tasks_, name);
  if (idx >= 0)
    tasks_.erase(tasks_.begin() + idx);
}

bool GUI::hasTask(const std::string &name) {
  std::lock_guard<std::mutex> lock(mutex_);
  return findTask(tasks_, name) >= 0;
}

void GUI::setSubProgress(float progress, const std::string &status) {
  setTaskProgress("Sub", progress, status);
}

} // namespace rsjfw
