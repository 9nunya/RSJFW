#ifndef RSJFW_STATE_HPP
#define RSJFW_STATE_HPP

#include <atomic>
#include <string>
#include <mutex>
#include <vector>
#include <map>

namespace rsjfw {

enum class AppState {
    IDLE,
    INITIALIZING,
    DOWNLOADING_WINE,
    DOWNLOADING_ROBLOX,
    CONFIGURING,
    VERIFYING,
    FIXING,
    LAUNCHING,
    IN_STUDIO, // One or more running
    ERROR
};

struct StudioInstance {
    int pid;
    std::string windowTitle;
    long startTime;
};

class State {
public:
    static State& instance();

    void set(AppState state);
    AppState get() const;
    
    // Helper to get string representation of state
    std::string getStateString() const;
    
    void setStatus(const std::string& status);
    std::string getStatus() const;
    
    void setProgress(float progress);
    float getProgress() const;
    
    // Multi-process tracking
    void addStudioInstance(int pid);
    void removeStudioInstance(int pid);
    std::vector<StudioInstance> getInstances() const;
    bool isStudioRunning() const;
    
    // Debug data
    void setDebugVar(const std::string& key, const std::string& value);
    std::map<std::string, std::string> getDebugVars() const;

private:
    State() = default;

    std::atomic<AppState> state_{AppState::IDLE};
    std::atomic<float> progress_{0.0f};
    
    mutable std::mutex mutex_;
    std::string status_;
    
    std::vector<StudioInstance> instances_;
    std::map<std::string, std::string> debugVars_;
};

} // namespace rsjfw

#endif // RSJFW_STATE_HPP
