#include "rsjfw/state.hpp"
#include <chrono>
#include <algorithm>

namespace rsjfw {

State& State::instance() {
    static State instance;
    return instance;
}

void State::set(AppState state) {
    state_.store(state);
}

AppState State::get() const {
    return state_.load();
}

std::string State::getStateString() const {
    switch(state_.load()) {
        case AppState::IDLE: return "Idle";
        case AppState::INITIALIZING: return "Initializing";
        case AppState::DOWNLOADING_WINE: return "Downloading Wine";
        case AppState::DOWNLOADING_ROBLOX: return "Downloading Roblox";
        case AppState::CONFIGURING: return "Configuring";
        case AppState::VERIFYING: return "Verifying";
        case AppState::FIXING: return "Fixing Issues";
        case AppState::LAUNCHING: return "Launching";
        case AppState::IN_STUDIO: return "In Studio";
        case AppState::ERROR: return "Error";
        default: return "Unknown";
    }
}

void State::setStatus(const std::string& status) {
    std::lock_guard<std::mutex> lock(mutex_);
    status_ = status;
}

std::string State::getStatus() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return status_;
}

void State::setProgress(float progress) {
    progress_.store(progress);
}

float State::getProgress() const {
    return progress_.load();
}

void State::addStudioInstance(int pid) {
    std::lock_guard<std::mutex> lock(mutex_);
    // Check if already exists
    for (const auto& inst : instances_) {
        if (inst.pid == pid) return;
    }
    
    StudioInstance inst;
    inst.pid = pid;
    inst.startTime = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    inst.windowTitle = "Detecting..."; // Will be updated by polling loop potentially
    instances_.push_back(inst);
    
    state_.store(AppState::IN_STUDIO);
}

void State::removeStudioInstance(int pid) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = std::remove_if(instances_.begin(), instances_.end(), [pid](const StudioInstance& i){ return i.pid == pid; });
    instances_.erase(it, instances_.end());
    
    if (instances_.empty() && state_.load() == AppState::IN_STUDIO) {
        state_.store(AppState::IDLE);
    }
}

std::vector<StudioInstance> State::getInstances() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return instances_;
}

bool State::isStudioRunning() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return !instances_.empty();
}

void State::setDebugVar(const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> lock(mutex_);
    debugVars_[key] = value;
}

std::map<std::string, std::string> State::getDebugVars() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return debugVars_;
}

} // namespace rsjfw

// End of file
