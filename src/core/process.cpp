#include "rsjfw/process.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <csignal>
#include <unistd.h>
#include <algorithm>

namespace rsjfw {

std::vector<ProcessInfo> Process::findByName(const std::string& name) {
    std::vector<ProcessInfo> found;
    for (const auto& entry : std::filesystem::directory_iterator("/proc")) {
        if (!entry.is_directory()) continue;
        
        std::string dirname = entry.path().filename().string();
        if (!std::all_of(dirname.begin(), dirname.end(), ::isdigit)) continue;
        
        int pid = std::stoi(dirname);
        auto exe = getProcessExe(pid);
        if (exe && exe->find(name) != std::string::npos) {
            ProcessInfo info;
            info.pid = pid;
            info.name = name;
            info.exe = *exe;
            info.winePrefix = getProcessPrefix(pid).value_or("");
            found.push_back(info);
        }
    }
    return found;
}

std::vector<ProcessInfo> Process::findStudioInPrefix(const std::string& prefixDir) {
    std::vector<ProcessInfo> found;
    std::filesystem::path targetPrefix = std::filesystem::absolute(prefixDir);

    for (const auto& entry : std::filesystem::directory_iterator("/proc")) {
        if (!entry.is_directory()) continue;
        
        std::string dirname = entry.path().filename().string();
        if (!std::all_of(dirname.begin(), dirname.end(), ::isdigit)) continue;
        
        int pid = std::stoi(dirname);
        auto exe = getProcessExe(pid);
        if (!exe || (exe->find("RobloxStudio") == std::string::npos && exe->find("wine") == std::string::npos)) continue;

        auto prefix = getProcessPrefix(pid);
        if (prefix) {
             std::filesystem::path p(*prefix);
             if (std::filesystem::exists(p) && std::filesystem::equivalent(p, targetPrefix)) {
                 ProcessInfo info;
                 info.pid = pid;
                 info.exe = *exe;
                 info.winePrefix = *prefix;
                 found.push_back(info);
             }
        }
    }
    return found;
}

bool Process::kill(int pid, bool force) {
    return ::kill(pid, force ? SIGKILL : SIGTERM) == 0;
}

bool Process::killAllInPrefix(const std::string& prefixDir) {
    auto procs = findStudioInPrefix(prefixDir);
    bool allSuccess = true;
    for (const auto& p : procs) {
        if (!kill(p.pid)) allSuccess = false;
    }
    return allSuccess;
}

std::optional<std::string> Process::getProcessPrefix(int pid) {
    std::filesystem::path envPath = std::filesystem::path("/proc") / std::to_string(pid) / "environ";
    std::ifstream ifs(envPath, std::ios::binary);
    if (!ifs) return std::nullopt;

    std::string env;
    char c;
    while (ifs.get(c)) {
        if (c == '\0') {
            if (env.find("WINEPREFIX=") == 0) {
                return env.substr(11);
            }
            env.clear();
        } else {
            env += c;
        }
    }
    return std::nullopt;
}

std::optional<std::string> Process::getProcessExe(int pid) {
    try {
        std::filesystem::path exePath = std::filesystem::path("/proc") / std::to_string(pid) / "exe";
        if (std::filesystem::exists(exePath)) {
            return std::filesystem::read_symlink(exePath).string();
        }
    } catch (...) {}
    return std::nullopt;
}

} // namespace rsjfw
