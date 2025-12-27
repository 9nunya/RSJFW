#include "rsjfw/task_runner.hpp"
#include "rsjfw/logger.hpp"

namespace rsjfw {

TaskRunner& TaskRunner::instance() {
    static TaskRunner instance;
    return instance;
}

void TaskRunner::run(std::function<void()> task) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Prune finished threads if any (optional, but good for long-running apps)
    // For now, just append to the list of jthreads. 
    // std::jthread automatically joins on destruction if not joined.
    threads_.emplace_back(std::move(task));
}

void TaskRunner::shutdown() {
    LOG_INFO("Shutting down TaskRunner, waiting for background threads...");
    std::lock_guard<std::mutex> lock(mutex_);
    
    // jthreads will automatically request stop and join on destruction
    // but explicit clearing ensures we join NOW.
    threads_.clear();
    LOG_INFO("TaskRunner shutdown complete.");
}

TaskRunner::~TaskRunner() {
    shutdown();
}

} // namespace rsjfw
