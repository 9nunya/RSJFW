#include "rsjfw/wine.hpp"
#include "rsjfw/logger.hpp"
#include <unistd.h>
#include <sys/wait.h>
#include <cstring>
#include <iostream>
#include <algorithm>
#include <fstream>
#include <sstream>

extern char** environ;

namespace rsjfw {
namespace wine {

Prefix::Prefix(const std::string& root, const std::string& dir) 
    : root_(root), dir_(dir) {
    if (dir_.empty()) {
        const char* home = getenv("HOME");
        dir_ = std::string(home ? home : ".") + "/.wine";
    }
}

bool Prefix::isProton() const {
    return std::filesystem::exists(std::filesystem::path(root_) / "proton");
}

std::string Prefix::bin(const std::string& prog) const {
    std::filesystem::path p(root_);
    if (isProton()) {
        p = p / "files" / "bin" / prog;
    } else if (!root_.empty()) {
        p = p / "bin" / prog;
    } else {
        return prog;
    }
    return p.string();
}

void Prefix::setEnv(const std::map<std::string, std::string>& env) {
    env_ = env;
}

void Prefix::appendEnv(const std::string& key, const std::string& value) {
    env_[key] = value;
}

std::vector<std::string> Prefix::buildEnv() const {
    std::vector<std::string> finalEnv;
    std::map<std::string, std::string> currentMap;

    for (char** s = environ; *s; s++) {
        std::string str(*s);
        size_t pos = str.find('=');
        if (pos != std::string::npos) {
            std::string key = str.substr(0, pos);
            std::string val = str.substr(pos + 1);
            currentMap[key] = val;
        }
    }

    for (const auto& [key, val] : env_) {
        currentMap[key] = val;
    }

    if (!dir_.empty()) {
        currentMap["WINEPREFIX"] = dir_;
    }

    for (const auto& [key, val] : currentMap) {
        finalEnv.push_back(key + "=" + val);
    }

    return finalEnv;
}

bool Prefix::runCommand(const std::string& exe, const std::vector<std::string>& args, std::function<void(const std::string&)> onOutput, const std::string& cwd) {
    std::vector<std::string> finalArgs;
    finalArgs.push_back(exe);
    finalArgs.insert(finalArgs.end(), args.begin(), args.end());

    std::vector<char*> argv;
    for (const auto& s : finalArgs) argv.push_back(const_cast<char*>(s.c_str()));
    argv.push_back(nullptr);

    int pipefd[2];
    if (onOutput) {
        if (pipe(pipefd) == -1) return false;
    }

    pid_t pid = fork();
    if (pid == -1) return false;

    if (pid == 0) {
        if (onOutput) {
            close(pipefd[0]);
            dup2(pipefd[1], STDOUT_FILENO);
            dup2(pipefd[1], STDERR_FILENO); 
            close(pipefd[1]);
        }

        if (!cwd.empty()) {
            if (chdir(cwd.c_str()) != 0) {
                perror("chdir");
            }
        }

        for (const auto& [key, val] : env_) {
            setenv(key.c_str(), val.c_str(), 1);
        }
        
        if (!dir_.empty()) {
            setenv("WINEPREFIX", dir_.c_str(), 1);
        }

        execvp(exe.c_str(), argv.data());
        
        std::cerr << "Failed to exec: " << exe << "\n";
        _exit(127);
    }

    if (onOutput) {
        close(pipefd[1]);
        FILE* stream = fdopen(pipefd[0], "r");
        if (stream) {
            char buffer[1024];
            while (fgets(buffer, sizeof(buffer), stream)) {
                onOutput(std::string(buffer));
            }
            fclose(stream);
        }
        close(pipefd[0]);
    }

    int status;
    waitpid(pid, &status, 0);
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

bool Prefix::wine(const std::string& target, const std::vector<std::string>& args, std::function<void(const std::string&)> onOutput, const std::string& cwd) {
    std::string wineBin = "wine";
    if (isProton()) {
        wineBin = root_ + "/proton";
    } else if (!root_.empty()) {
        wineBin = root_ + "/bin/wine";
    }

    std::vector<std::string> finalArgs;
    if (isProton()) {
        finalArgs.push_back("run");
    }
    finalArgs.push_back(target);
    finalArgs.insert(finalArgs.end(), args.begin(), args.end());

    return runCommand(wineBin, finalArgs, onOutput, cwd);
}

bool Prefix::registryAdd(const std::string& key, const std::string& valueName, const std::string& value, const std::string& type) {
    std::vector<std::string> args = {"reg", "add", key, "/f"};
    if (!valueName.empty()) {
        args.push_back("/v");
        args.push_back(valueName);
    } else {
        args.push_back("/ve");
    }
    
    if (!type.empty()) {
        args.push_back("/t");
        args.push_back(type);
    }

    if (!value.empty()) {
        args.push_back("/d");
        args.push_back(value);
    }
    
    return wine("reg", args, [](const std::string& s){}); 
}

bool Prefix::kill() {
    std::string ws = bin("wineserver");
    return runCommand(ws, {"-k"});
}

} // namespace wine
} // namespace rsjfw
