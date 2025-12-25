#include "rsjfw/gui.hpp"
#include "rsjfw/logger.hpp"
#include "rsjfw/config.hpp"
#include "rsjfw/launcher.hpp"
#include "rsjfw/downloader.hpp"
#include "rsjfw/pages/HomePage.hpp"
#include "rsjfw/pages/SettingsPage.hpp"
#include "rsjfw/pages/TroubleshootingPage.hpp"
#include <GLFW/glfw3.h>
#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"
#include <iostream>
#include <thread>
#include <cstdlib>
#include <filesystem>
#include <sstream>
#include <algorithm>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

namespace rsjfw {

static bool configDirty = false;

static void glfw_error_callback(int error, const char* description) {
    if (error == 65548) return; 
    LOG_ERROR("GLFW Error " + std::to_string(error) + ": " + std::string(description));
}

GUI& GUI::instance() {
    static GUI instance;
    return instance;
}

bool loadTextureFromFile(const char* filename, GLuint* out_texture, int* out_width, int* out_height) {
    int image_width = 0;
    int image_height = 0;
    unsigned char* image_data = stbi_load(filename, &image_width, &image_height, NULL, 4);
    if (image_data == NULL) return false;

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
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, image_width, image_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, image_data);
    stbi_image_free(image_data);

    *out_texture = image_texture;
    *out_width = image_width;
    *out_height = image_height;

    return true;
}

bool GUI::init(int width, int height, const std::string& title, bool resizable) {
    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit()) {
        LOG_ERROR("Failed to initialize GLFW");
        return false;
    }

    const char* glsl_version = "#version 130";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    glfwWindowHint(GLFW_RESIZABLE, resizable ? GLFW_TRUE : GLFW_FALSE);
    
    if (!resizable) {
        glfwWindowHint(GLFW_FLOATING, GLFW_TRUE);
        glfwWindowHint(GLFW_FOCUS_ON_SHOW, GLFW_TRUE);
        glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
    }
    
    GLFWwindow* window = glfwCreateWindow(width, height, title.c_str(), NULL, NULL);
    if (window == nullptr) {
        LOG_ERROR("Failed to create GLFW window");
        glfwTerminate();
        return false;
    }

    if (!resizable) glfwSetWindowSizeLimits(window, width, height, width, height);
    
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1); 

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr;
    
    io.Fonts->AddFontFromFileTTF("external/imgui/misc/fonts/Roboto-Medium.ttf", 18.0f);

    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    
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
    ImVec4* colors = style.Colors;
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

    if (!loadTextureFromFile("assets/logo.png", &logoTexture_, &logoWidth_, &logoHeight_)) {
        loadTextureFromFile("/usr/share/pixmaps/rsjfw.png", &logoTexture_, &logoWidth_, &logoHeight_);
    }

    window_ = window;
    initialized_ = true;
    
    pages_.push(std::make_shared<HomePage>(this, logoTexture_, logoWidth_, logoHeight_));
    
    return true;
}

