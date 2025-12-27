#ifndef RSJFW_PROCESS_HPP
#define RSJFW_PROCESS_HPP

#include <string>
#include <vector>
#include <optional>

namespace rsjfw {

struct ProcessInfo {
    int pid;
    std::string name;
    std::string exe;
    std::string winePrefix;
};

class Process {
public:
    // Finds all processes matching the name
    static std::vector<ProcessInfo> findByName(const std::string& name);
    
    // Finds all Roblox Studio processes in a specific prefix
    static std::vector<ProcessInfo> findStudioInPrefix(const std::string& prefixDir);
    
    // Forcefully kills a process
    static bool kill(int pid, bool force = true);
    
    // Kills all processes in a prefix
    static bool killAllInPrefix(const std::string& prefixDir);

private:
    static std::optional<std::string> getProcessPrefix(int pid);
    static std::optional<std::string> getProcessExe(int pid);
};

} // namespace rsjfw

#endif // RSJFW_PROCESS_HPP
