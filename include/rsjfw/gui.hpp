#pragma once

#include <string>
#include <functional>
#include <vector>
#include <mutex>
#include <memory>
#include "rsjfw/page.hpp"

namespace rsjfw {

class GUI {
public:
    enum Mode { MODE_CONFIG, MODE_LAUNCHER };
    
    static GUI& instance();
    
    bool init(int width, int height, const std::string& title, bool resizable);

    void setMode(Mode mode) { mode_ = mode; }
    
    void run(const std::function<void()>& renderCallback);

    void setProgress(float progress, const std::string& status);
    void setTaskProgress(const std::string& name, float progress, const std::string& status);
    void removeTask(const std::string& name);
    void setSubProgress(float progress, const std::string& status); 

    void setError(const std::string& errorMsg);

    float getProgress() const { return progress_; }
    std::string getStatus() const { return status_; }
    std::string getError() const { return error_; }
    bool hasError() const { return !error_.empty(); }

    void close();
    void shutdown();
    
    // Page Navigation
    PageStack& pages() { return pages_; }
    void navigateTo(std::shared_ptr<Page> page) { pages_.push(page); }
    void goBack() { pages_.pop(); }
    
    // Accessors for pages
    unsigned int logoTexture() const { return logoTexture_; }
    int logoWidth() const { return logoWidth_; }
    int logoHeight() const { return logoHeight_; }

    GUI(const GUI&) = delete;
    GUI& operator=(const GUI&) = delete;

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
    bool shouldClose_ = false;
    bool initialized_ = false;
    unsigned int logoTexture_ = 0;
    int logoWidth_ = 0;
    int logoHeight_ = 0;

    void* window_ = nullptr;
    void* glContext_ = nullptr;
    
    PageStack pages_;
    
    mutable std::mutex mutex_;
};

}
