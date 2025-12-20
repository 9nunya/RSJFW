#include "rsjfw/gui.hpp"
#include "rsjfw/logger.hpp"
#include "rsjfw/config.hpp"
#include "rsjfw/launcher.hpp"
#include <GLFW/glfw3.h>
#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"
#include <iostream>
#include <thread>
#include <cstdlib>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

namespace rsjfw {

static void glfw_error_callback(int error, const char* description) {
    LOG_ERROR("GLFW Error " + std::to_string(error) + ": " + std::string(description));
}

GUI& GUI::instance() {
    static GUI instance;
    return instance;
}

// Loads a texture from the filesystem into an OpenGL texture ID
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

// Initializes GLFW, OpenGL context, ImGui context, and style settings
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

    if (!resizable) {
        glfwSetWindowSizeLimits(window, width, height, width, height);
    }
    
    GLFWmonitor* monitor = glfwGetPrimaryMonitor();
    if (monitor) {
        const GLFWvidmode* mode = glfwGetVideoMode(monitor);
        if (mode) {
             glfwSetWindowPos(window, (mode->width - width) / 2, (mode->height - height) / 2);
        }
    }
    
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1); 

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.IniFilename = nullptr; 

    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 8.0f;
    style.FrameRounding = 6.0f;
    style.PopupRounding = 6.0f;
    style.ScrollbarRounding = 6.0f;
    style.GrabRounding = 6.0f;
    style.TabRounding = 6.0f;
    
    ImVec4* colors = style.Colors;
    colors[ImGuiCol_WindowBg] = ImVec4(0.10f, 0.10f, 0.12f, 1.00f);
    colors[ImGuiCol_Header] = ImVec4(0.80f, 0.10f, 0.20f, 0.5f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.90f, 0.15f, 0.25f, 0.6f);
    colors[ImGuiCol_HeaderActive] = ImVec4(0.90f, 0.15f, 0.25f, 0.8f);
    colors[ImGuiCol_Button] = ImVec4(0.80f, 0.10f, 0.20f, 0.6f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.90f, 0.15f, 0.25f, 0.7f);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.90f, 0.15f, 0.25f, 0.9f);
    colors[ImGuiCol_FrameBg] = ImVec4(0.20f, 0.20f, 0.22f, 1.00f);
    colors[ImGuiCol_PlotHistogram] = ImVec4(0.90f, 0.15f, 0.25f, 1.00f);

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glsl_version);

    if (!loadTextureFromFile("assets/logo.png", &logoTexture_, &logoWidth_, &logoHeight_)) {
        loadTextureFromFile("/usr/share/pixmaps/rsjfw.png", &logoTexture_, &logoWidth_, &logoHeight_);
    }

    window_ = window;
    glContext_ = nullptr; 

    initialized_ = true;
    return true;
}

