#ifndef RSJFW_TASK_RUNNER_HPP
#define RSJFW_TASK_RUNNER_HPP

#include <functional>
#include <thread>
#include <vector>
#include <mutex>
#include <memory>
#include <future>
#include <atomic>

namespace rsjfw {

class TaskRunner {
public:
    static TaskRunner& instance();

    // Runs a task in a managed jthread
    void run(std::function<void()> task);

    // Runs a task and returns a future for its result
    template<typename F, typename... Args>
    auto async(F&& f, Args&&... args) -> std::future<typename std::invoke_result<F, Args...>::type> {
        using return_type = typename std::invoke_result<F, Args...>::type;
        auto task = std::make_shared<std::packaged_task<return_type()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        );
        std::future<return_type> res = task->get_future();
        
        std::lock_guard<std::mutex> lock(mutex_);
        threads_.emplace_back([task]() {
            (*task)();
        });
        
        return res;
    }

    // Ensures all threads are joined. Called on app shutdown.
    void shutdown();

    ~TaskRunner();

    TaskRunner(const TaskRunner&) = delete;
    TaskRunner& operator=(const TaskRunner&) = delete;

private:
    TaskRunner() = default;

    std::vector<std::jthread> threads_;
    std::mutex mutex_;
};

} // namespace rsjfw

#endif // RSJFW_TASK_RUNNER_HPP
