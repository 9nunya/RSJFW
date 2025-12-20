#pragma once
#include <string>
#include <vector>
#include <functional>
#include <sys/un.h>

namespace rsjfw {

class SingleInstance {
public:
    explicit SingleInstance(const std::string& name);
    ~SingleInstance();

    // Returns true if this is the primary instance (bound to socket).
    // Returns false if another instance is already running.
    bool isPrimary();

    // If secondary, sends args to the primary instance.
    void sendArgs(const std::vector<std::string>& args);

    // If primary, listen for incoming args in a separate thread.
    void listen(std::function<void(const std::vector<std::string>&)> callback);

    // Stop listening (cleanup)
    void stop();

private:
    std::string socketPath_;
    int serverSocket_ = -1;
    bool isPrimary_ = false;
    bool running_ = false;
};

} // namespace rsjfw
