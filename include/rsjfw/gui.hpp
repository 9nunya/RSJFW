#pragma once

#include <string>
#include <functional>
#include <vector>

namespace rsjfw {

class GUI {
public:
    enum Mode { MODE_CONFIG, MODE_LAUNCHER };
    
    static GUI& instance();
    
    // Initialize the window and ImGui context
    bool init(int width, int height, const std::string& title, bool resizable);

    void setMode(Mode mode) { mode_ = mode; }
    
    // Run the main loop with a render callback
    void run(const std::function<void()>& renderCallback);

    // Update progress bar state
    void setProgress(float progress, const std::string& status);
    void setError(const std::string& errorMsg);

    // Accessors for state
    float getProgress() const { return progress_; }
    std::string getStatus() const { return status_; }
    std::string getError() const { return error_; }
    bool hasError() const { return !error_.empty(); }

    void close();
    void shutdown();

    // Forbidden
    GUI(const GUI&) = delete;
    GUI& operator=(const GUI&) = delete;

private:
    GUI() = default;
    ~GUI();


    Mode mode_ = MODE_CONFIG;
    float progress_ = 0.0f;
    std::string status_ = "Initializing...";
    std::string error_ = "";
    bool shouldClose_ = false;
    bool initialized_ = false;
    unsigned int logoTexture_ = 0;
    int logoWidth_ = 0;
    int logoHeight_ = 0;

    // We keep implementation details hidden in cpp to avoid leaking ImGui/SDL headers
    // But for simplicity in this prototype, we handle them in cpp mostly.
    void* window_ = nullptr; // SDL_Window*
    void* glContext_ = nullptr; // SDL_GLContext
};

} // namespace rsjfw