void GUI::run(const std::function<void()>& renderCallback) {
    if (!initialized_) return;

    GLFWwindow* window = (GLFWwindow*)window_;

    while (!glfwWindowShouldClose(window) && !shouldClose_) {
        glfwPollEvents();
        
        if (mode_ == MODE_LAUNCHER) {
             int w, h;
             glfwGetWindowSize(window, &w, &h);
             if (w != 500 || h != 300) glfwSetWindowSize(window, 500, 300);
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->WorkPos);
        
        if (mode_ == MODE_LAUNCHER) ImGui::SetNextWindowSize({ 500, 300 });
        else {
             int w, h;
             glfwGetWindowSize(window, &w, &h);
             ImGui::SetNextWindowSize(ImVec2((float)w, (float)h));
             ImGui::SetNextWindowPos(ImVec2(0, 0));
        }

        float display_w = viewport->WorkSize.x;
        float display_h = viewport->WorkSize.y;
        
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoSavedSettings;
        if (mode_ == MODE_LAUNCHER) windowFlags |= ImGuiWindowFlags_NoResize;

        ImGui::Begin("RSJFW_Container", nullptr, windowFlags);
        
        if (mode_ == MODE_LAUNCHER) {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = tasks_.begin();
            while (it != tasks_.end()) {
                if (it->second.progress >= 1.0f && it->second.status == "Done") {
                    it = tasks_.erase(it);
                } else {
                    ++it;
                }
            }
            
            float contentHeight = (logoTexture_ ? 150 : 0) + 120;
            ImGui::SetCursorPosY((display_h - contentHeight) * 0.5f);
            
            if (logoTexture_) {
                 float targetLogoHeight = 120.0f;
                 float scale = targetLogoHeight / (float)logoHeight_;
                 float targetWidth = logoWidth_ * scale;
                 ImGui::SetCursorPosX((display_w - targetWidth) * 0.5f);
                 ImGui::Image((void*)(intptr_t)logoTexture_, ImVec2(targetWidth, targetLogoHeight));
            }

            ImGui::Spacing();
            std::string statusMsg = status_;
            ImGui::SetCursorPosX((display_w - ImGui::CalcTextSize(statusMsg.c_str()).x) * 0.5f);
            ImGui::Text("%s", statusMsg.c_str());

            ImGui::Spacing();
            ImGui::Indent(40);
            ImGui::ProgressBar(progress_, ImVec2(display_w - 80, 10.0f));
            ImGui::Unindent(40);

            for (const auto& task : tasks_) {
                ImGui::Spacing();
                ImGui::SetCursorPosX((display_w - ImGui::CalcTextSize(task.second.status.c_str()).x) * 0.5f);
                ImGui::TextDisabled("%s", task.second.status.c_str());
                ImGui::Indent(60);
                ImGui::ProgressBar(task.second.progress, ImVec2(display_w - 120, 6.0f));
                ImGui::Unindent(60);
            }
        }
        
        if (mode_ == MODE_CONFIG) {
            // Animation state
            static int currentMainTab = 0;
            static int targetMainTab = 0;
            static float mainTabTransition = 0.0f;
            static int currentSettingsTab = 0;
            static int targetSettingsTab = 0;
            static float settingsTabTransition = 0.0f;
            const float transitionSpeed = 3.0f; // Slower animation
            float dt = ImGui::GetIO().DeltaTime;
            
            // Animate main tab transition
            if (currentMainTab != targetMainTab) {
                mainTabTransition += dt * transitionSpeed;
                if (mainTabTransition >= 1.0f) {
                    currentMainTab = targetMainTab;
                    mainTabTransition = 0.0f;
                }
            }
            
            // Animate settings tab transition
            if (currentSettingsTab != targetSettingsTab) {
                settingsTabTransition += dt * transitionSpeed;
                if (settingsTabTransition >= 1.0f) {
                    currentSettingsTab = targetSettingsTab;
                    settingsTabTransition = 0.0f;
                }
            }
            
            // Full-width accent navigation bar - no borders
            float navHeight = 40.0f;
            
            // Draw crimson background behind entire nav bar (extend up to cover top padding)
            ImVec2 navStart = ImGui::GetCursorScreenPos();
            ImVec2 windowPos = ImGui::GetWindowPos();
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            drawList->AddRectFilled(
                ImVec2(windowPos.x, windowPos.y),
                ImVec2(windowPos.x + display_w, windowPos.y + navHeight),
                IM_COL32(220, 20, 60, 255)
            );
            
            // Push styles to remove borders and spacing
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
            ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));
            
            // Move cursor to very top of window content area
            ImGui::SetCursorPos(ImVec2(0, 0));
            
            // Navigation buttons evenly spaced
            const char* mainTabs[] = {"HOME", "SETTINGS", "TROUBLESHOOT"};
            float tabWidth = display_w / 3.0f;
            
            for (int i = 0; i < 3; i++) {
                if (i > 0) ImGui::SameLine(0, 0);
                bool selected = (targetMainTab == i);
                
                // Selected = darker red, unselected = crimson
                if (selected) {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.45f, 0.02f, 0.10f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.50f, 0.04f, 0.12f, 1.0f));
                } else {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.86f, 0.08f, 0.24f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.75f, 0.05f, 0.18f, 1.0f));
                }
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.40f, 0.02f, 0.08f, 1.0f));
                
                if (ImGui::Button(mainTabs[i], ImVec2(tabWidth, navHeight))) {
                    if (targetMainTab != i) {
                        targetMainTab = i;
                        mainTabTransition = 0.0f;
                    }
                }
                ImGui::PopStyleColor(3);
            }
            
            ImGui::PopStyleColor(); // Border
            ImGui::PopStyleVar(3);
            
            ImGui::Spacing();
            
            // Two-phase animation: 0-0.5 = fade out + slide old, 0.5-1 = fade in + slide new
            float fadeAlpha = 1.0f;
            float slideX = 0.0f;
            int displayTab = currentMainTab;
            
            if (currentMainTab != targetMainTab) {
                if (mainTabTransition < 0.5f) {
                    // Phase 1: Fade out + slide away old content
                    float phase = mainTabTransition * 2.0f; // 0 to 1
                    fadeAlpha = 1.0f - phase;
                    int direction = (targetMainTab > currentMainTab) ? -1 : 1;
                    slideX = direction * phase * 80.0f;
                    displayTab = currentMainTab;
                } else {
                    // Phase 2: Fade in + slide in new content
                    float phase = (mainTabTransition - 0.5f) * 2.0f; // 0 to 1
                    fadeAlpha = phase;
                    int direction = (targetMainTab > currentMainTab) ? 1 : -1;
                    slideX = direction * (1.0f - phase) * 80.0f;
                    displayTab = targetMainTab;
                }
            }
            
            // Apply fade via style
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * fadeAlpha);
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + slideX);
            
            if (displayTab == 0) {
                // Home
                auto homePage = std::make_shared<HomePage>(this, logoTexture_, logoWidth_, logoHeight_);
                homePage->render();
            } 
            else if (displayTab == 1) {
                // Settings with sidebar
                const char* settingsItems[] = {"General", "Wine", "GPU", "FFlags", "Environment"};
                float sidebarWidth = 120.0f;
                
                // Sidebar
                ImGui::BeginChild("SettingsSidebar", ImVec2(sidebarWidth, 0), true);
                for (int i = 0; i < 5; i++) {
                    bool selected = (targetSettingsTab == i);
                    if (selected) {
                        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.86f, 0.08f, 0.24f, 1.0f));
                    }
                    if (ImGui::Button(settingsItems[i], ImVec2(sidebarWidth - 16, 35))) {
                        if (targetSettingsTab != i) {
                            targetSettingsTab = i;
                            settingsTabTransition = 0.0f;
                        }
                    }
                    if (selected) ImGui::PopStyleColor();
                }
                
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();
                
                if (ImGui::Button("Save", ImVec2(sidebarWidth - 16, 35))) {
                    Config::instance().save();
                    status_ = "Configuration saved.";
                    
                    auto& cfg = Config::instance();
                    if (cfg.getGeneral().wineSource == WineSource::VINEGAR || cfg.getGeneral().wineSource == WineSource::PROTON_GE) {
                         std::thread([&]() {
                             Downloader dl(std::getenv("HOME") + std::string("/.rsjfw"));
                             auto& configInst = Config::instance();
                             std::string ver = (configInst.getGeneral().wineSource == WineSource::VINEGAR) ? configInst.getGeneral().vinegarVersion : configInst.getGeneral().protonVersion;
                             GUI::instance().setTaskProgress("Wine", 0.05f, "Checking...");
                             bool res = dl.installWine(configInst.getGeneral().wineSource, ver, [&](const std::string& item, float p, size_t, size_t) {
                                 GUI::instance().setTaskProgress("Wine", p, item);
                             });
                             if (res) GUI::instance().removeTask("Wine");
                             else GUI::instance().setTaskProgress("Wine", 1.0f, "Error");
                         }).detach();
                    }
                    
                    if (cfg.getGeneral().dxvk && cfg.getGeneral().dxvkSource != DxvkSource::CUSTOM) {
                         std::thread([&]() {
                             Downloader dl(std::getenv("HOME") + std::string("/.rsjfw"));
                             auto& configInst = Config::instance();
                             GUI::instance().setTaskProgress("DXVK", 0.05f, "Checking...");
                             bool res = dl.installDxvk(configInst.getGeneral().dxvkSource, configInst.getGeneral().dxvkVersion, [&](const std::string& item, float p, size_t, size_t) {
                                 GUI::instance().setTaskProgress("DXVK", p, item);
                             });
                             if (res) GUI::instance().removeTask("DXVK");
                             else GUI::instance().setTaskProgress("DXVK", 1.0f, "Error");
                         }).detach();
                    }
                }
                ImGui::EndChild();
                
                ImGui::SameLine();
                
                // Settings content with two-phase animation (same as main tabs)
                ImGui::BeginChild("SettingsContent", ImVec2(0, 0), true);
                
                // Two-phase: 0-0.5 fade out + slide old, 0.5-1 fade in + slide new
                float settingsFade = 1.0f;
                float settingsSlide = 0.0f;
                int displaySettingsTab = currentSettingsTab;
                
                if (currentSettingsTab != targetSettingsTab) {
                    if (settingsTabTransition < 0.5f) {
                        // Phase 1: Fade out + slide away old content
                        float phase = settingsTabTransition * 2.0f;
                        settingsFade = 1.0f - phase;
                        int dir = (targetSettingsTab > currentSettingsTab) ? -1 : 1;
                        settingsSlide = dir * phase * 40.0f;
                        displaySettingsTab = currentSettingsTab;
                    } else {
                        // Phase 2: Fade in + slide in new content
                        float phase = (settingsTabTransition - 0.5f) * 2.0f;
                        settingsFade = phase;
                        int dir = (targetSettingsTab > currentSettingsTab) ? 1 : -1;
                        settingsSlide = dir * (1.0f - phase) * 40.0f;
                        displaySettingsTab = targetSettingsTab;
                    }
                }
                
                ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * settingsFade);
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() + settingsSlide);
                
                auto settingsPage = std::make_shared<SettingsPage>(this);
                if (displaySettingsTab == 0) settingsPage->renderGeneralTab();
                else if (displaySettingsTab == 1) settingsPage->renderWineTab();
                else if (displaySettingsTab == 2) settingsPage->renderGpuTab();
                else if (displaySettingsTab == 3) settingsPage->renderFFlagsTab();
                else if (displaySettingsTab == 4) settingsPage->renderEnvTab();
                
                ImGui::PopStyleVar();
                
                ImGui::Spacing();
                ImGui::Separator();
                
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    for (const auto& task : tasks_) {
                        if (task.second.progress > 0.0f || !task.second.status.empty()) {
                            ImGui::Text("%s: %s", task.first.c_str(), task.second.status.c_str());
                            ImGui::ProgressBar(task.second.progress, ImVec2(0.0f, 0.0f));
                        }
                    }
                }
                ImGui::EndChild();
            }
            else if (displayTab == 2) {
                // Troubleshooting
                auto troublePage = std::make_shared<TroubleshootingPage>();
                troublePage->render();
            }
            
            // Pop fade style
            ImGui::PopStyleVar();
        }

        if (!error_.empty()) ImGui::OpenPopup("Error");
        if (ImGui::BeginPopupModal("Error", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("An error occurred:");
            ImGui::Separator();
            ImGui::TextWrapped("%s", error_.c_str());
            ImGui::Spacing();
            if (ImGui::Button("Copy")) ImGui::SetClipboardText(error_.c_str());
            ImGui::SameLine();
            if (ImGui::Button("Close")) { close(); ImGui::CloseCurrentPopup(); }
            ImGui::EndPopup();
        }

        if (renderCallback) renderCallback();

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

void GUI::setProgress(float progress, const std::string& status) {
    std::lock_guard<std::mutex> lock(mutex_);
    progress_ = progress;
    status_ = status;
}

void GUI::setError(const std::string& errorMsg) {
    std::lock_guard<std::mutex> lock(mutex_);
    error_ = errorMsg;
}

void GUI::close() {
    shouldClose_ = true;
}

void GUI::shutdown() {
    if (!initialized_) return;
    
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    
    if (window_) {
        glfwDestroyWindow((GLFWwindow*)window_);
        window_ = nullptr;
    }
    glfwTerminate();
    initialized_ = false;
}

GUI::~GUI() {
    shutdown();
}

static int findTask(std::vector<std::pair<std::string, GUI::TaskInfo>>& tasks, const std::string& name) {
    for (size_t i = 0; i < tasks.size(); ++i) if (tasks[i].first == name) return (int)i;
    return -1;
}

void GUI::setTaskProgress(const std::string& name, float progress, const std::string& status) {
    std::lock_guard<std::mutex> lock(mutex_);
    int idx = findTask(tasks_, name);
    if (idx >= 0) tasks_[idx].second = {progress, status};
    else tasks_.push_back({name, {progress, status}});
}

void GUI::removeTask(const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);
    int idx = findTask(tasks_, name);
    if (idx >= 0) tasks_.erase(tasks_.begin() + idx);
}

void GUI::setSubProgress(float progress, const std::string& status) {
    setTaskProgress("Sub", progress, status);
}

}