// Main rendering loop for the GUI
void GUI::run(const std::function<void()>& renderCallback) {
    if (!initialized_) return;

    ImGuiIO& io = ImGui::GetIO();
    GLFWwindow* window = (GLFWwindow*)window_;

    while (!glfwWindowShouldClose(window) && !shouldClose_) {
        glfwPollEvents();
        
        if (mode_ == MODE_LAUNCHER) {
             int w, h;
             glfwGetWindowSize(window, &w, &h);
             if (w != 500 || h != 300) {
                 glfwSetWindowSize(window, 500, 300);
             }
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize({ 500, 300 });

        float display_w = viewport->WorkSize.x;
        float display_h = viewport->WorkSize.y;
        
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::Begin("RSJFW_Container", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoSavedSettings);
        
        if (mode_ == MODE_LAUNCHER) {
            float contentHeight = (logoTexture_ ? 150 : 0) + 100;
            ImGui::SetCursorPosY((display_h - contentHeight) * 0.5f);
            
            if (logoTexture_) {
                 float targetLogoHeight = 120.0f;
                 float scale = targetLogoHeight / (float)logoHeight_;
                 float targetWidth = logoWidth_ * scale;
                 
                 ImGui::SetCursorPosX((display_w - targetWidth) * 0.5f);
                 ImGui::Image((void*)(intptr_t)logoTexture_, ImVec2(targetWidth, targetLogoHeight));
            } else {
                 std::string title = "RSJFW";
                 ImGui::SetCursorPosX((display_w - ImGui::CalcTextSize(title.c_str()).x) * 0.5f);
                 ImGui::Text("%s", title.c_str());
            }

            ImGui::Spacing();
            ImGui::Spacing();
            
            std::string statusMsg = status_;
            float textWidth = ImGui::CalcTextSize(statusMsg.c_str()).x;
            ImGui::SetCursorPosX((display_w - textWidth) * 0.5f);
            ImGui::Text("%s", statusMsg.c_str());

            ImGui::Spacing();
            ImGui::Indent(40);
            ImGui::ProgressBar(progress_, ImVec2(display_w - 80, 10.0f));
            ImGui::Unindent(40);
            
        } 
        else if (mode_ == MODE_CONFIG) {
            ImGui::SetCursorPosY(10);
            std::string title = "ROBLOX STUDIO JUST FUCKING WORKS";
            float textWidth = ImGui::CalcTextSize(title.c_str()).x;
            ImGui::SetCursorPosX((display_w - textWidth) * 0.5f);
            ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), "%s", title.c_str());

            ImGui::Spacing();
            ImGui::Separator();
            
            if (ImGui::BeginTabBar("MainTabs")) {
                
                if (ImGui::BeginTabItem("Settings")) {
                    auto& cfg = Config::instance();
                    bool changed = false;

                    if (ImGui::BeginTabBar("ConfigTabs")) {
                        if (ImGui::BeginTabItem("General")) {
                            ImGui::Spacing();
                            
                            const char* renderers[] = { "D3D11", "Vulkan", "OpenGL", "D3D11FL10" };
                            static int currentRendererIdx = 0;
                            std::string currentRenderer = cfg.getGeneral().renderer;
                            
                            for (int i=0; i < 4; i++) {
                                if (currentRenderer == renderers[i]) currentRendererIdx = i;
                            }
                            
                            if (ImGui::Combo("Renderer", &currentRendererIdx, renderers, 4)) {
                                cfg.getGeneral().renderer = renderers[currentRendererIdx];
                                changed = true;
                            }
                            
                            bool dxvk = cfg.getGeneral().dxvk;
                            if (ImGui::Checkbox("Enable DXVK", &dxvk)) {
                                cfg.getGeneral().dxvk = dxvk;
                                changed = true;
                            }
                            
                            if (dxvk) {
                                char buf[64];
                                strncpy(buf, cfg.getGeneral().dxvkVersion.c_str(), sizeof(buf));
                                if (ImGui::InputText("DXVK Version", buf, sizeof(buf))) {
                                    cfg.getGeneral().dxvkVersion = std::string(buf);
                                    changed = true;
                                }
                            }

                            ImGui::EndTabItem();
                        }
                        
                        if (ImGui::BeginTabItem("Wine")) {
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
                                     const std::string home = std::getenv("HOME");
                                     rsjfw::Launcher launcher(home + "/.rsjfw");
                                     if (!launcher.openWineConfiguration()) {
                                         LOG_ERROR("Failed to open winecfg");
                                     }
                                 }).detach();
                            }
                            
                            ImGui::EndTabItem();
                        }

                        ImGui::EndTabBar();
                    }

                    ImGui::Spacing();
                    ImGui::Separator();
                    ImGui::Spacing();
                    
                    if (ImGui::Button("Save Configuration", ImVec2(200, 30))) {
                        cfg.save();
                        status_ = "Configuration saved.";
                    }
                    
                    ImGui::SameLine();
                    ImGui::TextDisabled("(?)");
                     if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Restart RSJFW to apply changes.");

                    ImGui::EndTabItem();
                }

                ImGui::EndTabBar();
            }
        }

        if (!error_.empty()) {
            ImGui::OpenPopup("Error");
        }

        if (ImGui::BeginPopupModal("Error", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("An error occurred:");
            ImGui::Separator();
            ImGui::TextWrapped("%s", error_.c_str());
            ImGui::Spacing();
            
            if (ImGui::Button("Copy to Clipboard")) {
                ImGui::SetClipboardText(error_.c_str());
            }
            ImGui::SameLine();
            if (ImGui::Button("Close")) {
                close();
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        if (renderCallback) {
            renderCallback();
        }

        ImGui::End();
        ImGui::PopStyleVar();

        ImGui::Render();
        
        int fb_w, fb_h;
        glfwGetFramebufferSize(window, &fb_w, &fb_h);
        glViewport(0, 0, fb_w, fb_h);
        glClearColor(0.10f, 0.10f, 0.12f, 1.00f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        
        glfwSwapBuffers(window);
    }
}

void GUI::setProgress(float progress, const std::string& status) {
    progress_ = progress;
    status_ = status;
}

void GUI::setError(const std::string& errorMsg) {
    error_ = errorMsg;
}

void GUI::close() {
    shouldClose_ = true;
}

void GUI::shutdown() {
    if (initialized_) {
        shouldClose_ = true;
        
        if (ImGui::GetCurrentContext()) {
            ImGui_ImplOpenGL3_Shutdown();
            ImGui_ImplGlfw_Shutdown();
            ImGui::DestroyContext();
        }

        if (window_) {
            glfwDestroyWindow((GLFWwindow*)window_);
            window_ = nullptr;
        }
        
        glfwTerminate();
        initialized_ = false;
    }
}

GUI::~GUI() {
    shutdown();
}

} // namespace rsjfw
